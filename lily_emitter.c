#include <stdlib.h>

#include "lily_ast.h"
#include "lily_impl.h"
#include "lily_emitter.h"
#include "lily_opcode.h"
#include "lily_emit_table.h"

#define WRITE_PREP(size) \
if ((m->pos + size) > m->len) { \
    m->len *= 2; \
    m->code = lily_realloc(m->code, sizeof(int) * m->len); \
    if (m->code == NULL) \
        lily_raise_nomem(emit->error); \
}

/* Most ops need 4 or less code spaces, so only growing once is okay. However,
   calls need 5 + #args. So more than one grow might be necessary.
   Note: This macro may need to check for overflow later. */
#define WRITE_PREP_LARGE(size) \
if ((m->pos + size) > m->len) { \
    while ((m->pos + size) > m->len) \
        m->len *= 2; \
    m->code = lily_realloc(m->code, sizeof(int) * m->len); \
    if (m->code == NULL) \
        lily_raise_nomem(emit->error); \
}

#define WRITE_1(one) \
WRITE_PREP(1) \
m->code[m->pos] = one; \
m->pos += 1;

#define WRITE_2(one, two) \
WRITE_PREP(2) \
m->code[m->pos] = one; \
m->code[m->pos+1] = two; \
m->pos += 2;

#define WRITE_3(one, two, three) \
WRITE_PREP(3) \
m->code[m->pos] = one; \
m->code[m->pos+1] = two; \
m->code[m->pos+2] = three; \
m->pos += 3;

#define WRITE_4(one, two, three, four) \
WRITE_PREP(4) \
m->code[m->pos] = one; \
m->code[m->pos+1] = two; \
m->code[m->pos+2] = three; \
m->code[m->pos+3] = four; \
m->pos += 4;

#define WRITE_5(one, two, three, four, five) \
WRITE_PREP(5) \
m->code[m->pos] = one; \
m->code[m->pos+1] = two; \
m->code[m->pos+2] = three; \
m->code[m->pos+3] = four; \
m->code[m->pos+4] = five; \
m->pos += 5;

/* The emitter sets error->line_adjust with a better line number before calling
   lily_raise. This gives debuggers a chance at a more useful line number.
   Example: integer a = 1.0 +
   1.0
   * line_adjust would be 1 (where the assignment happens), whereas the lexer
   would have the line at 2 (where the 1.0 is collected).
   * Note: This currently excludes nomem errors. */
static char *opname(lily_expr_op op)
{
    char *opnames[] = {"+", "-", "==", "<", "<=", ">", ">=", "="};

    return opnames[op];
}

static inline lily_storage *get_storage_sym(lily_emit_state *emit,
        lily_class *storage_class)
{
    lily_storage *s = storage_class->storage;
    if (s->expr_num == emit->expr_num) {
        /* Storages are circularly linked, so this only occurs when all the
           storages have already been taken. */
        if (!lily_try_add_storage(emit->symtab, storage_class))
            lily_raise_nomem(emit->error);
        s = s->next;
    }

    s->expr_num = emit->expr_num;
    /* Make it so the next node is grabbed next time. */
    storage_class->storage = s->next;

    return s;
}

static void generic_binop(lily_emit_state *emit, lily_ast *ast)
{
    int opcode;
    lily_method_val *m;
    lily_class *lhs_class, *rhs_class, *storage_class;
    lily_storage *s;

    m = emit->target;
    lhs_class = ast->left->result->sig->cls;
    rhs_class = ast->right->result->sig->cls;

    if (lhs_class->id <= SYM_CLASS_STR &&
        rhs_class->id <= SYM_CLASS_STR)
        opcode = generic_binop_table[ast->op][lhs_class->id][rhs_class->id];
    else
        opcode = -1;

    if (opcode == -1) {
        emit->error->line_adjust = ast->line_num;
        lily_raise(emit->error, "Invalid operation: %s %s %s.\n",
                   lhs_class->name, opname(ast->op), rhs_class->name);
    }

    if (ast->op == expr_plus || ast->op == expr_minus)
        if (lhs_class->id >= rhs_class->id)
            storage_class = lhs_class;
        else
            storage_class = rhs_class;
    else
        /* Assign is handled elsewhere, so these are just comparison ops. These
           always return 0 or 1, regardless of the classes put in. There's no
           bool class (yet), so an integer class is used instead. */
        storage_class = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

    s = get_storage_sym(emit, storage_class);

    WRITE_5(opcode,
            ast->line_num,
            (int)ast->left->result,
            (int)ast->right->result,
            (int)s)

    ast->result = (lily_sym *)s;
}

static void write_type(lily_msgbuf *mb, lily_sig *sig)
{
    /* todo: Dump complex stuff for callables. */
    lily_msgbuf_add(mb, sig->cls->name);
}

static void walk_tree(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->target;

    if (ast->expr_type == call) {
        int i, new_pos;

        lily_ast *arg = ast->arg_start;
        lily_var *v = (lily_var *)ast->result;
        lily_call_sig *csig = v->sig->node.call;

        /* The parser has already verified argument count. */
        for (i = 0;arg != NULL;arg = arg->next_arg, i++) {
            if (arg->expr_type != var)
                /* Walk the subexpressions so the result gets calculated. */
                walk_tree(emit, arg);

            /* This currently works because there are no nested funcs or
               methods. */
            if (csig->args[i] != arg->result->sig) {
                if (csig->args[i]->cls->id == SYM_CLASS_OBJECT) {
                    lily_storage *storage;
                    storage = get_storage_sym(emit, csig->args[i]->cls);
                    WRITE_4(o_obj_assign,
                            ast->line_num,
                            (int)storage,
                            (int)ast->result)

                    arg->result = (lily_sym *)storage;
                }
                else {
                    lily_msgbuf *mb = lily_new_msgbuf("Error : ");
                    lily_msgbuf_add(mb, v->name);
                    lily_msgbuf_add(mb, " arg #");
                    lily_msgbuf_add_int(mb, i);
                    lily_msgbuf_add(mb, " expects type '");
                    write_type(mb, csig->args[i]);
                    lily_msgbuf_add(mb, "' but got type '");
                    write_type(mb, arg->result->sig);
                    lily_msgbuf_add(mb, "'.\n");
    
                    /* The arg's line number is used, in case it's on a different
                    line than the call. */
                    emit->error->line_adjust = arg->line_num;
                    lily_raise_msgbuf(emit->error, mb);
                }
            }
        }

        new_pos = m->pos + 6 + ast->args_collected;
        WRITE_PREP_LARGE(6 + ast->args_collected)

        if (v->sig->cls->id == SYM_CLASS_FUNCTION)
            m->code[m->pos] = o_func_call;
        else
            m->code[m->pos] = o_method_call;

        m->code[m->pos+1] = ast->line_num;
        m->code[m->pos+2] = (int)v;
        m->code[m->pos+3] = (int)v->value.ptr;
        m->code[m->pos+4] = ast->args_collected;
        for (i = 6, arg = ast->arg_start;
            arg != NULL;
            arg = arg->next_arg, i++) {
            m->code[m->pos + i] = (int)arg->result;
        }

        if (csig->ret != NULL) {
            lily_storage *s = get_storage_sym(emit, csig->ret->cls);

            ast->result = (lily_sym *)s;
        }
        else {
            /* It's okay to not push a return value, unless something needs it.
               Assume that if the tree has a parent, something needs a value. */
            if (ast->parent == NULL)
                ast->result = NULL;
            else {
                emit->error->line_adjust = ast->line_num;
                lily_raise(emit->error,
                           "Call returning nil not at end of expression.");
            }
        }

        m->code[m->pos+5] = (int)ast->result;
        m->pos = new_pos;
    }
    else if (ast->expr_type == binary) {
        /* lhs and rhs must be walked first, regardless of op. */
        if (ast->left->expr_type != var)
            walk_tree(emit, ast->left);

        if (ast->right->expr_type != var)
            walk_tree(emit, ast->right);

        if (ast->op == expr_assign) {
            int opcode;
            lily_sym *left_sym, *right_sym;
            left_sym = ast->left->result;
            right_sym = ast->right->result;

            if (left_sym->sig != right_sym->sig) {
                if (left_sym->sig->cls->id == SYM_CLASS_OBJECT)
                    opcode = o_obj_assign;
                else {
                    emit->error->line_adjust = ast->line_num;
                    lily_raise(emit->error, "Cannot assign %s to %s.\n",
                               left_sym->sig->cls->name,
                               right_sym->sig->cls->name);
                }
            }
            else if (left_sym->sig->cls->id == SYM_CLASS_STR)
                opcode = o_str_assign;
            else
                opcode = o_assign;

            WRITE_4(opcode,
                    ast->line_num,
                    (int)left_sym,
                    (int)right_sym)
        }
        else
            generic_binop(emit, ast);
    }
}

void lily_emit_ast(lily_emit_state *emit, lily_ast *ast)
{
    walk_tree(emit, ast);
    emit->expr_num++;
}

void lily_emit_conditional(lily_emit_state *emit, lily_ast *ast)
{
    /* This does emitting for the condition of an if or elif. */
    lily_method_val *m = emit->target;

    walk_tree(emit, ast);
    emit->expr_num++;

    /* This jump will need to be rewritten with the first part of the next elif,
       else, or the end of the if. Save the position so it can be written over
       later. */
    WRITE_3(o_jump_if_false, (int)ast->result, 0)

    if (emit->patch_pos == emit->patch_size) {
        emit->patch_size *= 2;

        int *new_patches = lily_realloc(emit->patches,
            sizeof(int) * emit->patch_size);

        if (new_patches == NULL)
            lily_raise_nomem(emit->error);

        emit->patches = new_patches;
    }

    emit->patches[emit->patch_pos] = m->pos-1;
    emit->patch_pos++;
}

/* This is called at the end of an 'if' branch, but before the beginning of the
   new one. */
void lily_emit_clear_block(lily_emit_state *emit, int have_else)
{
    int save_jump;
    lily_method_val *m = emit->target;
    lily_var *v = emit->block_var_starts[emit->block_pos-1];

    if (have_else) {
        if (emit->block_types[emit->block_pos-1] == BLOCK_IFELSE)
            lily_raise(emit->error, "Only one 'else' per 'if' allowed.\n");
        else
            emit->block_types[emit->block_pos-1] = BLOCK_IFELSE;
    }
    else if (emit->block_types[emit->block_pos-1] == BLOCK_IFELSE)
        lily_raise(emit->error, "'elif' after 'else'.\n");

    if (v->next != NULL)
        lily_drop_block_vars(emit->symtab, v);

    /* Write an exit jump for this branch, thereby completing the branch. */
    WRITE_2(o_jump, 0);
    save_jump = m->pos - 1;

    /* The last jump actually collected wanted to know where the next branch
       would start. It's where the code is now. */
    m->code[emit->patches[emit->patch_pos-1]] = m->pos;
    /* Write the exit jump where the branch jump was. Since the branch jump got
       patched, storing the location is pointless. */
    emit->patches[emit->patch_pos-1] = save_jump;
}

void lily_emit_push_block(lily_emit_state *emit, int btype)
{
    if (emit->block_pos == emit->block_size) {
        emit->block_size *= 2;
        int *new_types = lily_realloc(emit->block_types,
            sizeof(int) * emit->block_size);
        lily_var **new_var_starts = lily_realloc(emit->block_var_starts,
            sizeof(lily_var *) * emit->block_size);

        if (new_types == NULL || new_var_starts == NULL) {
            lily_free(new_types);
            lily_free(new_var_starts);
            lily_raise_nomem(emit->error);
        }
        emit->block_types = new_types;
        emit->block_var_starts = new_var_starts;
    }

    if (btype == BLOCK_IF) {
        if (emit->ctrl_patch_pos == emit->ctrl_patch_size) {
            emit->ctrl_patch_size *= 2;
            int *new_starts = lily_realloc(emit->ctrl_patch_starts,
                sizeof(int) * emit->ctrl_patch_size);

            if (new_starts == NULL)
                lily_raise_nomem(emit->error);

            emit->ctrl_patch_starts = new_starts;
        }
        emit->ctrl_patch_starts[emit->ctrl_patch_pos] = emit->patch_pos;
        emit->ctrl_patch_pos++;
        emit->block_var_starts[emit->block_pos] = emit->symtab->var_top;
    }
    else if (btype == BLOCK_METHOD) {
        lily_var *v = emit->method_targets[emit->method_pos-1];
        emit->block_var_starts[emit->block_pos] = v;
    }

    emit->block_types[emit->block_pos] = btype;
    emit->block_pos++;
}

void lily_emit_pop_block(lily_emit_state *emit)
{
    lily_var *v;
    int btype;

    emit->block_pos--;
    v = emit->block_var_starts[emit->block_pos];
    btype = emit->block_types[emit->block_pos];

    if (v->next != NULL)
        lily_drop_block_vars(emit->symtab, v);

    if (btype < BLOCK_METHOD) {
        emit->ctrl_patch_pos--;

        int from, to, pos;
        from = emit->patch_pos-1;
        to = emit->ctrl_patch_starts[emit->ctrl_patch_pos];
        pos = emit->target->pos;

        for (;from >= to;from--)
            emit->target->code[emit->patches[from]] = pos;

        /* Use the space for new patches now. */
        emit->patch_pos = to;
    }
    else
        lily_emit_leave_method(emit);
}

void lily_emit_enter_method(lily_emit_state *emit, lily_var *var)
{
    if (emit->method_pos == emit->method_size) {
        emit->method_size *= 2;
        lily_method_val **new_vals = lily_realloc(emit->method_vals,
            sizeof(lily_method_val *) * emit->method_size);
        lily_sig **new_rets = lily_realloc(emit->method_rets,
            sizeof(lily_sig *) * emit->method_size);
        lily_var **new_targets = lily_realloc(emit->method_targets,
            sizeof(lily_var *) * emit->method_size);

        if (new_vals == NULL || new_rets == NULL || new_targets == NULL) {
            lily_free(new_vals);
            lily_free(new_rets);
            lily_free(new_targets);
            lily_raise_nomem(emit->error);
        }

        emit->method_vals = new_vals;
        emit->method_rets = new_rets;
        emit->method_targets = new_targets;
    }

    emit->target = (lily_method_val *)var->value.ptr;
    emit->method_rets[emit->method_pos] = var->sig->node.call->ret;
    emit->method_vals[emit->method_pos] = emit->target;
    emit->method_targets[emit->method_pos] = var;
    emit->method_pos++;
    emit->target_ret = var->sig->node.call->ret;
    lily_emit_push_block(emit, BLOCK_METHOD);
}

void lily_emit_leave_method(lily_emit_state *emit)
{
    emit->method_pos--;
    emit->target = emit->method_vals[emit->method_pos-1];
    emit->target_ret = emit->method_rets[emit->method_pos-1];
}

void lily_emit_return(lily_emit_state *emit, lily_ast *ast, lily_sig *sig)
{
    walk_tree(emit, ast);
    emit->expr_num++;

    lily_method_val *m = emit->target;
    WRITE_2(o_return_val, (int)ast->result)
}

void lily_emit_vm_return(lily_emit_state *emit)
{
    lily_method_val *m = emit->target;
    WRITE_1(o_vm_return)
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_free(emit->patches);
    lily_free(emit->ctrl_patch_starts);
    lily_free(emit->block_var_starts);
    lily_free(emit->block_types);
    lily_free(emit->method_vals);
    lily_free(emit->method_rets);
    lily_free(emit->method_targets);
    lily_free(emit);
}

lily_emit_state *lily_new_emit_state(lily_excep_data *excep)
{
    lily_emit_state *s = lily_malloc(sizeof(lily_emit_state));

    if (s == NULL)
        return NULL;

    s->patches = lily_malloc(sizeof(int) * 4);
    s->ctrl_patch_starts = lily_malloc(sizeof(int) * 4);
    s->block_var_starts = lily_malloc(sizeof(lily_var *) * 4);
    s->block_types = lily_malloc(sizeof(int) * 4);
    s->method_vals = lily_malloc(sizeof(lily_method_val *) * 4);
    s->method_rets = lily_malloc(sizeof(lily_sig *) * 4);
    s->method_targets = lily_malloc(sizeof(lily_var *) * 4);

    if (s->patches == NULL || s->ctrl_patch_starts == NULL ||
        s->block_var_starts == NULL || s->block_types == NULL ||
        s->method_vals == NULL || s->method_rets == NULL ||
        s->method_targets == NULL) {
        lily_free_emit_state(s);
        return NULL;
    }

    s->patch_pos = 0;
    s->patch_size = 4;
    s->ctrl_patch_pos = 0;
    s->ctrl_patch_size = 4;
    s->block_pos = 0;
    s->block_size = 4;
    s->method_pos = 0;
    s->method_size = 4;

    s->error = excep;
    s->expr_num = 1;

    return s;
}

/* Prepare @lily_main to receive new instructions after a parse step. Debug and
   the vm stay within 'pos', so no need to actually clear the code. */
void lily_reset_main(lily_emit_state *emit)
{
    ((lily_method_val *)emit->target)->pos = 0;
}

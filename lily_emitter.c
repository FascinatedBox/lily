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

    s = storage_class->storage;

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

    WRITE_4(opcode,
            (int)ast->left->result,
            (int)ast->right->result,
            (int)s)

    ast->result = (lily_sym *)s;
}

static void write_type(lily_msgbuf *mb, lily_sig *sig)
{
    /* todo: Dump complex stuff for functions and methods. */
    lily_msgbuf_add(mb, sig->cls->name);
}

static void walk_tree(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->target;

    if (ast->expr_type == func_call) {
        int i, new_pos;

        lily_ast *arg = ast->arg_start;
        lily_var *v = (lily_var *)ast->result;
        lily_func_sig *func_sig = v->sig->node.func;

        /* The parser has already verified argument count. */
        for (i = 0;arg != NULL;arg = arg->next_arg, i++) {
            if (arg->expr_type != var)
                /* Walk the subexpressions so the result gets calculated. */
                walk_tree(emit, arg);

            /* This currently works because there are no nested funcs or
               methods. */
            if (func_sig->args[i] != arg->result->sig) {
                lily_msgbuf *mb = lily_new_msgbuf("Error : ");
                lily_msgbuf_add(mb, v->name);
                lily_msgbuf_add(mb, " arg #");
                lily_msgbuf_add_int(mb, i);
                lily_msgbuf_add(mb, " expects type '");
                write_type(mb, func_sig->args[i]);
                lily_msgbuf_add(mb, "' but got type '");
                write_type(mb, arg->result->sig);
                lily_msgbuf_add(mb, "'.\n");
                lily_raise_msgbuf(emit->error, mb);
            }
        }

        new_pos = m->pos + 5 + ast->args_collected;
        WRITE_PREP(5 + ast->args_collected)

        if (v->sig->cls->id == SYM_CLASS_FUNCTION)
            m->code[m->pos] = o_func_call;
        else
            m->code[m->pos] = o_method_call;
        m->code[m->pos+1] = (int)v;
        m->code[m->pos+2] = (int)v->value.ptr;
        m->code[m->pos+3] = ast->args_collected;
        for (i = 5, arg = ast->arg_start;
            arg != NULL;
            arg = arg->next_arg, i++) {
            m->code[m->pos + i] = (int)arg->result;
        }

        if (func_sig->ret != NULL) {
            lily_class *storage_class = func_sig->ret->cls;
            lily_storage *s = storage_class->storage;

            if (s->expr_num == emit->expr_num) {
                /* Storages are circularly linked, so this only occurs when all
                   the storages have already been taken. */
                if (!lily_try_add_storage(emit->symtab, storage_class))
                    lily_raise_nomem(emit->error);
                s = s->next;
            }

            ast->result = (lily_sym *)s;
        }
        else
            ast->result = NULL;

        m->code[m->pos+4] = (int)ast->result;
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
            else
                opcode = o_assign;

            WRITE_3(opcode,
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

    lily_branches *b = emit->branches;

    if (b->patch_pos >= b->patch_size) {
        int *new_patches;

        b->patch_size *= 2;
        new_patches = lily_realloc(b->patches, sizeof(int) * b->patch_size);
        if (new_patches == NULL)
            lily_raise_nomem(emit->error);

        b->patches = new_patches;
    }

    b->patches[b->patch_pos] = m->pos-1;
    b->patch_pos++;
}

/*  Okay, if handling. This is a tough one.
    VM code is expressed as a series of instructions, but ifs require skipping
    some parts of code. This is done with a jump instruction. Since the future
    location is not known, it has to be fixed later on. Each if/elif/else is
    refered to as a branch.
    Within if/elif/else, there are two kinds of jumps:
    * Branch->branch. When the expression for an if tests false, control must
      transfer to the next elif, or the else. This is the same for elif's.
      If has only one branch jump at a time, except when there is nesting.
    * Exit. After the final statement in a branch, a jump is needed to avoid
      execution of any other code within the if.
    Jumps should be patched from a save location to current position. Also,
    remember that patch_pos is ahead most of the time. */

void lily_emit_clear_block(lily_emit_state *emit, int have_else)
{
    int save_jump;
    lily_branches *branches = emit->branches;
    lily_method_val *m = emit->target;
    lily_var *v = branches->saved_vars[branches->type_pos-1];

    if (have_else) {
        if (branches->types[branches->type_pos-1] == BLOCK_IFELSE)
            lily_raise(emit->error, "Only one 'else' per 'if' allowed.\n");
        else
            branches->types[branches->type_pos-1] = BLOCK_IFELSE;
    }
    else if (branches->types[branches->type_pos-1] == BLOCK_IFELSE)
        lily_raise(emit->error, "'elif' after 'else'.\n");

    if (v->next != NULL)
        lily_drop_block_vars(emit->symtab, v);

    /* When changing from an if/elif to another elif/else, two things need to
       be done:
       * An exit jump must be added that will go to the end of the condition.
       * The branch jump must be fixed to target the start of the next branch.
       The statements must go in this order. */

    WRITE_2(o_jump, 0);
    save_jump = m->pos - 1;

    /* Suppose that we're in the second elif branch, transitioning to the third.
       Jump stack:
       * [0] if branch exit jump
       * [1] elif branch #1 exit jump
       * [2] elif branch #2 branch jump
       Saves are 0, 2 (exit jumps start at 0, branch jumps at 2)
       * This writes [2]'s fix, sets [2] to the new exit jump, then changes the
       last save so [2] is counted as an exit jump. */

    m->code[branches->patches[branches->patch_pos-1]] = m->pos;
    branches->patches[branches->patch_pos-1] = save_jump;
    branches->saved_spots[branches->save_pos-1]++;
}

void lily_emit_push_block(lily_emit_state *emit, int btype)
{
    lily_branches *branches = emit->branches;

    if (branches->type_pos + 1 >= branches->type_size) {
        branches->type_size *= 2;
        int *new_types = lily_realloc(branches->types,
                                      sizeof(int) * branches->type_size);
        lily_var **new_saved_vars = lily_realloc(branches->saved_vars,
            sizeof(lily_var *) * branches->type_size);

        if (new_types == NULL || new_saved_vars == NULL) {
            lily_free(new_types);
            lily_free(new_saved_vars);
            lily_raise_nomem(emit->error);
        }

        branches->saved_vars = new_saved_vars;
        branches->types = new_types;
    }

    if (btype == BLOCK_IF) {
        if (branches->save_pos + 2 >= branches->save_size) {
            branches->save_size *= 2;
            int *new_saves = lily_realloc(branches->saved_spots,
                                        sizeof(int) * branches->save_size);
            if (new_saves == NULL)
                lily_raise_nomem(emit->error);

            branches->saved_spots = new_saves;
        }
        /* The bottom one is for exit jumps, the top for the branch jump. */
        branches->saved_spots[branches->save_pos] = branches->patch_pos;
        branches->saved_spots[branches->save_pos+1] = branches->patch_pos;
        branches->save_pos += 2;
        branches->saved_vars[branches->type_pos] = emit->symtab->var_top;
    }
    else if (btype == BLOCK_RETURN) {
        if (branches->save_pos + 1 >= branches->save_size) {
            branches->save_size *= 2;
            int *new_saves = lily_realloc(branches->saved_spots,
                                        sizeof(int) * branches->save_size);
            if (new_saves == NULL)
                lily_raise_nomem(emit->error);

            branches->saved_spots = new_saves;
        }
        branches->saved_spots[branches->save_pos] = branches->patch_pos;
        branches->save_pos++;
        /* The method's arguments have already been registered, and they'll need
           to go away when the method is done. So start from the method, not the
           current var. */
        branches->saved_vars[branches->type_pos] = emit->target_var;
    }

    branches->types[branches->type_pos] = btype;
    branches->type_pos++;
}

void lily_emit_pop_block(lily_emit_state *emit)
{
    lily_branches *branches = emit->branches;
    lily_method_val *m = emit->target;
    lily_var *v;
    int btype, from, to;

    btype = branches->types[branches->type_pos-1];
    from = branches->patch_pos-1;

    if (btype <= BLOCK_IFELSE) {
        to = branches->saved_spots[branches->save_pos-2];
        branches->save_pos--;
    }
    else if (btype == BLOCK_RETURN) {
        to = branches->saved_spots[branches->save_pos-1];
        lily_emit_leave_method(emit);
    }

    v = branches->saved_vars[branches->type_pos-1];
    if (v->next != NULL)
        lily_drop_block_vars(emit->symtab, v);

    for (;from >= to;from--)
        m->code[branches->patches[from]] = m->pos;

    branches->save_pos--;
    branches->type_pos--;
}

void lily_emit_enter_method(lily_emit_state *emit, lily_var *var)
{
    emit->saved_var = emit->target_var;
    emit->target_ret = var->sig->node.func->ret;
    emit->target_var = var;
    emit->target = (lily_method_val *)var->value.ptr;
    lily_emit_push_block(emit, BLOCK_RETURN);
}

void lily_emit_leave_method(lily_emit_state *emit)
{
    emit->target_var = emit->saved_var;
    emit->target_ret = emit->target_var->sig->node.func->ret;
    emit->target = (lily_method_val *)emit->target_var->value.ptr;
    emit->saved_var = NULL;
}

void lily_emit_return(lily_emit_state *emit, lily_ast *ast, lily_sig *sig)
{
    walk_tree(emit, ast);
    emit->expr_num++;

    lily_method_val *m = emit->target;
    WRITE_2(o_return_val, (int)ast->result)
}

void lily_emit_set_target(lily_emit_state *emit, lily_var *var)
{
    emit->target_var = var;
    emit->target = (lily_method_val *)var->value.ptr;
}

void lily_emit_vm_return(lily_emit_state *emit)
{
    lily_method_val *m = emit->target;
    WRITE_1(o_vm_return)
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_free(emit->branches->patches);
    lily_free(emit->branches->saved_spots);
    lily_free(emit->branches->types);
    lily_free(emit->branches);
    lily_free(emit);
}

lily_emit_state *lily_new_emit_state(lily_excep_data *excep)
{
    lily_emit_state *s = lily_malloc(sizeof(lily_emit_state));

    if (s == NULL)
        return NULL;

    int *patches = lily_malloc(sizeof(int) * 4);
    int *saves = lily_malloc(sizeof(int) * 4);
    int *types = lily_malloc(sizeof(int) * 4);
    lily_var **saved_vars = lily_malloc(sizeof(lily_var *) * 4);
    s->branches = lily_malloc(sizeof(lily_branches));

    if (s->branches == NULL || patches == NULL || saves == NULL ||
        types == NULL || saved_vars == NULL) {
        lily_free(s->branches);
        lily_free(saves);
        lily_free(patches);
        lily_free(types);
        lily_free(saved_vars);
        lily_free(s);
        return NULL;
    }

    s->branches->patches = patches;
    s->branches->saved_spots = saves;
    s->branches->types = types;
    s->branches->saved_vars = saved_vars;
    s->branches->type_pos = 0;
    s->branches->type_size = 4;
    s->branches->save_pos = 0;
    s->branches->save_size = 4;
    s->branches->patch_pos = 0;
    s->branches->patch_size = 4;
    s->target = NULL;
    s->error = excep;
    s->expr_num = 1;

    return s;
}

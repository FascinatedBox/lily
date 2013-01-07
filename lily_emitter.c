#include <stdlib.h>
#include <string.h>

#include "lily_ast.h"
#include "lily_impl.h"
#include "lily_emitter.h"
#include "lily_opcode.h"
#include "lily_emit_table.h"

#define WRITE_PREP(size) \
if ((m->pos + size) > m->len) { \
    uintptr_t *save_code; \
    m->len *= 2; \
    save_code = lily_realloc(m->code, sizeof(uintptr_t) * m->len); \
    if (save_code == NULL) \
        lily_raise_nomem(emit->raiser); \
    m->code = save_code; \
}

#define SAVE_PREP(size) \
if ((emit->save_cache_pos + size) > emit->save_cache_size) { \
    uintptr_t *save_cache = emit->save_cache; \
    while ((emit->save_cache_pos + size) > emit->save_cache_size) \
        emit->save_cache_size *= 2; \
    save_cache = lily_realloc(emit->save_cache, sizeof(uintptr_t) * \
            emit->save_cache_size); \
    if (save_cache == NULL) \
        lily_raise_nomem(emit->raiser); \
    emit->save_cache = save_cache; \
}

/* Most ops need 4 or less code spaces, so only growing once is okay. However,
   calls need 5 + #args. So more than one grow might be necessary.
   Note: This macro may need to check for overflow later. */
#define WRITE_PREP_LARGE(size) \
if ((m->pos + size) > m->len) { \
    uintptr_t *save_code; \
    while ((m->pos + size) > m->len) \
        m->len *= 2; \
    save_code = lily_realloc(m->code, sizeof(uintptr_t) * m->len); \
    if (save_code == NULL) \
        lily_raise_nomem(emit->raiser); \
    m->code = save_code; \
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

/* The emitter sets raiser->line_adjust with a better line number before calling
   lily_raise. This gives debuggers a chance at a more useful line number.
   Example: integer a = 1.0 +
   1.0
   * line_adjust would be 1 (where the assignment happens), whereas the lexer
   would have the line at 2 (where the 1.0 is collected).
   * Note: This currently excludes nomem errors. */
static char *opname(lily_expr_op op)
{
    char *opnames[] = {"+", "-", "==", "<", "<=", ">", ">=", "-", "="};

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
            lily_raise_nomem(emit->raiser);
        s = s->next;
    }

    s->expr_num = emit->expr_num;
    /* Make it so the next node is grabbed next time. */
    storage_class->storage = s->next;

    return s;
}

static void walk_tree(lily_emit_state *, lily_ast *);

static void do_jump_for_tree(lily_emit_state *emit, lily_ast *ast, int jump_on)
{
    lily_method_val *m = emit->target;

    WRITE_4(o_jump_if, jump_on, (uintptr_t)ast->result, 0);
    if (emit->patch_pos == emit->patch_size) {
        emit->patch_size *= 2;

        int *new_patches = lily_realloc(emit->patches,
            sizeof(int) * emit->patch_size);

        if (new_patches == NULL)
            lily_raise_nomem(emit->raiser);

        emit->patches = new_patches;
    }

    emit->patches[emit->patch_pos] = m->pos-1;
    emit->patch_pos++;
}

/* This handles both logical and, and logical or. */
static void do_logical_op(lily_emit_state *emit, lily_ast *ast)
{
    lily_symtab *symtab = emit->symtab;
    lily_storage *result;
    int is_top, jump_on;
    lily_method_val *m = emit->target;

    jump_on = (ast->op == expr_logical_or);

    /* The first tree must create the block, so that subsequent trees have a
       place to write the patches to. */
    if (ast->parent == NULL || 
        (ast->parent->expr_type == binary && ast->parent->op != ast->op)) {
        is_top = 1;
        lily_emit_push_block(emit, BLOCK_ANDOR);
    }
    else
        is_top = 0;

    /* The bottom tree is responsible for getting the storage. */
    if (ast->left->expr_type != binary || ast->left->op != ast->op) {
        result = get_storage_sym(emit,
            lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER));

        if (ast->left->expr_type != var)
            walk_tree(emit, ast->left);

        do_jump_for_tree(emit, ast->left, jump_on);
    }
    else {
        /* and/or do not require do_jump_for_tree, because that would be a
           double-check! */
        walk_tree(emit, ast->left);
        result = (lily_storage *)ast->left->result;
    }

    if (ast->right->expr_type != var)
        walk_tree(emit, ast->right);

    do_jump_for_tree(emit, ast->right, jump_on);

    if (is_top == 1) {
        /* The symtab adds literals 0 and 1 in that order during its init. */
        int save_pos;
        lily_literal *success, *failure;

        success = symtab->lit_start;
        if (ast->op == expr_logical_or)
            failure = success->next;
        else {
            failure = success;
            success = failure->next;
        }

        WRITE_4(o_assign,
                ast->line_num,
                (uintptr_t)result,
                (uintptr_t)success);
        WRITE_2(o_jump, 0);
        save_pos = m->pos-1;

        lily_emit_pop_block(emit);
        WRITE_4(o_assign,
                ast->line_num,
                (uintptr_t)result,
                (uintptr_t)failure);

        m->code[save_pos] = m->pos;
    }

    ast->result = (lily_sym *)result;
}

static void do_unary_op(lily_emit_state *emit, lily_ast *ast)
{
    uintptr_t opcode;
    lily_class *lhs_class;
    lily_storage *s;
    lily_method_val *m;

    m = emit->target;
    lhs_class = ast->left->result->sig->cls;
    if (lhs_class->id != SYM_CLASS_INTEGER) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax, "Invalid operation: %s%s.\n",
                   opname(ast->op), lhs_class->name);
    }

    s = get_storage_sym(emit,
            lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER));

    if (ast->op == expr_unary_minus)
        opcode = o_unary_minus;
    else if (ast->op == expr_unary_not)
        opcode = o_unary_not;

    WRITE_4(opcode,
            (uintptr_t)ast->line_num,
            (uintptr_t)ast->left->result,
            (uintptr_t)s);

    ast->result = (lily_sym *)s;
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
        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "Invalid operation: %s %s %s.\n", lhs_class->name,
                   opname(ast->op), rhs_class->name);
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
            (uintptr_t)ast->left->result,
            (uintptr_t)ast->right->result,
            (uintptr_t)s)

    ast->result = (lily_sym *)s;
}

static void write_type(lily_msgbuf *mb, lily_sig *sig)
{
    /* todo: Dump complex stuff for callables. */
    lily_msgbuf_add(mb, sig->cls->name);
}

static void do_bad_arg_error(lily_emit_state *emit, lily_ast *ast,
    lily_sig *got, int arg_num)
{
    lily_var *v = (lily_var *)ast->result;
    lily_call_sig *csig = v->sig->node.call;

    lily_msgbuf *mb = lily_new_msgbuf("Error : ");
    lily_msgbuf_add(mb, v->name);
    lily_msgbuf_add(mb, " arg #");
    lily_msgbuf_add_int(mb, arg_num);
    lily_msgbuf_add(mb, " expects type '");
    write_type(mb, csig->args[arg_num]);
    lily_msgbuf_add(mb, "' but got type '");
    write_type(mb, got);
    lily_msgbuf_add(mb, "'.\n");

    /* Just in case this arg was on a different line than the call. */
    emit->raiser->line_adjust = ast->line_num;
    lily_raise_msgbuf(emit->raiser, lily_ErrSyntax, mb);
}

static void check_call_args(lily_emit_state *emit, lily_ast *ast,
        lily_call_sig *csig)
{
    lily_ast *arg = ast->arg_start;
    int i, is_varargs, num_args;
    lily_method_val *m = emit->target;

    is_varargs = csig->is_varargs;
    num_args = csig->num_args;

    SAVE_PREP(num_args)
    /* The parser has already verified argument count. */
    for (i = 0;i != num_args;arg = arg->next_arg, i++) {
        if (arg->expr_type != var) {
            /* Walk the subexpressions so the result gets calculated. */
            walk_tree(emit, arg);
            if (arg->result != NULL) {
                emit->save_cache[emit->save_cache_pos] = (uintptr_t)arg->result;
                emit->save_cache_pos++;
            }
        }

        /* This currently works because there are no nested funcs or
           methods. */
        if (csig->args[i] != arg->result->sig) {
            if (csig->args[i]->cls->id == SYM_CLASS_OBJECT) {
                lily_storage *storage;
                storage = get_storage_sym(emit, csig->args[i]->cls);
                WRITE_4(o_obj_assign,
                        ast->line_num,
                        (uintptr_t)storage,
                        (uintptr_t)arg->result)

                arg->result = (lily_sym *)storage;
            }
            else
                do_bad_arg_error(emit, ast, arg->result->sig, i);
        }
    }

    if (is_varargs) {
        /* Remember that the above finishes with i == num_args, and the
           args are 0-based. So fix i. */
        i--;
        int j = 0;
        for (;arg != NULL;arg = arg->next_arg, j++) {
            if (arg->expr_type != var) {
                /* Walk the subexpressions so the result gets calculated. */
                walk_tree(emit, arg);
                if (arg->result != NULL) {
                    emit->save_cache[emit->save_cache_pos] = (uintptr_t)arg->result;
                    emit->save_cache_pos++;
                }
            }

            /* This currently works because there are no nested funcs or
               methods. */
            if (csig->args[i] != arg->result->sig) {
                if (csig->args[i]->cls->id == SYM_CLASS_OBJECT) {
                    lily_storage *storage;
                    storage = get_storage_sym(emit, csig->args[i]->cls);
                    WRITE_4(o_obj_assign,
                            ast->line_num,
                            (uintptr_t)storage,
                            (uintptr_t)arg->result)

                    arg->result = (lily_sym *)storage;
                }
                else
                    do_bad_arg_error(emit, ast, arg->result->sig, i);
            }
        }
    }
}

static void walk_tree(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->target;

    if (ast->expr_type == call) {
        lily_ast *arg = ast->arg_start;
        lily_var *v = (lily_var *)ast->result;
        lily_call_sig *csig = v->sig->node.call;
        int expect_size, i, is_method, num_local_saves, num_saves;
        lily_var *local_var;

        is_method = (v->sig->cls->id == SYM_CLASS_METHOD);
        expect_size = 6 + ast->args_collected;

        if (!is_method) {
            int cache_start = emit->save_cache_pos;
            check_call_args(emit, ast, csig);

            /* For functions, the args are pushed to be saved in case one of
               them is a method (which will drain the cache). If there were no
               methods, then remove all of the function's args, since they won't
               need saving. */
            if (emit->save_cache_pos > cache_start)
                emit->save_cache_pos = cache_start;

            num_saves = 0;
        }
        else {
            check_call_args(emit, ast, csig);

            if (ast->parent != NULL && ast->parent->expr_type == binary &&
                ast->parent->right == ast &&
                ast->parent->left->expr_type != var) {
                /* Ex: fib(n-1) + fib(n-2)
                   In this case, the left side of the plus (the first fib call)
                   is something saved to a storage. Since the plus hasn't been
                   completed, this call could modify the storage of the left
                   side. So save that storage. */
                SAVE_PREP(1)
                emit->save_cache[emit->save_cache_pos] = (uintptr_t)ast->parent->left->result;
                emit->save_cache_pos++;
            }

            num_saves = emit->save_cache_pos;
            /* Do not save @main's "local's" (they're globals). */
            if (emit->method_pos > 1) {
                num_local_saves = emit->symtab->var_top->id -
                    emit->method_id_offsets[emit->method_pos-1];
                num_saves += num_local_saves;
                local_var = emit->method_targets[emit->method_pos-1]->next;
            }
            else
                num_local_saves = 0;

            /* o_save needs 2 + #saves, o_restore needs a flat 2. */
            if (num_saves)
                expect_size += num_saves + 4;
        }

        WRITE_PREP_LARGE(expect_size)

        if (num_saves) {
            m->code[m->pos] = o_save;
            m->code[m->pos+1] = num_saves;
            m->pos += 2;

            if (emit->save_cache_pos) {
                memcpy(m->code + m->pos, emit->save_cache,
                       emit->save_cache_pos * sizeof(uintptr_t));

                m->pos += emit->save_cache_pos;
                emit->save_cache_pos = 0;
            }
            if (num_local_saves) {
                for (i = 0;i < num_local_saves;i++) {
                    m->code[m->pos+i] = (uintptr_t)local_var;
                    local_var = local_var->next;
                }

                m->pos += num_local_saves;
            }
        } 

        if (is_method)
            m->code[m->pos] = o_method_call;
        else
            m->code[m->pos] = o_func_call;

        m->code[m->pos+1] = ast->line_num;
        m->code[m->pos+2] = (uintptr_t)v;
        m->code[m->pos+3] = (uintptr_t)v->value.ptr;
        m->code[m->pos+4] = ast->args_collected;

        for (i = 6, arg = ast->arg_start;
            arg != NULL;
            arg = arg->next_arg, i++) {
            m->code[m->pos + i] = (uintptr_t)arg->result;
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
                emit->raiser->line_adjust = ast->line_num;
                lily_raise(emit->raiser, lily_ErrSyntax,
                           "Call returning nil not at end of expression.");
            }
        }

        m->code[m->pos+5] = (uintptr_t)ast->result;
        m->pos += 6 + ast->args_collected;
        if (num_saves) {
            m->code[m->pos] = o_restore;
            m->code[m->pos+1] = num_saves;
            m->pos += 2;
        }
    }
    else if (ast->expr_type == binary) {
        if (ast->op == expr_assign) {
            int opcode;
            lily_sym *left_sym, *right_sym;

            if (ast->left->expr_type != var) {
                emit->raiser->line_adjust = ast->line_num;
                lily_raise(emit->raiser, lily_ErrSyntax,
                           "Left side of = is not a var.\n");
            }

            if (ast->right->expr_type != var)
                walk_tree(emit, ast->right);

            left_sym = ast->left->result;
            right_sym = ast->right->result;

            if (left_sym->sig != right_sym->sig) {
                if (left_sym->sig->cls->id == SYM_CLASS_OBJECT)
                    opcode = o_obj_assign;
                else {
                    emit->raiser->line_adjust = ast->line_num;
                    lily_raise(emit->raiser, lily_ErrSyntax,
                               "Cannot assign %s to %s.\n",
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
                    (uintptr_t)left_sym,
                    (uintptr_t)right_sym)
        }
        else if (ast->op == expr_logical_or || ast->op == expr_logical_and)
            do_logical_op(emit, ast);
        else {
            if (ast->left->expr_type != var)
                walk_tree(emit, ast->left);

            if (ast->right->expr_type != var)
                walk_tree(emit, ast->right);

            generic_binop(emit, ast);
        }
    }
    else if (ast->expr_type == parenth) {
        if (ast->arg_start->expr_type != var)
            walk_tree(emit, ast->arg_start);
        ast->result = ast->arg_start->result;
    }
    else if (ast->expr_type == unary) {
        if (ast->left->expr_type != var)
            walk_tree(emit, ast->left);

        do_unary_op(emit, ast);
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
    walk_tree(emit, ast);
    emit->expr_num++;

    /* This jump will need to be rewritten with the first part of the next elif,
       else, or the end of the if. Save the position so it can be written over
       later.
       0 for jump_if_false. */
    do_jump_for_tree(emit, ast, 0);
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
            lily_raise(emit->raiser, lily_ErrSyntax,
                       "Only one 'else' per 'if' allowed.\n");
        else
            emit->block_types[emit->block_pos-1] = BLOCK_IFELSE;
    }
    else if (emit->block_types[emit->block_pos-1] == BLOCK_IFELSE)
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "'elif' after 'else'.\n");

    if (v->next != NULL) {
        int offset_add = lily_drop_block_vars(emit->symtab, v);
        emit->method_id_offsets[emit->method_pos-1] += offset_add;
    }

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
            if (new_types != NULL)
                emit->block_types = new_types;
            if (new_var_starts != NULL)
                emit->block_var_starts = new_var_starts;

            lily_raise_nomem(emit->raiser);
        }
        emit->block_types = new_types;
        emit->block_var_starts = new_var_starts;
    }

    if (btype == BLOCK_IF || btype == BLOCK_ANDOR) {
        if (emit->ctrl_patch_pos == emit->ctrl_patch_size) {
            emit->ctrl_patch_size *= 2;
            int *new_starts = lily_realloc(emit->ctrl_patch_starts,
                sizeof(int) * emit->ctrl_patch_size);

            if (new_starts == NULL)
                lily_raise_nomem(emit->raiser);

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

    /* This is done after a method leaves, so that the offset gets added into
       the outer method. Otherwise, if a method declares a variable after the
       inner method is done, then saves will be wrong from then on. */
    if (v->next != NULL) {
        int offset_add;
        offset_add = lily_drop_block_vars(emit->symtab, v);
        emit->method_id_offsets[emit->method_pos-1] += offset_add;
    }
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
        int *new_offsets = lily_realloc(emit->method_id_offsets,
            sizeof(int) * emit->method_size);

        if (new_vals == NULL || new_rets == NULL || new_targets == NULL ||
            new_offsets == NULL) {
            if (new_vals != NULL)
                emit->method_vals = new_vals;
            if (new_rets != NULL)
                emit->method_rets = new_rets;
            if (new_targets != NULL)
                emit->method_targets = new_targets;
            if (new_offsets != NULL)
                emit->method_id_offsets = new_offsets;

            lily_raise_nomem(emit->raiser);
        }

        emit->method_vals = new_vals;
        emit->method_rets = new_rets;
        emit->method_targets = new_targets;
        emit->method_id_offsets = new_offsets;
    }

    emit->target = (lily_method_val *)var->value.ptr;
    emit->method_rets[emit->method_pos] = var->sig->node.call->ret;
    emit->method_vals[emit->method_pos] = emit->target;
    emit->method_targets[emit->method_pos] = var;
    emit->method_id_offsets[emit->method_pos] = var->id;
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
    WRITE_2(o_return_val, (uintptr_t)ast->result)
}

void lily_emit_return_noval(lily_emit_state *emit)
{
    lily_method_val *m = emit->target;
    WRITE_1(o_return_noval)
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
    lily_free(emit->method_id_offsets);
    lily_free(emit->save_cache);
    lily_free(emit);
}

lily_emit_state *lily_new_emit_state(lily_raiser *raiser)
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
    s->method_id_offsets = lily_malloc(sizeof(int) * 4);
    s->save_cache = lily_malloc(sizeof(uintptr_t) * 4);

    if (s->patches == NULL || s->ctrl_patch_starts == NULL ||
        s->block_var_starts == NULL || s->block_types == NULL ||
        s->method_vals == NULL || s->method_rets == NULL ||
        s->method_targets == NULL || s->method_id_offsets == NULL ||
        s->save_cache == NULL) {
        lily_free_emit_state(s);
        return NULL;
    }

    s->patch_pos = 0;
    s->patch_size = 4;
    s->ctrl_patch_pos = 0;
    s->ctrl_patch_size = 4;
    s->block_pos = 0;
    s->block_size = 4;
    s->save_cache_pos = 0;
    s->save_cache_size = 4;
    s->method_pos = 0;
    s->method_size = 4;

    s->raiser = raiser;
    s->expr_num = 1;

    return s;
}

/* Prepare @lily_main to receive new instructions after a parse step. Debug and
   the vm stay within 'pos', so no need to actually clear the code. */
void lily_reset_main(lily_emit_state *emit)
{
    ((lily_method_val *)emit->target)->pos = 0;
}

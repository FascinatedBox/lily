#include <stdlib.h>

#include "lily_ast.h"
#include "lily_impl.h"
#include "lily_emitter.h"
#include "lily_opcode.h"
#include "lily_emit_table.h"

static char *opname(lily_expr_op op)
{
    char *ret;

    switch (op) {
        case expr_assign:
            ret = "assign";
            break;
        case expr_plus:
            ret = "add";
            break;
        default:
            ret = "undefined";
            break;
    }

    return ret;
}

static void generic_binop(lily_emit_state *emit, lily_ast *ast)
{
    struct lily_bin_expr bx;
    int opcode;
    lily_code_data *cd;
    lily_class *lhs_class, *rhs_class, *storage_class;
    lily_storage *s;

    bx = ast->data.bin_expr;
    cd = emit->target;
    lhs_class = bx.left->result->cls;
    rhs_class = bx.right->result->cls;

    if (lhs_class->id <= SYM_CLASS_STR &&
        rhs_class->id <= SYM_CLASS_STR)
        opcode = generic_binop_table[bx.op][lhs_class->id][rhs_class->id];
    else
        opcode = -1;

    if (opcode == -1)
        lily_raise(emit->error, "Cannot %s %s and %s.\n", opname(bx.op),
                   lhs_class->name,
                   rhs_class->name);

    if (lhs_class->id >= rhs_class->id)
        storage_class = lhs_class;
    else
        storage_class = rhs_class;

    s = storage_class->storage;

    /* Remember that storages are circularly-linked, so if this one
       doesn't work, then all are currently taken. */
    if (s->expr_num != emit->expr_num) {
        /* Add and use a new one. */
        lily_add_storage(emit->symtab, s);
        s = s->next;
    }

    s->expr_num = emit->expr_num;
    /* Make it so the next node is grabbed next time. */
    storage_class->storage = s->next;

    if ((cd->pos + 4) > cd->len) {
        cd->len *= 2;
        cd->code = lily_realloc(cd->code, sizeof(int) * cd->len);
        if (cd->code == NULL)
            lily_raise_nomem(emit->error);
    }

    cd->code[cd->pos] = opcode;
    cd->code[cd->pos+1] = (int)bx.left->result;
    cd->code[cd->pos+2] = (int)bx.right->result;
    cd->code[cd->pos+3] = (int)s;
    cd->pos += 4;

    ast->result = (lily_sym *)s;
}

static void walk_tree(lily_emit_state *emit, lily_ast *ast)
{
    lily_code_data *cd = emit->target;

    if (ast->expr_type == func_call) {
        int i, new_pos;

        lily_ast *arg = ast->data.call.arg_start;
        while (arg != NULL) {
            if (arg->expr_type != var)
                /* Walk the subexpressions so the result gets calculated. */
                walk_tree(emit, arg);

            arg = arg->next_arg;
        }

        new_pos = cd->pos + 1 + ast->data.call.args_collected;
        if (new_pos > cd->len) {
            cd->len *= 2;
            cd->code = lily_realloc(cd->code, sizeof(int) * cd->len);
            if (cd->code == NULL)
                lily_raise_nomem(emit->error);
        }

        cd->code[cd->pos] = o_builtin_print;
        for (i = 1, arg = ast->data.call.arg_start;
             arg != NULL;
             arg = arg->next_arg) {
            cd->code[cd->pos + i] = (int)arg->result;
        }
        cd->pos = new_pos;
    }
    else if (ast->expr_type == binary) {
        /* Make for less typing. */
        struct lily_bin_expr bx = ast->data.bin_expr;

        /* lhs and rhs must be walked first, regardless of op. */
        if (bx.left->expr_type != var)
            walk_tree(emit, bx.left);

        if (bx.right->expr_type != var)
            walk_tree(emit, bx.right);

        if (bx.op == expr_assign) {
            if (bx.left->result->cls != bx.right->result->cls) {
                lily_raise(emit->error, "Cannot assign %s to %s.\n",
                           bx.right->result->cls->name,
                           bx.left->result->cls->name);
            }

            if ((cd->pos + 3) > cd->len) {
                cd->len *= 2;
                cd->code = lily_realloc(cd->code, sizeof(int) * cd->len);
                if (cd->code == NULL)
                    lily_raise_nomem(emit->error);
            }

            cd->code[cd->pos] = o_assign;
            cd->code[cd->pos+1] = (int)bx.left->result;
            cd->code[cd->pos+2] = (int)bx.right->result;
            cd->pos += 3;
        }
        else if (bx.op == expr_plus)
            generic_binop(emit, ast);
    }
}

void lily_emit_ast(lily_emit_state *emit, lily_ast *ast)
{
    walk_tree(emit, ast);
    emit->expr_num++;
}

void lily_emit_set_target(lily_emit_state *emit, lily_var *var)
{
    emit->target = var->code_data;
}

void lily_emit_vm_return(lily_emit_state *emit)
{
    lily_code_data *cd = emit->target;
    if ((cd->pos + 1) > cd->len) {
        cd->len *= 2;
        cd->code = lily_realloc(cd->code, sizeof(int) * cd->len);
        if (cd->code == NULL)
            lily_raise_nomem(emit->error);
    }

    cd->code[cd->pos] = o_vm_return;
    cd->pos++;
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_free(emit);
}

lily_emit_state *lily_new_emit_state(lily_excep_data *excep)
{
    lily_emit_state *s = lily_malloc(sizeof(lily_emit_state));

    if (s == NULL)
        return NULL;

    s->target = NULL;
    s->error = excep;
    s->expr_num = 1;

    return s;
}

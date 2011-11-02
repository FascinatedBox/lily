#include <stdlib.h>

#include "lily_ast.h"
#include "lily_impl.h"
#include "lily_emitter.h"
#include "lily_opcode.h"

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

        if (bx.op == expr_assign) {
            if (bx.left->expr_type != var)
                walk_tree(emit, bx.left);

            if (bx.right->expr_type != var)
                walk_tree(emit, bx.right);

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
    }
}

void lily_emit_ast(lily_emit_state *emit, lily_ast *ast)
{
    walk_tree(emit, ast);
}

void lily_emit_set_target(lily_emit_state *emit, lily_symbol *sym)
{
    emit->target = sym->code_data;
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

    return s;
}

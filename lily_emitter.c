#include <stdlib.h>

#include "lily_ast.h"
#include "lily_opcode.h"
#include "lily_impl.h"
#include "lily_emitter.h"

static void walk_tree(lily_emit_state *emit, lily_ast *ast)
{
    lily_code_data *cd = emit->target;

    if (ast->expr_type == var) {
        ast->reg_pos = emit->next_reg;
        emit->next_reg++;

        if ((cd->pos + 3) > cd->len) {
            cd->len *= 2;
            cd->code = lily_realloc(cd->code, sizeof(int) * cd->len);
            if (cd->code == NULL)
                lily_raise_nomem(emit->error);
        }

        cd->code[cd->pos] = o_load_reg;
        cd->code[cd->pos+1] = ast->reg_pos;
        cd->code[cd->pos+2] = (int)ast->data.value;
        cd->pos += 3;
    }
    else if (ast->expr_type == func_call) {
        struct lily_ast_list *list;
        int i, new_pos;

        /* The args could be values or expressions. Either way, get the result
           calculated and the end values into registers. */
        list = ast->data.call.args;
        while (list != NULL) {
            walk_tree(emit, list->ast);
            list = list->next;
        }

        /* Check for available space now that any inner expressions have
           adjusted the position. */
        new_pos = cd->pos + 1 + ast->data.call.num_args;
        if (new_pos > cd->len) {
            cd->len *= 2;
            cd->code = lily_realloc(cd->code, sizeof(int) * cd->len);
            if (cd->code == NULL)
                lily_raise_nomem(emit->error);
        }

        /* hack: assumes print is the only function. Fix soon. */
        cd->code[cd->pos] = o_builtin_print;
        for (i = 1, list = ast->data.call.args;list != NULL;
             i++, list = list->next) {
            cd->code[cd->pos+i] = list->ast->reg_pos;
        }

        cd->pos = new_pos;
    }
    else if (ast->expr_type == binary) {
        if (ast->data.bin_expr.op == expr_assign) {
            walk_tree(emit, ast->data.bin_expr.left);
            walk_tree(emit, ast->data.bin_expr.right);

            if ((cd->pos + 3) > cd->len) {
                cd->len *= 2;
                cd->code = lily_realloc(cd->code, sizeof(int) *
                                             cd->len);
                if (cd->code == NULL)
                    lily_raise_nomem(emit->error);
            }

            cd->code[cd->pos] = o_assign;
            cd->code[cd->pos+1] = ast->data.bin_expr.left->reg_pos;
            cd->code[cd->pos+2] = ast->data.bin_expr.right->reg_pos;
            cd->pos += 3;
        }
    }
}

static void clear_reg_info(lily_emit_state *emit)
{
    emit->next_reg = 0;
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

void lily_emit_ast(lily_emit_state *emit, lily_ast *ast)
{
    walk_tree(emit, ast);
    clear_reg_info(emit);
}

lily_emit_state *lily_new_emit_state(lily_excep_data *excep)
{
    lily_emit_state *s = lily_malloc(sizeof(lily_emit_state));

    if (s == NULL)
        return NULL;

    s->next_reg = 0;
    s->target = NULL;
    s->error = excep;

    return s;
}

void lily_emit_set_target(lily_emit_state *emit, lily_symbol *sym)
{
    emit->target = sym->code_data;
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_free(emit);
}

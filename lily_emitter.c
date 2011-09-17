#include "lily_ast.h"
#include "lily_opcode.h"
#include "lily_impl.h"
#include "lily_emitter.h"

static void walk_tree(lily_emit_state *em_state, lily_ast *ast)
{
    lily_code_data *cd = em_state->target;

    if (ast->expr_type == var) {
        ast->reg_pos = em_state->next_reg;
        em_state->next_reg++;

        if ((cd->code_pos + 3) > cd->code_len) {
            cd->code_len *= 2;
            cd->code = lily_impl_realloc(cd->code, sizeof(int) * cd->code_len);
        }

        cd->code[cd->code_pos] = o_load_reg;
        cd->code[cd->code_pos+1] = ast->reg_pos;
        cd->code[cd->code_pos+2] = (int)ast->data.value;
        cd->code_pos += 3;
    }
    else if (ast->expr_type == func_call) {
        struct lily_ast_list *list;
        int i, new_pos;

        /* The args could be values or expressions. Either way, get the result
           calculated and the end values into registers. */
        list = ast->data.call.args;
        while (list != NULL) {
            walk_tree(em_state, list->ast);
            list = list->next;
        }

        /* Check for available space now that any inner expressions have
           adjusted the position. */
        new_pos = cd->code_pos + 1 + ast->data.call.num_args;
        if (new_pos > cd->code_len) {
            cd->code_len *= 2;
            cd->code = lily_impl_realloc(cd->code, sizeof(int) * cd->code_len);
        }

        /* hack: assumes print is the only function. Fix soon. */
        cd->code[cd->code_pos] = o_builtin_print;
        for (i = 1, list = ast->data.call.args;list != NULL;
             i++, list = list->next) {
            cd->code[cd->code_pos+i] = list->ast->reg_pos;
        }

        cd->code_pos = new_pos;
    }
    else if (ast->expr_type == binary) {
        if (ast->data.bin_expr.op == expr_assign) {
            walk_tree(em_state, ast->data.bin_expr.left);
            walk_tree(em_state, ast->data.bin_expr.right);

            if ((cd->code_pos + 3) > cd->code_len) {
                cd->code_len *= 2;
                cd->code = lily_impl_realloc(cd->code, sizeof(int) *
                                             cd->code_len);
            }

            cd->code[cd->code_pos] = o_assign;
            cd->code[cd->code_pos+1] = ast->data.bin_expr.left->reg_pos;
            cd->code[cd->code_pos+2] = ast->data.bin_expr.right->reg_pos;
            cd->code_pos += 3;
        }
    }
}

static void clear_reg_info(lily_emit_state *em_state)
{
    em_state->next_reg = 0;
}

void lily_emit_vm_return(lily_emit_state *em_state)
{
    lily_code_data *cd = em_state->target;
    if ((cd->code_pos + 1) > cd->code_len) {
        cd->code_len *= 2;
        cd->code = lily_impl_realloc(cd->code, sizeof(int) * cd->code_len);
    }

    cd->code[cd->code_pos] = o_vm_return;
    cd->code_pos++;
}

void lily_emit_ast(lily_emit_state *em_state, lily_ast *ast)
{
    walk_tree(em_state, ast);
    clear_reg_info(em_state);
}

lily_emit_state *lily_init_emit_state(lily_interp *interp)
{
    lily_emit_state *ret = lily_impl_malloc(sizeof(lily_emit_state));
    ret->next_reg = 0;
    ret->interp = interp;
    ret->target = NULL;

    return ret;
}

void lily_emit_set_target(lily_emit_state *em_state, lily_symbol *sym)
{
    em_state->target = sym->code_data;
}

void lily_free_emit_state(lily_emit_state *em_state)
{
    free(em_state);
}

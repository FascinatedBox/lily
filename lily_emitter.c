#include "lily_ast.h"
#include "lily_opcode.h"
#include "lily_impl.h"

typedef struct {
    int reg_num;
} reg_data;

reg_data *reg_info;

static void walk_tree(lily_ast *ast, lily_code_data *cd)
{
    if (ast->expr_type == var) {
        ast->reg_pos = reg_info->reg_num;
        reg_info->reg_num++;

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
            walk_tree(list->ast, cd);
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
            walk_tree(ast->data.bin_expr.left, cd);
            walk_tree(ast->data.bin_expr.right, cd);

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

static void clear_reg_info(void)
{
    reg_info->reg_num = 0;
}

void lily_emit_vm_return(lily_symbol *main_func)
{
    lily_code_data *cd = main_func->code_data;
    if ((cd->code_pos + 1) > cd->code_len) {
        cd->code_len *= 2;
        cd->code = lily_impl_realloc(cd->code, sizeof(int) * cd->code_len);
    }

    cd->code[cd->code_pos] = o_vm_return;
    cd->code_pos++;
}

void lily_emit_ast(lily_symbol *caller, lily_ast *ast)
{
    walk_tree(ast, caller->code_data);
    clear_reg_info();
}

void lily_init_emitter(void)
{
    reg_info = lily_impl_malloc(sizeof(reg_data));
    reg_info->reg_num = 0;
}

#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"
#include "lily_vm.h"
#include "lily_pkg.h"

#define FAST_INTEGER_OP(OP) \
((lily_sym *)code[i+3])->value.integer = \
((lily_sym *)code[i+1])->value.integer OP \
((lily_sym *)code[i+2])->value.integer; \
i += 4; \

#define NUMBER_OP(OP) \
lhs = (lily_sym *)code[i+1]; \
if (lhs->sig->cls->id == SYM_CLASS_NUMBER) { \
    rhs = (lily_sym *)code[i+2]; \
    if (rhs->sig->cls->id == SYM_CLASS_NUMBER) \
        ((lily_sym *)code[i+3])->value.number = \
        lhs->value.number OP rhs->value.number; \
    else \
        ((lily_sym *)code[i+3])->value.number = \
        lhs->value.number OP rhs->value.integer; \
} \
else \
    ((lily_sym *)code[i+3])->value.number = \
    lhs->value.integer OP ((lily_sym *)code[i+2])->value.number; \
i += 4;

#define COMPARE_OP(OP, STROP) \
lhs = (lily_sym *)code[i+1]; \
rhs = (lily_sym *)code[i+2]; \
if (lhs->sig->cls->id == SYM_CLASS_NUMBER) { \
    if (rhs->sig->cls->id == SYM_CLASS_NUMBER) \
        ((lily_sym *)code[i+3])->value.number = \
        lhs->value.number OP rhs->value.number; \
    else \
        ((lily_sym *)code[i+3])->value.number = \
        lhs->value.number OP rhs->value.integer; \
} \
else if (lhs->sig->cls->id == SYM_CLASS_INTEGER) { \
    if (rhs->sig->cls->id == SYM_CLASS_INTEGER) \
        ((lily_sym *)code[i+3])->value.integer =  \
        (lhs->value.integer OP rhs->value.integer); \
    else \
        ((lily_sym *)code[i+3])->value.number = \
        lhs->value.integer OP rhs->value.number; \
} \
else if (lhs->sig->cls->id == SYM_CLASS_STR) { \
    ((lily_sym *)code[i+3])->value.integer = \
    strcmp(((lily_strval *)lhs->value.ptr)->str, \
           ((lily_strval *)rhs->value.ptr)->str) STROP; \
} \
i += 4;

static void grow_vm(lily_vm_state *vm)
{
    vm->stack_size *= 2;

    int **new_saved_code = lily_malloc(sizeof(int) * vm->stack_size);
    int *new_saved_pos = lily_malloc(sizeof(int) * vm->stack_size);
    lily_sym **new_saved_ret = lily_malloc(sizeof(lily_sym *) * vm->stack_size);

    if (new_saved_code == NULL || new_saved_pos == NULL) {
        lily_free(new_saved_code);
        lily_free(new_saved_pos);
        lily_free(new_saved_ret);
        lily_raise_nomem(vm->error);
    }

    vm->saved_code = new_saved_code;
    vm->saved_pos = new_saved_pos;
    vm->saved_ret = new_saved_ret;
}

lily_vm_state *lily_new_vm_state(lily_excep_data *error)
{
    lily_vm_state *vm = lily_malloc(sizeof(lily_vm_state));
    if (vm == NULL)
        lily_raise_nomem(error);

    int **saved_code = lily_malloc(sizeof(int *) * 4);
    int *saved_pos = lily_malloc(sizeof(int) * 4);
    lily_sym **saved_ret = lily_malloc(sizeof(lily_sym *) * 4);
    if (saved_code == NULL || saved_pos == NULL) {
        lily_free(saved_code);
        lily_free(saved_pos);
        lily_free(saved_ret);
        lily_free(vm);
        lily_raise_nomem(error);
    }

    vm->error = error;
    vm->saved_code = saved_code;
    vm->saved_pos = saved_pos;
    vm->saved_ret = saved_ret;
    vm->stack_pos = 0;
    vm->stack_size = 4;
    return vm;
}

void lily_free_vm_state(lily_vm_state *vm)
{
    lily_free(vm->saved_code);
    lily_free(vm->saved_pos);
    lily_free(vm->saved_ret);
    lily_free(vm);
}

void lily_builtin_print(lily_sym **args)
{
    lily_impl_send_html(((lily_strval *)args[1]->value.ptr)->str);
}

void lily_vm_execute(lily_vm_state *vm)
{
    int *code, flag, i;
    lily_method_val *mval;
    lily_sym *lhs, *rhs;
    lily_var *v;
    lily_method_val *m;

    m = (lily_method_val *)vm->main->value.ptr;
    code = m->code;
    i = 0;

    while (1) {
        switch(code[i]) {
            case o_assign:
                lhs = ((lily_sym *)code[i+1]);
                lhs->flags &= ~S_IS_NIL;
                lhs->value = ((lily_sym *)code[i+2])->value;
                i += 3;
                break;
            case o_integer_add:
                FAST_INTEGER_OP(+)
                break;
            case o_integer_minus:
                FAST_INTEGER_OP(-)
                break;
            case o_number_add:
                NUMBER_OP(+)
                break;
            case o_number_minus:
                NUMBER_OP(-)
                break;
            case o_less:
                COMPARE_OP(<, == -1)
                break;
            case o_less_eq:
                COMPARE_OP(<=, <= 0)
                break;
            case o_is_equal:
                COMPARE_OP(>, == 0)
                break;
            case o_greater:
                COMPARE_OP(>, == 1)
                break;
            case o_greater_eq:
                COMPARE_OP(>, >= 0)
                break;
            case o_jump:
                i = code[i+1];
                break;
            case o_jump_if_false:
                lhs = (lily_sym *)code[i+1];
                {
                    int cls_id = lhs->sig->cls->id;
                    if (cls_id == SYM_CLASS_INTEGER) {
                        if (lhs->value.integer == 0)
                            i += 3;
                        else
                            i = code[i+2];
                    }
                    else if (cls_id == SYM_CLASS_NUMBER) {
                        if (lhs->value.number == 0.0)
                            i += 3;
                        else
                            i = code[i+2];
                    }
                }
                break;
            case o_func_call:
            {
                /* var, func, #args, ret, args... */
                lily_fast_func fc = (lily_fast_func)code[i+2];
                int j = code[i+3];
                fc((lily_sym **)code+i+4);
                i += 5+j;
            }
                break;
            case o_method_call:
            {
                if (vm->stack_pos == vm->stack_size)
                    grow_vm(vm);

                mval = (lily_method_val *)code[i+2];
                v = mval->first_arg;
                /* It's easier to save this before i gets adjusted. */
                vm->saved_ret[vm->stack_pos] = (lily_sym *)code[i+4];
                i += 5;
                /* Map call values to method arguments. */
                for (v = mval->first_arg;
                     v != mval->last_arg->next;v = v->next, i++) {
                    flag = ((lily_sym *)code[i])->flags & S_IS_NIL;
                    v->flags &= flag;
                    v->value = ((lily_sym *)code[i])->value;
                }
                /* Add this entry to the call stack. */
                vm->saved_code[vm->stack_pos] = code;
                vm->saved_pos[vm->stack_pos] = i;
                vm->stack_pos++;

                /* Finally, load up the new code to run. */
                code = mval->code;
                i = 0;
            }
                break;
            case o_return_val:
                vm->stack_pos--;
                lhs = vm->saved_ret[vm->stack_pos];
                rhs = (lily_sym *)code[i+1];
                lhs->value = rhs->value;

                code = vm->saved_code[vm->stack_pos];
                i = vm->saved_pos[vm->stack_pos];
                break;
            case o_obj_assign:
                lhs = ((lily_sym *)code[i+1]);
                lhs->flags &= ~S_IS_NIL;
                lhs->value = ((lily_sym *)code[i+2])->value;
                lhs->sig->node.value_sig = ((lily_sym *)code[i+2])->sig;
                i += 3;
                break;
            case o_vm_return:
                return;
        }
    }
}

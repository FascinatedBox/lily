#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"
#include "lily_vm.h"
#include "lily_pkg.h"

#define FAST_INTEGER_OP(OP) \
((lily_sym *)code[i+4])->value.integer = \
((lily_sym *)code[i+2])->value.integer OP \
((lily_sym *)code[i+3])->value.integer; \
i += 5; \

#define NUMBER_OP(OP) \
lhs = (lily_sym *)code[i+2]; \
if (lhs->sig->cls->id == SYM_CLASS_NUMBER) { \
    rhs = (lily_sym *)code[i+3]; \
    if (rhs->sig->cls->id == SYM_CLASS_NUMBER) \
        ((lily_sym *)code[i+4])->value.number = \
        lhs->value.number OP rhs->value.number; \
    else \
        ((lily_sym *)code[i+4])->value.number = \
        lhs->value.number OP rhs->value.integer; \
} \
else \
    ((lily_sym *)code[i+4])->value.number = \
    lhs->value.integer OP ((lily_sym *)code[i+3])->value.number; \
i += 5;

#define COMPARE_OP(OP, STROP) \
lhs = (lily_sym *)code[i+2]; \
rhs = (lily_sym *)code[i+3]; \
if (lhs->sig->cls->id == SYM_CLASS_NUMBER) { \
    if (rhs->sig->cls->id == SYM_CLASS_NUMBER) \
        ((lily_sym *)code[i+4])->value.number = \
        lhs->value.number OP rhs->value.number; \
    else \
        ((lily_sym *)code[i+4])->value.number = \
        lhs->value.number OP rhs->value.integer; \
} \
else if (lhs->sig->cls->id == SYM_CLASS_INTEGER) { \
    if (rhs->sig->cls->id == SYM_CLASS_INTEGER) \
        ((lily_sym *)code[i+4])->value.integer =  \
        (lhs->value.integer OP rhs->value.integer); \
    else \
        ((lily_sym *)code[i+4])->value.number = \
        lhs->value.integer OP rhs->value.number; \
} \
else if (lhs->sig->cls->id == SYM_CLASS_STR) { \
    ((lily_sym *)code[i+4])->value.integer = \
    strcmp(((lily_strval *)lhs->value.ptr)->str, \
           ((lily_strval *)rhs->value.ptr)->str) STROP; \
} \
i += 5;

static void grow_vm(lily_vm_state *vm)
{
    vm->stack_size *= 2;

    int **new_saved_code = lily_realloc(vm->saved_code,
            sizeof(int) * vm->stack_size);
    int *new_saved_pos = lily_realloc(vm->saved_pos,
            sizeof(int) * vm->stack_size);
    lily_sym **new_saved_ret = lily_realloc(vm->saved_ret,
            sizeof(lily_sym *) * vm->stack_size);

    if (new_saved_code == NULL || new_saved_pos == NULL ||
        new_saved_ret == NULL) {
        lily_free(new_saved_code);
        lily_free(new_saved_pos);
        lily_free(new_saved_ret);
        lily_raise_nomem(vm->error);
    }

    vm->saved_code = new_saved_code;
    vm->saved_pos = new_saved_pos;
    vm->saved_ret = new_saved_ret;
}

static void grow_saved_vals(lily_vm_state *vm, int upto)
{
    do {
        vm->val_size *= 2;
    } while ((vm->val_pos + upto) > vm->val_size);

    lily_saved_val *new_values = lily_realloc(vm->saved_values,
            sizeof(lily_saved_val) * vm->val_size);

    if (new_values == NULL)
        lily_raise_nomem(vm->error);

    vm->saved_values = new_values;
}

lily_vm_state *lily_new_vm_state(lily_excep_data *error)
{
    lily_vm_state *vm = lily_malloc(sizeof(lily_vm_state));
    if (vm == NULL)
        return NULL;

    int **saved_code = lily_malloc(sizeof(int *) * 4);
    int *saved_pos = lily_malloc(sizeof(int) * 4);
    lily_sym **saved_ret = lily_malloc(sizeof(lily_sym *) * 4);
    lily_saved_val *saved_values = lily_malloc(sizeof(lily_saved_val) * 8);
    if (saved_code == NULL || saved_pos == NULL || saved_ret == NULL ||
        saved_values == NULL) {
        lily_free(saved_code);
        lily_free(saved_pos);
        lily_free(saved_ret);
        lily_free(saved_values);
        lily_free(vm);
        return NULL;
    }

    vm->error = error;
    vm->saved_code = saved_code;
    vm->saved_pos = saved_pos;
    vm->saved_ret = saved_ret;
    vm->saved_values = saved_values;
    vm->val_pos = 0;
    vm->val_size = 8;
    vm->stack_pos = 0;
    vm->stack_size = 4;
    return vm;
}

void lily_free_vm_state(lily_vm_state *vm)
{
    lily_free(vm->saved_code);
    lily_free(vm->saved_pos);
    lily_free(vm->saved_ret);
    lily_free(vm->saved_values);
    lily_free(vm);
}

void lily_builtin_print(int num_args, lily_sym **args)
{
    lily_impl_send_html(((lily_strval *)args[1]->value.ptr)->str);
}

void lily_builtin_printfmt(int num_args, lily_sym **args)
{
    char fmtbuf[64];
    char save_ch;
    char *fmt, *str_start;
    int cls_id, is_nil;
    int arg_pos = 1, i = 0;
    lily_sym *arg;

    fmt = ((lily_strval *)args[1]->value.ptr)->str;
    str_start = fmt;
    while (1) {
        if (fmt[i] == '\0')
            break;
        else if (fmt[i] == '%') {
            if (arg_pos == num_args)
                return;

            save_ch = fmt[i];
            fmt[i] = '\0';
            lily_impl_send_html(str_start);
            fmt[i] = save_ch;
            i++;

            arg = args[arg_pos + 1];
            cls_id = arg->sig->node.value_sig->cls->id;
            is_nil = 0;

            if (fmt[i] == 'i') {
                if (cls_id != SYM_CLASS_INTEGER)
                    return;
                if (is_nil)
                    lily_impl_send_html("(nil)");
                else {
                    snprintf(fmtbuf, 63, "%d", arg->value.integer);
                    lily_impl_send_html(fmtbuf);
                }
            }
            else if (fmt[i] == 's') {
                if (cls_id != SYM_CLASS_STR)
                    return;
                if (is_nil)
                    lily_impl_send_html("(nil)");
                else
                    lily_impl_send_html(((lily_strval *)arg->value.ptr)->str);
            }
            else if (fmt[i] == 'n') {
                if (cls_id != SYM_CLASS_NUMBER)
                    return;

                if (is_nil)
                    lily_impl_send_html("(nil)");
                else {
                    snprintf(fmtbuf, 63, "%f", arg->value.number);
                    lily_impl_send_html(fmtbuf);
                }
            }

            i++;
            str_start = fmt + i;
            arg_pos++;
        }
        i++;
    }

    lily_impl_send_html(str_start);
}

void do_str_assign(lily_sym **syms)
{
    lily_sym *lhs, *rhs;
    lily_strval *lvalue;

    lhs = ((lily_sym *)syms[1]);
    rhs = ((lily_sym *)syms[2]);
    lvalue = (lily_strval *)lhs->value.ptr;

    if (!(lhs->flags & S_IS_NIL))
        lily_deref_strval(lvalue);
    if (!(rhs->flags & S_IS_NIL)) {
        ((lily_strval *)rhs->value.ptr)->refcount++;
        lhs->flags &= ~S_IS_NIL;
    }
    else
        lhs->flags |= S_IS_NIL;
    lhs->value = rhs->value;
}

void lily_vm_execute(lily_vm_state *vm)
{
    int *code, flag, i, j, k;
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
                lhs = ((lily_sym *)code[i+2]);
                rhs = ((lily_sym *)code[i+3]);
                lhs->flags = (lhs->flags & ~S_IS_NIL) ^ (rhs->flags & S_IS_NIL);
                lhs->value = rhs->value;
                i += 4;
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
                COMPARE_OP(==, == 0)
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
                            i = code[i+2];
                        else
                            i += 3;
                    }
                    else if (cls_id == SYM_CLASS_NUMBER) {
                        if (lhs->value.number == 0.0)
                            i = code[i+2];
                        else
                            i += 3;
                    }
                }
                break;
            case o_func_call:
            {
                /* var, func, #args, ret, args... */
                lily_fast_func fc = (lily_fast_func)code[i+3];
                int j = code[i+4];
                fc(code[i+4], (lily_sym **)code+i+5);
                i += 6+j;
            }
                break;
            case o_save:
                /* todo: Implement saving in vm, once it looks okay in debug. */
                j = code[i+1];

                if (vm->val_pos + j > vm->val_size)
                    grow_saved_vals(vm, vm->val_pos + j);

                i += 2;
                for (k = 0;k < j;k++, i++, vm->val_pos++) {
                    lhs = (lily_sym *)code[i];
                    vm->saved_values[vm->val_pos].sym = lhs;
                    vm->saved_values[vm->val_pos].flags = lhs->flags;
                    vm->saved_values[vm->val_pos].value = lhs->value;
                }
                break;
            case o_restore:
                /* o_save finishes with the position ahead, so fix that. */
                vm->val_pos--;
                for (j = code[i+1];j > 0;j--,vm->val_pos--) {
                    lhs = vm->saved_values[vm->val_pos].sym;
                    lhs->flags = vm->saved_values[vm->val_pos].flags;
                    lhs->value = vm->saved_values[vm->val_pos].value;
                }
                /* Make it point to the spot to be saved to again. */
                vm->val_pos++;
                i += 2;
                break;
            case o_method_call:
            {
                int j;
                if (vm->stack_pos == vm->stack_size)
                    grow_vm(vm);

                mval = (lily_method_val *)code[i+3];
                v = mval->first_arg;
                /* It's easier to save this before i gets adjusted. */
                vm->saved_ret[vm->stack_pos] = (lily_sym *)code[i+5];
                j = i + 6 + code[i+4];
                i += 6;
                /* Map call values to method arguments. */
                for (v = mval->first_arg; i < j;v = v->next, i++) {
                    flag = ((lily_sym *)code[i])->flags & S_IS_NIL;
                    if (v->sig->cls->id == SYM_CLASS_STR) {
                        /* Treat this like a string assignment: Deref old, ref
                           new. */
                        if (v->value.ptr != NULL)
                            lily_deref_strval((lily_strval *)v->value.ptr);
                        lily_strval *sv = ((lily_sym *)code[i])->value.ptr;
                        if (sv != NULL)
                            sv->refcount++;
                    }
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

                if (lhs->sig->cls->id == SYM_CLASS_STR &&
                    !(lhs->flags & S_IS_NIL)) {
                        if (lhs->value.ptr != NULL)
                            lily_deref_strval(lhs->value.ptr);

                        ((lily_strval *)rhs->value.ptr)->refcount++;
                    }

                lhs->value = rhs->value;
                code = vm->saved_code[vm->stack_pos];
                i = vm->saved_pos[vm->stack_pos];
                break;
            case o_str_assign:
                do_str_assign(((lily_sym **)code+i+1));
                i += 4;
                break;
            case o_obj_assign:
                lhs = ((lily_sym *)code[i+2]);
                rhs = ((lily_sym *)code[i+3]);
                lhs->flags = (lhs->flags & ~S_IS_NIL) ^ (rhs->flags & S_IS_NIL);
                lhs->value = rhs->value;
                lhs->sig->node.value_sig = rhs->sig;
                i += 4;
                break;
            case o_vm_return:
                return;
        }
    }
}

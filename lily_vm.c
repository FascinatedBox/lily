#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"
#include "lily_vm.h"
#include "lily_pkg.h"

#define INTEGER_OP(OP) \
lhs = (lily_sym *)code[i+2]; \
rhs = (lily_sym *)code[i+3]; \
if (lhs->flags & S_IS_NIL) \
    novalue_error(vm, i, lhs); \
else if (rhs->flags & S_IS_NIL) \
    novalue_error(vm, i, rhs); \
((lily_sym *)code[i+4])->value.integer = \
lhs->value.integer OP rhs->value.integer; \
((lily_sym *)code[i+4])->flags &= ~S_IS_NIL; \
i += 5; \

#define INTNUM_OP(OP) \
lhs = (lily_sym *)code[i+2]; \
rhs = (lily_sym *)code[i+3]; \
if (lhs->flags & S_IS_NIL) \
    novalue_error(vm, i, lhs); \
else if (rhs->flags & S_IS_NIL) \
    novalue_error(vm, i, rhs); \
if (lhs->sig->cls->id == SYM_CLASS_NUMBER) { \
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
((lily_sym *)code[i+4])->flags &= ~S_IS_NIL; \
i += 5;

#define COMPARE_OP(OP, STROP) \
lhs = (lily_sym *)code[i+2]; \
rhs = (lily_sym *)code[i+3]; \
if (lhs->flags & S_IS_NIL) \
    novalue_error(vm, i, lhs); \
else if (rhs->flags & S_IS_NIL) \
    novalue_error(vm, i, rhs); \
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
    strcmp(lhs->value.str->str, \
           rhs->value.str->str) STROP; \
} \
((lily_sym *)code[i+4])->flags &= ~S_IS_NIL; \
i += 5;

/** vm init and deletion **/
lily_vm_state *lily_new_vm_state(lily_raiser *raiser)
{
    lily_vm_state *vm = lily_malloc(sizeof(lily_vm_state));
    if (vm == NULL)
        return NULL;

    lily_saved_val *saved_values = lily_malloc(sizeof(lily_saved_val) * 8);
    lily_vm_stack_entry **method_stack = lily_malloc(
            sizeof(lily_vm_stack_entry *) * 4);

    int i = 0;
    if (method_stack) {
        for (i = 0;i < 4;i++) {
            method_stack[i] = lily_malloc(sizeof(lily_vm_stack_entry));
            if (method_stack[i] == NULL)
                break;
        }
    }

    if (saved_values == NULL || method_stack == NULL || i != 4) {
        if (method_stack && i != 4) {
            for (;i != 0;i--)
                lily_free(method_stack[i-1]);
        }
        lily_free(saved_values);
        lily_free(method_stack);
        lily_free(vm);
        return NULL;
    }

    vm->method_stack = method_stack;
    vm->method_stack_pos = 0;
    vm->method_stack_size = 4;
    vm->raiser = raiser;
    vm->saved_values = saved_values;
    vm->val_pos = 0;
    vm->val_size = 8;
    return vm;
}

void lily_free_vm_state(lily_vm_state *vm)
{
    int i;
    for (i = 0;i < vm->method_stack_size;i++)
        lily_free(vm->method_stack[i]);

    lily_free(vm->method_stack);
    lily_free(vm->saved_values);
    lily_free(vm);
}

/** VM helpers **/

/* grow_method_stack
   This function grows the vm's method stack so it can take more method info.
   Calls lily_raise_nomem if unable to create method info. */
static void grow_method_stack(lily_vm_state *vm)
{
    int i;
    lily_vm_stack_entry **new_stack;

    vm->method_stack_size *= 2;
    new_stack = lily_realloc(vm->method_stack,
            sizeof(lily_vm_stack_entry *) * vm->method_stack_size);

    if (new_stack == NULL)
        lily_raise_nomem(vm->raiser);

    vm->method_stack = new_stack;
    for (i = vm->method_stack_pos;i < vm->method_stack_size;i++) {
        vm->method_stack[i] = lily_malloc(sizeof(lily_vm_stack_entry));
        if (vm->method_stack[i] == NULL) {
            vm->method_stack_size = i;
            lily_raise_nomem(vm->raiser);
        }
    }
}

/* grow_method_stack
   This function grows the vm's saved values so it can save more storages. Calls
   lily_raise_nomem if unable to create method info. */
static void grow_saved_vals(lily_vm_state *vm, int upto)
{
    do {
        vm->val_size *= 2;
    } while ((vm->val_pos + upto) > vm->val_size);

    lily_saved_val *new_values = lily_realloc(vm->saved_values,
            sizeof(lily_saved_val) * vm->val_size);

    if (new_values == NULL)
        lily_raise_nomem(vm->raiser);

    vm->saved_values = new_values;
}

/* novalue_error
   This is a helper routine that raises ErrNoValue because the given sym is
   nil but should not be. code_pos is the current code position, because the
   current method's info is not saved in the stack (because it would almost
   always be stale). */
static void novalue_error(lily_vm_state *vm, int code_pos, lily_sym *sym)
{
    /* ...So fill in the current method's info before dying. */
    lily_vm_stack_entry *top = vm->method_stack[vm->method_stack_pos-1];
    /* Methods do not have a linetable that maps opcodes to line numbers.
       Instead, the emitter writes the line number right after the opcode for
       any opcode that might call novalue_error. */ 
    top->line_num = top->code[code_pos+1];
    /* Literals and storages can't be nil, so this must be a named var. */
    lily_raise(vm->raiser, lily_ErrNoValue, "%s is nil.\n",
               ((lily_var *)sym)->name);
}

/* divide_by_zero_error
   This is copied from novalue_error, except it raises ErrDivisionByZero and
   reports an attempt to divide by zero. */
void divide_by_zero_error(lily_vm_state *vm, int code_pos, lily_sym *sym)
{
    lily_vm_stack_entry *top = vm->method_stack[vm->method_stack_pos-1];
    top->line_num = top->code[code_pos+1];
    /* Literals and storages can't be nil, so this must be a named var. */
    lily_raise(vm->raiser, lily_ErrDivideByZero,
               "Attempt to divide by zero.\n");
}

/** Built-in functions. These are referenced by lily_seed_symtab.h **/

/* lily_builtin_print
   This is called by the vm to implement the print function. [0] is the return
   (which isn't used), so args begin at [1]. */
void lily_builtin_print(int num_args, lily_sym **args)
{
    lily_impl_send_html(args[1]->value.str->str);
}

/* lily_builtin_printfmt
   This is called by the vm to implement the printfmt function. [0] is the
   return, which is ignored in this case. [1] is the format string, and [2]+
   are the arguments. */
void lily_builtin_printfmt(int num_args, lily_sym **args)
{
    char fmtbuf[64];
    char save_ch;
    char *fmt, *str_start;
    int cls_id, is_nil;
    int arg_pos = 1, i = 0;
    lily_sym *arg;

    fmt = args[1]->value.str->str;
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
                    lily_impl_send_html(arg->value.str->str);
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

/** VM opcode helpers **/

/* op_str_assign
   VM helper called for handling o_str_assign. [1] is lhs, [2] is rhs. */
void op_str_assign(lily_sym **syms)
{
    lily_sym *lhs, *rhs;
    lily_str_val *lvalue;

    lhs = ((lily_sym *)syms[1]);
    rhs = ((lily_sym *)syms[2]);
    lvalue = lhs->value.str;

    if (!(lhs->flags & S_IS_NIL))
        lily_deref_str_val(lvalue);
    if (!(rhs->flags & S_IS_NIL)) {
        rhs->value.str->refcount++;
        lhs->flags &= ~S_IS_NIL;
    }
    else
        lhs->flags |= S_IS_NIL;
    lhs->value = rhs->value;
}

/* op_str_assign
   VM helper called for handling o_list_assign. [1] is lhs, [2] is rhs. */
void op_list_assign(lily_sym **syms)
{
    lily_sym *lhs, *rhs;
    lily_list_val *lvalue;

    lhs = ((lily_sym *)syms[1]);
    rhs = ((lily_sym *)syms[2]);
    lvalue = lhs->value.list;

    if (!(lhs->flags & S_IS_NIL))
        lily_deref_list_val(lhs->sig, lvalue);
    if (!(rhs->flags & S_IS_NIL)) {
        rhs->value.list->refcount++;
        lhs->flags &= ~S_IS_NIL;
    }
    else
        lhs->flags |= S_IS_NIL;
    lhs->value = rhs->value;
}

/* op_build_list
   VM helper called for handling o_build_list. This is a bit tricky, becaus the
   storage may have already had a previous list assigned to it. Additionally,
   the new list info may fail to allocate.
   But most importantly, list handling is a bit broken right now. Lists keep a
   copy of their element sig in the value and in the value part of their own
   sig. This...is a problem that will eventually get fixed. */
void op_build_list(lily_vm_state *vm, lily_sym **syms, int i)
{
    lily_sig *elem_sig = (lily_sig *)syms[4];
    lily_storage *storage = (lily_storage *)syms[2];
    int num_elems = (intptr_t)(syms[3]);
    int j;

    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    if (lv == NULL)
        lily_raise_nomem(vm->raiser);

    lv->values = lily_malloc(num_elems * sizeof(void *));
    if (lv->values == NULL) {
        lily_free(lv);
        lily_raise_nomem(vm->raiser);
    }

    /* It's possible that the storage this will assign to was used to
       assign a different list. Deref the old value, or it won't be
       collected. This must be done after allocating the new value,
       because symtab will want to deref the storage during cleanup
       (resulting in an extra deref). */

    if (!(storage->flags & S_IS_NIL))
        lily_deref_list_val(storage->sig, storage->value.list);

    /* Won't be using the old sig now, so deref it. */
    if (storage->sig->node.value_sig != NULL)
        lily_deref_sig(storage->sig->node.value_sig);
    storage->sig->node.value_sig = elem_sig;
    elem_sig->refcount++;

    if (elem_sig->cls->is_refcounted) {
        for (j = 0;j < num_elems;j++) {
            lv->values[j] = syms[5+j]->value;
            lv->values[j].generic->refcount++;
        }
    }
    else {
        for (j = 0;j < num_elems;j++)
            lv->values[j] = syms[5+j]->value;
    }

    lv->num_values = num_elems;
    lv->refcount = 1;
    storage->value.list = lv;
    storage->flags &= ~S_IS_NIL;
}

/* do_ref_deref
   Do an assignment where lhs loses a ref and rhs gains one. */
static void do_ref_deref(lily_class *cls, lily_sym *down_sym, lily_sym *up_sym)
{
    lily_generic_val *up_gv = up_sym->value.generic;

    if (down_sym->value.ptr != NULL) {
        if (cls->id == SYM_CLASS_STR)
            lily_deref_str_val(down_sym->value.str);
        else if (cls->id == SYM_CLASS_METHOD)
            lily_deref_method_val(down_sym->value.method);
        else if (cls->id == SYM_CLASS_LIST)
            lily_deref_list_val(down_sym->sig, down_sym->value.list);
    }

    up_gv->refcount++;
}

/** The mighty VM **/

/* lily_vm_execute
   This is the VM part of lily. It executes any code on @main, as well as
   anything called by @main. Finishes when it encounters the o_vm_return
   opcode.
   This function occasionally farms work out to other routines to keep the size
   from being too big. It does not recurse, instead saving everything necessary
   to the vm state for each call. */
void lily_vm_execute(lily_vm_state *vm)
{
    uintptr_t *code;
    int flag, i, j, k;
    lily_method_val *mval;
    lily_sym *lhs, *rhs;
    lily_var *v;
    lily_method_val *m;
    lily_vm_stack_entry *stack_entry;

    m = vm->main->value.method;
    code = m->code;
    i = 0;

    /* The stack always contains the information of the current method. The line
       number of the current entry will be filled in on the next call. When an
       error is thrown, it will need to write in the line number of the last
       call level. */
    stack_entry = vm->method_stack[0];
    stack_entry->method = (lily_sym *)vm->main;
    stack_entry->code = code;
    vm->method_stack_pos = 1;

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
                INTEGER_OP(+)
                break;
            case o_integer_minus:
                INTEGER_OP(-)
                break;
            case o_number_add:
                INTNUM_OP(+)
                break;
            case o_number_minus:
                INTNUM_OP(-)
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
            case o_not_eq:
                COMPARE_OP(!=, != 0)
                break;
            case o_jump:
                i = code[i+1];
                break;
            case o_integer_mul:
                INTEGER_OP(*)
                break;
            case o_number_mul:
                INTNUM_OP(*)
                break;
            case o_integer_div:
                /* Before doing INTEGER_OP, check for a division by zero. This
                   will involve some redundant checking of the rhs, but better
                   than dumping INTEGER_OP's contents here or rewriting
                   INTEGER_OP for the special case of division. */
                rhs = (lily_sym *)code[i+3];
                if (rhs->flags & S_IS_NIL)
                    novalue_error(vm, i, rhs);
                if (rhs->value.integer == 0)
                    divide_by_zero_error(vm, i, rhs);
                INTEGER_OP(/)
                break;
            case o_number_div:
                /* This is a little more tricky, because the rhs could be a
                   number or an integer... */
                rhs = (lily_sym *)code[i+3];
                if (rhs->flags & S_IS_NIL)
                    novalue_error(vm, i, rhs);
                if (rhs->sig->cls->id == SYM_CLASS_INTEGER &&
                    rhs->value.integer == 0)
                    divide_by_zero_error(vm, i, rhs);
                else if (rhs->sig->cls->id == SYM_CLASS_NUMBER &&
                         rhs->value.number == 0)
                    divide_by_zero_error(vm, i, rhs);

                INTNUM_OP(/)
                break;
            case o_jump_if:
                lhs = (lily_sym *)code[i+2];
                {
                    int cls_id, result;
                    cls_id = lhs->sig->cls->id;

                    if (cls_id == SYM_CLASS_INTEGER)
                        result = (lhs->value.integer == 0);
                    else if (cls_id == SYM_CLASS_NUMBER)
                        result = (lhs->value.number == 0);

                    if (result != code[i+1])
                        i = code[i+3];
                    else
                        i += 4;
                }
                break;
            case o_func_call:
            {
                /* var, func, #args, ret, args... */
                lily_fast_func fc;
                /* The func HAS to be grabbed from the var to support passing
                   funcs as args. */
                fc = (lily_fast_func)((lily_var *)code[i+2])->value.ptr;;
                int j = code[i+3];
                fc(code[i+3], (lily_sym **)code+i+4);
                i += 5+j;
            }
                break;
            case o_save:
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
                if (vm->method_stack_pos == vm->method_stack_size)
                    grow_method_stack(vm);

                /* This has to be grabbed each time, because of methods passing
                   as args. This can't be written in at emit time, because the
                   method arg would be empty then. */
                mval = ((lily_var *)code[i+2])->value.method;
                v = mval->first_arg;
                j = i + 5 + code[i+3];

                stack_entry = vm->method_stack[vm->method_stack_pos-1];
                stack_entry->line_num = code[i+1];
                /* Remember to use j, because each arg will take a code spot. */
                stack_entry->code_pos = j;
                stack_entry->ret = (lily_sym *)code[i+4];
                stack_entry = vm->method_stack[vm->method_stack_pos];

                i += 5;
                /* Map call values to method arguments. */
                for (v = mval->first_arg; i < j;v = v->next, i++) {
                    flag = ((lily_sym *)code[i])->flags & S_IS_NIL;

                    if (v->sig->cls->is_refcounted)
                        do_ref_deref(v->sig->cls, (lily_sym *)v,
                                     (lily_sym *)code[i]);

                    v->flags &= flag;
                    v->value = ((lily_sym *)code[i])->value;
                }

                /* Add this entry to the call stack. */
                stack_entry->method = (lily_sym *)code[i-5+2];
                stack_entry->code = mval->code;
                vm->method_stack_pos++;

                /* Finally, load up the new code to run. */
                code = mval->code;
                i = 0;
            }
                break;
            case o_unary_not:
                lhs = (lily_sym *)code[i+2];
                if (lhs->flags & S_IS_NIL)
                    novalue_error(vm, i, lhs);
                rhs = (lily_sym *)code[i+3];
                rhs->flags &= ~S_IS_NIL;
                rhs->value.integer = !(lhs->value.integer);
                i += 4;
                break;
            case o_unary_minus:
                lhs = (lily_sym *)code[i+2];
                if (lhs->flags & S_IS_NIL)
                    novalue_error(vm, i, lhs);
                rhs = (lily_sym *)code[i+3];
                rhs->flags &= ~S_IS_NIL;
                rhs->value.integer = -lhs->value.integer;
                i += 4;
                break;
            case o_return_val:
                vm->method_stack_pos--;
                stack_entry = vm->method_stack[vm->method_stack_pos-1];
                lhs = stack_entry->ret;
                rhs = (lily_sym *)code[i+1];

                if (!(lhs->flags & S_IS_NIL)) {
                    if (lhs->sig->cls->is_refcounted)
                        do_ref_deref(lhs->sig->cls, (lily_sym *)lhs,
                                     (lily_sym *)rhs);
                }

                lhs->flags &= ~S_IS_NIL;
                lhs->value = rhs->value;
                code = stack_entry->code;
                i = stack_entry->code_pos;
                break;
            case o_return_noval:
                vm->method_stack_pos--;
                stack_entry = vm->method_stack[vm->method_stack_pos-1];
                code = stack_entry->code;
                i = stack_entry->code_pos;
                break;
            case o_str_assign:
                op_str_assign(((lily_sym **)code+i+1));
                i += 4;
                break;
            case o_build_list:
                op_build_list(vm, (lily_sym **)code+i, i);
                i += code[i+3] + 5;
                break;
            case o_list_assign:
                op_list_assign(((lily_sym **)code+i+1));
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

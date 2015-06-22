#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_seed.h"
#include "lily_value.h"

lily_list_val *lily_new_list_val()
{
    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    lv->refcount = 1;
    lv->gc_entry = NULL;
    lv->elems = NULL;
    lv->num_values = -1;
    lv->visited = 0;

    return lv;
}

int lily_list_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    if (*depth == 100)
        lily_raise(vm->raiser, lily_RecursionError, "Infinite loop in comparison.\n");

    int ret;

    if (left->value.list->num_values == right->value.list->num_values) {
        class_eq_func eq_func = left->type->subtypes[0]->cls->eq_func;
        lily_value **left_elems = left->value.list->elems;
        lily_value **right_elems = right->value.list->elems;

        int i;
        ret = 1;

        for (i = 0;i < left->value.list->num_values;i++) {
            (*depth)++;
            if (eq_func(vm, depth, left_elems[i], right_elems[i]) == 0) {
                ret = 0;
                (*depth)--;
                break;
            }
            (*depth)--;
        }
    }
    else
        ret = 0;

    return ret;
}

void lily_gc_list_marker(int pass, lily_value *v)
{
    lily_list_val *list_val = v->value.list;
    int i;

    if (list_val->gc_entry &&
        list_val->gc_entry->last_pass != pass) {
        list_val->gc_entry->last_pass = pass;

        lily_type *elem_type = v->type->subtypes[0];
        void (*gc_marker)(int, lily_value *);

        gc_marker = elem_type->cls->gc_marker;

        if (gc_marker) {
            for (i = 0;i < list_val->num_values;i++) {
                lily_value *elem = list_val->elems[i];

                if ((elem->flags & VAL_IS_NIL) == 0)
                    gc_marker(pass, elem);
            }
        }
    }
}

void lily_destroy_list(lily_value *v)
{
    lily_type *type = v->type;
    lily_list_val *lv = v->value.list;

    /* If this list has a gc entry, then make the value of it NULL. This
        prevents the gc from trying to access the list once it has been
        destroyed. */
    if (lv->gc_entry != NULL)
        lv->gc_entry->value.generic = NULL;

    int i;
    if (type->subtypes[0]->cls->is_refcounted) {
        for (i = 0;i < lv->num_values;i++) {
            lily_deref(lv->elems[i]);

            lily_free(lv->elems[i]);
        }
    }
    else {
        for (i = 0;i < lv->num_values;i++)
            lily_free(lv->elems[i]);
    }

    lily_free(lv->elems);
    lily_free(lv);
}

void lily_gc_collect_list(lily_type *list_type, lily_list_val *list_val)
{
    /* The first check is done because this list might be inside of an any
       that is being collected. So it may not be in the gc, but it needs to be
       destroyed because it was trapped in a circular ref.
       The second check acts as a 'lock' to make sure that this cannot be done
       twice for the same list, thus preventing recursion. */
    int marked = 0;
    if (list_val->gc_entry == NULL ||
        (list_val->gc_entry->last_pass != -1 &&
         list_val->gc_entry->value.generic != NULL)) {

        lily_type *value_type = list_type->subtypes[0];

        if (list_val->gc_entry) {
            list_val->gc_entry->last_pass = -1;
            /* If this list has a gc entry, then it can contains elements which
               refer to itself. Set last_pass to -1 to indicate that everything
               inside this list has already been deleted. The gc will delete the
               list later. */
            marked = 1;
        }

        int i;

        /* This is important because this could be a list[str], and the strings
           will need to be free'd. */
        if (value_type->cls->is_refcounted) {
            for (i = 0;i < list_val->num_values;i++) {
                /* Pass stuff off to the gc to collect. This will use a typical
                   deref for stuff like string. */
                lily_value *elem = list_val->elems[i];
                if ((elem->flags & VAL_IS_NOT_DEREFABLE) == 0) {
                    lily_raw_value v = elem->value;
                    if (v.generic->refcount == 1)
                        lily_gc_collect_value(value_type, v);
                    else
                        v.generic->refcount--;
                }
                lily_free(elem);
            }
        }
        else {
            /* Still need to free all the list elements, even if not
               refcounted. */
            for (i = 0;i < list_val->num_values;i++)
                lily_free(list_val->elems[i]);
        }
        /* else the values aren't refcounted (ex: list[integer]). No-op. */

        lily_free(list_val->elems);
        if (marked == 0)
            lily_free(list_val);
    }
}

void lily_list_size(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[0]]->value.list;
    lily_value *ret_reg = vm_regs[code[1]];

    lily_raw_value v = {.integer = list_val->num_values};
    lily_move_raw_value(vm, ret_reg, 0, v);
}

void lily_list_append(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[0]]->value.list;
    lily_value *insert_value = vm_regs[code[1]];

    int value_count = list_val->num_values;

    lily_value *value_holder = lily_malloc(sizeof(lily_value));

    value_holder->type = insert_value->type;
    value_holder->flags = VAL_IS_NIL;
    value_holder->value.integer = 0;
    list_val->elems = lily_realloc(list_val->elems,
        (value_count + 1) * sizeof(lily_value *));;
    list_val->elems[value_count] = value_holder;
    list_val->num_values++;

    lily_assign_value(vm, value_holder, insert_value);
}

/*  lily_list_apply
    Implements list::apply

    Arguments:
    * Input: A list to iterate over.
    * Call:  A function to call for each element of the list.
             This function takes the type of the list as an argument, and returns
             a value of the type of the list.

             If the list is type 'T'
             then the call is 'function (T):T' */
void lily_list_apply(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[0]]->value.list;
    lily_value *function_reg = vm_regs[code[1]];
    lily_value *vm_result;

    /* This must be called exactly once at the beginning of a foreign call to
       the vm. This ensures the vm has enough registers + stack for a foreign
       call (among other things). */
    lily_vm_foreign_prep(vm, function_reg);
    int i;
    for (i = 0;i < list_val->num_values;i++) {
        /* Arguments to the native call begin at index 1. The native call needs
           one arg, so give that to it. */
        lily_vm_foreign_load_by_val(vm, 1, list_val->elems[i]);

        /* Do the actual call. This bumps the stack so that it records that
           apply has been entered. */
        lily_vm_foreign_call(vm);

        /* The result is always at index 0 if it exists. */
        vm_result = lily_vm_get_foreign_reg(vm, 0);
        lily_assign_value(vm, list_val->elems[i], vm_result);
    }
}

static lily_func_seed apply =
    {NULL, "apply", dyna_function, "function apply[A](list[A], function(A => A))", lily_list_apply};

static const lily_func_seed append =
    {&apply, "append", dyna_function, "function append[A](list[A], A)", lily_list_append};

static const lily_func_seed dynaload_start =
    {&append, "size", dyna_function, "function size[A](list[A] => integer)", lily_list_size};

static const lily_class_seed list_seed =
{
    NULL,                 /* next */
    "list",               /* name */
    dyna_class,           /* load_type */
    1,                    /* is_refcounted */
    1,                    /* generic_count */
    0,                    /* flags */
    &dynaload_start,      /* dynaload_start */
    &lily_gc_list_marker, /* gc_marker */
    &lily_list_eq,        /* eq_func */
    lily_destroy_list,    /* destroy_func */
};

lily_class *lily_list_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &list_seed);
}

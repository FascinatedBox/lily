#include <string.h>

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
    lv->iter_count = 0;
    lv->extra_space = 0;

    return lv;
}

int lily_list_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    if (*depth == 100)
        lily_raise(vm->raiser, lily_RuntimeError, "Infinite loop in comparison.\n");

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
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *ret_reg = vm_regs[code[0]];

    lily_raw_value v = {.integer = list_val->num_values};
    lily_move_raw_value(ret_reg, v);
}

/* This expands the list value so there's more extra space. Growth is done
   relative to the current size of the list, because why not? */
static void make_extra_space_in_list(lily_list_val *lv)
{
    /* There's probably room for improvement here, later on. */
    int extra = (lv->num_values + 8) >> 2;
    lv->elems = lily_realloc(lv->elems,
            (lv->num_values + extra) * sizeof(lily_value *));
    lv->extra_space = extra;
}

void lily_list_push(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *insert_value = vm_regs[code[2]];

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    int value_count = list_val->num_values;

    list_val->elems[value_count] = lily_copy_value(insert_value);
    list_val->num_values++;
    list_val->extra_space--;
}

/*  Implements list::pop

    This function attempts to take the last element out of the list and return
    it. If the list is empty, then IndexError is raised. */
void lily_list_pop(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *result_reg = vm_regs[code[0]];

    if (list_val->num_values == 0)
        lily_raise(vm->raiser, lily_IndexError, "Pop from an empty list.\n");

    /* Do not use assign here: It will increase the refcount, and the element is
       no longer in the list. Instead, use move and pretend the value does not
       exist in the list any longer. */
    lily_move_raw_value(result_reg,
        list_val->elems[list_val->num_values - 1]->value);

    /* For now, free extra values instead of trying to keep reserves around.
       Not the best course of action, perhaps, but certainly the simplest. */
    lily_free(list_val->elems[list_val->num_values - 1]);
    list_val->num_values--;
    list_val->extra_space++;
}

/*  Implements list::clear
    This clears all the elements out of the list. */
void lily_list_clear(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    int i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_deref(list_val->elems[i]);
        lily_free(list_val->elems[i]);
    }

    list_val->extra_space += list_val->num_values;
    list_val->num_values = 0;
}

/*  Implements list::each

    This function iterates over a list, calling a function on each element of
    the list. The list that was sent to this function is returned at the end of
    the call. The function called should not return a value, but that does not
    prevent it from mutating the element. */
void lily_list_each(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *list_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_list_val *list_val = list_reg->value.list;
    lily_type *expect_type = list_reg->type->subtypes[0];
    lily_value *result_reg = vm_regs[code[0]];
    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++)
        lily_foreign_call(vm, &cached, expect_type, function_reg, 1,
                list_val->elems[i]);

    lily_assign_value(result_reg, list_reg);
}

/*  Implements list::each_index

    This is similar to list::each, except that the calling function receives the
    index instead of the element. */
void lily_list_each_index(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *list_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_list_val *list_val = list_reg->value.list;
    lily_value *result_reg = vm_regs[code[0]];
    lily_value fake_reg;

    fake_reg.value.integer = 0;
    /* [1] is the first input of the function. This grabs the integer type. */
    fake_reg.type = function_reg->type->subtypes[1];
    fake_reg.flags = VAL_IS_PRIMITIVE;

    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++, fake_reg.value.integer++)
        lily_foreign_call(vm, &cached, NULL, function_reg, 1, &fake_reg);

    lily_assign_value(result_reg, list_reg);
}

/*  Implement list::fill

    Create a new list with a given value repeated 'n' times.

    Arguments:
    * n:         The number of times to repeat the value.
    * to_repeat: The value used to fill the list.

    Errors:
    * if n < 0, ValueError is raised. */
void lily_list_fill(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int n = vm_regs[code[1]]->value.integer;
    if (n < 0)
        lily_raise(vm->raiser, lily_ValueError,
                "Repeat count must be >= 0 (%d given).\n", n);

    lily_value *to_repeat = vm_regs[code[2]];
    lily_value *result = vm_regs[code[0]];
    lily_list_val *lv = lily_new_list_val();

    /* Note: I can't seem to write a test that causes a leak if this isn't */
    if (result->type->flags & TYPE_MAYBE_CIRCULAR)
        lily_add_gc_item(vm, result->type, (lily_generic_gc_val *)lv);

    lily_raw_value v = {.list = lv};
    lily_move_raw_value(result, v);

    lily_value **elems = lily_malloc(sizeof(lily_value *) * n);
    lv->elems = elems;

    int i;
    for (i = 0;i < n;i++)
        elems[i] = lily_copy_value(to_repeat);

    lv->num_values = n;
}

/*  This function will take 'vm_list->pos - vm_list_start' elements out of the
    vm's vm_list and move them into a newly-made list. vm_list->pos is then
    rewound to vm_list_start.
    This function assumes that values which are put into vm_list are copied (and
    thus receive a refcount bump). This allows the new list to simply take
    ownership of the values in the vm_list. */
static void slice_vm_list(lily_vm_state *vm, int vm_list_start,
        lily_value *result_reg)
{
    lily_vm_list *vm_list = vm->vm_list;
    lily_list_val *result_list = lily_new_list_val();
    int num_values = vm_list->pos - vm_list_start;

    if (result_reg->type->flags & TYPE_MAYBE_CIRCULAR)
        lily_add_gc_item(vm, result_reg->type,
                (lily_generic_gc_val *)result_list);

    result_list->num_values = num_values;
    result_list->elems = lily_malloc(sizeof(lily_value *) * num_values);

    int i;
    for (i = 0;i < num_values;i++)
        result_list->elems[i] = vm_list->values[vm_list_start + i];

    vm_list->pos = vm_list_start;

    lily_raw_value v = {.list = result_list};
    lily_move_raw_value(result_reg, v);
}

static void list_select_reject_common(lily_vm_state *vm, uint16_t argc,
        uint16_t *code, int expect)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *function_reg = vm_regs[code[2]];
    lily_type *expect_type = function_reg->type->subtypes[0];

    lily_vm_list *vm_list = vm->vm_list;
    int vm_list_start = vm_list->pos;
    int cached = 0;

    lily_vm_list_ensure(vm, list_val->num_values);

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_value *result = lily_foreign_call(vm, &cached, expect_type,
                function_reg, 1, list_val->elems[i]);

        if (result->value.integer == expect) {
            vm_list->values[vm_list->pos] = lily_copy_value(list_val->elems[i]);
            vm_list->pos++;
        }
    }

    slice_vm_list(vm, vm_list_start, result_reg);
}

/*  Implements list::count

    This takes a predicate function, and calls it using each element of the
    list. The result is a count of how many times the predicate function
    returns true. */
void lily_list_count(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *function_reg = vm_regs[code[2]];
    lily_type *expect_type = function_reg->type->subtypes[0];
    int count = 0;

    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_value *result = lily_foreign_call(vm, &cached, expect_type,
                function_reg, 1, list_val->elems[i]);

        if (result->value.integer == 1)
            count++;
    }

    lily_move_raw_value(result_reg, (lily_raw_value){.integer = count});
}

/*  Implements list::select

    Create a new list where all members of a list satisfy some predicate.

    Arguments:
    * f: A function taking A and returning boolean. */
void lily_list_select(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    list_select_reject_common(vm, argc, code, 1);
}

/*  Implements list::reject

    Create a new list where all members of a list do not satisfy a predicate.

    Arguments:
    * f: A function taking A and returning boolean. */
void lily_list_reject(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    list_select_reject_common(vm, argc, code, 0);
}

/*  Implements list::map

    This creates a new list that is a result of applying a transformation
    function to an original list. This is VERY similar to both select and
    reject, except that the list is carried over each time. */
void lily_list_map(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *function_reg = vm_regs[code[2]];
    lily_type *expect_type = function_reg->type->subtypes[0];

    lily_vm_list *vm_list = vm->vm_list;
    int vm_list_start = vm_list->pos;
    int cached = 0;

    lily_vm_list_ensure(vm, list_val->num_values);

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_value *result = lily_foreign_call(vm, &cached, expect_type,
                function_reg, 1, list_val->elems[i]);

        vm_list->values[vm_list->pos] = lily_copy_value(result);
        vm_list->pos++;
    }

    slice_vm_list(vm, vm_list_start, result_reg);
}

/*  Implements list::shift

    This function attempts to take the first element out of the list and return
    it. If the list is empty, then IndexError is raised. */
void lily_list_shift(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *result_reg = vm_regs[code[0]];

    if (list_val->num_values == 0)
        lily_raise(vm->raiser, lily_IndexError, "Shift on an empty list.\n");

    /* Do not use assign here: It will increase the refcount, and the element is
       no longer in the list. Instead, use move and pretend the value does not
       exist in the list any longer. */
    lily_move_raw_value(result_reg, list_val->elems[0]->value);

    /* For now, free extra values instead of trying to keep reserves around.
       Not the best course of action, perhaps, but certainly the simplest. */
    lily_free(list_val->elems[0]);

    if (list_val->num_values != 1)
        memmove(list_val->elems, list_val->elems + 1,
                (list_val->num_values - 1) *
                sizeof(lily_value *));

    list_val->num_values--;
    list_val->extra_space++;
}

/*  Implements list::unshift

    This function attempts to add an element to the beginning of a list. */
void lily_list_unshift(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *input_reg = vm_regs[code[2]];

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    if (list_val->num_values != 0)
        memmove(list_val->elems + 1, list_val->elems,
                list_val->num_values * sizeof(lily_value *));

    list_val->elems[0] = lily_copy_value(input_reg);

    list_val->num_values++;
    list_val->extra_space--;
}

/*  Implements list::fold

    This function combines all of the elements in a list using a given function.
    The function will take two elements: One, the accumulated value so far, and
    two, the next element in the list. The placement of the accumulated value is
    intentional (Scala also places the accumulated value first). */
void lily_list_fold(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *starting_reg = vm_regs[code[2]];
    lily_value *function_reg = vm_regs[code[3]];
    lily_type *expect_type = starting_reg->type;
    lily_value *current = starting_reg;
    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        current = lily_foreign_call(vm, &cached, expect_type,
                function_reg, 2, current, list_val->elems[i]);
    }

    lily_assign_value(result_reg, current);
}

static lily_func_seed clear =
    {NULL, "clear", dyna_function, "[A](list[A])", lily_list_clear};

static lily_func_seed count =
    {&clear, "count", dyna_function, "[A](list[A], function(A => boolean)):integer", lily_list_count};

static lily_func_seed each =
    {&count, "each", dyna_function, "[A](list[A], function(A)):list[A]", lily_list_each};

static lily_func_seed each_index =
    {&each, "each_index", dyna_function, "[A](list[A], function(integer)):list[A]", lily_list_each_index};

static lily_func_seed fill =
    {&each_index, "fill", dyna_function, "[A](integer, A):list[A]", lily_list_fill};

static lily_func_seed fold =
    {&fill, "fold", dyna_function, "[A](list[A], A, function(A, A => A)):A", lily_list_fold};

static lily_func_seed map =
    {&fold, "map", dyna_function, "[A,B](list[A], function(A => B)):list[B]", lily_list_map};

static lily_func_seed pop =
    {&map, "pop", dyna_function, "[A](list[A]):A", lily_list_pop};

static const lily_func_seed push =
    {&pop, "push", dyna_function, "[A](list[A], A)", lily_list_push};

static lily_func_seed reject =
    {&push, "reject", dyna_function, "[A](list[A], function(A => boolean)):list[A]", lily_list_reject};

static lily_func_seed select_fn =
    {&reject, "select", dyna_function, "[A](list[A], function(A => boolean)):list[A]", lily_list_select};

static const lily_func_seed size =
    {&select_fn, "size", dyna_function, "[A](list[A]):integer", lily_list_size};

static lily_func_seed shift =
    {&size, "shift", dyna_function, "[A](list[A]):A", lily_list_shift};

static lily_func_seed dynaload_start =
    {&shift, "unshift", dyna_function, "[A](list[A], A)", lily_list_unshift};

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

#include <string.h>

#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_seed.h"
#include "lily_value.h"

extern lily_gc_entry *lily_gc_stopper;

lily_list_val *lily_new_list_val()
{
    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    lv->refcount = 1;
    lv->gc_entry = NULL;
    lv->elems = NULL;
    lv->num_values = -1;
    lv->extra_space = 0;

    return lv;
}

lily_list_val *lily_new_list_val_0()
{
    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    lv->refcount = 0;
    lv->gc_entry = NULL;
    lv->elems = NULL;
    lv->num_values = -1;
    lv->extra_space = 0;

    return lv;
}

void lily_gc_list_marker(int pass, lily_value *v)
{
    lily_list_val *list_val = v->value.list;
    int i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_value *elem = list_val->elems[i];

        if (elem->flags & VAL_IS_GC_SWEEPABLE)
            lily_gc_mark(pass, elem);
    }
}

void lily_destroy_list(lily_value *v)
{
    lily_list_val *lv = v->value.list;

    int full_destroy = 1;
    if (lv->gc_entry) {
        if (lv->gc_entry->last_pass == -1) {
            full_destroy = 0;
            lv->gc_entry = lily_gc_stopper;
        }
        else
            lv->gc_entry->value.generic = NULL;
    }

    int i;
    for (i = 0;i < lv->num_values;i++) {
        lily_deref(lv->elems[i]);
        lily_free(lv->elems[i]);
    }

    lily_free(lv->elems);

    if (full_destroy)
        lily_free(lv);
}

void lily_list_size(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *ret_reg = vm_regs[code[0]];

    lily_move_integer(ret_reg, list_val->num_values);
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

void lily_list_pop(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *result_reg = vm_regs[code[0]];

    if (list_val->num_values == 0)
        lily_raise(vm->raiser, lily_IndexError, "Pop from an empty list.\n");

    lily_value *source = list_val->elems[list_val->num_values - 1];

    /* Do not use assign here: It will increase the refcount, and the element is
       no longer in the list. Instead, use move and pretend the value does not
       exist in the list any longer. */
    lily_move(result_reg, source->value, source->flags);

    /* For now, free extra values instead of trying to keep reserves around.
       Not the best course of action, perhaps, but certainly the simplest. */
    lily_free(list_val->elems[list_val->num_values - 1]);
    list_val->num_values--;
    list_val->extra_space++;
}

static int64_t get_relative_index(lily_vm_state *vm, lily_list_val *list_val,
        int64_t pos)
{
    if (pos < 0) {
        uint64_t unsigned_pos = -(int64_t)pos;
        if (unsigned_pos > list_val->num_values)
            lily_raise(vm->raiser, lily_IndexError, "Index %d is too small for list (minimum: %d)\n",
                    pos, -(int64_t)list_val->num_values);

        pos = list_val->num_values - unsigned_pos;
    }
    else if (pos > list_val->num_values)
        lily_raise(vm->raiser, lily_IndexError, "Index %d is too large for list (maximum: %d)\n",
                pos, list_val->num_values);

    return pos;
}

void lily_list_insert(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    int64_t insert_pos = vm_regs[code[2]]->value.integer;
    lily_value *insert_value = vm_regs[code[3]];

    insert_pos = get_relative_index(vm, list_val, insert_pos);

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    /* Shove everything rightward to make space for the new value. */
    if (insert_pos != list_val->num_values)
        memmove(list_val->elems + insert_pos + 1, list_val->elems + insert_pos,
                (list_val->num_values - insert_pos) * sizeof(lily_value *));

    list_val->elems[insert_pos] = lily_copy_value(insert_value);
    list_val->num_values++;
    list_val->extra_space--;
}

void lily_list_delete_at(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    int64_t pos = vm_regs[code[2]]->value.integer;

    if (list_val->num_values == 0)
        lily_raise(vm->raiser, lily_IndexError, "Cannot delete from an empty list.\n");

    pos = get_relative_index(vm, list_val, pos);

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    lily_value *to_delete = list_val->elems[pos];
    lily_deref(to_delete);
    lily_free(to_delete);

    /* Shove everything leftward hide the hole from erasing the value. */
    if (pos != list_val->num_values)
        memmove(list_val->elems + pos, list_val->elems + pos + 1,
                (list_val->num_values - pos) * sizeof(lily_value *));

    list_val->num_values--;
    list_val->extra_space++;
}

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

void lily_list_each(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *list_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_list_val *list_val = list_reg->value.list;
    lily_value *result_reg = vm_regs[code[0]];
    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++)
        lily_foreign_call(vm, &cached, 1, function_reg, 1,
                list_val->elems[i]);

    lily_assign_value(result_reg, list_reg);
}

void lily_list_each_index(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *list_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_list_val *list_val = list_reg->value.list;
    lily_value *result_reg = vm_regs[code[0]];
    lily_value fake_reg;

    fake_reg.value.integer = 0;
    fake_reg.flags = VAL_IS_INTEGER;

    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++, fake_reg.value.integer++)
        lily_foreign_call(vm, &cached, 0, function_reg, 1, &fake_reg);

    lily_assign_value(result_reg, list_reg);
}

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

    lily_move_list(result, lv);

    lily_value **elems = lily_malloc(sizeof(lily_value *) * n);
    lv->elems = elems;

    int i;
    for (i = 0;i < n;i++)
        elems[i] = lily_copy_value(to_repeat);

    lv->num_values = n;
}

/* This function will take 'vm_list->pos - vm_list_start' elements out of the
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

    result_list->num_values = num_values;
    result_list->elems = lily_malloc(sizeof(lily_value *) * num_values);

    int i;
    for (i = 0;i < num_values;i++)
        result_list->elems[i] = vm_list->values[vm_list_start + i];

    vm_list->pos = vm_list_start;

    lily_move_list(result_reg, result_list);
}

static void list_select_reject_common(lily_vm_state *vm, uint16_t argc,
        uint16_t *code, int expect)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *function_reg = vm_regs[code[2]];

    lily_vm_list *vm_list = vm->vm_list;
    int vm_list_start = vm_list->pos;
    int cached = 0;

    lily_vm_list_ensure(vm, list_val->num_values);

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_value *result = lily_foreign_call(vm, &cached, 1,
                function_reg, 1, list_val->elems[i]);

        if (result->value.integer == expect) {
            vm_list->values[vm_list->pos] = lily_copy_value(list_val->elems[i]);
            vm_list->pos++;
        }
    }

    slice_vm_list(vm, vm_list_start, result_reg);
}

void lily_list_count(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *function_reg = vm_regs[code[2]];
    int count = 0;

    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_value *result = lily_foreign_call(vm, &cached, 1,
                function_reg, 1, list_val->elems[i]);

        if (result->value.integer == 1)
            count++;
    }

    lily_move_integer(result_reg, count);
}

void lily_list_join(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm_regs[code[0]];
    lily_list_val *lv = vm_regs[code[1]]->value.list;
    const char *delim = "";
    if (argc == 2)
        delim = vm_regs[code[2]]->value.string->string;

    lily_msgbuf *vm_buffer = vm->vm_buffer;
    lily_msgbuf_flush(vm_buffer);

    if (lv->num_values) {
        int i, stop = lv->num_values - 1;
        lily_value **values = lv->elems;
        for (i = 0;i < stop;i++) {
            lily_vm_add_value_to_msgbuf(vm, vm_buffer, values[i]);
            lily_msgbuf_add(vm_buffer, delim);
        }
        if (stop != -1)
            lily_vm_add_value_to_msgbuf(vm, vm_buffer, values[i]);
    }

    lily_move_string(result_reg, lily_new_raw_string(vm_buffer->message));
}

void lily_list_select(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    list_select_reject_common(vm, argc, code, 1);
}

void lily_list_reject(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    list_select_reject_common(vm, argc, code, 0);
}

void lily_list_map(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *function_reg = vm_regs[code[2]];

    lily_vm_list *vm_list = vm->vm_list;
    int vm_list_start = vm_list->pos;
    int cached = 0;

    lily_vm_list_ensure(vm, list_val->num_values);

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_value *result = lily_foreign_call(vm, &cached, 1,
                function_reg, 1, list_val->elems[i]);

        vm_list->values[vm_list->pos] = lily_copy_value(result);
        vm_list->pos++;
    }

    slice_vm_list(vm, vm_list_start, result_reg);
}

void lily_list_shift(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *result_reg = vm_regs[code[0]];

    if (list_val->num_values == 0)
        lily_raise(vm->raiser, lily_IndexError, "Shift on an empty list.\n");

    lily_value *source = list_val->elems[0];

    /* Do not use assign here: It will increase the refcount, and the element is
       no longer in the list. Instead, use move and pretend the value does not
       exist in the list any longer. */
    lily_move(result_reg, source->value, source->flags);

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

void lily_list_fold(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *starting_reg = vm_regs[code[2]];
    lily_value *function_reg = vm_regs[code[3]];
    lily_value *current = starting_reg;
    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        current = lily_foreign_call(vm, &cached, 1, function_reg, 2, current,
                list_val->elems[i]);
    }

    lily_assign_value(result_reg, current);
}

static lily_func_seed clear =
    {NULL, "clear", dyna_function, "[A](List[A])", lily_list_clear};

static lily_func_seed count =
    {&clear, "count", dyna_function, "[A](List[A], Function(A => Boolean)):Integer", lily_list_count};

static lily_func_seed delete_at =
    {&count, "delete_at", dyna_function, "[A](List[A], Integer)", lily_list_delete_at};

static lily_func_seed each =
    {&delete_at, "each", dyna_function, "[A](List[A], Function(A)):List[A]", lily_list_each};

static lily_func_seed each_index =
    {&each, "each_index", dyna_function, "[A](List[A], Function(Integer)):List[A]", lily_list_each_index};

static lily_func_seed fill =
    {&each_index, "fill", dyna_function, "[A](Integer, A):List[A]", lily_list_fill};

static lily_func_seed fold =
    {&fill, "fold", dyna_function, "[A](List[A], A, Function(A, A => A)):A", lily_list_fold};

static lily_func_seed insert =
    {&fold, "insert", dyna_function, "[A](List[A], Integer, A)", lily_list_insert};

static const lily_func_seed join =
    {&insert, "join", dyna_function, "[A](List[A], *String):String", lily_list_join};

static lily_func_seed map =
    {&join, "map", dyna_function, "[A,B](List[A], Function(A => B)):List[B]", lily_list_map};

static lily_func_seed pop =
    {&map, "pop", dyna_function, "[A](List[A]):A", lily_list_pop};

static const lily_func_seed push =
    {&pop, "push", dyna_function, "[A](List[A], A)", lily_list_push};

static lily_func_seed reject =
    {&push, "reject", dyna_function, "[A](List[A], Function(A => Boolean)):List[A]", lily_list_reject};

static lily_func_seed select_fn =
    {&reject, "select", dyna_function, "[A](List[A], Function(A => Boolean)):List[A]", lily_list_select};

static const lily_func_seed size =
    {&select_fn, "size", dyna_function, "[A](List[A]):Integer", lily_list_size};

static lily_func_seed shift =
    {&size, "shift", dyna_function, "[A](List[A]):A", lily_list_shift};

static lily_func_seed dynaload_start =
    {&shift, "unshift", dyna_function, "[A](List[A], A)", lily_list_unshift};

static const lily_class_seed list_seed =
{
    NULL,                 /* next */
    "List",               /* name */
    dyna_class,           /* load_type */
    1,                    /* is_refcounted */
    1,                    /* generic_count */
    &dynaload_start       /* dynaload_start */
};

lily_class *lily_list_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &list_seed);
}

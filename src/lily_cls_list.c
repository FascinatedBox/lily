#include "lily_impl.h"
#include "lily_vm.h"

#define malloc_mem(size)             vm->mem_func(NULL, size)
#define realloc_mem(ptr, size)       vm->mem_func(ptr, size)
#define free_mem(ptr)          (void)vm->mem_func(ptr, 0)

void lily_list_size(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[0]]->value.list;
    lily_value *ret_reg = vm_regs[code[1]];

    ret_reg->value.integer = list_val->num_values;
    ret_reg->flags &= ~VAL_IS_NIL;
}

void lily_list_append(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[0]]->value.list;
    lily_value *insert_value = vm_regs[code[1]];

    int value_count = list_val->num_values;

    lily_value *value_holder = malloc_mem(sizeof(lily_value));

    value_holder->type = insert_value->type;
    value_holder->flags = VAL_IS_NIL;
    value_holder->value.integer = 0;
    list_val->elems = realloc_mem(list_val->elems,
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
void lily_list_apply(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[0]]->value.list;
    lily_value *function_reg = vm_regs[code[1]];
    lily_value *vm_result;

    /* This must be called exactly once at the beginning of a foreign call to
       the vm. This ensures the vm has enough registers + stack for a foreign
       call (among other things). */
    lily_vm_foreign_prep(vm, self, function_reg);
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
    {"apply", "function apply[A](list[A], function(A => A))", lily_list_apply, NULL};

static const lily_func_seed append =
    {"append", "function append[A](list[A], A)", lily_list_append, &apply};

static const lily_func_seed size =
    {"size", "function size[A](list[A] => integer)", lily_list_size, &append};

#define SEED_START size

int lily_list_setup(lily_symtab *symtab, lily_class *cls)
{
    cls->seed_table = &SEED_START;
    return 1;
}

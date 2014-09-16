#include "lily_impl.h"
#include "lily_vm.h"

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

    lily_value **new_list_values = lily_realloc(list_val->elems,
        (value_count + 1) * sizeof(lily_value *));
    lily_value *value_holder = lily_malloc(sizeof(lily_value));

    if (new_list_values == NULL || value_holder == NULL) {
        /* realloc may free the pointer, so update the list so it has valid
           values. The gc will then take care of properly free-ing the list
           later. */
        if (new_list_values != NULL)
            list_val->elems = new_list_values;

        lily_free(value_holder);
        lily_raise_nomem(vm->raiser);
    }

    value_holder->sig = insert_value->sig;
    value_holder->flags = VAL_IS_NIL;
    value_holder->value.integer = 0;
    list_val->elems = new_list_values;
    list_val->elems[value_count] = value_holder;
    list_val->num_values++;

    lily_assign_value(vm, value_holder, insert_value);
}

/*  lily_list_apply
    Implements list::apply

    Arguments:
    * Input: A list to iterate over. If this is nil, ErrBadValue is raised.
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
    {"apply", lily_list_apply, NULL,
        {SYM_CLASS_FUNCTION, 3, 0,
            -1,
            SYM_CLASS_LIST, SYM_CLASS_TEMPLATE, 0,
            SYM_CLASS_FUNCTION, 2, 0,
                SYM_CLASS_TEMPLATE, 0,
                SYM_CLASS_TEMPLATE, 0
        }
    };


static const lily_func_seed append =
    {"append", lily_list_append, &apply,
        {SYM_CLASS_FUNCTION, 3, 0,
            -1,
            SYM_CLASS_LIST, SYM_CLASS_TEMPLATE, 0,
            SYM_CLASS_TEMPLATE, 0
        }
    };

static const lily_func_seed size =
    {"size", lily_list_size, &append,
        {SYM_CLASS_FUNCTION, 2, 0, SYM_CLASS_INTEGER, SYM_CLASS_LIST, SYM_CLASS_TEMPLATE, 0}};

#define SEED_START size

int lily_list_setup(lily_class *cls)
{
    cls->seed_table = &SEED_START;
    return 1;
}

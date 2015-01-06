#include "lily_impl.h"
#include "lily_value.h"
#include "lily_vm.h"

static void make_error(lily_vm_state *vm, char *exception_name,
        lily_value *result_reg, lily_value *message_reg)
{
    lily_class *base_except_class = lily_class_by_name(vm->symtab,
            "Exception");
    lily_class *except_class = lily_class_by_name(vm->symtab, exception_name);
    lily_instance_val *ival = lily_try_new_instance_val();
    lily_list_val *trace_list = lily_try_new_list_val();
    lily_value *message_value = lily_malloc(sizeof(lily_value));
    lily_value *trace_value = lily_malloc(sizeof(lily_value));
    lily_value **ival_values = lily_malloc(2 * sizeof(lily_value *));

    if (ival == NULL || trace_list == NULL || trace_value == NULL ||
        message_value == NULL || ival_values == NULL) {
        lily_free(ival);
        lily_free(trace_list);
        lily_free(trace_value);
        lily_free(ival_values);
        lily_raise_nomem(vm->raiser);
    }

    ival->true_class = except_class;
    ival->values = ival_values;

    message_value->value.string = message_reg->value.string;
    message_value->flags = message_reg->flags;
    message_value->type = message_reg->type;

    lily_type *traceback_type = base_except_class->properties->next->type;
    trace_value->value.list = trace_list;
    trace_value->flags = 0;
    trace_value->type = traceback_type;

    ival->values[0] = message_value;
    if ((message_reg->flags & VAL_IS_PROTECTED) == 0)
        message_reg->value.string->refcount++;

    ival->values[1] = trace_value;
    ival->num_values = 2;

    if ((result_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
        lily_deref_instance_val(result_reg->type, result_reg->value.instance);

    result_reg->value.instance = ival;
    result_reg->flags = 0;
}

void lily_error_new(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value *message_reg = vm->vm_regs[code[0]];
    lily_value *result_reg = vm->vm_regs[code[1]];

    make_error(vm, result_reg->type->cls->name, result_reg, message_reg);
}

static const lily_func_seed nomemory_new =
    {"new", "function new(string => NoMemoryError)", lily_error_new, NULL};

static const lily_func_seed dbz_new =
    {"new", "function new(string => DivisionByZeroError)", lily_error_new, NULL};

static const lily_func_seed index_new =
    {"new", "function new(string => IndexError)", lily_error_new, NULL};

static const lily_func_seed badtc_new =
    {"new", "function new(string => BadTypecastError)", lily_error_new, NULL};

static const lily_func_seed noreturn_new =
    {"new", "function new(string => NoReturnError)", lily_error_new, NULL};

static const lily_func_seed value_new =
    {"new", "function new(string => ValueError)", lily_error_new, NULL};

static const lily_func_seed recursion_new =
    {"new", "function new(string => RecursionError)", lily_error_new, NULL};

static const lily_func_seed key_new =
    {"new", "function new(string => KeyError)", lily_error_new, NULL};

static const lily_func_seed format_new =
    {"new", "function new(string => FormatError)", lily_error_new, NULL};

int lily_error_setup(lily_class *cls)
{
    if (cls->id == SYM_CLASS_NOMEMORYERROR)
        cls->seed_table = &nomemory_new;
    else if (cls->id == SYM_CLASS_DBZERROR)
        cls->seed_table = &dbz_new;

    return 1;
}

#define SETUP_PROC(name) \
int lily_##name##error_setup(lily_class *cls) \
{ \
    cls->seed_table = &name##_new; \
    return 1; \
}

SETUP_PROC(nomemory)
SETUP_PROC(dbz)
SETUP_PROC(index)
SETUP_PROC(badtc)
SETUP_PROC(noreturn)
SETUP_PROC(value)
SETUP_PROC(recursion)
SETUP_PROC(key)
SETUP_PROC(format)

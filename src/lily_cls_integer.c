#include "lily_impl.h"
#include "lily_vm.h"
#include "lily_value.h"

#include "inttypes.h"
#include "string.h"

/*  lily_integer_to_string
    Implements integer::to_string() */
void lily_integer_to_string(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int64_t integer_val = vm_regs[code[0]]->value.integer;
    lily_value *result_reg = vm_regs[code[1]];

    char buffer[32];
    snprintf(buffer, 32, "%"PRId64, integer_val);

    lily_string_val *new_sv = lily_malloc(sizeof(lily_string_val));
    char *text = lily_malloc(strlen(buffer) + 1);
    if (new_sv == NULL || text == NULL) {
        lily_free(new_sv);
        lily_free(text);
        lily_raise_nomem(vm->raiser);
    }

    strcpy(text, buffer);
    new_sv->string = text;
    new_sv->size = strlen(buffer);
    new_sv->refcount = 1;

    if ((result_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
        lily_deref_string_val(result_reg->value.string);

    result_reg->flags = 0;
    result_reg->value.string = new_sv;
}

void lily_integer_to_double(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int64_t integer_val = vm_regs[code[0]]->value.integer;
    lily_value *result_reg = vm_regs[code[1]];

    result_reg->flags = 0;
    result_reg->value.doubleval = (double)integer_val;
}

static const lily_func_seed to_double =
    {"to_double", "function to_double(integer => double)", lily_integer_to_double, NULL};

static const lily_func_seed to_string =
    {"to_string", "function to_string(integer => string)", lily_integer_to_string, &to_double};

int lily_integer_setup(lily_class *cls)
{
    cls->seed_table = &to_string;
    return 1;
}


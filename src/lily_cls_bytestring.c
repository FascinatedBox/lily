#include <string.h>

#include "lily_alloc.h"
#include "lily_core_types.h"
#include "lily_lexer.h"
#include "lily_vm.h"
#include "lily_seed.h"

#include "lily_cls_string.h"

int lily_bytestring_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    int ret;

    if (left->value.string->size == right->value.string->size &&
        (left->value.string == right->value.string ||
         memcmp(left->value.string->string, right->value.string->string,
                left->value.string->size) == 0))
        ret = 1;
    else
        ret = 0;

    return ret;
}

void lily_bytestring_encode(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_string_val *input_bytestring = vm_regs[code[0]]->value.string;
    const char *encode_method =
            (argc == 2) ? vm_regs[code[1]]->value.string->string : "error";
    lily_value *result = vm_regs[code[argc]];

    if (strcmp(encode_method, "error") != 0) {
        lily_raise(vm->raiser, lily_ValueError,
                "Encode option should be either 'ignore' or 'error'.\n");
    }

    char *byte_buffer = input_bytestring->string;
    int byte_buffer_size = input_bytestring->size;

    if (lily_is_valid_utf8(byte_buffer, byte_buffer_size) == 0) {
        lily_raise(vm->raiser, lily_ValueError,
                "Invalid utf-8 sequence found in buffer.\n");
    }

    lily_string_val *new_sv = lily_malloc(sizeof(lily_string_val));
    char *sv_buffer = lily_malloc(byte_buffer_size + 1);

    /* The utf-8 validator function also ensures that there are no embedded
       \0's, so it's safe to use strcpy. */
    strcpy(sv_buffer, byte_buffer);

    new_sv->refcount = 1;
    new_sv->string = sv_buffer;
    new_sv->size = byte_buffer_size;

    lily_raw_value v = {.string = new_sv};

    lily_move_raw_value(vm, result, 0, v);
}

static const lily_func_seed dynaload_start =
    {NULL, "encode", dyna_function, "function encode(bytestring, *string => string)", lily_bytestring_encode};

static const lily_class_seed bytestring_seed =
{
    NULL,                /* next */
    "bytestring",        /* name */
    dyna_class,          /* load_type */
    1,                   /* is_refcounted */
    0,                   /* generic_count */
    0,                   /* flags */
    &dynaload_start,     /* dynaload_table */
    NULL,                /* gc_marker */
    &lily_bytestring_eq, /* eq_func */
    lily_destroy_string  /* destroy_func */
};

lily_class *lily_bytestring_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &bytestring_seed);
}

#include <string.h>

#include "lily_alloc.h"
#include "lily_core_types.h"
#include "lily_lexer.h"
#include "lily_vm.h"
#include "lily_seed.h"
#include "lily_utf8.h"
#include "lily_value.h"
#include "lily_cls_string.h"

void lily_bytestring_encode(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_string_val *input_bytestring = vm_regs[code[1]]->value.string;
    const char *encode_method =
            (argc == 2) ? vm_regs[code[2]]->value.string->string : "error";
    lily_value *result = vm_regs[code[0]];

    if (strcmp(encode_method, "error") != 0) {
        lily_raise(vm->raiser, lily_ValueError,
                "Encode option should be either 'ignore' or 'error'.\n");
    }

    char *byte_buffer = input_bytestring->string;
    int byte_buffer_size = input_bytestring->size;

    if (lily_is_valid_sized_utf8(byte_buffer, byte_buffer_size) == 0) {
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

    lily_move_string(result, new_sv);
}

static const lily_func_seed dynaload_start =
    {NULL, "encode", dyna_function, "(ByteString, *String):String", lily_bytestring_encode};

static const lily_class_seed bytestring_seed =
{
    NULL,                /* next */
    "ByteString",        /* name */
    dyna_class,          /* load_type */
    1,                   /* is_refcounted */
    0,                   /* generic_count */
    &dynaload_start,     /* dynaload_table */
    lily_destroy_string  /* destroy_func */
};

lily_class *lily_bytestring_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &bytestring_seed);
}

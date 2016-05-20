#include <string.h>

#include "lily_core_types.h"
#include "lily_lexer.h"
#include "lily_vm.h"
#include "lily_utf8.h"

#include "lily_api_alloc.h"
#include "lily_api_dynaload.h"
#include "lily_api_value.h"

void lily_bytestring_encode(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_string_val *input_bytestring = vm_regs[code[1]]->value.string;
    const char *encode_method =
            (argc == 2) ? vm_regs[code[2]]->value.string->string : "error";
    lily_value *result = vm_regs[code[0]];
    char *byte_buffer = NULL;

    if (strcmp(encode_method, "error") == 0) {
        byte_buffer = input_bytestring->string;
        int byte_buffer_size = input_bytestring->size;

        if (lily_is_valid_sized_utf8(byte_buffer, byte_buffer_size) == 0) {
            lily_move_enum_f(MOVE_SHARED_NO_GC, result, lily_get_none(vm));
            return;
        }
    }
    else {
        lily_move_enum_f(MOVE_SHARED_NO_GC, result, lily_get_none(vm));
        return;
    }

    lily_value *v = lily_new_string(byte_buffer);
    lily_move_enum_f(MOVE_DEREF_NO_GC, result, lily_new_some(v));
}

#define DYNA_NAME bytestring

DYNA_FUNCTION(NULL, encode, "(ByteString, *String):Option[String]")

static const lily_class_seed bytestring_seed =
{
    NULL,                /* next */
    "ByteString",        /* name */
    dyna_class,          /* load_type */
    1,                   /* is_refcounted */
    0,                   /* generic_count */
    &seed_encode         /* dynaload_table */
};

lily_class *lily_bytestring_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &bytestring_seed);
}

#include <stdio.h>
#include <string.h>

#include "lily_alloc.h"
#include "lily_symtab.h"
#include "lily_value.h"

/*  This file contains functions for creating proper a proper lily_value from
    a basic C value (ex: a string from a char *, or integer from int). This is
    used by mod_lily to bind server values, and the vm to bind internal vm
    data to valid lily values.
    These functions take the symtab so there's no need to do multiple type
    lookups (that's annoying). */


/*  lily_bind_string_take_buffer
    The same thing as lily_bind_string, but use the buffer given as the
    string's buffer. */
lily_value *lily_bind_string_take_buffer(lily_symtab *symtab, char *buffer)
{
    lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
    int string_size = strlen(buffer);

    sv->refcount = 1;
    sv->string = buffer;
    sv->size = string_size;

    return lily_new_value(0, symtab->string_class->type,
            (lily_raw_value){.string = sv});
}

/*  lily_bind_string
    Create a new lily_value of type 'string' with the given text. The value
    created will use a copy of the string.

    On success: A new proper lily_value is created and returned.
    On failure: NULL is returned. */
lily_value *lily_bind_string(lily_symtab *symtab, const char *string)
{
    char *buffer = lily_malloc(strlen(string) + 1);

    strcpy(buffer, string);
    return lily_bind_string_take_buffer(symtab, buffer);
}

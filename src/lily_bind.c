#include <stdio.h>
#include <string.h>

#include "lily_symtab.h"
#include "lily_value.h"

/*  This file contains functions for creating proper a proper lily_value from
    a basic C value (ex: a string from a char *, or integer from int). This is
    used by mod_lily to bind server values, and the vm to bind internal vm
    data to valid lily values.
    These functions take the symtab so there's no need to do multiple type
    lookups (that's annoying). */

#define malloc_mem(size) mem_func(NULL, size)
#define free_mem(ptr)    mem_func(ptr, 0)

/*  lily_bind_string_take_buffer
    The same thing as lily_bind_string, but use the buffer given as the
    string's buffer. */
lily_value *lily_bind_string_take_buffer(lily_symtab *symtab, char *buffer)
{
    lily_mem_func mem_func = symtab->mem_func;
    lily_value *new_value = malloc_mem(sizeof(lily_value));
    lily_string_val *sv = malloc_mem(sizeof(lily_string_val));
    int string_size = strlen(buffer);

    sv->refcount = 1;
    sv->string = buffer;
    sv->size = string_size;

    new_value->value.string = sv;
    new_value->flags = 0;
    new_value->type = symtab->string_class->type;

    return new_value;
}

/*  lily_bind_string
    Create a new lily_value of type 'string' with the given text. The value
    created will use a copy of the string.

    On success: A new proper lily_value is created and returned.
    On failure: NULL is returned. */
lily_value *lily_bind_string(lily_symtab *symtab, const char *string)
{
    lily_mem_func mem_func = symtab->mem_func;
    char *buffer = malloc_mem(strlen(string) + 1);

    strcpy(buffer, string);
    return lily_bind_string_take_buffer(symtab, buffer);
}

/*  lily_bind_integer
    Create a new lily_value of type 'integer' with the given value. Returns the
    new value, or NULL. */
lily_value *lily_bind_integer(lily_symtab *symtab,
        int64_t intval)
{
    lily_mem_func mem_func = symtab->mem_func;
    lily_value *new_value = malloc_mem(sizeof(lily_value));
    if (new_value) {
        new_value->value.integer = intval;
        new_value->type = symtab->integer_class->type;
        new_value->flags = 0;
    }

    return new_value;
}

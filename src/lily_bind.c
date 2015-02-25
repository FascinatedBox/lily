#include <stdio.h>
#include <string.h>

#include "lily_impl.h"
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

    lily_class *string_cls = lily_class_by_id(symtab, SYM_CLASS_STRING);

    new_value->value.string = sv;
    new_value->flags = 0;
    new_value->type = string_cls->type;

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
    if (buffer == NULL)
        return NULL;

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
        lily_class *integer_cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);

        new_value->value.integer = intval;
        new_value->type = integer_cls->type;
        new_value->flags = 0;
    }

    return new_value;
}

/*  lily_bind_destroy
    This will deref the given value (free-ing the contents), then free the
    value itself unless the value is NULL.

    This can be called on values created by lily_bind_* functions to safely
    ensure they are destroyed if there is an error. */
void lily_bind_destroy(lily_mem_func mem_func, lily_value *value)
{
    if (value != NULL) {
        lily_deref_unknown_val(mem_func, value);
        free_mem(value);
    }
}

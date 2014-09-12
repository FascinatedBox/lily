#include <stdio.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_value.h"

/*  This file contains functions for creating proper a proper lily_value from
    a basic C value (ex: a string from a char *, or integer from int). This is
    used by mod_lily to bind server values, and the vm to bind internal vm
    data to valid lily values.
    These functions take the symtab so there's no need to do multiple signature
    lookups (that's annoying). */

/*  lily_bind_string_take_buffer
    The same thing as lily_bind_string, but use the buffer given as the
    string's buffer. */
lily_value *lily_bind_string_take_buffer(lily_symtab *symtab,
        char *buffer)
{
    lily_value *new_value = lily_malloc(sizeof(lily_value));
    lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
    int string_size = strlen(buffer);

    if (sv == NULL || new_value == NULL) {
        lily_free(sv);
        /* This function takes over the buffer on success, so make sure if
           there is an error that it destroys the buffer. */
        lily_free(buffer);
        lily_free(new_value);
        return NULL;
    }

    sv->refcount = 1;
    sv->string = buffer;
    sv->size = string_size;

    lily_class *string_cls = lily_class_by_id(symtab, SYM_CLASS_STRING);

    new_value->value.string = sv;
    new_value->flags = 0;
    new_value->sig = string_cls->sig;

    return new_value;
}

/*  lily_bind_string
    Create a new lily_value of type 'string' with the given text. The value
    created will use a copy of the string.

    On success: A new proper lily_value is created and returned.
    On failure: NULL is returned. */
lily_value *lily_bind_string(lily_symtab *symtab, const char *string)
{
    char *buffer = lily_malloc(strlen(string) + 1);
    if (buffer == NULL)
        return NULL;

    strcpy(buffer, string);
    return lily_bind_string_take_buffer(symtab, buffer);
}

/*  lily_bind_integer
    Create a new lily_value of type 'integer' with the given value. Returns the
    new value, or NULL. */
lily_value *lily_bind_integer(lily_symtab *symtab, int64_t intval)
{
    lily_value *new_value = lily_malloc(sizeof(lily_value));
    if (new_value) {
        lily_class *integer_cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);

        new_value->value.integer = intval;
        new_value->sig = integer_cls->sig;
        new_value->flags = 0;
    }

    return new_value;
}

/*  lily_bind_destroy
    This will deref the given value (free-ing the contents), then free the
    value itself unless the value is NULL.

    This can be called on values created by lily_bind_* functions to safely
    ensure they are destroyed if there is an error. */
void lily_bind_destroy(lily_value *value)
{
    if (value != NULL) {
        lily_deref_unknown_val(value);
        lily_free(value);
    }
}

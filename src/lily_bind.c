#include <stdio.h>
#include <string.h>

#include "lily_alloc.h"
#include "lily_symtab.h"
#include "lily_value.h"

/* This creates a new string. The string will be considered the owner of the
   buffer that is given. */
lily_value *lily_bind_string_take_buffer(lily_symtab *symtab, char *buffer)
{
    lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
    int string_size = strlen(buffer);

    sv->refcount = 1;
    sv->string = buffer;
    sv->size = string_size;

    return lily_new_string(sv);
}

/* This creates a new string value with a copy of 'string' as the underlying
   buffer. */
lily_value *lily_bind_string(lily_symtab *symtab, const char *string)
{
    char *buffer = lily_malloc(strlen(string) + 1);

    strcpy(buffer, string);
    return lily_bind_string_take_buffer(symtab, buffer);
}

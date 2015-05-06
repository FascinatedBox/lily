#include "lily_alloc.h"
#include "lily_core_types.h"

void lily_destroy_symbol(lily_value *v)
{
    lily_symbol_val *symv = v->value.symbol;

    if (symv->has_literal)
        /* Keep the refcount at one so that the symtab can use this function
            to free symbols at exit (by stripping away the literal). */
        symv->refcount++;
    else {
        /* Since this symbol has no literal associated with it, it exists only
           in vm space and it can die.
           But first, make sure the symtab's entry has that spot blanked out to
           prevent an invalid read when looking over symbols associated with
           entries. */
        symv->entry->symbol = NULL;
        lily_free(symv->string);
        lily_free(symv);
    }
}

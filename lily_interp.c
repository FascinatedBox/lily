#include "lily_impl.h"
#include "lily_interp.h"

lily_interp *lily_init_interp(void)
{
    lily_interp *interp = lily_malloc(sizeof(lily_interp));

    lily_init_symtab(interp);
    lily_init_lexer(interp);
    return interp;
}

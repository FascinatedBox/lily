#include <stdarg.h>

#include "lily_impl.h"
#include "lily_interp.h"

void lily_free_interp(lily_interp *interp)
{
    lily_free_parse_state(interp->parser);
    lily_free(interp->error->message);
    lily_free(interp->error);
    lily_free(interp);
}

lily_interp *lily_new_interp(void)
{
    lily_interp *interp = lily_malloc(sizeof(lily_interp));
    if (interp == NULL)
        return NULL;

    interp->error = lily_malloc(sizeof(lily_excep_data));
    if (interp->error == NULL) {
        lily_free(interp);
        return NULL;
    }

    interp->parser = lily_new_parse_state(interp->error);
    interp->error->message = NULL;

    if (interp->parser == NULL) {
        lily_free(interp->error);
        lily_free(interp);
        return NULL;
    }

    return interp;
}

int lily_parse_file(lily_interp *interp, char *filename)
{
    if (setjmp(interp->error->jump) == 0) {
        lily_include(interp->parser->lex, filename);
        lily_parser(interp->parser);
        return 1;
    }

    return 0;
}

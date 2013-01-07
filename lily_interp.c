#include <stdarg.h>

#include "lily_impl.h"
#include "lily_interp.h"

void lily_free_interp(lily_interp *interp)
{
    lily_free_parse_state(interp->parser);
    lily_free(interp->raiser->message);
    lily_free(interp->raiser);
    lily_free(interp);
}

lily_interp *lily_new_interp(void)
{
    lily_interp *interp = lily_malloc(sizeof(lily_interp));
    if (interp == NULL)
        return NULL;

    interp->raiser = lily_malloc(sizeof(lily_raiser));
    if (interp->raiser == NULL) {
        lily_free(interp);
        return NULL;
    }

    interp->parser = lily_new_parse_state(interp->raiser);
    interp->raiser->line_adjust = 0;
    interp->raiser->message = NULL;

    if (interp->parser == NULL) {
        lily_free(interp->raiser);
        lily_free(interp);
        return NULL;
    }

    return interp;
}

int lily_parse_file(lily_interp *interp, char *filename)
{
    if (setjmp(interp->raiser->jump) == 0) {
        lily_load_file(interp->parser->lex, filename);
        lily_parser(interp->parser);
        return 1;
    }

    return 0;
}

int lily_parse_string(lily_interp *interp, char *str)
{
    if (setjmp(interp->raiser->jump) == 0) {
        lily_load_str(interp->parser->lex, str);
        lily_parser(interp->parser);
        interp->parser->lex->lex_buffer = NULL;
        return 1;
    }

    return 0;
}

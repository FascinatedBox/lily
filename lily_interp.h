#ifndef LILY_INTERP_H
# define LILY_INTERP_H

# include <setjmp.h>

struct lily_symbol_t;
struct lily_lex_data_t;

typedef enum {
    err_include,
    err_internal,
    err_nomem,
    err_stub,
    err_syntax
} lily_excep_code;

typedef struct lily_interp_t {
    struct lily_lex_data_t *lex_data;
    struct lily_symbol_t *main_func;
    struct lily_symbol_t *symtab_top;
    struct lily_symbol_t *symtab;
    int new_sym_id;
    jmp_buf excep_jmp;
    lily_excep_code excep_code;
    char *excep_msg;
} lily_interp;

# include "lily_symtab.h"
# include "lily_lexer.h"

lily_interp *lily_init_interp(void);
void lily_raise(lily_interp *, lily_excep_code, char *, ...);
void lily_raise_nomem(lily_interp *);
int lily_parse_file(lily_interp *, char *);

#endif

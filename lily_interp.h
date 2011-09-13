#ifndef LILY_INTERP_H
# define LILY_INTERP_H

struct lily_symbol_t;
struct lily_lex_data_t;

typedef struct lily_interp_t {
    struct lily_lex_data_t *lex_data;
    struct lily_symbol_t *main_func;
    struct lily_symbol_t *symtab_top;
    struct lily_symbol_t *symtab;
    int new_sym_id;
} lily_interp;

# include "lily_symtab.h"
# include "lily_lexer.h"

lily_interp *lily_init_interp(void);

#endif

#ifndef LILY_LEXER_H
# define LILY_LEXER_H

# include <stdio.h>

# include "lily_error.h"
# include "lily_symtab.h"

typedef enum {
    tk_invalid,
    tk_word,
    tk_left_parenth,
    tk_right_parenth,
    tk_double_quote,
    tk_integer,
    tk_number,
    tk_equal,
    tk_comma,
    tk_plus,
    tk_minus,
    tk_end_tag,
    tk_eof
} lily_token;

typedef struct {
    FILE *lex_file;
    int line_num;
    char *ch_class;
    char *html_cache;
    char *lex_buffer;
    char *save_buffer;
    /* There's no position for the cache because it's always sent before
       handling lily code. */
    int cache_size;
    int lex_bufpos;
    int lex_bufend;
    int lex_bufsize;
    char *label;
    lily_token token;
    lily_value value;
    lily_excep_data *error;
} lily_lex_state;

void lily_free_lex_state(lily_lex_state *);
void lily_lexer(lily_lex_state *);
void lily_lexer_handle_page_data(lily_lex_state *);
void lily_load_file(lily_lex_state *, char *);
void lily_load_str(lily_lex_state *, char *);
lily_lex_state *lily_new_lex_state(lily_excep_data *);
char *tokname(lily_token);

#endif

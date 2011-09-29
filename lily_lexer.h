#ifndef LILY_LEXER_H
# define LILY_LEXER_H

#include <stdio.h>
#include "lily_error.h"

typedef enum {
    tk_invalid,
    tk_word,
    tk_left_parenth,
    tk_right_parenth,
    tk_double_quote,
    tk_num_int,
    tk_num_dbl,
    tk_equal,
    tk_end_tag,
    tk_eof
} lily_tok_type;

typedef struct lily_token_t {
    lily_tok_type tok_type;
    char *word_buffer;
    int int_val;
    double dbl_val;
} lily_token;

typedef struct {
    FILE *lex_file;
    int line_num;
    char *ch_class;
    char *html_cache;
    char *lex_buffer;
    /* There's no position for the cache because it's always sent before
       handling lily code. */
    int cache_size;
    int lex_bufpos;
    int lex_bufend;
    int lex_bufsize;
    lily_token *token;
    lily_excep_data *error;
} lily_lex_state;

lily_lex_state *lily_new_lex_state(lily_excep_data *);
void lily_lexer(lily_lex_state *);
void lily_lexer_handle_page_data(lily_lex_state *);
void lily_include(lily_lex_state *, char *);

#endif

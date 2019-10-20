#ifndef LILY_LEXER_H
# define LILY_LEXER_H

# include <stdio.h>

# include "lily_raiser.h"
# include "lily_symtab.h"

typedef enum {
    tk_right_parenth,
    tk_comma,
    tk_left_curly,
    tk_right_curly,
    tk_left_bracket,
    tk_colon,
    tk_tilde,
    tk_bitwise_xor,
    tk_bitwise_xor_eq,
    tk_not,
    tk_not_eq,
    tk_modulo,
    tk_modulo_eq,
    tk_multiply,
    tk_multiply_eq,
    tk_divide,
    tk_divide_eq,
    tk_plus,
    tk_plus_plus,
    tk_plus_eq,
    tk_minus,
    tk_minus_eq,
    /* Note: Lexer assumes that less and greater have x_eq, x_shift, and
       x_shift_eq are 1, 2, and 3 places after the less/greater token.
       You can move lt/gt so long as all four tokens are moved together. */
    tk_lt,
    tk_lt_eq,
    tk_left_shift,
    tk_left_shift_eq,
    tk_gt,
    tk_gt_eq,
    tk_right_shift,
    tk_right_shift_eq,
    tk_equal,
    tk_eq_eq,
    tk_left_parenth,
    tk_lambda,       /* (| */
    tk_tuple_open,   /* <[ */
    tk_tuple_close,  /* ]> */
    /* } closes a lambda, so there's no special close token. */
    tk_right_bracket,
    tk_arrow,
    tk_word,
    tk_prop_word,
    tk_double_quote,
    tk_bytestring,
    tk_byte,
    tk_integer,
    tk_double,
    tk_docstring,
    tk_keyword_arg,
    tk_dot,
    tk_bitwise_and,
    tk_bitwise_and_eq,
    tk_logical_and,
    tk_bitwise_or,
    tk_bitwise_or_eq,
    tk_logical_or,
    tk_typecast_parenth,
    tk_three_dots,
    tk_func_pipe,
    tk_scoop,
    tk_invalid,
    tk_end_lambda,
    tk_end_tag,
    tk_eof
} lily_token;

typedef enum {
    et_file,
    et_shallow_string,
    et_copied_string,
    et_lambda,
} lily_lex_entry_type;

typedef struct lily_lex_entry_ {
    struct lily_lex_state_ *lexer;
    lily_literal *saved_last_literal;
    char *saved_input;
    uint32_t saved_input_pos;
    uint32_t saved_input_size;

    lily_token saved_token : 16;
    lily_token final_token : 16;
    uint16_t saved_line_num;
    lily_lex_entry_type entry_type : 16;
    int64_t saved_last_integer;

    void *source;
    void *extra;

    struct lily_lex_entry_ *prev;
    struct lily_lex_entry_ *next;
} lily_lex_entry;

typedef struct lily_lex_state_ {
    lily_lex_entry *entry;
    char *ch_class;
    char *input_buffer;
    char *label;

    uint16_t line_num;
    uint16_t expand_start_line;
    uint32_t pad;

    uint32_t label_size;
    uint32_t pad2;

    uint32_t input_size;
    uint32_t input_pos;

    int64_t last_integer;

    lily_token token;

    /* When the lexer sees a numeric or string literal, it calls the symtab to
       make a literal value. Said value is stored here, for the parser to use. */
    lily_literal *last_literal;
    lily_symtab *symtab;
    lily_raiser *raiser;
} lily_lex_state;

void lily_free_lex_state(lily_lex_state *);
void lily_rewind_lex_state(lily_lex_state *);
void lily_grow_lexer_buffers(lily_lex_state *);
void lily_lexer(lily_lex_state *);
int lily_lexer_digit_rescan(lily_lex_state *);
void lily_verify_template(lily_lex_state *);
int lily_lexer_read_content(lily_lex_state *, char **);
void lily_lexer_verify_path_string(lily_lex_state *);

void lily_lexer_load(lily_lex_state *, lily_lex_entry_type, const void *);

void lily_pop_lex_entry(lily_lex_state *);
lily_lex_state *lily_new_lex_state(lily_raiser *);
char *tokname(lily_token);

#endif
 

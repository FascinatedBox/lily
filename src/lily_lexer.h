#ifndef LILY_LEXER_H
# define LILY_LEXER_H

# include <stdio.h>

# include "lily_membuf.h"
# include "lily_raiser.h"
# include "lily_symtab.h"

typedef enum {
    tk_left_parenth,
    tk_right_parenth,
    tk_comma,
    tk_right_curly,
    tk_left_bracket,
    tk_caret,
    tk_not,
    tk_not_eq,
    tk_modulo,
    tk_modulo_eq,
    tk_multiply,
    tk_multiply_eq,
    tk_divide,
    tk_divide_eq,
    tk_plus,
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
    tk_left_curly,
    tk_lambda,       /* {| */
    tk_tuple_open,   /* <[ */
    tk_tuple_close,  /* ]> */
    /* } closes a lambda, so there's no special close token. */
    tk_right_bracket,
    tk_arrow,
    tk_word,
    tk_prop_word,
    tk_double_quote,
    tk_bytestring,
    tk_integer,
    tk_double,
    tk_dot,
    tk_colon,
    tk_colon_colon,
    tk_bitwise_and,
    tk_logical_and,
    tk_bitwise_or,
    tk_logical_or,
    tk_typecast_parenth,
    tk_three_dots,
    tk_invalid,
    tk_end_tag,
    tk_eof
} lily_token;

typedef enum {
    /* Code is the data between '<?lily' and '?>'. Everything else is html and
       written as-is. Multiple runs are possible, and eof should be reached
       within html handling. */
    lm_tags,
    /* Everything is code, and code terminates with eof. */
    lm_no_tags
} lily_lex_mode;

struct lily_lex_entry_;
typedef int (*lily_reader_fn)(struct lily_lex_entry_ *);
typedef void (*lily_close_fn)(struct lily_lex_entry_ *);

typedef struct lily_lex_entry_ {
    char *filename;
    struct lily_lex_state_ *lexer;

    lily_reader_fn read_line_fn;
    lily_close_fn  close_fn;

    lily_tie *saved_last_literal;
    char *saved_input;
    uint16_t saved_input_pos;
    uint16_t saved_input_size;
    uint16_t saved_input_end;
    lily_token saved_token : 16;
    uint32_t saved_line_num;
    uint32_t pad2;

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

    uint32_t line_num;
    /* Where the last digit scan started at. This is used by parser to fixup
       the '1+1' case. */
    uint16_t last_digit_start;
    uint16_t pad;

    uint16_t input_end;
    uint16_t input_size;
    uint16_t input_pos;
    uint16_t label_size;

    char *lambda_data;
    uint32_t lambda_data_size;
    uint32_t lambda_start_line;

    lily_token token;
    lily_lex_mode mode;

    /* When the lexer sees a numeric or string literal, it calls the symtab to
       make a literal value. Said value is stored here, for the parser to use. */
    lily_tie *last_literal;
    lily_symtab *symtab;
    lily_membuf *membuf;
    lily_raiser *raiser;
    void *data;
} lily_lex_state;

void lily_free_lex_state(lily_lex_state *);
void lily_grow_lexer_buffers(lily_lex_state *);
int lily_is_valid_utf8(char *, int);
void lily_lexer(lily_lex_state *);
void lily_lexer_handle_page_data(lily_lex_state *);
void lily_lexer_digit_rescan(lily_lex_state *);
void lily_load_file(lily_lex_state *, lily_lex_mode, char *);
void lily_load_str(lily_lex_state *, char *, lily_lex_mode, char *);
void lily_load_copy_string(lily_lex_state *, char *, lily_lex_mode, char *);
int lily_try_load_file(lily_lex_state *, char *);

void lily_pop_lex_entry(lily_lex_state *);
lily_lex_state *lily_new_lex_state(lily_options *, lily_raiser *);
char *tokname(lily_token);

#endif

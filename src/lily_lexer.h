#ifndef LILY_LEXER_H
# define LILY_LEXER_H

# include <stdio.h>

# include "lily_raiser.h"
# include "lily_symtab.h"

typedef enum {
    tk_left_parenth,
    tk_right_parenth,
    tk_comma,
    tk_left_curly,
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
    tk_tuple_open,   /* <[ */
    tk_tuple_close,  /* ]> */
    tk_right_bracket,
    tk_arrow,
    tk_word,
    tk_prop_word,
    tk_double_quote,
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
    tk_inner_eof, /* The end of any 'file' except the first. */
    tk_final_eof /* The end of the first 'file' entered. */
} lily_token;

typedef enum {
    /* Code is the data between '<?lily' and '?>'. Everything else is html and
       written as-is. Multiple runs are possible, and eof should be reached
       within html handling. */
    lm_tags,
    /* Everything is code, and code terminates with eof. */
    lm_no_tags
} lily_lex_mode;

struct lily_lex_entry_t;
typedef int (*lily_reader_fn)(struct lily_lex_entry_t *);
typedef void (*lily_close_fn)(struct lily_lex_entry_t *);

typedef struct lily_lex_entry_t {
    void *source;

    lily_reader_fn read_line_fn;
    lily_close_fn  close_fn;

    char *filename;
    struct lily_lex_state_t *lexer;

    char *saved_input;
    int saved_input_pos;
    int saved_input_size;
    int saved_input_end;
    lily_token saved_token;
    lily_literal *saved_last_literal;
    int saved_hit_eof;

    uint16_t saved_line_num;

    struct lily_lex_entry_t *prev;
    struct lily_lex_entry_t *next;
} lily_lex_entry;

typedef struct lily_lex_state_t {
    lily_lex_entry *entry;
    char *filename;
    char *ch_class;
    char *input_buffer;
    int input_end;
    int input_size;
    int input_pos;
    char *label;
    int label_size;
    uint16_t line_num;

    /* Where the last digit scan started at. This is used by parser to fixup
       the '1+1' case. */
    int last_digit_start;
    lily_token token;
    lily_lex_mode mode;

    /* When the lexer sees a numeric or string literal, it calls the symtab to
       make a literal value. Said value is stored here, for the parser to use. */
    lily_literal *last_literal;
    lily_symtab *symtab;
    lily_raiser *raiser;
    void *data;
} lily_lex_state;

void lily_free_lex_state(lily_lex_state *);
void lily_grow_lexer_buffers(lily_lex_state *);
void lily_lexer_utf8_check(lily_lex_state *);
void lily_lexer(lily_lex_state *);
void lily_lexer_handle_page_data(lily_lex_state *);
void lily_lexer_digit_rescan(lily_lex_state *);
void lily_load_file(lily_lex_state *, lily_lex_mode, char *);
void lily_load_str(lily_lex_state *, lily_lex_mode, char *);
void lily_load_special(lily_lex_state *, lily_lex_mode, void *, char *,
    lily_reader_fn, lily_close_fn);
lily_lex_state *lily_new_lex_state(lily_raiser *, void *);
char *tokname(lily_token);

#endif

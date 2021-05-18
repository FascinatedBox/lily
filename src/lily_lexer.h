#ifndef LILY_LEXER_H
# define LILY_LEXER_H

# include <stdio.h>

# include "lily_raiser.h"
# include "lily_string_pile.h"
# include "lily_token.h"

typedef enum {
    et_copied_string,
    et_file,
    et_lambda,
    et_shallow_string,
    et_unused,
} lily_lex_entry_type;

typedef union {
    int64_t integer_val;
    double double_val;
} lily_lex_number;

typedef struct lily_lex_entry_ {
    union {
        FILE *entry_file;
        /* For string-based entries, where to read the next line from. */
        const char *entry_cursor;
    };

    /* For copied strings, this holds the origin so that it can be free'd when
       the entry is done. */
    char *cursor_origin;

    /* These fields are aligned with fields of the same name in lily_lex_state.
       When another entry becomes current, the lexer's state is saved into the
       entry. */
    lily_token token: 8;
    uint8_t pad;
    uint16_t line_num;
    uint16_t expand_start_line;
    uint16_t string_length;
    lily_lex_number n;

    /* Where this entry starts in lexer's string pile. */
    uint16_t pile_start;
    /* How long the identifier saved is. */
    uint16_t ident_length;
    /* How far the read cursor is from the source line. */
    uint16_t cursor_offset;
    lily_lex_entry_type entry_type: 16;

    struct lily_lex_entry_ *prev;
    struct lily_lex_entry_ *next;
} lily_lex_entry;

typedef struct {
    /* Where the next token read should begin. */
    char *read_cursor;
    /* A buffer for storing the last identifier read in. This buffer's size is
       always at least the size of the source, so that identifier reading
       doesn't need to do buffer size checks when copying over. */
    char *label;

    uint32_t source_size;
    uint32_t label_size;

    lily_token token: 8;
    uint8_t pad;
    uint16_t line_num;
    /* For tokens that can span multiple lines, this is their starting line. */
    uint16_t expand_start_line;
    union {
        /* How many bytes are in the String/ByteString literal. */
        uint16_t string_length;
        /* If the last digit scanned had a sign, then that sign is at source
           plus this offset.
           If there wasn't a sign, this is uint16 max. */
        uint16_t number_sign_offset;
    };

    lily_lex_number n;
    char *source;
    lily_lex_entry *entry;

    /* This holds the source lines and identifiers of entries that aren't
       current. This pile is not shared anywhere else. */
    lily_string_pile *string_pile;

    lily_raiser *raiser;
} lily_lex_state;

lily_lex_state *lily_new_lex_state(lily_raiser *);
void lily_rewind_lex_state(lily_lex_state *, uint16_t);
void lily_free_lex_state(lily_lex_state *);

void lily_lexer_load(lily_lex_state *, lily_lex_entry_type, const void *);
void lily_pop_lex_entry(lily_lex_state *);

void lily_next_token(lily_lex_state *);

char *lily_read_template_content(lily_lex_state *, int *);
int lily_read_manifest_header(lily_lex_state *);
int lily_read_template_header(lily_lex_state *);

int lily_lexer_digit_rescan(lily_lex_state *);
void lily_lexer_verify_path_string(lily_lex_state *);

int64_t lily_scan_number(char *, int *);

#endif
 

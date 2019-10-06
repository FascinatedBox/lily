#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stddef.h>

#include "lily_config.h"
#include "lily_lexer.h"
#include "lily_utf8.h"
#include "lily_alloc.h"

/* Group 1: Increment pos, return a simple token. */
#define CC_G_ONE_OFFSET  0
/* CC_LEFT_PARENTH isn't here because (| opens a lambda. */
#define CC_RIGHT_PARENTH 0
#define CC_COMMA         1
#define CC_LEFT_CURLY    2
#define CC_RIGHT_CURLY   3
#define CC_LEFT_BRACKET  4
#define CC_COLON         5
#define CC_TILDE         6
#define CC_G_ONE_LAST    6

/* Group 2: Return self, or self= */
#define CC_G_TWO_OFFSET  7
#define CC_CARET         7
#define CC_NOT           8
#define CC_PERCENT       9
#define CC_MULTIPLY      10
#define CC_DIVIDE        11
#define CC_G_TWO_LAST    11

/* Greater and Less are able to do shifts, self=, and self. < can become <[,
   but the reverse of that is ]>, so these two aren't exactly the same. So
   there's no group for them. */
#define CC_GREATER       12
#define CC_LESS          13
#define CC_PLUS          14
#define CC_MINUS         15
#define CC_WORD          16
#define CC_DOUBLE_QUOTE  17
#define CC_NUMBER        18
#define CC_LEFT_PARENTH  19
#define CC_RIGHT_BRACKET 20

#define CC_EQUAL         21
#define CC_NEWLINE       22
#define CC_SHARP         23
#define CC_DOT           24
#define CC_AT            25
#define CC_AMPERSAND     26
#define CC_VBAR          27
#define CC_QUESTION      28
#define CC_B             29
#define CC_DOLLAR        30
#define CC_SINGLE_QUOTE  31
#define CC_INVALID       32

static int read_line(lily_lex_state *);
static void close_entry(lily_lex_entry *);

/* Is the given character a valid identifier? This table is checked after the
   first letter, so it includes numbers.
   The 80-BF range is marked as okay because read_line verifies that they are
   not starting a sequence. */
static const char ident_table[256] =
{
     /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/* 0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 1 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 2 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 3 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
/* 4 */ 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
/* 6 */ 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 7 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
/* 8 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 9 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* A */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* B */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* C */ 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* D */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* E */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* F */ 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Group 1 doesn't need a table because the token is just ch_class[ch]. */
static const lily_token grp_two_table[] =
{
    tk_bitwise_xor, tk_not, tk_modulo, tk_multiply, tk_divide
};

static const lily_token grp_two_eq_table[] =
{
    tk_bitwise_xor_eq, tk_not_eq, tk_modulo_eq, tk_multiply_eq, tk_divide_eq,
};


/** Lexer init and deletion **/
lily_lex_state *lily_new_lex_state(lily_raiser *raiser)
{
    lily_lex_state *lexer = lily_malloc(sizeof(*lexer));

    char *ch_class;

    lexer->entry = NULL;
    lexer->raiser = raiser;
    lexer->input_buffer = lily_malloc(128 * sizeof(*lexer->input_buffer));
    lexer->label = lily_malloc(128 * sizeof(*lexer->label));
    lexer->ch_class = NULL;
    lexer->last_literal = NULL;
    lexer->last_integer = 0;
    ch_class = lily_malloc(256 * sizeof(*ch_class));

    lexer->input_pos = 0;
    lexer->input_size = 128;
    lexer->label_size = 128;
    /* This must start at 0 since the line reader will bump it by one. */
    lexer->line_num = 0;

    /* Initialize ch_class, which is used to determine what 'class' a letter
       is in. */
    memset(ch_class, CC_INVALID, (256 * sizeof(char)));

    int i;
    for (i = 'a';i <= 'z';i++)
        ch_class[i] = CC_WORD;

    for (i = 'A';i <= 'Z';i++)
        ch_class[i] = CC_WORD;

    for (i = '0';i <= '9';i++)
        ch_class[i] = CC_NUMBER;

    /* These are valid 2, 3, and 4 byte sequence starters. */
    for (i = 194;i <= 244;i++)
        ch_class[i] = CC_WORD;

    ch_class[(unsigned char)'B'] = CC_B;
    ch_class[(unsigned char)'_'] = CC_WORD;
    ch_class[(unsigned char)'('] = CC_LEFT_PARENTH;
    ch_class[(unsigned char)')'] = CC_RIGHT_PARENTH;
    ch_class[(unsigned char)'"'] = CC_DOUBLE_QUOTE;
    ch_class[(unsigned char)'\''] = CC_SINGLE_QUOTE;
    ch_class[(unsigned char)'@'] = CC_AT;
    ch_class[(unsigned char)'?'] = CC_QUESTION;
    ch_class[(unsigned char)'#'] = CC_SHARP;
    ch_class[(unsigned char)'='] = CC_EQUAL;
    ch_class[(unsigned char)'.'] = CC_DOT;
    ch_class[(unsigned char)','] = CC_COMMA;
    ch_class[(unsigned char)'+'] = CC_PLUS;
    ch_class[(unsigned char)'-'] = CC_MINUS;
    ch_class[(unsigned char)'{'] = CC_LEFT_CURLY;
    ch_class[(unsigned char)'}'] = CC_RIGHT_CURLY;
    ch_class[(unsigned char)'<'] = CC_LESS;
    ch_class[(unsigned char)'>'] = CC_GREATER;
    ch_class[(unsigned char)':'] = CC_COLON;
    ch_class[(unsigned char)'!'] = CC_NOT;
    ch_class[(unsigned char)'*'] = CC_MULTIPLY;
    ch_class[(unsigned char)'/'] = CC_DIVIDE;
    ch_class[(unsigned char)'&'] = CC_AMPERSAND;
    ch_class[(unsigned char)'%'] = CC_PERCENT;
    ch_class[(unsigned char)'|'] = CC_VBAR;
    ch_class[(unsigned char)'^'] = CC_CARET;
    ch_class[(unsigned char)'['] = CC_LEFT_BRACKET;
    ch_class[(unsigned char)']'] = CC_RIGHT_BRACKET;
    ch_class[(unsigned char)'$'] = CC_DOLLAR;
    ch_class[(unsigned char)'~'] = CC_TILDE;
    ch_class[(unsigned char)'\n'] = CC_NEWLINE;

    /* This is set so that token is never unset, which allows parser to check
       the token before the first lily_lexer call. This is important, because
       lily_lexer_handle_content may hit eof if there is nothing to parse. */
    lexer->token = tk_invalid;
    lexer->ch_class = ch_class;
    return lexer;
}

void lily_rewind_lex_state(lily_lex_state *lexer)
{
    if (lexer->entry) {
        lily_lex_entry *entry_iter = lexer->entry;
        while (entry_iter->prev)
            entry_iter = entry_iter->prev;

        lexer->entry = entry_iter;

        while (entry_iter) {
            if (entry_iter->source != NULL) {
                close_entry(entry_iter);
                entry_iter->source = NULL;
            }

            lily_free(entry_iter->saved_input);
            entry_iter->saved_input = NULL;
            entry_iter = entry_iter->next;
        }
    }

    lexer->last_literal = NULL;
    lexer->last_integer = 0;
    lexer->input_pos = 0;
}

void lily_free_lex_state(lily_lex_state *lexer)
{
    if (lexer->entry) {
        lily_lex_entry *entry_iter = lexer->entry;
        while (entry_iter->prev)
            entry_iter = entry_iter->prev;

        lily_lex_entry *entry_next;
        while (entry_iter) {
            if (entry_iter->source != NULL)
                close_entry(entry_iter);

            entry_next = entry_iter->next;
            lily_free(entry_iter->saved_input);
            lily_free(entry_iter);
            entry_iter = entry_next;
        }
    }

    lily_free(lexer->input_buffer);
    lily_free(lexer->ch_class);
    lily_free(lexer->label);
    lily_free(lexer);
}

/* Get an entry to hold the given filename. This attempts to use an unused one,
   but makes a new one if needed. */
static lily_lex_entry *get_entry(lily_lex_state *lexer)
{
    lily_lex_entry *ret_entry = NULL;

    if (lexer->entry == NULL ||
        (lexer->entry->source != NULL && lexer->entry->next == NULL)) {
        ret_entry = lily_malloc(sizeof(*ret_entry));

        if (lexer->entry == NULL) {
            lexer->entry = ret_entry;
            ret_entry->prev = NULL;
        }
        else {
            lexer->entry->next = ret_entry;
            ret_entry->prev = lexer->entry;
        }

        ret_entry->source = NULL;
        ret_entry->extra = NULL;
        ret_entry->saved_input = NULL;
        ret_entry->saved_input_pos = 0;

        ret_entry->next = NULL;
        ret_entry->lexer = lexer;
    }
    else {
        if (lexer->entry->source == NULL)
            ret_entry = lexer->entry;
        else
            ret_entry = lexer->entry->next;
    }

    if (ret_entry->prev) {
        lily_lex_entry *prev_entry = ret_entry->prev;
        char *new_input;
        /* size + 1 isn't needed here because the input buffer's size includes
           space for the \0. */
        if (prev_entry->saved_input == NULL)
            new_input = lily_malloc(lexer->input_size * sizeof(*new_input));
        else if (prev_entry->saved_input_size < lexer->input_size)
            new_input = lily_realloc(prev_entry->saved_input,
                    lexer->input_size * sizeof(*new_input));
        else
            new_input = prev_entry->saved_input;

        strcpy(new_input, lexer->input_buffer);
        prev_entry->saved_input = new_input;
        prev_entry->saved_line_num = lexer->line_num;
        prev_entry->saved_input_pos = lexer->input_pos;
        prev_entry->saved_input_size = lexer->input_size;
        prev_entry->saved_token = lexer->token;
        prev_entry->saved_last_literal = lexer->last_literal;
        prev_entry->saved_last_integer = lexer->last_integer;

        lexer->line_num = 0;
    }

    lexer->input_pos = 0;
    lexer->entry = ret_entry;

    return ret_entry;
}

/* Remove the top-most entry. Restore state to what the previous entry held. */
void lily_pop_lex_entry(lily_lex_state *lexer)
{
    lily_lex_entry *entry = lexer->entry;

    close_entry(entry);
    entry->source = NULL;

    if (entry->prev) {
        entry = entry->prev;

        strcpy(lexer->input_buffer, entry->saved_input);

        lexer->line_num = entry->saved_line_num;
        lexer->input_pos = entry->saved_input_pos;
        /* The lexer's input buffer may have been resized by the entered file.
           Do NOT restore lexer->input_size here. Ever. */
        lexer->entry = entry;
        lexer->last_literal = entry->saved_last_literal;
        lexer->last_integer = entry->saved_last_integer;

        lexer->token = entry->saved_token;

        /* lexer->label has almost certainly been overwritten with something
           else. Restore it by rolling back and calling for a rescan. */
        if (lexer->token == tk_word || lexer->token == tk_prop_word ||
            lexer->token == tk_keyword_arg) {
            int end, pos;
            end = pos = lexer->input_pos;

            do {
                char ch = lexer->input_buffer[pos - 1];
                if (ident_table[(unsigned int)ch] == 0)
                    break;
                pos--;
            } while (pos);

            strncpy(lexer->label, lexer->input_buffer + pos, end - pos);
            lexer->label[(end - pos)] = '\0';
        }
    }
    else
        /* Nothing to return...but set this to 0 or the next entry will start
           from where the closed one left off at. */
        lexer->line_num = 0;
}

#define READER_PREP \
int bufsize, i; \
lily_lex_state *lexer = entry->lexer; \
char *input_buffer = lexer->input_buffer; \
 \
bufsize = lexer->input_size; \
i = 0; \
int utf8_check = 0;

#define READER_GROW_CHECK \
if ((i + 2) == bufsize) { \
    lily_grow_lexer_buffers(lexer); \
    input_buffer = lexer->input_buffer; \
    bufsize = lexer->input_size; \
}

#define READER_EOF_CHECK(to_check, against) \
if (to_check == against) { \
    input_buffer[i] = '\n'; \
    input_buffer[i + 1] = '\0'; \
    /* Bump the line number, unless only EOF or \0 was seen. */ \
    lexer->line_num += !!i; \
    break; \
}

#define READER_END \
if (utf8_check && lily_is_valid_utf8(input_buffer) == 0) { \
    lily_raise_err(lexer->raiser, "Invalid utf-8 sequence on line %d.", \
            lexer->line_num); \
} \
 \
return i;

/** file and str reading functions **/

/* This reads a line from a file-backed entry. */
static int read_file_line(lily_lex_entry *entry)
{
    READER_PREP
    FILE *input_file = (FILE *)entry->source;
    int ch;

    while (1) {
        ch = fgetc(input_file);

        READER_GROW_CHECK
        READER_EOF_CHECK(ch, EOF)

        input_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            lexer->line_num++;

            if (ch == '\r') {
                input_buffer[i] = '\n';
                ch = fgetc(input_file);
                if (ch != '\n')
                    ungetc(ch, input_file);
            }

            i++;
            input_buffer[i] = '\0';
            break;
        }
        else if ((unsigned char)ch > 127)
            utf8_check = 1;

        i++;
    }

    READER_END
}

/* This reads a line from a string-backed entry. */
static int read_str_line(lily_lex_entry *entry)
{
    READER_PREP
    char *ch = (char *)entry->source;

    while (1) {
        READER_GROW_CHECK
        READER_EOF_CHECK(*ch, '\0')

        input_buffer[i] = *ch;

        if (*ch == '\r' || *ch == '\n') {
            lexer->line_num++;

            if (*ch == '\r') {
                input_buffer[i] = '\n';
                ch++;
                if (*ch == '\n')
                    ch++;
            }
            else
                ch++;

            i++;
            input_buffer[i] = '\0';
            break;
        }
        else if (((unsigned char)*ch) > 127)
            utf8_check = 1;

        i++;
        ch++;
    }

    entry->source = ch;
    READER_END
}

#undef READER_PREP
#undef READER_GROW_CHECK
#undef READER_EOF_CHECK
#undef READER_END

static int read_line(lily_lex_state *lex)
{
    lily_lex_entry *entry = lex->entry;
    if (entry->entry_type == et_file)
        return read_file_line(entry);
    else
        return read_str_line(entry);
}

static void close_entry(lily_lex_entry *entry)
{
    if (entry->entry_type == et_file)
        fclose((FILE *)entry->source);
    else if (entry->entry_type != et_shallow_string)
        /* entry->source moves, but entry->extra doesn't. Use this. */
        lily_free(entry->extra);
}

/** Scanning functions and helpers **/

/* Returns the char code of an escape. *source_ch starts on the first character
   after the slash, and is updated to 1 past the end of the escape sequence. */
static char scan_escape(lily_lex_state *lexer, char **source_ch)
{
    char ret;
    char *ch = *source_ch;

    if (*ch == 'n')
        ret = '\n';
    else if (*ch == 'r')
        ret = '\r';
    else if (*ch == 't')
        ret = '\t';
    else if (*ch == '\'')
        ret = '\'';
    else if (*ch == '"')
        ret = '"';
    else if (*ch == '\\')
        ret = '\\';
    else if (*ch == 'b')
        ret = '\b';
    else if (*ch == 'a')
        ret = '\a';
    else if (*ch == '/')
        ret = LILY_PATH_CHAR;
    else if (*ch >= '0' && *ch <= '9') {
        /* It's a numeric escape. Keep swallowing up numeric values until either
           3 are caught in total OR there is a possible 'overflow'.
           This...seems like the right thing to do, I guess. */
        int i, value = 0, total = 0;
        for (i = 0;i < 3;i++, ch++) {
            if (*ch < '0' || *ch > '9')
                break;

            value = *ch - '0';
            if ((total * 10) + value > 255)
                break;

            total = (total * 10) + value;
        }

        ch -= 1;
        ret = (char)total;
    }
    else {
        /* Keeps the compiler happy (ret always given a value). */
        ret = 0;
        lily_raise_syn(lexer->raiser, "Invalid escape sequence.");
    }

    *source_ch = ch + 1;

    return ret;
}

/** Numeric scanning **/
/* Most of the numeric scanning functions follow a pattern: Each defines the max
   number of digits it can take so that the unsigned result cannot overflow.
   scan_decimal takes an extra is_integer param because it may have an exponent
   which automatically meant it will yield a double result. */

static void scan_exponent(lily_lex_state *lexer, int *pos, char *new_ch)
{
    int num_pos = *pos + 1;
    int num_digits = 0;

    new_ch++;
    if (*new_ch == '+' || *new_ch == '-') {
        num_pos++;
        new_ch++;
    }

    if (*new_ch < '0' || *new_ch > '9')
        lily_raise_syn(lexer->raiser,
                   "Expected a base 10 number after exponent.");

    while (*new_ch >= '0' && *new_ch <= '9') {
        num_digits++;
        if (num_digits > 3) {
            lily_raise_syn(lexer->raiser, "Exponent is too large.");
        }
        num_pos++;
        new_ch++;
    }

    *pos = num_pos;
}

static uint64_t scan_binary(int *pos, char *ch)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 65;
    int num_pos = *pos + 1;

    /* Skip the 'b' part of the 0b intro. */
    ch++;

    while (*ch == '0') {
        num_pos++;
        ch++;
    }

    while ((*ch == '0' || *ch == '1') && num_digits != max_digits) {
        num_digits++;
        result = (result * 2) + *ch - '0';
        ch++;
        num_pos++;
    }

    *pos = num_pos;
    return result;
}

static uint64_t scan_octal(int *pos, char *ch)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 23;
    int num_pos = *pos + 1;

    /* Skip the 'c' part of the 0c intro. */
    ch++;

    while (*ch == '0') {
        num_pos++;
        ch++;
    }

    while (*ch >= '0' && *ch <= '7' && num_digits != max_digits) {
        num_digits++;
        result = (result * 8) + *ch - '0';
        num_pos++;
        ch++;
    }

    *pos = num_pos;
    return result;
}

static uint64_t scan_decimal(lily_lex_state *lexer, int *pos, int *is_integer,
        char *new_ch)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 21;
    /* This is the only scanner that doesn't start off on pos+1. This is because
       there's no formatting character to skip over. */
    int num_pos = *pos;
    int have_dot = 0;

    while (*new_ch == '0') {
        num_pos++;
        new_ch++;
    }

    while (num_digits != max_digits) {
        if (*new_ch >= '0' && *new_ch <= '9') {
            if (*is_integer) {
                num_digits++;
                result = (result * 10) + *new_ch - '0';
            }
        }
        else if (*new_ch == '.') {
            /* The second check is important for things like '10.@(...',
               10.to_string, and more. */
            unsigned char next_ch = (unsigned char)*(new_ch + 1);

            if (have_dot == 1 ||
                isdigit(next_ch) == 0)
                break;

            have_dot = 1;
            *is_integer = 0;
        }
        else if (*new_ch == 'e') {
            *is_integer = 0;
            scan_exponent(lexer, &num_pos, new_ch);
            break;
        }
        else
            break;

        num_digits++;
        num_pos++;
        new_ch++;
    }

    *pos = num_pos;
    return result;
}

static uint64_t scan_hex(int *pos, char *new_ch)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 17;
    int num_pos = *pos + 1;

    /* Skip the 'x' part of the 0x intro. */
    new_ch++;

    while (*new_ch == '0') {
        num_pos++;
        new_ch++;
    }

    while (num_digits != max_digits) {
        char mod;
        if (*new_ch >= '0' && *new_ch <= '9')
            mod = '0';
        else if (*new_ch >= 'a' && *new_ch <= 'f')
            mod = 'a' - 10;
        else if (*new_ch >= 'A' && *new_ch <= 'F')
            mod = 'A' - 10;
        else
            break;

        result = (result * 16) + *new_ch - mod;
        num_digits++;
        num_pos++;
        new_ch++;
    }

    *pos = num_pos;
    return result;
}

/* This handles all numeric scanning. 'pos' is used as the starting spot. The
   results are sent to 'tok' (which is set to either tk_integer or tk_double),
   and 'new_ch' (which is set to the place to start scanning from next time). */
static void scan_number(lily_lex_state *lexer, int *pos, lily_token *tok,
        char *new_ch)
{
    char sign = *new_ch;
    int num_pos = *pos;
    int num_start = *pos;
    int is_integer = 1;
    uint64_t integer_value = 0;
    lily_raw_value yield_val;

    if (sign == '-' || sign == '+') {
        num_pos++;
        new_ch++;
    }

    if (*new_ch == '0') {
        num_pos++;
        new_ch++;

        if (*new_ch == 'b')
            integer_value = scan_binary(&num_pos, new_ch);
        else if (*new_ch == 'c')
            integer_value = scan_octal(&num_pos, new_ch);
        else if (*new_ch == 'x')
            integer_value = scan_hex(&num_pos, new_ch);
        else
            integer_value = scan_decimal(lexer, &num_pos, &is_integer, new_ch);
    }
    else
        integer_value = scan_decimal(lexer, &num_pos, &is_integer, new_ch);

    char suffix = lexer->input_buffer[num_pos];

    if (suffix == 't') {
        if (is_integer == 0)
            lily_raise_syn(lexer->raiser, "Double value with Byte suffix.");

        if (sign == '-' || sign == '+')
            lily_raise_syn(lexer->raiser, "Byte values cannot have a sign.");

        if (integer_value > 0xFF)
            lily_raise_syn(lexer->raiser, "Byte value is too large.");

        lexer->last_integer = integer_value;
        *tok = tk_byte;
        num_pos++;
    }
    else if (is_integer) {
        /* This won't be used uninitialized. I promise. */
        yield_val.doubleval = 0.0;
        if (sign != '-') {
            if (integer_value <= INT64_MAX)
                yield_val.integer = (int64_t)integer_value;
            else
                lily_raise_syn(lexer->raiser, "Integer value is too large.");
        }
        else {
            /* This is negative, and typened min is 1 higher than typened max. This is
               written as a literal so that gcc doesn't complain about overflow. */
            uint64_t max = 9223372036854775808ULL;
            if (integer_value <= max)
                yield_val.integer = -(int64_t)integer_value;
            else
                lily_raise_syn(lexer->raiser, "Integer value is too small.");
        }

        lexer->last_integer = yield_val.integer;
        *tok = tk_integer;
    }
    /* Not an integer, so use strtod to try converting it to a double so it can
       be stored as a double. */
    else {
        double double_result;
        char *input_buffer = lexer->input_buffer;
        int str_size = num_pos - num_start;
        strncpy(lexer->label, input_buffer+num_start, str_size * sizeof(char));

        lexer->label[str_size] = '\0';
        errno = 0;
        double_result = strtod(lexer->label, NULL);
        if (errno == ERANGE)
            lily_raise_syn(lexer->raiser, "Double value is out of range.");

        yield_val.doubleval = double_result;
        lexer->last_literal = lily_get_double_literal(lexer->symtab,
                yield_val.doubleval);
        *tok = tk_double;
    }

    *pos = num_pos;
}

static void scan_multiline_comment(lily_lex_state *lexer, char **source_ch)
{
    int start_line = lexer->line_num;
    /* +2 to skip the #[ intro. */
    char *new_ch = *source_ch + 2;

    while (1) {
        if (*new_ch == ']' &&
            *(new_ch + 1) == '#') {
            new_ch += 2;
            break;
        }
        else if (*new_ch == '\n') {
            if (read_line(lexer)) {
                new_ch = &(lexer->input_buffer[0]);
                /* Must continue, in case the first char is the # of ]#.\n */
                continue;
            }
            else {
                lily_raise_syn(lexer->raiser,
                           "Unterminated multi-line comment (started at line %d).",
                           start_line);
            }
        }

        new_ch++;
    }

    *source_ch = new_ch;
}

static void ensure_label_size(lily_lex_state *lexer, int at_least)
{
    if (lexer->label_size > at_least)
        return;

    int new_size = lexer->label_size;
    while (new_size < at_least)
        new_size *= 2;

    char *new_data = lily_realloc(lexer->label, new_size * sizeof(*new_data));

    lexer->label = new_data;
    lexer->label_size = new_size;
}

/* This scans a docstring to verify it has the proper structure. The interpreter
   doesn't store docstrings though. Instead, that job is left up to tooling. */
static void scan_docstring(lily_lex_state *lexer, char **ch)
{
    int offset = (int)(*ch - lexer->input_buffer);
    char *buffer = lexer->input_buffer;

    while (1) {
        int i = 0;

        while (buffer[i] == ' ' || buffer[i] == '\t')
            i++;

        if (buffer[i] != '#')
            break;
        else if (buffer[i] == '#') {
            if (buffer[i + 1] != '#' ||
                buffer[i + 2] != '#')
                lily_raise_syn(lexer->raiser,
                        "Docstring line does not start with full '###'.");
            else if (i != offset)
                lily_raise_syn(lexer->raiser,
                        "Docstring has inconsistent indentation.");
        }

        if (read_line(lexer))
            /* Must reassign, in case input_buffer was moved. */
            buffer = lexer->input_buffer;
        else
            break;
    }
}

static void scan_quoted_raw(lily_lex_state *, char **, int *, int);

#define SQ_IS_BYTESTRING 0x01
/* Lambdas need to collect everything inside of them as-is. Later, they'll be
   loaded and read in. If this is set, include quote marks, but don't run
   escapes. */
#define SQ_IN_LAMBDA     0x02

static void scan_quoted_raw(lily_lex_state *lexer, char **source_ch, int *start,
        int flags)
{
    char *label;
    int label_pos, multiline_start = 0;
    int is_multiline = 0;

    char *new_ch = *source_ch;
    label = lexer->label;

    /* ch is actually the first char after the opening ". */
    if (*(new_ch + 1) == '"' &&
        *(new_ch + 2) == '"') {
        is_multiline = 1;
        multiline_start = lexer->line_num;
        new_ch += 2;
    }

    if (flags & SQ_IN_LAMBDA) {
        int num = is_multiline ? 3 : 1;
        strncpy(lexer->label + *start, "\"\"\"", num);
        *start += num;
    }

    /* Skip the last " of either kind of string. */
    new_ch++;
    label_pos = *start;
    int backslash_before_newline = 0;

    while (1) {
        if (*new_ch == '\\') {
            char *start_ch = new_ch;

            new_ch++;

            if (*new_ch == '\n') {
                if (flags & SQ_IN_LAMBDA) {
                    label[label_pos] = '\\';
                    label_pos++;
                }

                backslash_before_newline = 1;
                continue;
            }

            char esc_ch = scan_escape(lexer, &new_ch);
            /* Make sure String is \0 terminated and utf-8 clean. */
            if ((flags & SQ_IS_BYTESTRING) == 0 &&
                (esc_ch == 0 || (unsigned char)esc_ch > 127))
                lily_raise_syn(lexer->raiser, "Invalid escape sequence.");

            if ((flags & SQ_IN_LAMBDA) == 0) {
                label[label_pos] = esc_ch;
                label_pos++;
            }
            else {
                /* Lambdas will get loaded later, so give them the original
                   source that will get escape processed later. */
                while (start_ch != new_ch) {
                    label[label_pos] = *start_ch;
                    label_pos++;
                    start_ch++;
                }
            }
        }
        else if (*new_ch == '\n') {
            if (is_multiline == 0 && backslash_before_newline == 0)
                lily_raise_syn(lexer->raiser, "Newline in single-line string.");
            int line_length = read_line(lexer);
            if (line_length == 0) {
                lily_raise_syn(lexer->raiser,
                           "Unterminated string (started at line %d).",
                           multiline_start);
            }

            ensure_label_size(lexer, label_pos + line_length + 3);
            label = lexer->label;
            new_ch = &lexer->input_buffer[0];

            if (backslash_before_newline == 0 || (flags & SQ_IN_LAMBDA)) {
                label[label_pos] = '\n';
                label_pos++;
            }
            else {
                while (*new_ch == ' ')
                    new_ch++;

                backslash_before_newline = 0;
            }
        }
        else if (*new_ch == '"' &&
                 ((is_multiline == 0) ||
                  (*(new_ch + 1) == '"' && *(new_ch + 2) == '"'))) {
            new_ch++;
            break;
        }
        else if (*new_ch == '\0')
            break;
        else {
            label[label_pos] = *new_ch;
            label_pos++;
            new_ch++;
        }
    }

    if (is_multiline)
        new_ch += 2;

    if (flags & SQ_IN_LAMBDA) {
        int num = is_multiline ? 3 : 1;
        strncpy(lexer->label + label_pos, "\"\"\"", num);
        label_pos += num;
    }

    if ((flags & (SQ_IN_LAMBDA | SQ_IS_BYTESTRING)) == 0)
        label[label_pos] = '\0';

    if ((flags & SQ_IN_LAMBDA) == 0) {
        if ((flags & SQ_IS_BYTESTRING) == 0)
            lexer->last_literal = lily_get_string_literal(lexer->symtab, label);
        else
            lexer->last_literal = lily_get_bytestring_literal(lexer->symtab,
                    label, label_pos);
    }

    *source_ch = new_ch;
    *start = label_pos;
}

static void scan_quoted(lily_lex_state *lexer, char **source_ch, int flags)
{
    int dummy = 0;
    scan_quoted_raw(lexer, source_ch, &dummy, flags);
}

static void scan_single_quote(lily_lex_state *lexer, char **source_ch)
{
    char *new_ch = *source_ch + 1;
    char ch = *new_ch;

    if (ch == '\\') {
        new_ch++;
        ch = scan_escape(lexer, &new_ch);
    }
    else if (ch != '\'') {
        ch = *new_ch;
        new_ch += 1;
    }
    else
        lily_raise_syn(lexer->raiser, "Byte literals cannot be empty.");

    if (*new_ch != '\'')
        lily_raise_syn(lexer->raiser, "Multi-character byte literal.");

    *source_ch = new_ch + 1;
    lexer->last_integer = (unsigned char)ch;
}

static void scan_lambda(lily_lex_state *lexer, char **source_ch)
{
    char *label = lexer->label, *ch = *source_ch;
    int brace_depth = 1, i = 0;

    lexer->expand_start_line = lexer->line_num;
    label = lexer->label;

    while (1) {
        if (*ch == '\n' ||
            (*ch == '#' &&
             *(ch + 1) != '[')) {
            int line_length = read_line(lexer);
            if (line_length == 0)
                lily_raise_syn(lexer->raiser,
                        "Unterminated lambda (started at line %d).",
                        lexer->expand_start_line);

            ensure_label_size(lexer, i + line_length + 3);
            label = lexer->label;
            ch = &lexer->input_buffer[0];
            label[i] = '\n';
            i++;
            continue;
        }
        else if (*ch == '#' &&
                 *(ch + 1) == '[') {
            int saved_line_num = lexer->line_num;
            scan_multiline_comment(lexer, &ch);

            /* For each line that the multi-line comment hit, add a newline to
               the lambda so that error lines are right. */
            if (saved_line_num != lexer->line_num) {
                int increase = lexer->line_num - saved_line_num;
                /* Write \n for each line seen so that the lines match up.
                   Make sure that lexer->label can handle the increases AND the
                   now-current line (plus 3 for that termination). */
                ensure_label_size(lexer,
                        i + increase + 3 + strlen(lexer->input_buffer));
                label = lexer->label;

                memset(label + i, '\n', increase);
                i += increase;
            }
            continue;
        }
        else if (*ch == '"') {
            int flags = SQ_IN_LAMBDA;

            /* This lets scan_quoted_raw know that ByteString escape codes are
               allowed instead of failing on them. */
            if (ch != &lexer->input_buffer[0] && *(ch - 1) == 'B')
                flags |= SQ_IS_BYTESTRING;

            scan_quoted_raw(lexer, &ch, &i, flags);
            label = lexer->label;
            /* Don't ensure check: scan_quoted already did it if that was
               necessary. */
            continue;
        }
        else if (*ch == '\'') {
            scan_single_quote(lexer, &ch);
            ensure_label_size(lexer, i + 7);
            label = lexer->label;

            char buffer[8];
            sprintf(buffer, "'\\%d'", (uint8_t)lexer->last_integer);
            strcpy(label + i, buffer);
            i += strlen(buffer);
            continue;
        }
        else if (*ch == '(')
            brace_depth++;
        else if (*ch == ')') {
            if (brace_depth == 1)
                break;

            brace_depth--;
        }

        label[i] = *ch;
        ch++;
        i++;
    }

    /* Add in the closing ')' at the end so the parser will know for sure when
       the lambda is done. */
    label[i] = '\0';

    *source_ch = ch + 1;
}

/* Walk the string literal that 'import' was given to make sure that it's
   valid. This also fixes the '/' characters into proper path characters for the
   given platform. */
void lily_lexer_verify_path_string(lily_lex_state *lexer)
{
    char *label = lexer->label;

    if (label[0] == '\0')
        lily_raise_syn(lexer->raiser, "Import path must not be empty.");

    int original_len = strlen(lexer->label);
    int len = original_len;
    int necessary = 0;
    char *reverse_iter = &lexer->input_buffer[lexer->input_pos - 2];
    char *reverse_label = label + len - 1;

    if (lexer->input_pos > 3 &&
        *reverse_iter == '"' &&
        *(reverse_iter - 1) != '\\')
        lily_raise_syn(lexer->raiser,
                "Import path cannot be a triple-quote string.");

    if (*reverse_label == '/' || *label == '/')
        lily_raise_syn(lexer->raiser,
                "Import path cannot begin or end with '/'.");

    while (len) {
        if (*reverse_iter != *reverse_label)
            lily_raise_syn(lexer->raiser,
                    "Import path cannot contain escape characters.");

        char label_ch = *reverse_label;

        if (ident_table[(unsigned char)label_ch] == 0) {
            necessary = 1;
            if (label_ch == '/')
                *reverse_label = LILY_PATH_CHAR;
        }

        reverse_iter--;
        reverse_label--;
        len--;
    }

    if (necessary == 0)
        lily_raise_syn(lexer->raiser,
                "Simple import paths do not need to be quoted.");
}

/* The lexer reads `-1` and `+1` as negative or positive literals. Most of the
   time that's correct. But sometimes it's part of, say, `1+1`. This reads back
   to see if the digit started with + or -. If it did, this returns 1 and
   rescans the value. Otherwise, this returns 0. */
int lily_lexer_digit_rescan(lily_lex_state *lexer)
{
    int pos = lexer->input_pos - 1;
    char *input = lexer->input_buffer;
    int ch = ' ';

    while (pos) {
        ch = input[pos];
        if (isalnum(ch) == 0)
            break;

        pos--;
    }

    int plus_or_minus = (ch == '-' || ch == '+');

    if (plus_or_minus) {
        lexer->input_pos = pos + 1;
        lily_lexer(lexer);
    }

    return plus_or_minus;
}

/** Lexer API **/

/* This grows the lexer's input_buffer and lexer's label. The two are kept in
   sync so that labels can be copied over without size checking. */
void lily_grow_lexer_buffers(lily_lex_state *lexer)
{
    int new_size = lexer->input_size;
    new_size *= 2;

    /* The label buffer is grown when a large multi-line string is
       collected. Don't resize it if it's bigger than the line buffer.
       It's important that the label buffer be at least the size of the
       line buffer at all times, so that lexer's word scanning does not
       have to check for potential overflows. */
    if (lexer->label_size == lexer->input_size) {
        char *new_label;
        new_label = lily_realloc(lexer->label, new_size * sizeof(*new_label));

        lexer->label = new_label;
        lexer->label_size = new_size;
    }

    char *new_lb;
    new_lb = lily_realloc(lexer->input_buffer, new_size * sizeof(*new_lb));

    lexer->input_buffer = new_lb;
    lexer->input_size = new_size;
}

void lily_lexer_load(lily_lex_state *lexer, lily_lex_entry_type entry_type,
        const void *source)
{
    lily_lex_entry *entry = get_entry(lexer);

    entry->extra = NULL;
    entry->entry_type = entry_type;
    entry->final_token = tk_eof;

    if (entry_type != et_file &&
        entry_type != et_shallow_string) {
        char *str_source = (char *)source;
        char *copy = lily_malloc((strlen(str_source) + 1) * sizeof(*copy));

        strcpy(copy, str_source);
        source = copy;
        entry->extra = copy;

        entry->final_token = tk_end_lambda;
    }

    entry->source = (void *)source;
    read_line(lexer);
}

/* Magic scanning function. */
void lily_lexer(lily_lex_state *lexer)
{
    char *ch_class;
    int input_pos = lexer->input_pos;
    lily_token token;

    ch_class = lexer->ch_class;

    while (1) {
        char *start_ch, *ch;
        int group;

        ch = &lexer->input_buffer[input_pos];
        start_ch = ch;

        while (*ch == ' ' || *ch == '\t')
            ch++;

        input_pos += ch - start_ch;
        group = ch_class[(unsigned char)*ch];
        if (group == CC_WORD) {
            token = tk_word;

            label_handling: ;

            /* The word and line buffers have the same size, plus \n is not a
               valid word character. So, there's no point in checking for
               overflow. */
            int word_pos = 0;
            char *label = lexer->label;

            do {
                label[word_pos] = *ch;
                word_pos++;
                ch++;
            } while (ident_table[(unsigned char)*ch]);
            input_pos += word_pos;
            label[word_pos] = '\0';
        }
        else if (group == CC_COLON) {
            if (ident_table[(unsigned char)*(ch + 1)]) {
                ch++;
                input_pos++;
                token = tk_keyword_arg;
                goto label_handling;
            }
            else {
                input_pos++;
                token = group;
            }
        }
        else if (group <= CC_G_ONE_LAST) {
            input_pos++;
            /* This is okay because the first group starts at 0, and the tokens
               for it start at 0. */
            token = group;
        }
        else if (group == CC_NEWLINE) {
            if (read_line(lexer)) {
                input_pos = 0;
                continue;
            }
            else {
                token = lexer->entry->final_token;
                input_pos = 0;
            }
        }
        else if (group == CC_SHARP) {
            if (*(ch + 1) == '[') {
                scan_multiline_comment(lexer, &ch);
                input_pos = ch - lexer->input_buffer;
                continue;
            }
            else if (*(ch + 1) == '#' &&
                     *(ch + 2) == '#') {
                scan_docstring(lexer, &ch);
                input_pos = ch - lexer->input_buffer;
                token = tk_docstring;
            }
            else if (read_line(lexer)) {
                input_pos = 0;
                continue;
            }
            /* read_line has no more data to offer. */
            else {
                token = lexer->entry->final_token;
                input_pos = 0;
            }
        }
        else if (group == CC_DOUBLE_QUOTE) {
            scan_quoted(lexer, &ch, 0);
            input_pos = ch - lexer->input_buffer;
            token = tk_double_quote;
        }
        else if (group == CC_SINGLE_QUOTE) {
            scan_single_quote(lexer, &ch);
            input_pos = ch - lexer->input_buffer;
            token = tk_byte;
        }
        else if (group == CC_B) {
            if (*(ch + 1) == '"') {
                ch++;
                input_pos++;
                scan_quoted(lexer, &ch, SQ_IS_BYTESTRING);
                input_pos = ch - lexer->input_buffer;
                token = tk_bytestring;
            }
            else {
                token = tk_word;
                goto label_handling;
            }
        }
        else if (group <= CC_G_TWO_LAST) {
            input_pos++;
            if (lexer->input_buffer[input_pos] == '=') {
                input_pos++;
                token = grp_two_eq_table[group - CC_G_TWO_OFFSET];
            }
            else
                token = grp_two_table[group - CC_G_TWO_OFFSET];
        }
        else if (group == CC_NUMBER)
            scan_number(lexer, &input_pos, &token, ch);
        else if (group == CC_DOT) {
            if (ch_class[(unsigned char)*(ch + 1)] == CC_NUMBER)
                scan_number(lexer, &input_pos, &token, ch);
            else {
                ch++;
                input_pos++;
                token = tk_dot;
                if (*ch == '.') {
                    ch++;
                    input_pos++;

                    if (*ch == '.') {
                        input_pos++;
                        token = tk_three_dots;
                    }
                    else
                        lily_raise_syn(lexer->raiser,
                                "'..' is not a valid token (expected 1 or 3 dots).");
                }
            }
        }
        else if (group == CC_PLUS) {
            if (ch_class[(unsigned char)*(ch + 1)] == CC_NUMBER)
                scan_number(lexer, &input_pos, &token, ch);
            else if (*(ch + 1) == '=') {
                ch += 2;
                input_pos += 2;
                token = tk_plus_eq;
            }
            else if (*(ch + 1) == '+') {
                if (*(ch + 2) == '=')
                    lily_raise_syn(lexer->raiser,
                            "'++=' is not a valid token.");

                ch += 2;
                input_pos += 2;
                token = tk_plus_plus;
            }
            else {
                ch++;
                input_pos++;
                token = tk_plus;
            }
        }
        else if (group == CC_MINUS) {
            if (ch_class[(unsigned char)*(ch + 1)] == CC_NUMBER)
                scan_number(lexer, &input_pos, &token, ch);
            else if (*(ch + 1) == '=') {
                ch += 2;
                input_pos += 2;
                token = tk_minus_eq;
            }
            else {
                ch++;
                input_pos++;
                token = tk_minus;
            }
        }
        else if (group == CC_LEFT_PARENTH) {
            input_pos++;
            ch++;
            if (*ch == '|') {
                scan_lambda(lexer, &ch);
                input_pos = ch - lexer->input_buffer;
                token = tk_lambda;
            }
            else
                token = tk_left_parenth;
        }
        else if (group == CC_AMPERSAND) {
            input_pos++;
            ch++;

            if (*ch == '&') {
                input_pos++;
                ch++;
                token = tk_logical_and;
            }
            else if (*ch == '=') {
                input_pos++;
                ch++;
                token = tk_bitwise_and_eq;
            }
            else
                token = tk_bitwise_and;
        }
        else if (group == CC_VBAR) {
            input_pos++;
            ch++;

            if (*ch == '|') {
                input_pos++;
                ch++;
                token = tk_logical_or;
            }
            else if (*ch == '>') {
                input_pos++;
                ch++;
                token = tk_func_pipe;
            }
            else if (*ch == '=') {
                input_pos++;
                ch++;
                token = tk_bitwise_or_eq;
            }
            else
                token = tk_bitwise_or;
        }
        else if (group == CC_GREATER || group == CC_LESS) {
            /* This relies on the assumption that x= is the next token, xx is
               the token after, and xx= is the token after that. This is
               mentioned in lily_lexer.h. */
            input_pos++;
            if (group == CC_GREATER)
                token = tk_gt;
            else
                token = tk_lt;

            ch++;
            if (*ch == '=') {
                token++;
                input_pos++;
            }
            else if (*ch == *(ch - 1)) {
                input_pos++;
                ch++;
                if (*ch == '=') {
                    /* xx=, which should be 3 spots after x. */
                    input_pos++;
                    token += 3;
                }
                else
                    /* xx, which should be 2 spots after x. */
                    token += 2;
            }
            else if (*ch == '[' && token == tk_lt) {
                input_pos++;
                token = tk_tuple_open;
            }
        }
        else if (group == CC_EQUAL) {
            input_pos++;
            if (lexer->input_buffer[input_pos] == '=') {
                token = tk_eq_eq;
                input_pos++;
            }
            else if (lexer->input_buffer[input_pos] == '>') {
                token = tk_arrow;
                input_pos++;
            }
            else
                token = tk_equal;
        }
        else if (group == CC_RIGHT_BRACKET) {
            input_pos++;
            ch++;
            if (*ch == '>') {
                input_pos++;
                token = tk_tuple_close;
            }
            else
                token = tk_right_bracket;
        }
        else if (group == CC_AT) {
            ch++;
            input_pos++;
            if (*ch == '(') {
                input_pos++;
                token = tk_typecast_parenth;
            }
            else if (ch_class[(unsigned char)*ch] == CC_WORD ||
                     *ch == 'B') {
                /* Explicit checking for B is necessary because B isn't an
                   identifier. It's not because B"". */
                char *label = lexer->label;
                int word_pos = 0;

                do {
                    label[word_pos] = *ch;
                    word_pos++;
                    ch++;
                } while (ident_table[(unsigned char)*ch]);
                input_pos += word_pos;
                label[word_pos] = '\0';
                token = tk_prop_word;
            }
            else
                token = tk_invalid;
        }
        else if (group == CC_QUESTION) {
            ch++;
            input_pos++;
            if (*ch == '>') {
                input_pos++;
                token = tk_end_tag;
            }
            else
                token = tk_invalid;
        }
        else if (group == CC_DOLLAR) {
            ch++;
            input_pos++;
            if (*ch == '1') {
                input_pos++;
                lexer->last_integer = 1;
                token = tk_scoop;
            }
            else
                token = tk_invalid;
        }
        else
            token = tk_invalid;

        lexer->input_pos = input_pos;
        lexer->token = token;

        return;
    }
}

void lily_verify_template(lily_lex_state *lexer)
{
    if (strncmp(lexer->input_buffer, "<?lily", 5) != 0)
        lily_raise_syn(lexer->raiser,
                "Files in template mode must start with '<?lily'.");

    lexer->input_pos = 6;
}

/* This function loads content outside of the `<?lily ... ?>` tags. This returns
   1 if there is more content, or 0 if the content is done. If 0, the token is
   either a non-EOF value (if there is now Lily code to read), or EOF if the
   source has been exhausted.
   Regardless of the result, *out_buffer is set to the buffer that the lexer
   stored the content into. */
int lily_lexer_read_content(lily_lex_state *lexer, char **out_buffer)
{
    char c;
    int lbp, htmlp;

    /* htmlp and lbp are used so it's obvious they aren't globals. */
    lbp = lexer->input_pos;
    c = lexer->input_buffer[lbp];
    htmlp = 0;

    /* For `?>\n`, don't render the newline (it's annoying). */
    if (c == '\n' &&
        lbp > 2 &&
        lexer->input_buffer[lbp - 1] == '>' &&
        lexer->input_buffer[lbp - 2] == '?') {

        goto next_line;
    }

    /* Send html to the server either when unable to hold more or the lily tag
       is found. */
    while (1) {
        lbp++;
        if (c == '<') {
            if (strncmp(lexer->input_buffer + lbp, "?lily", 5) == 0) {
                lexer->label[htmlp] = '\0';
                *out_buffer = lexer->label;
                lexer->input_pos = lbp + 5;
                return 0;
            }
        }
        lexer->label[htmlp] = c;
        htmlp++;
        if (htmlp == (lexer->input_size - 1)) {
            lexer->label[htmlp] = '\0';
            *out_buffer = lexer->label;
            lexer->input_pos = lbp;
            return 1;
        }

        if (c == '\n') {
next_line:
            if (read_line(lexer))
                lbp = 0;
            else {
                lexer->token = lexer->entry->final_token;

                /* This allows files to have a newline at the end, without
                   sending just that newline. */
                if (htmlp == 1 && lexer->label[0] == '\n')
                    htmlp = 0;

                lexer->label[htmlp] = '\0';
                *out_buffer = lexer->label;
                lexer->input_pos = 0;
                return 0;
            }
        }

        c = lexer->input_buffer[lbp];
    }
}

/* Give a printable name for a given token. Assumes only valid tokens. */
char *tokname(lily_token t)
{
    static char *toknames[] =
    {")", ",", "{", "}", "[", ":", "~", "^", "^=", "!", "!=", "%", "%=", "*",
     "*=", "/", "/=", "+", "++", "+=", "-", "-=", "<", "<=", "<<", "<<=", ">",
     ">=", ">>", ">>=", "=", "==", "(", "a lambda", "<[", "]>", "]", "=>",
     "a label", "a property name", "a string", "a bytestring", "a byte",
     "an integer", "a double", "a docstring", "a named argument", ".", "&",
     "&=", "&&", "|", "|=", "||", "@(", "...", "|>", "$1", "invalid token",
     "end of lambda", "?>", "end of file"};
    char *result = NULL;

    if (t < (sizeof(toknames) / sizeof(toknames[0])))
        result = toknames[t];

    return result;
}

#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "lily_impl.h"
#include "lily_lexer.h"

/** The lexer is responsible for:
    * Opening and reading the file given by parser. It also handles strings sent
      by parser. The lexer has some mode-switching code, since scanning strings
      needs to avoid calling read_line.
    * Scanning the given file/string and returning a token based upon the info.
      The lexer stores labels and strings scanned to lexer->label.
    * The lexer assumes that all files/strings are valid utf-8, and will give up
      upon finding invalid utf-8. Other encodings may be added in the future.
      Additionally, utf-8 can be used in identifiers and strings. utf-8
      validation is in read_line for files, and in lily_load_str for strings.
    * For files, lily_lexer_handle_page_data is available to scan and push the
      HTML outside of the lily tag. The lily tag starts with <@lily and ends
      with @>. Parser is responsible for telling lexer to do the skip when it
      encounters the end tag (@>)
    Caveats:
    * The lexer's way of scanning characters is fairly complicated, and could
      probably be automated or added to a small database for ease of use.
    * Currently, each character scanned is checked against ch_class, which
      indicates what type of character it is. This allows several common cases
      to be reduced into one.
    * However, it's currently difficult to add new items in. New tokens need to
      be placed in the right group, and also the right place in lily_token. **/

/* Group 1: Increment pos, return a simple token. */
#define CC_G_ONE_OFFSET  0
#define CC_LEFT_PARENTH  0
#define CC_RIGHT_PARENTH 1
#define CC_COMMA         2
#define CC_LEFT_CURLY    3
#define CC_RIGHT_CURLY   4
#define CC_COLON         5
#define CC_LEFT_BRACKET  6
#define CC_RIGHT_BRACKET 7
#define CC_G_ONE_LAST    7

/* Group 2: Return self, or self= */
#define CC_G_TWO_OFFSET  8
#define CC_EQUAL         8
#define CC_LESS          9
#define CC_GREATER      10
#define CC_NOT          11
#define CC_MULTIPLY     12
#define CC_DIVIDE       13
#define CC_G_TWO_LAST   13

#define CC_PLUS         14
#define CC_MINUS        15
#define CC_WORD         16
#define CC_DOUBLE_QUOTE 17
#define CC_NUMBER       18

#define CC_NEWLINE      19
#define CC_SHARP        20
#define CC_STR_NEWLINE  21
#define CC_STR_END      22
#define CC_DOT          23
#define CC_AT           24
#define CC_AMPERSAND    25
#define CC_VBAR         26
#define CC_INVALID      27

/*  This table indicates how many more bytes need to be successfully read after
    that particular byte for proper utf-8. -1 = invalid.
    80-BF : These only follow.
    C0-C1 : Can only be used for overlong encoding of ascii.
    F5-FD : RFC 3629 took these out.
    FE-FF : The standard was originally 31-bits.

    Table idea, above info came from wikipedia. */
static const char follower_table[256] = 
{
     /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/* 0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 1 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 2 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 3 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 4 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 6 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 7 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 8 */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* 9 */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* A */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* B */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* C */-1,-1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* D */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* E */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
/* F */ 4, 4, 4, 4, 4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

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
    tk_equal, tk_lt, tk_gr, tk_not, tk_multiply, tk_divide
};

static const lily_token grp_two_eq_table[] =
{
    tk_eq_eq, tk_lt_eq, tk_gr_eq, tk_not_eq, tk_multiply_eq, tk_divide_eq,
};

/** Lexer init and deletion **/
lily_lex_state *lily_new_lex_state(lily_raiser *raiser)
{
    lily_lex_state *lex = lily_malloc(sizeof(lily_lex_state));
    char *ch_class;

    if (lex == NULL)
        return NULL;

    lex->hit_eof = 0;
    lex->lex_file = NULL;
    lex->filename = NULL;
    lex->raiser = raiser;
    lex->save_buffer = NULL;
    lex->lex_buffer = lily_malloc(128 * sizeof(char));
    lex->label = lily_malloc(128 * sizeof(char));
    lex->mode = lm_from_file;
    ch_class = lily_malloc(256 * sizeof(char));

    if (ch_class == NULL || lex->label == NULL || lex->lex_buffer == NULL) {
        lily_free_lex_state(lex);
        return NULL;
    }

    lex->lex_bufpos = 0;
    lex->lex_bufsize = 128;
    lex->label_size = 128;
    /* This must start at 0 since the line reader will bump it by one. */
    lex->line_num = 0;

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

    ch_class[(unsigned char)'_'] = CC_WORD;
    ch_class[(unsigned char)'('] = CC_LEFT_PARENTH;
    ch_class[(unsigned char)')'] = CC_RIGHT_PARENTH;
    ch_class[(unsigned char)'"'] = CC_DOUBLE_QUOTE;
    ch_class[(unsigned char)'@'] = CC_AT;
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
    ch_class[(unsigned char)'|'] = CC_VBAR;
    ch_class[(unsigned char)'['] = CC_LEFT_BRACKET;
    ch_class[(unsigned char)']'] = CC_RIGHT_BRACKET;
    /* Prep for file-based, since lex_buffer isn't NULL. */
    ch_class[(unsigned char)'\r'] = CC_NEWLINE;
    ch_class[(unsigned char)'\n'] = CC_NEWLINE;

    /* This is set so that token is never invalid, which allows parser to check
       the token before the first lily_lexer call. This is important, because
       lily_lexer_handle_page_data may return tk_eof if there is nothing to
       parse. */
    lex->token = tk_invalid;
    lex->ch_class = ch_class;
    return lex;
}

void lily_free_lex_state(lily_lex_state *lex)
{
    if (lex->lex_file != NULL)
        fclose(lex->lex_file);

    if (lex->save_buffer == NULL)
        lily_free(lex->lex_buffer);
    else
        lily_free(lex->save_buffer);

    lily_free(lex->ch_class);
    lily_free(lex->label);
    lily_free(lex);
}

/** Scanning methods and helpers **/

/* simple_escape
   This takes in the character after an escape, and returns what the escape
   character translates into. Returns 0 for invalid escapes. */
static char simple_escape(char ch)
{
    char ret;

    if (ch == 'n')
        ret = '\n';
    else if (ch == 'r')
        ret = '\r';
    else if (ch == 't')
        ret = '\t';
    else if (ch == '\'')
        ret = '\'';
    else if (ch == '"')
        ret = '"';
    else if (ch == '\\')
        ret = '\\';
    else if (ch == 'b')
        ret = '\b';
    else if (ch == 'a')
        ret = '\a';
    else
        ret = 0;

    return ret;
}

/** Numeric scanning **/
/* Most of the number scanning functions follow a pattern: Each defines the max
   number of digits it can take so that the unsigned result cannot overflow.
   scan_decimal takes an extra is_integer param because it may have an exponent
   which automatically meant it will yield a number result. */

/* scan_exponent
   This scans across the exponent of a decimal number to find where the end is.
   Since the result will definitely be a number, no value calculation is done
   (scan_number will handle it). */
static void scan_exponent(lily_lex_state *lexer, int *pos)
{
    char ch;
    char *lex_buffer = lexer->lex_buffer;
    int num_pos = *pos + 1;
    int num_digits = 0;
    ch = lex_buffer[num_pos];

    if (ch == '+' || ch == '-') {
        num_pos++;
        ch = lex_buffer[num_pos];
    }

    if (ch < '0' || ch > '9')
        lily_raise(lexer->raiser, lily_ErrSyntax,
                   "Expected a base 10 number after exponent.\n");

    while (ch >= '0' && ch <= '9') {
        num_digits++;
        if (num_digits > 3) {
            lily_raise(lexer->raiser, lily_ErrSyntax,
                       "Exponent is too large.\n");
        }
        num_pos++;
        ch = lex_buffer[num_pos];
    }

    *pos = num_pos;
}

/* scan_binary
   Helper for scan_number. This handles binary numbers. */
static uint64_t scan_binary(lily_lex_state *lexer, int *pos)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 65;
    int num_pos = *pos+1;
    char *lex_buffer = lexer->lex_buffer;
    char ch = lex_buffer[num_pos];

    while (ch == '0') {
        num_pos++;
        ch = lex_buffer[num_pos];
    }

    while ((ch == '0' || ch == '1') && num_digits != max_digits) {
        num_digits++;
        result = (result * 2) + ch - '0';
        num_pos++;
        ch = lex_buffer[num_pos];
    }

    *pos = num_pos;
    return result;
}

/* scan_octal
   Helper for scan_number. This handles octal numbers. */
static uint64_t scan_octal(lily_lex_state *lexer, int *pos)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 23;
    int num_pos = *pos+1;
    char *lex_buffer = lexer->lex_buffer;
    char ch = lex_buffer[num_pos];

    while (ch == '0') {
        num_pos++;
        ch = lex_buffer[num_pos];
    }

    while (ch >= '0' && ch <= '7' && num_digits != max_digits) {
        num_digits++;
        result = (result * 8) + ch - '0';
        num_pos++;
        ch = lex_buffer[num_pos];
    }

    *pos = num_pos;
    return result;
}

/* scan_decimal
   Helper for scan_number. This handles decimal numbers, including dots and
   exponent values. This takes in is_integer because a dot or exponent will
   make is_integer = 0. */
static uint64_t scan_decimal(lily_lex_state *lexer, int *pos, int *is_integer)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 21;
    /* This is the only scanner that doesn't start off on pos+1. This is because
       there's no formatting character to skip over. */
    int num_pos = *pos;
    int have_dot = 0;
    char *lex_buffer = lexer->lex_buffer;
    char ch = lex_buffer[num_pos];

    while (ch == '0') {
        num_pos++;
        ch = lex_buffer[num_pos];
    }

    while (num_digits != max_digits) {
        if (ch >= '0' && ch <= '9') {
            if (*is_integer) {
                num_digits++;
                result = (result * 10) + ch - '0';
            }
        }
        else if (ch == '.') {
            if (have_dot == 1)
                break;
            have_dot = 1;
            *is_integer = 0;
        }
        else if (ch == 'e') {
            *is_integer = 0;
            scan_exponent(lexer, &num_pos);
            break;
        }
        else
            break;

        num_digits++;
        num_pos++;
        ch = lex_buffer[num_pos];
    }

    *pos = num_pos;
    return result;
}

/* scan_hex
   Helper for scan_number. This handles hex numbers. */
static uint64_t scan_hex(lily_lex_state *lexer, int *pos)
{
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 17;
    int num_pos = *pos+1;
    char *lex_buffer = lexer->lex_buffer;
    char ch = lex_buffer[num_pos];

    while (ch == '0') {
        num_pos++;
        ch = lex_buffer[num_pos];
    }

    while (num_digits != max_digits) {
        char mod;
        if (ch >= '0' && ch <= '9')
            mod = '0';
        else if (ch >= 'a' && ch <= 'f')
            mod = 'a' - 10;
        else if (ch >= 'A' && ch <= 'F')
            mod = 'A' - 10;
        else
            break;

        result = (result * 16) + ch - mod;
        num_digits++;
        num_pos++;
        ch = lex_buffer[num_pos];
    }

    *pos = num_pos;
    return result;
}

/* scan_number
   This handles all integer and number scanning within Lily. Updates the
   position in lex_buffer for lily_lexer. This also takes a pointer to the
   lexer's token so it can update that (to either tk_number or tk_integer). */
static void scan_number(lily_lex_state *lexer, int *pos, lily_token *tok)
{
    char ch;
    char *lex_buffer;
    int is_negative, is_integer, num_start, num_pos;
    uint64_t integer_value;
    lily_value yield_val;

    num_pos = *pos;
    num_start = num_pos;
    lex_buffer = lexer->lex_buffer;
    ch = lex_buffer[num_pos];
    is_negative = 0;
    is_integer = 1;
    integer_value = 0;

    if (ch == '-') {
        is_negative = 1;
        num_pos++;
        ch = lex_buffer[num_pos];
    }
    else if (ch == '+') {
        num_pos++;
        ch = lex_buffer[num_pos];
    }

    if (ch == '0') {
        num_pos++;
        ch = lex_buffer[num_pos];

        if (ch == 'b')
            integer_value = scan_binary(lexer, &num_pos);
        else if (ch == 'c')
            integer_value = scan_octal(lexer, &num_pos);
        else if (ch == 'x')
            integer_value = scan_hex(lexer, &num_pos);
        else
            integer_value = scan_decimal(lexer, &num_pos, &is_integer);
    }
    else
        integer_value = scan_decimal(lexer, &num_pos, &is_integer);

    if (is_negative == 0) {
        if (integer_value <= INT64_MAX)
            yield_val.integer = (int64_t)integer_value;
        else
            lily_raise(lexer->raiser, lily_ErrSyntax,
                       "Integer value is too large.\n");
    }
    else {
        /* This is negative, and signed min is 1 higher than signed max. This is
           written as a literal so that gcc doesn't complain about overflow. */
        uint64_t max = 9223372036854775808ULL;
        if (integer_value <= max)
            yield_val.integer = -(int64_t)integer_value;
        else
            lily_raise(lexer->raiser, lily_ErrSyntax,
                       "Integer value is too large.\n");
    }

    /* Not an integer, so use strtod to try converting it to a double so it can
       be stored as a number. */
    if (is_integer == 0) {
        double number_result;
        int str_size = num_pos - num_start;
        strncpy(lexer->label, lex_buffer+num_start, str_size * sizeof(char));

        lexer->label[str_size] = '\0';
        errno = 0;
        number_result = strtod(lexer->label, NULL);
        if (errno == ERANGE) {
            lily_raise(lexer->raiser, lily_ErrSyntax,
                       "Number value is too large.\n");
        }

        yield_val.number = number_result;
        *tok = tk_number;
    }
    else
        *tok = tk_integer;

    *pos = num_pos;
    lexer->value = yield_val;
}

static int read_line(lily_lex_state *);

/* scan_str
   This handles strings for lily_lexer. This updates the position in lex_buffer
   for lily_lexer. */
static void scan_str(lily_lex_state *lexer, int *pos)
{
    char ch, esc_ch;
    char *label, *lex_buffer, *str;
    int i, is_multiline, label_pos, escape_this_line, multiline_start, str_size,
        word_start, word_pos;
    lily_str_val *sv;

    lex_buffer = lexer->lex_buffer;
    label = lexer->label;
    escape_this_line = 0;
    /* Where to start cutting from. */
    word_start = *pos + 1;
    /* Where to finish cutting from. */
    word_pos = word_start;

    ch = lex_buffer[word_pos];

    /* ch is actually the first char after the opening ". */
    if (ch == '"' && lex_buffer[word_pos+1] == '"') {
        is_multiline = 1;
        /* This will be used to print out the line number the str starts on, in
           case the str reaches EOF. */
        multiline_start = lexer->line_num;
        word_start += 2;
        word_pos += 2;
        ch = lex_buffer[word_pos];
    }
    else
        is_multiline = 0;

    label_pos = 0;
    label[0] = '\0';

    while (1) {
        if (ch == '\\') {
            /* For escapes, the non-escape part of the data is copied to the
               label, then the escape value is written in. */
            i = word_pos - word_start;
            memcpy(&label[label_pos], &lex_buffer[word_start], i * sizeof(char));
            label_pos += i;
            word_start += i;
            word_pos++;

            /* If there was an escape in the line, then the final part of the
               str after the escape needs to be cut (unless there is nothing
               after the escape). */
            escape_this_line = 1;

            ch = lex_buffer[word_pos];
            esc_ch = simple_escape(ch);
            if (esc_ch == 0)
                lily_raise(lexer->raiser, lily_ErrSyntax,
                           "Invalid escape \\%c\n", ch);

            label[label_pos] = esc_ch;
            label_pos++;

            /* Add two so it starts off after the escape char the next time. */
            word_start += 2;
            ch = lex_buffer[word_start];
        }
        else if (ch == '\n' || ch == '\r') {
            if (is_multiline) {
                if (ch == '\r' &&
                    lexer->lex_bufend != word_pos &&
                    lex_buffer[word_pos+1] == '\n')
                        /* Swallow the \r of the \r\n. */
                        word_pos++;

                /* Swallow the \n or \r. */
                word_pos++;
                i = word_pos - word_start;
                memcpy(&label[label_pos], &lex_buffer[word_start],
                       (i * sizeof(char)));
                label_pos += i;

                if (read_line(lexer) == 0) {
                    lily_raise(lexer->raiser, lily_ErrSyntax,
                               "Unterminated multi-line string (started at line %d).\n",
                               multiline_start);
                }
                /* read_line may realloc lex_buffer, so it must be set again. */
                lex_buffer = lexer->lex_buffer;
                if ((label_pos + lexer->lex_bufend) >= lexer->label_size) {
                    /* lex_bufend is at most == label_pos, so a *2 grow will
                       always solve this. */
                    int new_label_size = lexer->label_size * 2;
                    char *new_label;
                    new_label = lily_realloc(lexer->label,
                            (new_label_size * sizeof(char)));

                    if (new_label == NULL)
                        lily_raise_nomem(lexer->raiser);

                    lexer->label = new_label;
                    lexer->label_size = new_label_size;
                }

                label = lexer->label;
                /* Set the buffer again, because read_line may realloc it and
                   make it be at a different spot. */

                /* This next line doesn't have an escape, so fix that. */
                escape_this_line = 0;
                /* Must manually set this + continue, or else the 0th letter
                   will get skipped. */
                word_start = 0;
                word_pos = 0;
                ch = lex_buffer[word_pos];
                continue;
            }
            else
                lily_raise(lexer->raiser, lily_ErrSyntax,
                           "Unterminated string at line %d.\n",
                           lexer->line_num);
        }
        else if (ch == '"') {
            if (is_multiline == 0)
                break;
            else if (ch == '"' &&
                     lex_buffer[word_pos+1] == '"' &&
                     lex_buffer[word_pos+2] == '"')
                break;
        }

        word_pos++;
        ch = lex_buffer[word_pos];
    }

    if (!escape_this_line) {
        /* No escape chars seen, so copy the full string over. */
        if (is_multiline) {
            if (word_pos != word_start) {
                i = word_pos - word_start;
                memcpy(&label[label_pos], &lex_buffer[word_start], i * sizeof(char));
                label_pos += i;
            }
            /* +1 for terminator. */
            str_size = label_pos + 1;

            word_pos += 2;
        }
        else
            str_size = word_pos + 1 - word_start;
    }
    else {
        /* Had escapes, so copy over the last section */
        if (word_pos != word_start) {
            i = word_pos - word_start;
            for (;i > 0;i--,  word_start++, label_pos++)
                label[label_pos] = lex_buffer[word_start];
        }
        /* +1 for the terminator. */
        str_size = label_pos + 1;
        if (is_multiline)
            word_pos += 2;
    }

    /* Prepare the string for the parser. */
    sv = lily_malloc(sizeof(lily_str_val));
    if (sv == NULL)
        lily_raise_nomem(lexer->raiser);
    str = lily_malloc(str_size);
    if (str == NULL) {
        lily_free(sv);
        lily_raise_nomem(lexer->raiser);
    }

    if (!escape_this_line && is_multiline == 0)
        strncpy(str, lex_buffer+word_start, str_size-1);
    else
        strncpy(str, label, label_pos);

    str[str_size-1] = '\0';
    sv->refcount = 1;
    sv->str = str;
    sv->size = word_pos;
    lexer->value.str = sv;
    word_pos++;
    *pos = word_pos;
}

/* grow_lexer_buffers
   This is used by read_line to resize the scanning buffer (lexer->lex_buffer).
   It will resize lexer->label too if both buffers are the same size. */
static void grow_lexer_buffers(lily_lex_state *lexer)
{
    int new_size = lexer->lex_bufsize;
    new_size *= 2;

    /* The label buffer is grown when a large multi-line string is
       collected. Don't resize it if it's bigger than the line buffer.
       It's important that the label buffer be at least the size of the
       line buffer at all times, so that lexer's word scanning does not
       have to check for potential overflows. */
    if (lexer->label_size == lexer->lex_bufsize) {
        char *new_label;
        new_label = lily_realloc(lexer->label, new_size * sizeof(char));

        if (new_label == NULL)
            lily_raise_nomem(lexer->raiser);

        lexer->label = new_label;
        lexer->label_size = new_size;
    }

    char *new_lb;
    new_lb = lily_realloc(lexer->lex_buffer, new_size * sizeof(char));

    if (new_lb == NULL)
        lily_raise_nomem(lexer->raiser);

    lexer->lex_buffer = new_lb;
    lexer->lex_bufsize = new_size;
}

/* read_line
   Add a line from the current page into the buffer. */
static int read_line(lily_lex_state *lexer)
{
    int bufsize, ch, followers, i, ok;
    char *lex_buffer = lexer->lex_buffer;
    FILE *lex_file = lexer->lex_file;

    bufsize = lexer->lex_bufsize;
    i = 0;

    while (1) {
        ch = fgetc(lex_file);
        if (ch == EOF) {
            if ((i + 1) == bufsize) {
                grow_lexer_buffers(lexer);

                lex_buffer = lexer->lex_buffer;
            }

            lexer->lex_buffer[i] = '\n';
            lexer->lex_bufend = i + 1;
            /* If i is 0, then nothing is on this line except eof. Return 0 to
               let the caller know this is the end.
             * If it isn't, then there's stuff with an unexpected eof at the
               end. Return 1, then 0 the next time. */
            ok = !!i;
            /* Make sure the second eof found does not increment the line
               number again. */
            if (lexer->hit_eof == 0) {
                lexer->line_num++;
                lexer->hit_eof = 1;
            }
            break;
        }

        /* i + 2 is used so that when \r\n that the \n can be safely added
           to the buffer. Otherwise, it would be i + 1. */
        if ((i + 2) == bufsize) {
            grow_lexer_buffers(lexer);
            /* Do this in case the realloc decides to use a different block
               instead of growing what it had. */
            lex_buffer = lexer->lex_buffer;
        }

        lexer->lex_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            lexer->lex_bufend = i;
            lexer->line_num++;
            ok = 1;

            if (ch == '\r') {
                ch = fgetc(lex_file);
                if (ch != '\n')
                    ungetc(ch, lex_file);
                else {
                    /* This is safe: See i + 2 == size comment above. */
                    lexer->lex_buffer[i+1] = ch;
                    lexer->lex_bufend++;
                }
            }
            break;
        }
        else if (ch > 127) {
            followers = follower_table[(unsigned int)ch];
            if (followers >= 2) {
                int j;
                i++;
                for (j = 1;j < followers;j++,i++) {
                    ch = fgetc(lex_file);
                    if ((unsigned char)ch < 128 || ch == EOF) {
                        lily_raise(lexer->raiser, lily_ErrEncoding,
                                   "Invalid utf-8 sequence on line %d.\n",
                                   lexer->line_num);
                    }
                    lex_buffer[i] = ch;
                }
            }
            else if (followers == -1) {
                lily_raise(lexer->raiser, lily_ErrEncoding,
                           "Invalid utf-8 sequence on line %d.\n",
                           lexer->line_num);
            }
        }
        else
            i++;
    }
    return ok;
}

/* is_valid_utf8
   This determines if the str (to be used for string-based scanning) is valid
   utf-8. */
static int is_valid_utf8(char *str)
{
    char ch;
    int i, ret;

    i = 0;
    ret = 1;
    ch = str[i];

    while (ch) {
        if (ch > 127) {
            int followers = follower_table[(unsigned char)ch];
            if (followers >= 2) {
                int j;
                i++;
                for (j = 1;j < followers;j++,i++) {
                    ch = str[i];
                    if ((unsigned char)ch < 128) {
                        ret = 0;
                        break;
                    }
                    str[i] = ch;
                }
            }
            else if (followers == -1) {
                ret = 0;
                break;
            }
        }
        else
            i++;

        ch = str[i];
    }

    return ret;
}

/** Lexer API **/

/* lily_load_file
   This function tells the given lexer to load the given filename. This will
   switch the lexer's mode to being file-based.
   Note: The lexer will not free the given filename. */
void lily_load_file(lily_lex_state *lexer, char *filename)
{
    if (lexer->mode != lm_from_file) {
        /* Change from string-based to file-based. */
        lexer->mode = lm_from_file;
        lexer->lex_buffer = lexer->save_buffer;
        lexer->save_buffer = NULL;
        lexer->ch_class[(unsigned char)'\r'] = CC_NEWLINE;
        lexer->ch_class[(unsigned char)'\n'] = CC_NEWLINE;
        lexer->ch_class[0] = CC_INVALID;
        lexer->line_num = 0;
    }

    FILE *lex_file = fopen(filename, "r");
    if (lex_file == NULL) {
        lexer->filename = NULL;
        lily_raise(lexer->raiser, lily_ErrImport, "Failed to open %s.\n",
                   filename);
    }

    lexer->filename = filename;
    lexer->lex_file = lex_file;
    lexer->hit_eof = 0;

    read_line(lexer);
    /* Make sure the lexer starts after the <@lily block. */
    lily_lexer_handle_page_data(lexer);
}

/* lily_load_str 
   This function tells the given lexer to load a string for scanning. If str is
   not valid utf-8, then the lexer will not load it and return 0. Otherwise, it
   returns 1. */
int lily_load_str(lily_lex_state *lexer, char *str)
{
    if (!is_valid_utf8(str))
        return 0;

    if (lexer->mode != lm_from_str) {
        lexer->mode = lm_from_str;
        lexer->save_buffer = lexer->lex_buffer;
        lexer->ch_class[(unsigned char)'\r'] = CC_STR_NEWLINE;
        lexer->ch_class[(unsigned char)'\n'] = CC_STR_NEWLINE;
        lexer->ch_class[0] = CC_STR_END;
        lexer->lex_file = NULL;
        lexer->lex_bufsize = strlen(str);
        lexer->line_num = 1;
    }

    /* Line number isn't set, to allow for repl-like use. */
    lexer->lex_buffer = str;

    lexer->filename = "<str>";
    /* String-based has no support for the html skipping, because I can't see
       that being useful. */

    return 1;
}

/* lily_lexer 
   This is the main scanning function. It sometimes farms work out to other
   functions in the case of strings and numeric values. */
void lily_lexer(lily_lex_state *lexer)
{
    char *ch_class;
    int lex_bufpos = lexer->lex_bufpos;
    lily_token token;

    ch_class = lexer->ch_class;

    while (1) {
        char ch;
        int group;

        ch = lexer->lex_buffer[lex_bufpos];
        while (ch == ' ' || ch == '\t') {
            lex_bufpos++;
            ch = lexer->lex_buffer[lex_bufpos];
        }

        group = ch_class[(unsigned char)ch];
        if (group == CC_WORD) {
            /* The word and line buffers have the same size, plus \n is not a
               valid word character. So, there's no point in checking for
               overflow. */
            int word_pos = 0;
            char *label = lexer->label;
            do {
                label[word_pos] = ch;
                word_pos++;
                lex_bufpos++;
                ch = lexer->lex_buffer[lex_bufpos];
            } while (ident_table[(unsigned char)ch]);
            label[word_pos] = '\0';
            token = tk_word;
        }
        else if (group <= CC_G_ONE_LAST) {
            lex_bufpos++;
            /* This is okay because the first group starts at 0, and the tokens
               for it start at 0. */
            token = group;
        }
        else if (group == CC_NEWLINE) {
            if (read_line(lexer) == 1) {
                lex_bufpos = 0;
                continue;
            }
            else
                token = tk_eof;
        }
        else if (group == CC_SHARP) {
            ch = lexer->lex_buffer[lex_bufpos];
            if (read_line(lexer) == 1) {
                lex_bufpos = 0;
                continue;
            }
            else
                token = tk_eof;
        }
        else if (group == CC_STR_NEWLINE) {
            /* This catches both \r and \n. Make sure that \r\n comes in as one
               newline though. */
            if (ch == '\r' && lexer->lex_buffer[lex_bufpos+1] == '\n')
                lex_bufpos += 2;
            else
                lex_bufpos++;

            lexer->line_num++;
            continue;
        }
        else if (group == CC_STR_END) {
            /* todo: This should probably yield to the repl implementation and
               possibly collect more data. */
            token = tk_eof;
        }
        else if (group == CC_DOUBLE_QUOTE) {
            scan_str(lexer, &lex_bufpos);
            token = tk_double_quote;
        }
        else if (group <= CC_G_TWO_LAST) {
            lex_bufpos++;
            if (lexer->lex_buffer[lex_bufpos] == '=') {
                lex_bufpos++;
                token = grp_two_eq_table[group - CC_G_TWO_OFFSET];
            }
            else
                token = grp_two_table[group - CC_G_TWO_OFFSET];
        }
        else if (group == CC_NUMBER)
            scan_number(lexer, &lex_bufpos, &token);
        else if (group == CC_DOT) {
            ch = lexer->lex_buffer[lex_bufpos+1];
            if (ch_class[(unsigned char)ch] == CC_NUMBER)
                scan_number(lexer, &lex_bufpos, &token);
            else {
                lex_bufpos++;
                if (lexer->lex_buffer[lex_bufpos] == '.' &&
                    lexer->lex_buffer[lex_bufpos + 1] == '.') {
                    lex_bufpos += 2;
                    token = tk_three_dots;
                }
                else
                    token = tk_dot;
            }
        }
        else if (group == CC_PLUS) {
            ch = lexer->lex_buffer[lex_bufpos+1];
            if (ch_class[(unsigned char)ch] == CC_NUMBER)
                scan_number(lexer, &lex_bufpos, &token);
            else if (ch == '=') {
                lex_bufpos += 2;
                token = tk_plus_eq;
            }
            else {
                lex_bufpos++;
                token = tk_plus;
            }
        }
        else if (group == CC_MINUS) {
            ch = lexer->lex_buffer[lex_bufpos+1];
            if (ch_class[(unsigned char)ch] == CC_NUMBER)
                scan_number(lexer, &lex_bufpos, &token);
            else if (ch == '=') {
                lex_bufpos += 2;
                token = tk_minus_eq;
            }
            else {
                lex_bufpos++;
                token = tk_minus;
            }
        }
        else if (group == CC_AMPERSAND) {
            lex_bufpos++;
            if (lexer->lex_buffer[lex_bufpos] == '&') {
                lex_bufpos++;
                token = tk_logical_and;
            }
            else
                token = tk_bitwise_and;
        }
        else if (group == CC_VBAR) {
            lex_bufpos++;
            if (lexer->lex_buffer[lex_bufpos] == '|') {
                lex_bufpos++;
                token = tk_logical_or;
            }
            else
                token = tk_bitwise_or;
        }
        else if (group == CC_AT) {
            lex_bufpos++;
            /* Disable @> for string-based. */
            if (lexer->lex_buffer[lex_bufpos] == '>' &&
                lexer->save_buffer == NULL) {
                /* Skip the > of @> so it's not sent as html. */
                lex_bufpos++;
                token = tk_end_tag;
            }
            else if (lexer->lex_buffer[lex_bufpos] == '(') {
                lex_bufpos++;
                token = tk_typecast_parenth;
            }
            else
                lily_raise(lexer->raiser, lily_ErrSyntax,
                           "Expected '>' after '@'.\n");
        }
        else
            token = tk_invalid;

        lexer->lex_bufpos = lex_bufpos;
        lexer->token = token;
        return;
    }
}

/* lily_lexer_handle_page_data
   This scans the html outside of the <@lily and @> tags, sending it off to
   lily_impl_send_html (which differs depending on the runner). This is only
   called when handling files (lily_lexer won't send the @> end_tag token for
   string-based scanning).*/
void lily_lexer_handle_page_data(lily_lex_state *lexer)
{
    char c;
    int lbp, htmlp;

    /* htmlp and lbp are used so it's obvious they aren't globals. */
    lbp = lexer->lex_bufpos;
    c = lexer->lex_buffer[lbp];
    htmlp = 0;

    /* Send html to the server either when unable to hold more or the lily tag
       is found. */
    while (1) {
        lbp++;
        if (c == '<') {
            if ((lbp + 4) <= lexer->lex_bufend &&
                strncmp(lexer->lex_buffer + lbp, "@lily", 5) == 0) {
                if (htmlp != 0) {
                    /* Don't include the '<', because it goes with <@lily. */
                    lexer->label[htmlp] = '\0';
                    lily_impl_send_html(lexer->label);
                }
                lbp += 5;
                /* Yield control to the lexer. */
                break;
            }
        }
        lexer->label[htmlp] = c;
        htmlp++;
        if (htmlp == (lexer->lex_bufsize - 1)) {
            lexer->label[htmlp] = '\0';
            lily_impl_send_html(lexer->label);
            /* This isn't done, so fix htmlp. */
            htmlp = 0;
        }

        if (c == '\n' || c == '\r') {
            if (read_line(lexer))
                lbp = 0;
            else {
                if (htmlp != 0) {
                    lexer->label[htmlp] = '\0';
                    lily_impl_send_html(lexer->label);
                }
                lexer->token = tk_eof;
                break;
            }
        }

        c = lexer->lex_buffer[lbp];
    }

    lexer->lex_bufpos = lbp;
}

/* tokname
   This function returns a printable name for a given token. This is mostly for
   parser to be able to print the token when it has an unexpected token. */
char *tokname(lily_token t)
{
    static char *toknames[] =
    {"(", ")", ",", "{", "}", ":", "[", "]", "=", "==", "<", "<=", ">", ">=",
     "!", "!=", "*", "*=", "/", "/=", "+", "+=", "-", "-=", "a label",
     "a string", "an integer", "a number", ".", "&", "&&", "|", "||", "@(",
     "...", "invalid token", "@>", "end of file"};

    if (t < (sizeof(toknames) / sizeof(toknames[0])))
        return toknames[t];

    return NULL;
}

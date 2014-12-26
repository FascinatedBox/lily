#include <ctype.h>
#include <string.h>
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
      HTML outside of the lily tag. The lily tag starts with <?lily and ends
      with ?>. Parser is responsible for telling lexer to do the skip when it
      encounters the end tag (?>)
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
/* CC_LEFT_CURLY isn't here because {| opens a lambda. */
#define CC_RIGHT_CURLY   3
#define CC_LEFT_BRACKET  4
#define CC_CARET         5
#define CC_G_ONE_LAST    5

/* Group 2: Return self, or self= */
#define CC_G_TWO_OFFSET  6
#define CC_NOT           6
#define CC_PERCENT       7
#define CC_MULTIPLY      8
#define CC_DIVIDE        9
#define CC_G_TWO_LAST    9

/* Greater and Less are able to do shifts, self=, and self. < can become <[,
   but the reverse of that is ]>, so these two aren't exactly the same. So
   there's no group for them. */
#define CC_GREATER       10
#define CC_LESS          11
#define CC_PLUS          12
#define CC_MINUS         13
#define CC_WORD          14
#define CC_DOUBLE_QUOTE  15
#define CC_NUMBER        16
#define CC_COLON         17
#define CC_LEFT_CURLY    18
#define CC_RIGHT_BRACKET 19

#define CC_EQUAL         20
#define CC_NEWLINE       21
#define CC_SHARP         22
#define CC_DOT           23
#define CC_AT            24
#define CC_AMPERSAND     25
#define CC_VBAR          26
#define CC_QUESTION      27
#define CC_INVALID       28

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
    tk_not, tk_modulo, tk_multiply, tk_divide
};

static const lily_token grp_two_eq_table[] =
{
    tk_not_eq, tk_modulo_eq, tk_multiply_eq, tk_divide_eq,
};

/** Lexer init and deletion **/
lily_lex_state *lily_new_lex_state(lily_raiser *raiser, void *data)
{
    lily_lex_state *lex = lily_malloc(sizeof(lily_lex_state));
    char *ch_class;

    if (lex == NULL)
        return NULL;

    lex->last_digit_start = 0;
    lex->entry = NULL;
    lex->filename = NULL;
    lex->raiser = raiser;
    lex->data = data;
    lex->input_buffer = lily_malloc(128 * sizeof(char));
    lex->label = lily_malloc(128 * sizeof(char));
    lex->ch_class = NULL;
    lex->last_literal = NULL;
    /* Allocate space for making lambdas only if absolutely needed. */
    lex->lambda_data = NULL;
    lex->lambda_data_size = 0;
    ch_class = lily_malloc(256 * sizeof(char));

    if (ch_class == NULL || lex->label == NULL || lex->input_buffer == NULL) {
        lily_free_lex_state(lex);
        return NULL;
    }

    lex->input_pos = 0;
    lex->input_size = 128;
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
    ch_class[(unsigned char)'['] = CC_LEFT_BRACKET;
    ch_class[(unsigned char)']'] = CC_RIGHT_BRACKET;
    ch_class[(unsigned char)'\n'] = CC_NEWLINE;

    /* This is set so that token is never invalid, which allows parser to check
       the token before the first lily_lexer call. This is important, because
       lily_lexer_handle_page_data may return tk_final_eof if there is nothing
       to parse. */
    lex->token = tk_invalid;
    lex->ch_class = ch_class;
    return lex;
}

void lily_free_lex_state(lily_lex_state *lex)
{
    if (lex->entry) {
        lily_lex_entry *entry_iter = lex->entry;
        while (entry_iter->prev)
            entry_iter = entry_iter->prev;

        lily_lex_entry *entry_next;
        while (entry_iter) {
            if (entry_iter->source != NULL)
                lex->entry->close_fn(lex->entry);

            entry_next = entry_iter->next;
            lily_free(entry_iter->saved_input);
            lily_free(entry_iter);
            entry_iter = entry_next;
        }
    }

    lily_free(lex->lambda_data);
    lily_free(lex->input_buffer);
    lily_free(lex->ch_class);
    lily_free(lex->label);
    lily_free(lex);
}

/*  get_entry
    Obtain a lily_lex_entry to be used for entering a string, file, or some
    other source. The lexer retains a linked list of these, and attempts to
    reuse them when it can. */
static lily_lex_entry *get_entry(lily_lex_state *lex)
{
    lily_lex_entry *ret_entry = NULL;

    if (lex->entry == NULL ||
        (lex->entry->source != NULL && lex->entry->next == NULL)) {
        ret_entry = lily_malloc(sizeof(lily_lex_entry));
        if (ret_entry == NULL)
            lily_raise_nomem(lex->raiser);

        if (lex->entry == NULL) {
            lex->entry = ret_entry;
            ret_entry->prev = NULL;
        }
        else {
            lex->entry->next = ret_entry;
            ret_entry->prev = lex->entry;
        }

        ret_entry->source = NULL;
        ret_entry->saved_input = NULL;
        ret_entry->saved_input_pos = 0;
        ret_entry->saved_input_size = 0;
        ret_entry->saved_input_end = 0;

        ret_entry->next = NULL;
        ret_entry->lexer = lex;
    }
    else {
        if (lex->entry->source == NULL)
            ret_entry = lex->entry;
        else
            ret_entry = lex->entry->next;
    }

    if (ret_entry->prev) {
        lily_lex_entry *prev_entry = ret_entry->prev;
        char *new_input;
        /* size + 1 isn't needed here because the input buffer's size includes
           space for the \0. */
        if (prev_entry->saved_input == NULL)
            new_input = lily_malloc(lex->input_size);
        else if (prev_entry->saved_input_size < lex->input_size)
            new_input = lily_realloc(prev_entry->saved_input, lex->input_size);
        else
            new_input = prev_entry->saved_input;

        if (new_input == NULL)
            lily_raise_nomem(lex->raiser);

        strcpy(new_input, lex->input_buffer);
        prev_entry->saved_input = new_input;
        prev_entry->saved_line_num = lex->line_num;
        prev_entry->saved_input_pos = lex->input_pos;
        prev_entry->saved_input_size = lex->input_size;
        prev_entry->saved_input_end = lex->input_end;
        prev_entry->filename = lex->filename;
        prev_entry->saved_token = lex->token;
        prev_entry->saved_last_literal = lex->last_literal;

        lex->line_num = 0;
    }

    lex->input_pos = 0;
    lex->entry = ret_entry;

    return ret_entry;
}

/*  leave_entry
    This is called when the end of an entry is reached. The current entry is
    left, and the previously-entered one is set up in place of it.

    Note: Callers of this function should make sure that lex->input_pos is not
    set to a bad value after this is called (ex: from a loop). */
static lily_token leave_entry(lily_lex_state *lex)
{
    lily_lex_entry *entry = lex->entry;
    lily_token token;

    if (entry->prev) {
        /* Teardown the old entry and make it NULL so that the lexer knows it's
           not in use when it goes for another entry. */
        entry->close_fn(entry);
        entry->source = NULL;

        entry = entry->prev;

        strcpy(lex->input_buffer, entry->saved_input);

        lex->line_num = entry->saved_line_num;
        lex->input_pos = entry->saved_input_pos;
        /* The lexer's input buffer may have been resized by the entered file.
           Do NOT restore lex->input_size here. Ever. */
        lex->input_end = entry->saved_input_end;
        lex->filename = entry->filename;
        lex->entry = entry;
        lex->last_literal = entry->saved_last_literal;

        token = entry->saved_token;
        /* lex->label has almost certainly been overwritten with something
           else. Restore it by rolling back and calling for a rescan. */
        if (token == tk_word) {
            int pos = lex->input_pos - 1;
            char ch = lex->input_buffer[pos];
            while (ident_table[(unsigned int)ch] && pos != 0) {
                pos--;
                ch = lex->input_buffer[pos];
            }
            /* The rewinding stops on a non-identifier position if it isn't
               zero. Move it forward one, or the lexer will yield the wrong
               token. */
            if (pos != 0)
                pos++;

            lex->input_pos = pos;
            lily_lexer(lex);
        }
    }
    else {
        /* Don't pop the first entry: Just contiually yield eof for as long as
           the parser needs it. */
        token = tk_final_eof;
    }

    return token;
}

/** file and str reading functions **/

/*  file_read_line_fn
    This is the function that scans in a line to input_buffer when the source
    is a raw C FILE *. */
static int file_read_line_fn(lily_lex_entry *entry)
{
    int bufsize, ch, i, ok;
    lily_lex_state *lexer = entry->lexer;
    char *input_buffer = lexer->input_buffer;
    FILE *input_file = (FILE *)entry->source;

    bufsize = lexer->input_size;
    i = 0;
    int utf8_check = 0;

    while (1) {
        ch = fgetc(input_file);

        if ((i + 2) == bufsize) {
            lily_grow_lexer_buffers(lexer);
            /* Do this in case the realloc decides to use a different block
               instead of growing what it had. */
            input_buffer = lexer->input_buffer;
        }

        if (ch == EOF) {
            lexer->input_buffer[i] = '\n';
            lexer->input_buffer[i + 1] = '\0';
            lexer->input_end = i + 1;
            /* If i is 0, then nothing is on this line except eof. Return 0 to
               let the caller know this is the end. Don't bump the line number
               because it's already been counted.

             * If it isn't 0, then the last line ended with an eof instead of a
               newline. Return 1 to let the caller know that there's more. The
               line number is also bumped (since a line has been scanned in). */
            ok = !!i;
            lexer->line_num += ok;
            break;
        }


        input_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            lexer->input_end = i;
            lexer->line_num++;
            ok = 1;

            if (ch == '\r') {
                input_buffer[i] = '\n';
                ch = fgetc(input_file);
                if (ch != '\n')
                    ungetc(ch, input_file);
            }

            input_buffer[i + 1] = '\0';
            break;
        }
        else if ((unsigned char)ch > 127)
            utf8_check = 1;

        i++;
    }

    if (utf8_check)
        lily_lexer_utf8_check(lexer);

    return ok;
}

/*  str_read_line_fn
    This is the function that scans text into input_buffer when the source is
    a string. The address of the string to start at is at entry->source. It's
    much the same as file_read_line_fn, except that *ch and ch++ are
    used instead of C's file IO. */
static int str_read_line_fn(lily_lex_entry *entry)
{
    int bufsize, i, ok, utf8_check;
    lily_lex_state *lexer = entry->lexer;
    char *input_buffer = lexer->input_buffer;
    char *ch = (char *)entry->source;

    bufsize = lexer->input_size;
    i = 0;
    utf8_check = 0;

    while (1) {
        if ((i + 2) == bufsize) {
            lily_grow_lexer_buffers(lexer);
            /* Do this in case the realloc decides to use a different block
               instead of growing what it had. */
            input_buffer = lexer->input_buffer;
        }

        if (*ch == '\0') {
            input_buffer[i] = '\n';
            lexer->input_buffer[i] = '\n';
            lexer->input_buffer[i + 1] = '\0';
            lexer->input_end = i + 1;
            /* If i is 0, then nothing is on this line except eof. Return 0 to
               let the caller know this is the end. Don't bump the line number
               because it's already been counted.

             * If it isn't 0, then the last line ended with an eof instead of a
               newline. Return 1 to let the caller know that there's more. The
               line number is also bumped (since a line has been scanned in). */
            ok = !!i;
            lexer->line_num += ok;
            break;
        }

        input_buffer[i] = *ch;

        if (*ch == '\r' || *ch == '\n') {
            lexer->input_end = i;
            lexer->line_num++;
            ok = 1;

            if (*ch == '\r') {
                input_buffer[i] = '\n';
                ch++;
                if (*ch == '\n') {
                    lexer->input_end++;
                    ch++;
                }
            }
            else
                ch++;

            input_buffer[i + 1] = '\0';
            break;
        }
        else if (((unsigned char)*ch) > 127)
            utf8_check = 1;

        i++;
        ch++;
    }

    if (utf8_check)
        lily_lexer_utf8_check(lexer);

    entry->source = ch;
    return ok;
}

static void file_close_fn(lily_lex_entry *entry)
{
    fclose((FILE *)entry->source);
}

static void str_close_fn(lily_lex_entry *entry)
{
    /* The string is assumed to be non-malloced (coming from argv), so there's
       nothing to do here. */
}

/** Scanning functions and helpers **/

/* simple_escape
   This takes in the character after an escape, and returns what the escape
   character translates into. Returns 0 for invalid escapes. */
static char simple_escape(char *ch)
{
    char ret;
    ch++;

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
    else
        ret = 0;

    return ret;
}

/** Numeric scanning **/
/* Most of the numeric scanning functions follow a pattern: Each defines the max
   number of digits it can take so that the unsigned result cannot overflow.
   scan_decimal takes an extra is_integer param because it may have an exponent
   which automatically meant it will yield a double result. */

/* scan_exponent
   This scans across the exponent of a decimal number to find where the end is.
   Since the result will definitely be a double, no value calculation is done
   (scan_number will handle it). */
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
        lily_raise(lexer->raiser, lily_SyntaxError,
                   "Expected a base 10 number after exponent.\n");

    while (*new_ch >= '0' && *new_ch <= '9') {
        num_digits++;
        if (num_digits > 3) {
            lily_raise(lexer->raiser, lily_SyntaxError,
                       "Exponent is too large.\n");
        }
        num_pos++;
        new_ch++;
    }

    *pos = num_pos;
}

/* scan_binary
   Helper for scan_number. This handles binary numbers. */
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

/* scan_octal
   Helper for scan_number. This handles octal numbers. */
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

/* scan_decimal
   Helper for scan_number. This handles decimal numbers, including dots and
   exponent values. This takes in is_integer because a dot or exponent will
   make is_integer = 0. */
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
            if (have_dot == 1 ||
                isdigit(*(new_ch + 1)) == 0)
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

/* scan_hex
   Helper for scan_number. This handles hex numbers. */
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

/* scan_number
   This handles all integer and double scanning within Lily. Updates the
   position in lexer's input_buffer for lily_lexer. This also takes a pointer to
   the lexer's token so it can update that (to either tk_double or
   tk_integer). */
static void scan_number(lily_lex_state *lexer, int *pos, lily_token *tok,
        char *new_ch)
{
    int is_negative, is_integer, num_start, num_pos;
    uint64_t integer_value;
    lily_raw_value yield_val;

    num_pos = *pos;
    num_start = num_pos;
    lexer->last_digit_start = num_pos;
    is_negative = 0;
    is_integer = 1;
    integer_value = 0;

    if (*new_ch == '-') {
        is_negative = 1;
        num_pos++;
        new_ch++;
    }
    else if (*new_ch == '+') {
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

    if (is_integer) {
        /* This won't be used uninitialized. I promise. */
        yield_val.doubleval = 0.0;
        if (is_negative == 0) {
            if (integer_value <= INT64_MAX)
                yield_val.integer = (int64_t)integer_value;
            else
                lily_raise(lexer->raiser, lily_SyntaxError,
                           "Integer value is too large.\n");
        }
        else {
            /* This is negative, and signed min is 1 higher than signed max. This is
               written as a literal so that gcc doesn't complain about overflow. */
            uint64_t max = 9223372036854775808ULL;
            if (integer_value <= max)
                yield_val.integer = -(int64_t)integer_value;
            else
                lily_raise(lexer->raiser, lily_SyntaxError,
                           "Integer value is too large.\n");
        }

        lexer->last_literal = lily_get_integer_literal(lexer->symtab,
                yield_val.integer);
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
        if (errno == ERANGE) {
            lily_raise(lexer->raiser, lily_SyntaxError,
                       "Double value is too large.\n");
        }

        yield_val.doubleval = double_result;
        lexer->last_literal = lily_get_double_literal(lexer->symtab,
                yield_val.doubleval);
        *tok = tk_double;
    }

    *pos = num_pos;
}

/* scan_multiline_comment
   This handles a comment that begins with ### and ends with ###. This is called
   after the opening ### has already been received. Position is updated to after
   the # of the ending ###. */
static void scan_multiline_comment(lily_lex_state *lexer, int *pos)
{
    int comment_pos, start_line;

    /* +3 to skip the ### intro. */
    char *new_ch = &(lexer->input_buffer[*pos + 3]);

    comment_pos = *pos + 3;
    comment_pos++;
    start_line = lexer->line_num;

    while (1) {
        if (*new_ch == '#') {
            if (comment_pos + 2 <= lexer->input_end &&
                *(new_ch + 1) == '#' &&
                *(new_ch + 2) == '#') {
                comment_pos += 2;
                break;
            }
        }
        else if (*new_ch == '\n') {
            if (lexer->entry->read_line_fn(lexer->entry) == 1) {
                new_ch = &(lexer->input_buffer[0]);
                comment_pos = 0;
                /* Must continue, in case the first char is the # of ###.\n */
                continue;
            }
            else {
                lily_raise(lexer->raiser, lily_SyntaxError,
                           "Unterminated multi-line comment (started at line %d).\n",
                           start_line);
            }
        }

        comment_pos++;
        new_ch++;
    }

    *pos = comment_pos;
}

static void ensure_lambda_data_size(lily_lex_state *lexer, int at_least)
{
    int new_size = lexer->lambda_data_size;
    while (new_size < at_least)
        new_size *= 2;

    char *new_data = lily_realloc(lexer->lambda_data, new_size);
    if (new_data == NULL)
        lily_raise_nomem(lexer->raiser);

    lexer->lambda_data = new_data;
    lexer->lambda_data_size = new_size;
}

/*  scan_lambda
    This is called when {| is found. The lexer is responsible for scooping up
    text until the end of the lambda. This function should also ensure that a
    proper number of braces are found. */
static void scan_lambda(lily_lex_state *lexer, int *pos)
{
    char *input = lexer->input_buffer;
    lexer->lambda_start_line = lexer->line_num;

    if (lexer->lambda_data == NULL) {
        lexer->lambda_data = lily_malloc(64);
        if (lexer->lambda_data == NULL)
            lily_raise_nomem(lexer->raiser);

        lexer->lambda_data_size = 64;
    }

    char *lambda_data = lexer->lambda_data;
    char *ch = &input[*pos];
    int i = 0, max = lexer->lambda_data_size - 2;
    int brace_depth = 1, input_pos = *pos;
    lily_lex_entry *entry = lexer->entry;

    /* todo: Proper lambda scanning. */
    while (1) {
        if (i == max) {
            ensure_lambda_data_size(lexer, lexer->lambda_data_size * 2);

            lambda_data = lexer->lambda_data;
            max = lexer->lambda_data_size - 2;
        }

        if (*ch == '\n' ||
            (*ch == '#' &&
             *(ch + 1) != '#' &&
             *(ch + 2) != '#')) {
            if (entry->read_line_fn(entry) == 0)
                lily_raise(lexer->raiser, lily_SyntaxError,
                        "Unterminated lambda (started at line %d).\n",
                        lexer->lambda_start_line);

            input_pos = 0;
            lambda_data[i] = '\n';
            i++;
            ch = &lexer->input_buffer[0];
            continue;
        }
        else if (*ch == '#' &&
                 *(ch + 1) == '#' &&
                 *(ch + 2) == '#') {
            int saved_line_num = lexer->line_num;
            scan_multiline_comment(lexer, &input_pos);
            /* For each line that the multi-line comment hit, add a newline to
               the lambda so that error lines are right. */
            if (saved_line_num != lexer->line_num) {
                int increase = lexer->line_num - saved_line_num;
                ensure_lambda_data_size(lexer, i + increase);

                lambda_data = lexer->lambda_data;
                memset(lambda_data + i, '\n', increase);
                i += increase;
            }
            input_pos++;
            ch = &lexer->input_buffer[input_pos];
            continue;
        }
        /* todo: Handle strings too. */
        else if (*ch == '{')
            brace_depth++;
        else if (*ch == '}') {
            if (brace_depth == 1)
                break;

            brace_depth--;
        }

        lambda_data[i] = *ch;
        ch++;
        i++;
        input_pos++;
    }

    /* Add in the closing '}' at the end so the parser will know for sure when
       the lambda is done. */
    lambda_data[i] = '}';
    lambda_data[i+1] = '\0';

    /* The caller will start after the closing } of this lambda. */
    *pos = input_pos + 1;
}

/* scan_string
   This handles strings for lily_lexer. This updates the position in lexer's
   input_buffer for lily_lexer. */
static void scan_string(lily_lex_state *lexer, int *pos, char *new_ch)
{
    char esc_ch;
    char *label, *input_buffer;
    int i, is_multiline, label_pos, escape_this_line, multiline_start, str_size,
        word_start, word_pos;

    input_buffer = lexer->input_buffer;
    label = lexer->label;
    escape_this_line = 0;
    /* Where to start cutting from. */
    word_start = *pos + 1;
    /* Where to finish cutting from. */
    word_pos = word_start;

    /* ch is actually the first char after the opening ". */
    if (*(new_ch + 1) == '"' &&
        *(new_ch + 2) == '"') {
        is_multiline = 1;
        /* This will be used to print out the line number the str starts on, in
           case the str reaches EOF. */
        multiline_start = lexer->line_num;
        word_start += 2;
        word_pos += 2;
        new_ch += 2;
    }
    else
        is_multiline = 0;

    /* Skip over the last " of a multi-line string, or the " of a single-line
       string. */
    new_ch++;
    label_pos = 0;
    label[0] = '\0';

    while (1) {
        if (*new_ch == '\\') {
            /* For escapes, the non-escape part of the data is copied to the
               label, then the escape value is written in. */
            i = word_pos - word_start;
            memcpy(&label[label_pos], &input_buffer[word_start],
                    i * sizeof(char));
            label_pos += i;
            word_start += i;
            word_pos++;

            /* If there was an escape in the line, then the final part of the
               str after the escape needs to be cut (unless there is nothing
               after the escape). */
            escape_this_line = 1;

            esc_ch = simple_escape(new_ch);
            if (esc_ch == 0)
                lily_raise(lexer->raiser, lily_SyntaxError,
                           "Invalid escape \\%c\n", *(new_ch + 1));

            label[label_pos] = esc_ch;
            label_pos++;

            /* Add two so it starts off after the escape char the next time. */
            word_start += 2;
            new_ch = &(input_buffer[word_start - 1]);
        }
        else if (*new_ch == '\n') {
            if (is_multiline) {
                word_pos++;
                i = word_pos - word_start;
                memcpy(&label[label_pos], &input_buffer[word_start],
                       (i * sizeof(char)));
                label_pos += i;

                if (lexer->entry->read_line_fn(lexer->entry) == 0) {
                    lily_raise(lexer->raiser, lily_SyntaxError,
                               "Unterminated multi-line string (started at line %d).\n",
                               multiline_start);
                }
                /* The read line function may reallocate lexer's input_buffer,
                   so set it again here. */
                input_buffer = lexer->input_buffer;
                if ((label_pos + lexer->input_end) >= lexer->label_size) {
                    /* input_end is at most == label_pos, so a *2 grow will
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

                /* It can also realloc lexer's label... */
                label = lexer->label;

                /* This next line doesn't have an escape, so fix that. */
                escape_this_line = 0;
                /* Must manually set this + continue, or else the 0th letter
                   will get skipped. */
                word_start = 0;
                word_pos = 0;
                new_ch = &(input_buffer[word_pos]);
                continue;
            }
            else
                lily_raise(lexer->raiser, lily_SyntaxError,
                           "Unterminated string at line %d.\n",
                           lexer->line_num);
        }
        else if (*new_ch == '"') {
            if (is_multiline == 0)
                break;
            else if (*(new_ch + 1) == '"' &&
                     *(new_ch + 2) == '"')
                break;
        }

        word_pos++;
        new_ch++;
    }

    if (!escape_this_line) {
        /* No escape chars seen, so copy the full string over. */
        if (is_multiline) {
            if (word_pos != word_start) {
                i = word_pos - word_start;
                memcpy(&label[label_pos], &input_buffer[word_start],
                        i * sizeof(char));
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
                label[label_pos] = input_buffer[word_start];
        }
        /* +1 for the terminator. */
        str_size = label_pos + 1;
        if (is_multiline)
            word_pos += 2;
    }

    if (!escape_this_line && is_multiline == 0) {
        strncpy(label, input_buffer+word_start, str_size-1);
        label[str_size-1] = '\0';
    }
    else
        label[label_pos] = '\0';

    lexer->last_literal = lily_get_string_literal(lexer->symtab, label);
    *pos = word_pos + 1;
}

/*  lily_lexer_digit_rescan
    The lexer doesn't have context, so it scanned '-1' or something like that
    as a single value (negative 1 as a literal). But...parser was expecting a
    binary value, so it should have really just been the digit value by itself.
    A rescan is done because it will trap over/under-flows.
    This should only be called by parser. */
void lily_lexer_digit_rescan(lily_lex_state *lexer)
{
    /* Reset the scanning position to just after the '+' or '-' and try
       again. This is safe, because the lexer hasn't been invoked since the
       digit scan. */
    lexer->input_pos = lexer->last_digit_start + 1;

    lily_lexer(lexer);
}

/** Lexer API **/

void lily_lexer_utf8_check(lily_lex_state *lexer)
{
    char *input_buffer = lexer->input_buffer;
    char *ch = &input_buffer[0];
    int followers;

    while (1) {
        if (((unsigned char)*ch) > 127) {
            followers = follower_table[(unsigned char)*ch];
            if (followers >= 2) {
                int j;
                for (j = 1;j < followers;j++,ch++) {
                    if (((unsigned char)*ch) < 128) {
                        lily_raise(lexer->raiser, lily_EncodingError,
                                   "Invalid utf-8 sequence on line %d.\n",
                                   lexer->line_num);
                    }
                }
            }
            else if (followers == -1) {
                lily_raise(lexer->raiser, lily_EncodingError,
                           "Invalid utf-8 sequence on line %d.\n",
                           lexer->line_num);
            }
        }
        else if (*ch == '\n')
            break;

        ch++;
    }
}

/*  lily_grow_lexer_buffers
    This is used to grow lexer->input_buffer. If it's the same size as
    lexer->label, then both of them will be resized. By keeping lexer->label
    the same size or more than lexer->input_buffer, the lexer does not have to
    worry about an overflow when scanning in a label. */
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
        new_label = lily_realloc(lexer->label, new_size * sizeof(char));

        if (new_label == NULL)
            lily_raise_nomem(lexer->raiser);

        lexer->label = new_label;
        lexer->label_size = new_size;
    }

    char *new_lb;
    new_lb = lily_realloc(lexer->input_buffer, new_size * sizeof(char));

    if (new_lb == NULL)
        lily_raise_nomem(lexer->raiser);

    lexer->input_buffer = new_lb;
    lexer->input_size = new_size;
}

/*  The loaders take the mode of the first file given to determine what the
    mode should be. The mode is intentionally ignored for subsequent includes.

    There is no include system (yet!), but here is the rationale:
    * An included file can't accidentally send headers if there's space in it
      somewhere.
    * Since included files will only contain code, they will be loadable
      regardless of the mode of the starting file (so if I have functions in
      a file for apache, I can clone them over for plain interpreter use with
      no fuss). */

/*  lily_load_file
    This function creates a new entry for the lexer based off a fopen-ing the
    given path. This will call up the first line and ensure the lexer starts
    after the <?lily block.

    Note: If unable to create a new entry, ErrNoMem is raised.
          If unable to open the given path, ErrImport is raised. */
void lily_load_file(lily_lex_state *lexer, lily_lex_mode mode, char *filename)
{
    lily_lex_entry *new_entry = get_entry(lexer);

    new_entry->read_line_fn = file_read_line_fn;
    new_entry->close_fn = file_close_fn;
    new_entry->filename = filename;
    new_entry->source = fopen(filename, "r");
    if (new_entry->source == NULL) {
        lily_free(new_entry);
        lily_raise(lexer->raiser, lily_ImportError, "Failed to open %s.\n",
                   filename);
    }

    lexer->filename = filename;

    if (new_entry->prev == NULL) {
        lexer->mode = mode;
        file_read_line_fn(lexer->entry);

        if (mode == lm_tags)
            lily_lexer_handle_page_data(lexer);
    }
    else
        file_read_line_fn(lexer->entry);
}

/*  lily_load_str
    This creates a new entry for the lexer that will use the given string as
    the source. This calls up the first line, but doesn't do <?lily or ?>
    because that seems silly.

    Note: If unable to allocate a new entry, ErrNoMem is raised. */
void lily_load_str(lily_lex_state *lexer, lily_lex_mode mode, char *str)
{
    lily_lex_entry *new_entry = get_entry(lexer);

    new_entry->source = &str[0];
    new_entry->read_line_fn = str_read_line_fn;
    new_entry->close_fn = str_close_fn;
    new_entry->filename = "<str>";

    lexer->filename = "<str>";

    if (new_entry->prev == NULL) {
        lexer->mode = mode;

        str_read_line_fn(lexer->entry);
        if (mode == lm_tags)
            lily_lexer_handle_page_data(lexer);
    }
    else
        str_read_line_fn(lexer->entry);
}

/*  lily_load_special
    This creates a new entry for the lexer that will use data provided by the
    runner. The runner is responsible for providing a data source, a filename,
    a read function, and a close function.

    The mode determines if the given entry will parse tags or not.

    This function should only be called by lily_parse_special, since it raises
    ErrNoMem if unable to allocate a new entry (and that's very bad if there's
    no jump installed in the raiser). */
void lily_load_special(lily_lex_state *lexer, lily_lex_mode mode, void *source,
    char *filename, lily_reader_fn read_line_fn, lily_close_fn close_fn)
{
    lily_lex_entry *new_entry = get_entry(lexer);
    new_entry->source = source;
    new_entry->read_line_fn = read_line_fn;
    new_entry->close_fn = close_fn;
    new_entry->filename = filename;

    lexer->filename = filename;

    if (new_entry->prev == NULL) {
        lexer->mode = mode;
        read_line_fn(lexer->entry);
        if (mode == lm_tags)
            lily_lexer_handle_page_data(lexer);
    }
    else
        read_line_fn(lexer->entry);
}

/* lily_lexer
   This is the main scanning function. It sometimes farms work out to other
   functions in the case of strings and numeric values. */
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
            token = tk_word;
        }
        else if (group <= CC_G_ONE_LAST) {
            input_pos++;
            /* This is okay because the first group starts at 0, and the tokens
               for it start at 0. */
            token = group;
        }
        else if (group == CC_NEWLINE) {
            if (lexer->entry->read_line_fn(lexer->entry) == 1) {
                input_pos = 0;
                continue;
            }
            else {
                token = leave_entry(lexer);
                input_pos = lexer->input_pos;
            }
        }
        else if (group == CC_SHARP) {
            if (*ch       == '#' &&
                *(ch + 1) == '#' &&
                *(ch + 2) == '#') {
                scan_multiline_comment(lexer, &input_pos);
                continue;
            }
            else {
                if (lexer->entry->read_line_fn(lexer->entry) == 1) {
                    input_pos = 0;
                    continue;
                }
                else {
                    token = leave_entry(lexer);
                    input_pos = lexer->input_pos;
                }
            }
        }
        else if (group == CC_DOUBLE_QUOTE) {
            scan_string(lexer, &input_pos, ch);
            token = tk_double_quote;
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
                if (*ch == '.') {
                    ch++;
                    input_pos++;

                    if (*ch == '.') {
                        input_pos++;
                        token = tk_three_dots;
                    }
                    else
                        lily_raise(lexer->raiser, lily_SyntaxError,
                                "'..' is not a valid token (expected 1 or 3 dots).\n");
                }
                else
                    token = tk_dot;
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
        else if (group == CC_LEFT_CURLY) {
            input_pos++;
            ch++;
            if (*ch == '|') {
                scan_lambda(lexer, &input_pos);
                token = tk_lambda;
            }
            else
                token = tk_left_curly;
        }
        else if (group == CC_COLON) {
            input_pos++;
            ch++;
            if (*ch == ':') {
                input_pos++;
                ch++;
                token = tk_colon_colon;
            }
            else
                token = tk_colon;
        }
        else if (group == CC_AMPERSAND) {
            input_pos++;
            ch++;

            if (*ch == '&') {
                input_pos++;
                ch++;
                token = tk_logical_and;
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
            else if (ch_class[(unsigned char)*ch] == CC_WORD) {
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
                if (lexer->mode == lm_no_tags)
                    lily_raise(lexer->raiser, lily_SyntaxError,
                            "Found ?> but not expecting tags.\n");
                if (lexer->entry->prev != NULL)
                    lily_raise(lexer->raiser, lily_SyntaxError,
                            "Tags not allowed in included files.\n");

                input_pos++;
                token = tk_end_tag;
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

/* lily_lexer_handle_page_data
   This scans the html outside of the <?lily and ?> tags, sending it off to
   lily_impl_puts (which differs depending on the runner). This is only called
   when handling files (lily_lexer won't send the ?> end_tag token for
   string-based scanning).*/
void lily_lexer_handle_page_data(lily_lex_state *lexer)
{
    char c;
    int lbp, htmlp;
    void *data = lexer->data;

    /* htmlp and lbp are used so it's obvious they aren't globals. */
    lbp = lexer->input_pos;
    c = lexer->input_buffer[lbp];
    htmlp = 0;

    /* Send html to the server either when unable to hold more or the lily tag
       is found. */
    while (1) {
        lbp++;
        if (c == '<') {
            if ((lbp + 4) <= lexer->input_end &&
                strncmp(lexer->input_buffer + lbp, "?lily", 5) == 0) {
                if (htmlp != 0) {
                    /* Don't include the '<', because it goes with <?lily. */
                    lexer->label[htmlp] = '\0';
                    lily_impl_puts(data, lexer->label);
                }
                lbp += 5;
                /* Yield control to the lexer. */
                break;
            }
        }
        lexer->label[htmlp] = c;
        htmlp++;
        if (htmlp == (lexer->input_size - 1)) {
            lexer->label[htmlp] = '\0';
            lily_impl_puts(data, lexer->label);
            /* This isn't done, so fix htmlp. */
            htmlp = 0;
        }

        if (c == '\n') {
            if (lexer->entry->read_line_fn(lexer->entry))
                lbp = 0;
            else {
                if (htmlp != 0) {
                    lexer->label[htmlp] = '\0';
                    lily_impl_puts(data, lexer->label);
                }

                lexer->token = leave_entry(lexer);
                lbp = lexer->input_pos;
                break;
            }
        }

        c = lexer->input_buffer[lbp];
    }

    lexer->input_pos = lbp;
}

/* tokname
   This function returns a printable name for a given token. This is mostly for
   parser to be able to print the token when it has an unexpected token. */
char *tokname(lily_token t)
{
    static char *toknames[] =
    {"(", ")", ",", "}", "[", "^", "!", "!=", "%", "%=", "*", "*=", "/", "/=",
     "+", "+=", "-", "-=", "<", "<=", "<<", "<<=", ">", ">=", ">>", ">>=", "=",
     "==", "{", "a lambda", "<[", "]>", "]", "=>", "a label", "a property name",
     "a string", "an integer", "a double", ".", ":", "::", "&", "&&", "|",
     "||", "@(", "...", "invalid token", "?>", "end of file", "end of file"};

    if (t < (sizeof(toknames) / sizeof(toknames[0])))
        return toknames[t];

    return NULL;
}

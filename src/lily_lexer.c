#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lily_alloc.h"
#include "lily_config.h"
#include "lily_lexer.h"
#include "lily_lexer_data.h"
#include "lily_utf8.h"

/** Lexer init and deletion **/
lily_lex_state *lily_new_lex_state(lily_raiser *raiser)
{
    lily_lex_state *lex = lily_malloc(sizeof(*lex));
    lily_lex_entry *entry = lily_malloc(sizeof(*entry));

    /* Only these fields need to be set. The other fields will be set when this
       entry's content is saved. */
    entry->prev = NULL;
    entry->next = NULL;
    entry->pile_start = 0;
    entry->entry_type = et_unused;

    lex->label_size = 128;
    lex->source_size = 128;

    /* The read cursor is only accessed during line reads and token fetching.
       None of those occur without a source being set first. */
    lex->label = lily_malloc(lex->label_size * sizeof(*lex->label));
    lex->token = tk_eof;

    /* This must start at 0 since the line reader will bump it by one. */
    lex->line_num = 0;
    lex->expand_start_line = 0;
    lex->string_length = 0;
    lex->n.integer_val = 0;
    lex->source = lily_malloc(lex->source_size * sizeof(*lex->source));
    lex->entry = entry;
    lex->string_pile = lily_new_string_pile();
    lex->raiser = raiser;

    return lex;
}

static void close_entry(lily_lex_entry *entry)
{
    switch (entry->entry_type) {
        case et_copied_string:
        case et_lambda:
            lily_free(entry->cursor_origin);
            break;
        case et_file:
            fclose(entry->entry_file);
            break;
        default:
            break;
    }

    entry->entry_type = et_unused;
}

void lily_rewind_lex_state(lily_lex_state *lex, uint16_t line_num)
{
    lily_lex_entry *entry_iter = lex->entry;

    while (1) {
        lily_lex_entry *prev_entry = entry_iter->prev;

        close_entry(entry_iter);

        if (prev_entry == NULL)
            break;

        entry_iter = prev_entry;
    }

    lex->entry = entry_iter;
    lex->line_num = line_num;
}

void lily_free_lex_state(lily_lex_state *lex)
{
    lily_lex_entry *entry_iter = lex->entry;

    while (entry_iter->next)
        entry_iter = entry_iter->next;

    while (1) {
        lily_lex_entry *prev_entry = entry_iter->prev;

        close_entry(entry_iter);
        lily_free(entry_iter);

        if (prev_entry == NULL)
            break;

        entry_iter = prev_entry;
    }

    lily_free_string_pile(lex->string_pile);
    lily_free(lex->source);
    lily_free(lex->label);
    lily_free(lex);
}

static void grow_source_buffer(lily_lex_state *lex)
{
    uint32_t new_size = lex->source_size * 2;

    /* Make sure the identifier buffer is always at least the size of the
       source buffer. This allows single-line strings and identifiers of all
       sorts to copy into the identifier buffer without doing size checks. */
    if (lex->label_size == lex->source_size) {
        char *new_label;
        new_label = lily_realloc(lex->label, new_size * sizeof(*new_label));

        lex->label = new_label;
        lex->label_size = new_size;
    }

    char *new_source;
    new_source = lily_realloc(lex->source, new_size * sizeof(*new_source));

    lex->source = new_source;
    lex->source_size = new_size;
}

#define READER_PREP \
int bufsize, i; \
lily_lex_entry *entry = lex->entry; \
char *source = lex->source; \
 \
bufsize = lex->source_size - 2; \
i = 0; \
int utf8_check = 0;

#define READER_GROW_CHECK \
if (i == bufsize) { \
    grow_source_buffer(lex); \
    source = lex->source; \
    bufsize = lex->source_size - 2; \
}

#define READER_EOF_CHECK(to_check, against) \
if (to_check == against) { \
    source[i] = '\n'; \
    source[i + 1] = '\0'; \
    /* Bump the line number, unless only EOF or \0 was seen. */ \
    lex->line_num += !!i; \
    break; \
}

#define READER_END \
if (utf8_check && lily_is_valid_utf8(source) == 0) { \
    lily_raise_raw(lex->raiser, "Invalid utf-8 sequence on line %d.", \
            lex->line_num); \
} \
 \
lex->read_cursor = lex->source; \
return i;

/** file and str reading functions **/

/* This reads a line from a file-backed entry. */

static int read_file_line(lily_lex_state *lex)
{
    READER_PREP
    FILE *input_file = (FILE *)entry->entry_file;
    int ch;

    while (1) {
        ch = fgetc(input_file);

        READER_GROW_CHECK
        READER_EOF_CHECK(ch, EOF)

        source[i] = (char)ch;

        if (ch == '\r' || ch == '\n') {
            lex->line_num++;

            if (ch == '\r') {
                source[i] = '\n';
                ch = fgetc(input_file);
                if (ch != '\n')
                    ungetc(ch, input_file);
            }

            i++;
            source[i] = '\0';
            break;
        }
        else if ((unsigned char)ch > 127)
            utf8_check = 1;

        i++;
    }

    READER_END
}

/* This reads a line from a string-backed entry. */
static int read_str_line(lily_lex_state *lex)
{
    READER_PREP
    char *ch = (char *)entry->entry_cursor;

    while (1) {
        READER_GROW_CHECK
        READER_EOF_CHECK(*ch, '\0')

        source[i] = *ch;

        if (*ch == '\r' || *ch == '\n') {
            lex->line_num++;

            if (*ch == '\r') {
                source[i] = '\n';
                ch++;
                if (*ch == '\n')
                    ch++;
            }
            else
                ch++;

            i++;
            source[i] = '\0';
            break;
        }
        else if (((unsigned char)*ch) > 127)
            utf8_check = 1;

        i++;
        ch++;
    }

    entry->entry_cursor = ch;
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
        return read_file_line(lex);
    else
        return read_str_line(lex);
}

/** Scanning functions and helpers **/

/* Returns the char code of an escape. *source_ch starts on the first character
   after the slash, and is updated to 1 past the end of the escape sequence. */
static uint16_t scan_escape(char *ch, char *out)
{
    uint16_t result = 1;

    if (*ch == 'n')
        *out = '\n';
    else if (*ch == 'r')
        *out = '\r';
    else if (*ch == 't')
        *out = '\t';
    else if (*ch == '\'')
        *out = '\'';
    else if (*ch == '"')
        *out = '"';
    else if (*ch == '\\')
        *out = '\\';
    else if (*ch == 'b')
        *out = '\b';
    else if (*ch == 'a')
        *out = '\a';
    else if (*ch == '/')
        *out = LILY_PATH_CHAR;
    else if (*ch >= '0' && *ch <= '9') {
        /* This is a numeric escape. Scan up to three digits, including any
           leading zeroes. This allows \0 as well as \000. */
        int total;
        uint16_t i;

        for (i = 0, total = 0;i < 3;i++, ch++) {
            if (*ch < '0' || *ch > '9')
                break;

            total = (total * 10) + (*ch - '0');

            if (total > 255) {
                i = 0;
                break;
            }
        }

        result = i;
        *out = (char)total;
    }
    else
        result = 0;

    return result;
}

/** Numeric scanning **/
/* Most of the numeric scanning functions follow a pattern: Each defines the max
   number of digits it can take so that the unsigned result cannot overflow.
   scan_decimal takes an extra is_integer param because it may have an exponent
   which automatically meant it will yield a double result. */

static void scan_exponent(lily_lex_state *lex, char **source_ch)
{
    char *ch = *source_ch;
    int num_digits = 0;

    ch++;

    if (*ch == '+' || *ch == '-')
       ch++;

    if (*ch < '0' || *ch > '9')
        lily_raise_syn(lex->raiser,
                   "Expected a base 10 number after exponent.");

    while (*ch >= '0' && *ch <= '9') {
        num_digits++;

        if (num_digits > 3)
            lily_raise_syn(lex->raiser, "Exponent is too large.");

        ch++;
    }

    *source_ch = ch;
}

static uint64_t scan_binary(char **source_ch)
{
    char *ch = *source_ch;
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 65;

    /* Skip the 'b' part of the 0b intro. */
    ch++;

    while (*ch == '0')
        ch++;

    while ((*ch == '0' || *ch == '1') && num_digits != max_digits) {
        num_digits++;
        result = (result * 2) + *ch - '0';
        ch++;
    }

    *source_ch = ch;
    return result;
}

static uint64_t scan_octal(char **source_ch)
{
    char *ch = *source_ch;
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 23;

    /* Skip the 'c' part of the 0c intro. */
    ch++;

    while (*ch == '0')
        ch++;

    while (*ch >= '0' && *ch <= '7' && num_digits != max_digits) {
        num_digits++;
        result = (result * 8) + *ch - '0';
        ch++;
    }

    *source_ch = ch;
    return result;
}

static uint64_t scan_decimal(lily_lex_state *lex, char **source_ch,
        int *is_integer)
{
    char *ch = *source_ch;
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 21;
    int have_dot = 0;
    /* This is the only scanner that doesn't start off on pos+1. This is because
       there's no formatting character to skip over. */

    while (*ch == '0')
        ch++;

    while (num_digits != max_digits) {
        if (*ch >= '0' && *ch <= '9') {
            if (*is_integer) {
                num_digits++;
                result = (result * 10) + *ch - '0';
            }
        }
        else if (*ch == '.') {
            unsigned char next_ch = (unsigned char)*(ch + 1);

            /* The second check is for methods on bare literals
               (ex: `10.to_s()`). */
            if (have_dot == 1 ||
                isdigit(next_ch) == 0)
                break;

            have_dot = 1;
            *is_integer = 0;
        }
        else if (*ch == 'e') {
            *is_integer = 0;
            scan_exponent(lex, &ch);
            break;
        }
        else
            break;

        num_digits++;
        ch++;
    }

    *source_ch = ch;
    return result;
}

static uint64_t scan_hex(char **source_ch)
{
    char *ch = *source_ch;
    uint64_t result = 0;
    int num_digits = 0;
    int max_digits = 17;

    /* Skip the 'x' part of the 0x intro. */
    ch++;

    while (*ch == '0')
        ch++;

    while (num_digits != max_digits) {
        char mod;
        if (*ch >= '0' && *ch <= '9')
            mod = '0';
        else if (*ch >= 'a' && *ch <= 'f')
            mod = 'a' - 10;
        else if (*ch >= 'A' && *ch <= 'F')
            mod = 'A' - 10;
        else
            break;

        result = (result * 16) + *ch - mod;
        num_digits++;
        ch++;
    }

    *source_ch = ch;
    return result;
}

/* This handles all numeric scanning. 'pos' is used as the starting spot. The
   results are sent to 'tok' (which is set to either tk_integer or tk_double),
   and 'new_ch' (which is set to the place to start scanning from next time). */
static void scan_number(lily_lex_state *lex, char **source_ch, uint8_t *tok)
{
    char *ch = *source_ch;
    char sign = *ch;
    int is_integer = 1;
    uint64_t integer_value = 0;

    if (sign == '-' || sign == '+') {
        lex->number_sign_offset = (uint16_t)(ch - lex->source);
        ch++;
    }
    else {
        lex->number_sign_offset = UINT16_MAX;
        sign = 0;
    }

    if (*ch == '0') {
        ch++;

        if (*ch == 'b')
            integer_value = scan_binary(&ch);
        else if (*ch == 'c')
            integer_value = scan_octal(&ch);
        else if (*ch == 'x')
            integer_value = scan_hex(&ch);
        else
            integer_value = scan_decimal(lex, &ch, &is_integer);
    }
    else
        integer_value = scan_decimal(lex, &ch, &is_integer);

    if (*ch == 't') {
        ch++;

        if (is_integer == 0)
            lily_raise_syn(lex->raiser, "Double value with Byte suffix.");

        if (sign == '-' || sign == '+')
            lily_raise_syn(lex->raiser, "Byte values cannot have a sign.");

        if (integer_value > 0xFF)
            lily_raise_syn(lex->raiser, "Byte value is too large.");

        lex->n.integer_val = integer_value;
        *tok = tk_byte;
    }
    else if (is_integer) {
        /* This won't be used uninitialized. I promise. */
        int64_t signed_val = 0;

        if (sign != '-') {
            if (integer_value <= INT64_MAX)
                signed_val = (int64_t)integer_value;
            else
                lily_raise_syn(lex->raiser, "Integer value is too large.");
        }
        else {
            uint64_t max = (uint64_t)INT64_MAX + 1ULL;
            if (integer_value <= max)
                signed_val = -(int64_t)integer_value;
            else
                lily_raise_syn(lex->raiser, "Integer value is too small.");
        }

        lex->n.integer_val = signed_val;
        *tok = tk_integer;
    }
    else {
        char *iter = *source_ch;
        /* Line syncing ensures this cannot overflow. */
        char *label = lex->label;

        while (iter != ch) {
            *label = *iter;
            label++;
            iter++;
        }

        *label = '\0';

        errno = 0;

        /* Doubles are complicated, so let's phone a friend and ask them to help
           figure out what the value should be. */
        double double_val = strtod(lex->label, NULL);

        if (errno == ERANGE)
            lily_raise_syn(lex->raiser, "Double value is out of range.");

        lex->n.double_val = double_val;
        *tok = tk_double;
    }

    *source_ch = ch;
}

static void scan_multiline_comment(lily_lex_state *lex, char **source_ch)
{
    lex->expand_start_line = lex->line_num;

    /* Start at the first char of the comment. */
    char *ch = *source_ch + 1;

    while (1) {
        if (*ch == ']' &&
            *(ch + 1) == '#') {
            ch += 2;
            break;
        }
        else if (*ch == '\n') {
            if (read_line(lex)) {
                ch = lex->read_cursor;
                continue;
            }
            else {
                lily_raise_syn(lex->raiser,
                        "Unterminated multi-line comment (started at line %d).",
                        lex->expand_start_line);
            }
        }

        ch++;
    }

    *source_ch = ch;
}

static void check_label_size(lily_lex_state *lex, uint32_t at_least)
{
    if (lex->label_size > at_least)
        return;

    uint32_t new_size = lex->label_size;

    while (new_size < at_least)
        new_size *= 2;

    char *new_label = lily_realloc(lex->label, new_size * sizeof(*new_label));

    lex->label = new_label;
    lex->label_size = new_size;
}

static void scan_docblock(lily_lex_state *lex, char **source_ch)
{
    uint16_t offset = (uint16_t)(*source_ch - lex->source);
    uint16_t start_line = lex->line_num;
    uint16_t label_pos = 0;
    char *label = lex->label;
    char *ch = lex->source;

    while (1) {
        uint16_t i = 0;

        while (*ch == ' ' || *ch == '\t') {
            ch++;
            i++;
        }

        if (*ch != '#') {
            if (lex->line_num == start_line)
                lily_raise_syn(lex->raiser,
                        "Docblock is preceded by non-whitespace.");

            break;
        }
        else if (*(ch + 1) != '#' ||
                 *(ch + 2) != '#')
            lily_raise_syn(lex->raiser,
                    "Docblock line does not start with full '###'.");
        else if (i != offset)
            lily_raise_syn(lex->raiser,
                    "Docblock has inconsistent indentation.");

        /* Don't include the triple # in the docblock. */
        ch += 3;

        /* Same for the spacing that comes after. */
        while (*ch == ' ' || *ch == '\t')
            ch++;

        /* The rest goes into the outgoing text. */
        while (*ch != '\n') {
            label[label_pos] = *ch;
            label_pos++;
            ch++;
        }

        label[label_pos] = '\n';
        label_pos++;

        int line_length = read_line(lex);

        if (line_length == 0)
            break;

        check_label_size(lex, label_pos + line_length);
        label = lex->label;
        ch = lex->read_cursor;
    }

    *source_ch = ch;
    label[label_pos] = '\0';
}

/* Scan a String or ByteString literal. This starts on the cursor provided and
   updates it.
   The caller is expected to set the cursor on the earliest part of the literal.
   In doing so, this function is able to determine the kind of literal to scan
   for without having to send any flags. */
static void scan_string(lily_lex_state *lex, char **source_ch)
{
    int is_multiline = 0;
    int is_bytestring = 0;
    int backslash_before_newline = 0;
    uint32_t label_pos = 0;
    char *label = lex->label;
    char *ch = *source_ch;

    if (*ch == 'B') {
        is_bytestring = 1;
        ch++;
    }

    /* Triple quote is a multi-line string. */
    if (*(ch + 1) == '"' &&
        *(ch + 2) == '"') {
        lex->expand_start_line = lex->line_num;
        is_multiline = 1;
        ch += 2;
    }

    ch++;

    while (1) {
        if (*ch == '\\') {
            ch++;

            if (*ch == '\n') {
                backslash_before_newline = 1;
                continue;
            }

            char esc_ch;
            uint16_t distance = scan_escape(ch, &esc_ch);

            if (distance == 0)
                lily_raise_syn(lex->raiser, "Invalid escape sequence.");

            ch += distance;

            /* Make sure String is \0 terminated and utf-8 clean. */
            if (is_bytestring == 0 &&
                (esc_ch == 0 || (unsigned char)esc_ch > 127))
                lily_raise_syn(lex->raiser, "Invalid escape sequence.");

            label[label_pos] = esc_ch;
            label_pos++;
        }
        else if (*ch == '\n') {
            if (is_multiline == 0 && backslash_before_newline == 0)
                lily_raise_syn(lex->raiser, "Newline in single-line string.");

            int line_length = read_line(lex);
            if (line_length == 0) {
                lily_raise_syn(lex->raiser,
                           "Unterminated string (started at line %d).",
                           lex->expand_start_line);
            }

            check_label_size(lex, label_pos + line_length);
            label = lex->label;
            ch = lex->read_cursor;

            if (backslash_before_newline == 0) {
                label[label_pos] = '\n';
                label_pos++;
            }
            else {
                while (*ch == ' ')
                    ch++;

                backslash_before_newline = 0;
            }
        }
        else if (*ch == '"') {
            ch++;

            if (is_multiline == 0)
                break;
            else if (*ch == '"' && *(ch + 1) == '"') {
                ch += 2;
                break;
            }
            else {
                label[label_pos] = '"';
                label_pos++;
            }
        }
        else if (*ch == '\0')
            break;
        else {
            label[label_pos] = *ch;
            label_pos++;
            ch++;
        }
    }

    if (is_bytestring == 0)
        label[label_pos] = '\0';

    lex->string_length = label_pos;
    *source_ch = ch;
}

static void scan_single_quote(lily_lex_state *lex, char **source_ch)
{
    char *ch = *source_ch + 1;
    char result = '\0';

    if (*ch == '\\') {
        ch++;

        char esc_ch;
        uint16_t distance = scan_escape(ch, &esc_ch);

        if (distance == 0)
            lily_raise_syn(lex->raiser, "Invalid escape sequence.");

        result = esc_ch;
        ch += distance;
    }
    else if (*ch != '\'') {
        result = *ch;
        ch++;
    }
    else
        lily_raise_syn(lex->raiser, "Byte literals cannot be empty.");

    if (*ch != '\'')
        lily_raise_syn(lex->raiser, "Multi-character byte literal.");

    *source_ch = ch + 1;
    lex->n.integer_val = (unsigned char)result;
}

static int read_line_for_buffer(lily_lex_state *lex, char **label, int start)
{
    int line_length = read_line(lex);

    if (line_length) {
        check_label_size(lex, start + line_length);
        *label = lex->label;
    }

    return line_length;
}

static void scan_string_for_lambda(lily_lex_state *lex, char **source_ch,
        int *start)
{
    char *label = lex->label;
    char *ch = *source_ch;
    int i = *start;
    int backslash_before = 0;
    int is_multiline = 0;
    uint16_t start_line = lex->line_num;

    label[i] = '"';
    i++;
    ch++;

    if (*(ch) == '"' &&
        *(ch + 1) == '"') {
        label[i] = '"';
        label[i + 1] = '"';
        ch += 2;
        i += 2;
        is_multiline = 1;
    }

    while (1) {
        if (*ch == '\n') {
            if (read_line_for_buffer(lex, &label, i) == 0)
                lily_raise_syn(lex->raiser,
                           "Unterminated string (started at line %d).",
                           start_line);

            ch = lex->read_cursor;
            label[i] = '\n';
            i++;

            if (is_multiline || backslash_before) {
                backslash_before = 0;
                continue;
            }

            lily_raise_syn(lex->raiser, "Newline in single-line string.");
        }
        else if (*ch == '"') {
            label[i] = *ch;
            i++;
            ch++;

            if (backslash_before) {
                /* This is part of an escape sequence. */
                backslash_before = 0;
                continue;
            }

            /* Multi-line strings need the full sequence to leave. */
            if (is_multiline == 0)
                break;

            if (*ch == '"' &&
                *(ch + 1) == '"') {

                label[i] = '"';
                label[i + 1] = '"';
                i += 2;
                ch += 2;
                break;
            }
        }
        else if (*ch == '\\') {
            label[i] = *ch;
            i++;
            ch++;

            if (*ch == '"' ||
                (is_multiline == 0 && *ch == '\n'))
                backslash_before = 1;
            else if (*ch == '\\') {
                /* This is an escaped backslash. Scoop it up so the second slash
                   can't be seen as an escape for a double quote. */
                label[i] = *ch;
                i++;
                ch++;
            }
        }
        else {
            label[i] = *ch;
            i++;
            ch++;
        }
    }

    *source_ch = ch;
    *start = i;
}

static void scan_lambda(lily_lex_state *lex, char **source_ch)
{
    char *label = lex->label;
    char *ch = *source_ch;
    int depth = 1;
    int i = 0;
    uint16_t start_line = lex->line_num;

    while (1) {
        if (*ch == '\n' ||
            (*ch == '#' &&
             *(ch + 1) != '[')) {
            int length = read_line_for_buffer(lex, &label, i);
            if (length == 0)
                lily_raise_syn(lex->raiser,
                        "Unterminated lambda (started at line %d).",
                        start_line);

            ch = lex->read_cursor;
            label[i] = '\n';
            i++;
            continue;
        }
        else if (*ch == '#' &&
                 *(ch + 1) == '[') {
            uint16_t saved_line_num = lex->line_num;

            scan_multiline_comment(lex, &ch);
            /* For each line that the multi-line comment hit, add a newline to
               the lambda so that error lines are right. */
            if (saved_line_num != lex->line_num) {
                uint16_t increase = lex->line_num - saved_line_num;
                /* Write \n for each line seen so that the lines match up.
                   Make sure that lex->label can handle the increases AND the
                   now-current line (plus 3 for that termination). */
                check_label_size(lex,
                        i + increase + 3 + strlen(lex->source));
                label = lex->label;

                memset(label + i, '\n', increase);
                i += increase;
            }
            else {
                /* Insert a space to prevent token gluing. */
                label[i] = ' ';
                i++;
            }
            continue;
        }
        else if (*ch == '"') {
            scan_string_for_lambda(lex, &ch, &i);
            label = lex->label;
            continue;
        }
        else if (*ch == '\'') {
            char *end_ch = ch;

            scan_single_quote(lex, &end_ch);

            while (ch != end_ch) {
                label[i] = *ch;
                i++;
                ch++;
            }

            continue;
        }
        else if (*ch == '(')
            depth++;
        else if (*ch == ')') {
            if (depth == 1)
                break;

            depth--;
        }

        label[i] = *ch;
        ch++;
        i++;
    }

    lex->expand_start_line = start_line;
    label[i] = '\0';
    *source_ch = ch + 1;
}

/* Walk the string literal that 'import' was given to make sure that it's
   valid. This also fixes the '/' characters into proper path characters for the
   given platform. */
void lily_lexer_verify_path_string(lily_lex_state *lex)
{
    char *label = lex->label;

    if (label[0] == '\0')
        lily_raise_syn(lex->raiser, "Import path must not be empty.");

    int original_len = strlen(lex->label);
    int len = original_len;
    int necessary = 0;
    char *reverse_iter = lex->read_cursor - 2;
    char *reverse_label = label + len - 1;

    if (lex->source + 2 < lex->read_cursor &&
        *reverse_iter == '"' &&
        *(reverse_iter - 1) == '"')
        lily_raise_syn(lex->raiser,
                "Import path cannot be a triple-quote string.");

    if (*reverse_label == '/' || *label == '/')
        lily_raise_syn(lex->raiser,
                "Import path cannot begin or end with '/'.");

    while (len) {
        if (*reverse_iter != *reverse_label)
            lily_raise_syn(lex->raiser,
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
        lily_raise_syn(lex->raiser,
                "Simple import paths do not need to be quoted.");
}

/* The lexer reads `-1` and `+1` as negative or positive literals. Most of the
   time that's correct. But sometimes it's part of, say, `1+1`. This checks if
   the last digit scanned had a sign. If it did, the number is rescanned to get
   the correct value. */
int lily_lexer_digit_rescan(lily_lex_state *lex)
{
    /* This is used to denote that no sign was seen. */
    if (lex->number_sign_offset == UINT16_MAX)
        return 0;

    uint8_t t;
    char *ch = lex->source + lex->number_sign_offset + 1;

    scan_number(lex, &ch, &t);

    return 1;
}

/** Lexer API **/

static lily_lex_entry *add_new_entry(lily_lex_state *lex)
{
    lily_lex_entry *result = lily_malloc(sizeof(*result));

    lex->entry->next = result;
    result->prev = lex->entry;
    result->next = NULL;

    return result;
}

/* Remove the top-most entry. Restore state to what the previous entry held. */
void lily_pop_lex_entry(lily_lex_state *lex)
{
    lily_lex_entry *entry = lex->entry;

    close_entry(entry);
    entry = entry->prev;

    if (entry == NULL) {
        lex->line_num = 0;
        return;
    }

    lex->token = entry->token;
    lex->line_num = entry->line_num;
    lex->expand_start_line = entry->expand_start_line;
    lex->n = entry->n;

    /* The source buffer is always saved first so it's easy to restore back. */
    const char *saved_source = lily_sp_get(lex->string_pile, entry->pile_start);

    strcpy(lex->source, saved_source);
    lex->read_cursor = lex->source + entry->cursor_offset;

    switch (entry->token) {
        case tk_word:
        case tk_prop_word:
        case tk_keyword_arg:
        case tk_double_quote:
        case tk_lambda:
        {
            lex->string_length = entry->ident_length;

            uint16_t start = entry->next->pile_start - entry->ident_length;
            char *ident = lily_sp_get(lex->string_pile, start);

            strcpy(lex->label, ident);
        }
            break;
        case tk_bytestring:
        {
            lex->string_length = entry->ident_length;

            uint16_t start = entry->next->pile_start - entry->ident_length;
            char *ident = lily_sp_get(lex->string_pile, start);

            memcpy(lex->label, ident, entry->ident_length);
        }
            break;
        default:
            entry->ident_length = 0;
    }

    lex->entry = entry;
}

static void save_lex_state(lily_lex_state *lex)
{
    lily_lex_entry *target = lex->entry;

    target->token = lex->token;
    target->line_num = lex->line_num;
    target->expand_start_line = lex->expand_start_line;
    target->n = lex->n;
    target->cursor_offset = (uint16_t)(lex->read_cursor - lex->source);

    uint16_t pile_start = target->pile_start;
    uint16_t ident_start = pile_start;

    /* Save the current line first since it always needs to be saved. It makes
       for a consistent place to restore from. */
    lily_sp_insert(lex->string_pile, lex->source, &ident_start);

    uint16_t next_start = ident_start;

    switch (target->token) {
        case tk_word:
        case tk_keyword_arg:
        case tk_prop_word:
        case tk_double_quote:
        case tk_lambda:
            lily_sp_insert(lex->string_pile, lex->label, &next_start);
            target->ident_length = next_start - ident_start;
            break;
        case tk_bytestring:
            lily_sp_insert_bytes(lex->string_pile, lex->label, &next_start,
                    lex->string_length);
            target->ident_length = next_start - ident_start;
        default:
            break;
    }

    target->next->pile_start = next_start;
}

void lily_lexer_load(lily_lex_state *lex, lily_lex_entry_type entry_type,
        const void *source)
{
    lily_lex_entry *current = lex->entry;
    lily_lex_entry *new_entry;

    if (current->entry_type != et_unused) {
        /* Make sure a next entry exists before doing the save. Saving will
           scribble the pile start on the next, so next needs to be not-NULL. */
        new_entry = current->next;

        if (new_entry == NULL)
            new_entry = add_new_entry(lex);

        save_lex_state(lex);

        lex->entry = new_entry;
        lex->line_num = 0;
    }
    else
        /* Not in any content, so use the first entry and don't save. */
        new_entry = current;

    switch (entry_type) {
        case et_file:
            new_entry->entry_file = (FILE *)source;
            break;
        case et_lambda:
        case et_copied_string:
        {
            char *str_source = (char *)source;
            char *copy = lily_malloc((strlen(str_source) + 1) * sizeof(*copy));

            strcpy(copy, str_source);
            new_entry->entry_cursor = copy;
            new_entry->cursor_origin = copy;
        }
            break;
        case et_shallow_string:
            new_entry->entry_cursor = (const char *)source;
        default:
            break;
    }

    new_entry->entry_type = entry_type;

    read_line(lex);
}

#define move_2_set_token(t) \
{ \
    ch += 2; \
    token = t; \
}

#define move_set_token(t) \
{ \
    ch++; \
    token = t; \
}

/* This function pulls up the next token. This function is called extremely
   often, so it needs to be fast. */
void lily_next_token(lily_lex_state *lex)
{
start: ;
    char *ch = lex->read_cursor;

    while (*ch == ' ' || *ch == '\t')
        ch++;

    /* Lexer's ch_table is used to reduce the number of cases:
       * Invalid characters become tk_invalid.

       * Identifier letters (except 'B') are tk_word.
         B is CC_B because it can start a ByteString.

       * If the character by itself is a valid token, it's that token.
         (tk_dot for '.', tk_bitwise_and for '&', etc.)
         Valid single characters only need to move the cursor.

       * Characters without a single token representation get one of the
         CC_* values defined in src/lily_lexer_tables.h.
         These fake token values will never be returned by this function. */
    uint8_t token = ch_table[(unsigned char)*ch];

    switch (token) {
        case tk_word: {
word_case: ;
            char *label = lex->label;
            do {
                *label = *ch;
                label++;
                ch++;
            } while (ident_table[(unsigned char)*ch]);
            *label = '\0';
            break;
        }
        case tk_colon:
            ch++;
            if (ident_table[(unsigned char)*ch]) {
                token = tk_keyword_arg;
                goto word_case;
            }
            break;
        case tk_comma:
        case tk_left_bracket:
        case tk_left_curly:
        case tk_right_curly:
        case tk_right_parenth:
        case tk_tilde:
            ch++;
            break;
        case CC_SHARP:
            if (*(ch + 1) == '[') {
                scan_multiline_comment(lex, &ch);
                lex->read_cursor = ch;
                goto start;
            }
            else if (*(ch + 1) == '#' &&
                     *(ch + 2) == '#') {
                scan_docblock(lex, &ch);
                token = tk_docblock;
                break;
            }
            /* Fall through to skip this line. */
        case CC_NEWLINE:
            /* Parser will call to pop this entry back when it's ready. */
            if (read_line(lex))
                goto start;
            else if (lex->entry->entry_type != et_lambda)
                token = tk_eof;
            else
                token = tk_end_lambda;

            break;
        case tk_double_quote:
            scan_string(lex, &ch);
            break;
        case tk_byte:
            scan_single_quote(lex, &ch);
            break;
        case CC_B:
            if (*(ch + 1) == '"') {
                /* This is a bytestring. Don't bump ch so the function
                   figures it out and does proper scanning. */
                scan_string(lex, &ch);
                token = tk_bytestring;
            }
            else {
                token = tk_word;
                goto word_case;
            }
            break;
        /* These five tokens have their assign as +1 from their base. */
        case tk_bitwise_xor:
        case tk_divide:
        case tk_not:
        case tk_modulo:
        case tk_multiply:
            ch++;
            if (*ch == '=') {
                ch++;
                token++;
            }
            break;
        case CC_DIGIT:
            scan_number(lex, &ch, &token);
            break;
        case tk_dot:
            if (ch_table[(unsigned char)*(ch + 1)] == CC_DIGIT)
                scan_number(lex, &ch, &token);
            else if (*(ch + 1) != '.')
                ch++;
            else if (*(ch + 2) == '.') {
                ch += 3;
                token = tk_three_dots;
            }
            else {
                lily_raise_syn(lex->raiser,
                        "'..' is not a valid token (expected 1 or 3 dots).");
            }
            break;
        case tk_plus:
            if (ch_table[(unsigned char)*(ch + 1)] == CC_DIGIT)
                scan_number(lex, &ch, &token);
            else if (*(ch + 1) == '=')
                move_2_set_token(tk_plus_eq)
            else if (*(ch + 1) == '+') {
                if (*(ch + 2) == '=')
                    lily_raise_syn(lex->raiser,
                            "'++=' is not a valid token.");

                move_2_set_token(tk_plus_plus)
            }
            else
                ch++;

            break;
        case tk_minus:
            if (ch_table[(unsigned char)*(ch + 1)] == CC_DIGIT)
                scan_number(lex, &ch, &token);
            else if (*(ch + 1) == '=')
                move_2_set_token(tk_minus_eq)
            else
                ch++;
            break;
        case tk_left_parenth:
            ch++;
            if (*ch == '|') {
                scan_lambda(lex, &ch);
                token = tk_lambda;
            }
            break;
        case tk_bitwise_and:
            ch++;
            if (*ch == '&')
                move_set_token(tk_logical_and)
            else if (*ch == '=')
                move_set_token(tk_bitwise_and_eq)
            break;
        case tk_bitwise_or:
            ch++;
            if (*ch == '|')
                move_set_token(tk_logical_or)
            else if (*ch == '>')
                move_set_token(tk_func_pipe)
            else if (*ch == '=')
                move_set_token(tk_bitwise_or_eq)
            break;
        case tk_gt:
            ch++;
            if (*ch == '>') {
                ch++;
                if (*ch == '=')
                    move_set_token(tk_right_shift_eq)
                else
                    token = tk_right_shift;
                break;
            }
            else if (*ch == '=')
                move_set_token(tk_gt_eq)
            break;
        case tk_lt:
            ch++;
            if (*ch == '<') {
                ch++;
                if (*ch == '=')
                    move_set_token(tk_left_shift_eq)
                else
                    token = tk_left_shift;
                break;
            }
            else if (*ch == '=')
                move_set_token(tk_lt_eq)
            else if (*ch == '[')
                move_set_token(tk_tuple_open)
            break;
        case tk_right_bracket:
            ch++;
            if (*ch == '>')
                move_set_token(tk_tuple_close)
            break;
        case tk_equal:
            ch++;
            if (*ch == '=')
                move_set_token(tk_eq_eq)
            else if (*ch == '>')
                move_set_token(tk_arrow)
            break;
        case CC_AT:
            ch++;
            if (*ch == '(')
                move_set_token(tk_typecast_parenth)
            /* Remember that B is not tk_word in ch_table. */
            else if (IS_IDENT_START((unsigned char)*ch)) {
                token = tk_prop_word;
                goto word_case;
            }
            else
                token = tk_invalid;
            break;
        case CC_QUESTION:
            ch++;
            if (*ch == '>')
                move_set_token(tk_end_tag)
            else
                token = tk_invalid;
            break;
        case CC_CASH:
            ch++;
            if (*ch == '1')
                move_set_token(tk_scoop)
            else
                token = tk_invalid;
            break;
        case tk_invalid:
        default:
            token = tk_invalid;
            break;
    }

    lex->read_cursor = ch;
    lex->token = (lily_token)token;
}

#undef move_set_token
#undef move_2_set_token

int lily_read_manifest_header(lily_lex_state *lex)
{
    int result = strcmp(lex->source, "import manifest\n") == 0;

    /* Parser hasn't pulled a token yet to make sure the header check is at the
       absolute start of the file. Skip the line, but let parser call up the
       token to be consistent with template header reading below. */
    if (result)
        read_line(lex);

    return result;
}

int lily_read_template_header(lily_lex_state *lex)
{
    /* An error is raised if this fails, so don't worry about the cursor going
       too far. */
    lex->read_cursor += 6;
    return strncmp(lex->source, "<?lily", 6) == 0;
}

/* This function loads content outside of the `<?lily ... ?>` tags. This returns
   1 if there is more content, or 0 if the content is done. If 0, the token is
   either a non-EOF value (if there is now Lily code to read), or EOF if the
   source has been exhausted.
   Regardless of the result, *out_buffer is set to the buffer that the lexer
   stored the content into. */
char *lily_read_template_content(lily_lex_state *lex, int *has_more)
{
    char *ch = lex->read_cursor;
    char *buffer = lex->label;
    char *buffer_stop = buffer + lex->label_size - 1;

    if (*ch == '\n' && *has_more == 0) {
        if (read_line(lex)) {
            ch = lex->read_cursor;
            buffer = lex->label;
            buffer_stop = buffer + lex->label_size - 1;
        }
        else {
            lex->token = tk_eof;
            *buffer = '\0';
            *has_more = 0;
            return lex->label;
        }
    }

    while (1) {
        *buffer = *ch;

        if (*ch == '<') {
            if (strncmp(ch, "<?lily", 6) == 0) {
                lex->read_cursor = ch + 6;
                *buffer = '\0';
                *has_more = 0;
                break;
            }
        }
        else if (*ch == '\n') {
            ptrdiff_t offset = buffer - lex->label;

            if (read_line(lex)) {
                ch = lex->read_cursor - 1;
                buffer = lex->label + offset;
                buffer_stop = buffer + lex->label_size - offset - 1;
            }
            else {
                lex->token = tk_eof;

                *buffer = '\0';
                *has_more = 0;

                break;
            }
        }

        ch++;
        buffer++;

        if (buffer == buffer_stop) {
            lex->read_cursor = ch;
            *buffer = '\0';
            *has_more = 1;
            break;
        }
    }

    return lex->label;
}

const char *tokname(lily_token t)
{
    return token_name_table[t];
}

uint8_t lily_priority_for_token(lily_token t)
{
    return priority_table[t];
}

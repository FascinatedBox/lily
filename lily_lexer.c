#include <string.h>
#include <ctype.h>

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
#define CC_G_TWO_LAST   11

#define CC_PLUS         12
#define CC_MINUS        13
#define CC_WORD         14
#define CC_DOUBLE_QUOTE 15
#define CC_NUMBER       16

#define CC_NEWLINE      17
#define CC_SHARP        18
#define CC_STR_NEWLINE  19
#define CC_STR_END      20
#define CC_DOT          21
#define CC_AT           22
#define CC_AMPERSAND    23
#define CC_VBAR         24
#define CC_INVALID      25

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
    tk_equal, tk_lt, tk_gr, tk_not
};

static const lily_token grp_two_eq_table[] =
{
    tk_eq_eq, tk_lt_eq, tk_gr_eq, tk_not_eq
};

/** Lexer init and deletion **/
lily_lex_state *lily_new_lex_state(lily_raiser *raiser)
{
    lily_lex_state *s = lily_malloc(sizeof(lily_lex_state));
    char *ch_class;

    if (s == NULL)
        return NULL;

    s->lex_file = NULL;
    s->filename = NULL;

    /* File will be set by the loader. */
    s->raiser = raiser;

    s->lex_buffer = lily_malloc(128 * sizeof(char));
    s->lex_bufpos = 0;
    s->lex_bufsize = 127;
    s->save_buffer = NULL;

    s->label = lily_malloc(128 * sizeof(char));
    /* This must start at 0 since the line reader will bump it by one. */
    s->line_num = 0;

    ch_class = lily_malloc(256 * sizeof(char));

    if (ch_class == NULL || s->label == NULL || s->lex_buffer == NULL) {
        lily_free(ch_class);
        lily_free(s->label);
        lily_free(s->lex_buffer);
        lily_free(s);
        return NULL;
    }

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
    ch_class[(unsigned char)'&'] = CC_AMPERSAND;
    ch_class[(unsigned char)'|'] = CC_VBAR;
    ch_class[(unsigned char)'['] = CC_LEFT_BRACKET;
    ch_class[(unsigned char)']'] = CC_RIGHT_BRACKET;
    /* Prep for file-based, since lex_buffer isn't NULL. */
    ch_class[(unsigned char)'\r'] = CC_NEWLINE;
    ch_class[(unsigned char)'\n'] = CC_NEWLINE;

    s->ch_class = ch_class;
    return s;
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

/* scan_str
   This handles strings for lily_lexer. This updates the position in lex_buffer
   for lily_lexer. */
static void scan_str(lily_lex_state *lexer, int *pos)
{
    char ch, i, esc_ch;
    char *label, *lex_buffer, *str;
    int label_pos, escape_seen, str_size, word_start, word_pos;
    lily_str_val *sv;

    lex_buffer = lexer->lex_buffer;
    escape_seen = 0;
    /* Where to start cutting from. */
    word_start = *pos + 1;
    /* Where to finish cutting from. */
    word_pos = word_start;

    ch = lex_buffer[word_pos];
    while (ch != '"') {
        if (ch == '\\') {
            if (!escape_seen) {
                label = lexer->label;
                label[0] = '\0';
                label_pos = 0;
                escape_seen = 1;
            }
            /* For escapes, the non-escape part of the data is copied to the
               label, then the escape value is written in. */
            i = word_pos - word_start;
            for (;i > 0;i--, word_start++, label_pos++)
                label[label_pos] = lex_buffer[word_start];

            word_pos++;
            ch = lex_buffer[word_pos];
            esc_ch = simple_escape(ch);
            if (esc_ch == 0)
                lily_raise(lexer->raiser, lily_ErrSyntax,
                           "Invalid escape \\%c\n", ch);

            label[label_pos] = esc_ch;
            label_pos++;

            /* At this point, word_pos is right on the escape character (n in \n
               for example). Adjust word_start so that it doesn't include the
               source escape characters the next time. */
            word_start = word_pos + 1;
            ch = lex_buffer[word_start];
        }
        else if (ch == '\r' || ch == '\n')
            lily_raise(lexer->raiser, lily_ErrSyntax, "Unterminated string.\n");

        word_pos++;
        ch = lex_buffer[word_pos];
    }

    if (!escape_seen)
        str_size = word_pos + 1 - word_start;
    else {
        if (word_pos != word_start) {
            i = word_pos - word_start;
            for (;i > 0;i--,  word_start++, label_pos++)
                label[label_pos] = lex_buffer[word_start];
        }
        /* +1 for the terminator. */
        label_pos++;
        str_size = label_pos;
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

    if (!escape_seen)
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

/* scan_decimal_number
   This handles decimals for lily_lexer. This updates the position in lex_buffer
   for lily_lexer. */
static double scan_decimal_number(char *buffer, int *start)
{
    int i, pos;
    double div, total;

    i = 0;
    pos = *start;
    div = 10.0;
    total = (buffer[pos] - '0') / div;

    pos++;

    while (i < 9 && isdigit(buffer[pos])) {
        div *= 10;
        total += (buffer[pos] - '0') / div;
        i++;
        pos++;
    }

    *start = pos;
    return total;
}

/* scan_whole_number
   This handles the part of a number that's before the decimal. */
static int scan_whole_number(char *buffer, int *start)
{
    int i, pos, total;

    i = 0;
    pos = *start;

    total = buffer[pos] - '0';
    pos++;

    while (i < 9 && isdigit(buffer[pos])) {
        total = (total * 10) + buffer[pos] - '0';
        i++;
        pos++;
    }

    *start = pos;
    return total;
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
            ok = 0;
            break;
        }

        if (i == bufsize - 1) {
            char *new_lb, *new_label;
            bufsize *= 2;

            new_label = lily_realloc(lexer->label, bufsize * sizeof(char));
            new_lb = lily_realloc(lexer->lex_buffer, bufsize * sizeof(char));

            /* Realloc may have resized the block, or free'd the old one and
               returned a new block. This makes sure that things get free'd,
               regardless. */
            if (new_label != NULL)
                lexer->label = new_label;
            if (new_lb != NULL)
                lexer->lex_buffer = new_lb;

            if (new_label == NULL || new_lb == NULL)
                lily_raise_nomem(lexer->raiser);

            lexer->lex_bufsize = bufsize;
        }
        lexer->lex_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            lexer->lex_bufend = i;
            lexer->line_num++;
            ok = 1;
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
    if (lexer->lex_buffer == NULL) {
        /* Change from string-based to file-based. */
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

    if (lexer->save_buffer == NULL) {
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
        else if (group == CC_NEWLINE || group == CC_SHARP) {
            read_line(lexer);
            lex_bufpos = 0;
            continue;
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
        else if (group == CC_NUMBER) {
            int integer_total;
            integer_total = scan_whole_number(lexer->lex_buffer, &lex_bufpos);
            if (lexer->lex_buffer[lex_bufpos] == '.') {
                lex_bufpos++;
                double num_total = scan_decimal_number(lexer->lex_buffer,
                                                       &lex_bufpos);
                num_total += integer_total;
                lexer->value.number = num_total;
                token = tk_number;
            }
            else {
                lexer->value.integer = integer_total;
                token = tk_integer;
            }
        }
        else if (group == CC_DOT) {
            ch = lexer->lex_buffer[lex_bufpos];
            if (ch_class[(unsigned char)ch] == CC_NUMBER) {
                double number_total = scan_decimal_number(lexer->lex_buffer,
                                                          &lex_bufpos);
                lexer->value.number = number_total;
                token = tk_number;
            }
            else {
                lex_bufpos++;
                token = tk_dot;
            }
        }
        else if (group == CC_PLUS) {
            lex_bufpos++;
            token = tk_plus;
        }
        else if (group == CC_MINUS) {
            lex_bufpos++;
            token = tk_minus;
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
     "!", "!=", "+", "-", "a label", "a string", "an integer", "a number", ".",
     "&", "&&", "|", "||", "invalid token", "@>", "end of file"};

    if (t < (sizeof(toknames) / sizeof(toknames[0])))
        return toknames[t];

    return NULL;
}

#include <string.h>
#include <ctype.h>

#include "lily_lexer.h"
#include "lily_impl.h"

static FILE *lex_file;
static char *lex_buffer;
static char *html_buffer;
static int html_bufsize;
static int lex_bufsize;
static int lex_bufpos;
static int lex_bufend;
static int lex_linenum;
static lily_token *lex_token;

static char ch_class[255];
#define CC_INVALID       0
#define CC_WORD          1
#define CC_LEFT_PARENTH  2
#define CC_RIGHT_PARENTH 3
#define CC_DOUBLE_QUOTE  4
#define CC_AT            5
#define CC_NEWLINE       6
#define CC_SHARP         7
#define CC_EQUAL         8
#define CC_NUMBER        9

/* Add a line from the current page into the buffer. */
static int read_line(void)
{
    int ch, i, ok;

    i = 0;

    while (1) {
        ch = fgetc(lex_file);
        if (ch == EOF) {
            ok = 0;
            break;
        }
        lex_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            lex_bufend = i;
            lex_linenum++;
            ok = 1;
            break;
        }
        i++;
    }
    return ok;
}

static char handle_str_escape()
{
    /* lex_bufpos points to the first character in the escape. */
    char ch, ret;
    ch = lex_buffer[lex_bufpos];

    /* Make sure the buffer position stays ahead. */
    lex_bufpos++;

    if (ch == 'n')
        return '\n';
    else if (ch == 'r')
        return '\r';
    else if (ch == 't')
        return '\t';
    else if (ch == '\'')
        return '\'';
    else if (ch == '"')
        return '"';
    else if (ch == '\\')
        return '\\';
    else if (ch == 'b')
        return '\b';
    else if (ch == 'a')
        return '\a';
    else {
        lily_impl_fatal("Unexpected escape char '%c'.\n", ch);

        /* So compilers don't think this can exit without a return. */
        return 0;
    }
}

void lily_lexer_handle_page_data(void)
{
    char c;
    int at_file_end, html_bufpos;

    at_file_end = 0;
    c = lex_buffer[lex_bufpos];
    html_bufpos = 0;

    /* Send html to the server either when unable to hold more or the lily tag
       is found. */
    while (1) {
        lex_bufpos++;
        if (c == '<') {
            if ((lex_bufpos + 4) <= lex_bufend &&
                strncmp(lex_buffer + lex_bufpos, "@lily", 5) == 0) {
                if (html_bufpos != 0) {
                    /* Don't include the '<', because it goes with <@lily. */
                    html_buffer[html_bufpos] = '\0';
                    lily_impl_send_html(html_buffer);
                    html_bufpos = 0;
                }
                lex_bufpos += 5;
                /* Yield control to the lexer. */
                break;
            }
        }
        html_buffer[html_bufpos] = c;
        html_bufpos++;
        if (html_bufpos == (html_bufsize - 1)) {
            html_buffer[html_bufpos] = '\0';
            lily_impl_send_html(html_buffer);
            html_bufpos = 0;
        }

        if (c == '\n' || c == '\r') {
            if (read_line())
                lex_bufpos = 0;
            else {
                at_file_end = 1;
                break;
            }
        }

        c = lex_buffer[lex_bufpos];
    }

    if (html_bufpos != 0) {
        html_buffer[html_bufpos] = '\0';
        lily_impl_send_html(html_buffer);
        html_bufpos = 0;
    }

    if (at_file_end)
        lex_token->tok_type = tk_eof;
}

void lily_init_lexer(char *filename)
{
    lex_file = fopen(filename, "r");
    if (lex_file == NULL)
        lily_impl_fatal("Couldn't open '%s'.\n", filename);

    html_buffer = lily_impl_malloc(1024 * sizeof(char));
    lex_buffer = lily_impl_malloc(1024 * sizeof(char));
    lex_token = lily_impl_malloc(sizeof(lily_token));
    lex_token->word_buffer = lily_impl_malloc(1024 * sizeof(char));

    html_bufsize = 1023;
    lex_bufsize = 1023;
    lex_linenum = 0;
    lex_bufpos = 0;

    /* Initialize ch_class, which is used to determine what 'class' a letter
       is in. */
    memset(ch_class, CC_INVALID, sizeof(ch_class));

    int i;
    for (i = 'a';i <= 'z';i++)
        ch_class[i] = CC_WORD;

    for (i = 'A';i <= 'Z';i++)
        ch_class[i] = CC_WORD;

    for (i = '0';i <= '9';i++)
        ch_class[i] = CC_NUMBER;

    ch_class[(unsigned char)'_'] = CC_WORD;
    ch_class[(unsigned char)'('] = CC_LEFT_PARENTH;
    ch_class[(unsigned char)')'] = CC_RIGHT_PARENTH;
    ch_class[(unsigned char)'"'] = CC_DOUBLE_QUOTE;
    ch_class[(unsigned char)'@'] = CC_AT;
    ch_class[(unsigned char)'\r'] = CC_NEWLINE;
    ch_class[(unsigned char)'\n'] = CC_NEWLINE;
    ch_class[(unsigned char)'#'] = CC_SHARP;
    ch_class[(unsigned char)'='] = CC_EQUAL;

    read_line();
    /* Make sure the lexer starts after the <@lily block. */
    lily_lexer_handle_page_data();
}

lily_token *lily_lexer_token()
{
    return lex_token;
}

void lily_lexer(void)
{
    while (1) {
        char ch;
        int group;

        ch = lex_buffer[lex_bufpos];
        while (ch == ' ' || ch == '\t') {
            lex_bufpos++;
            ch = lex_buffer[lex_bufpos];
        }

        group = ch_class[(unsigned char)ch];

        if (group == CC_WORD) {
            /* The word and line buffers have the same size, plus \n is not a
               valid word character. So, there's no point in checking for
               overflow. */
            int word_pos = 0;
            char *word_buffer = lex_token->word_buffer;
            do {
                word_buffer[word_pos] = ch;
                word_pos++;
                lex_bufpos++;
                ch = lex_buffer[lex_bufpos];
            } while (ch_class[(unsigned char)ch] == CC_WORD);
            word_buffer[word_pos] = '\0';
            lex_token->tok_type = tk_word;
        }
        else if (group == CC_LEFT_PARENTH) {
            lex_bufpos++;
            lex_token->tok_type = tk_left_parenth;
        }
        else if (group == CC_RIGHT_PARENTH) {
            lex_bufpos++;
            lex_token->tok_type = tk_right_parenth;
        }
        else if (group == CC_DOUBLE_QUOTE) {
            /* todo : Allow multiline strings. */
            int word_pos = 0;
            char *word_buffer = lex_token->word_buffer;

            /* Skip opening quote. */
            lex_bufpos++;
            ch = lex_buffer[lex_bufpos];
            do {
                word_buffer[word_pos] = ch;
                word_pos++;
                lex_bufpos++;
                if (ch == '\\')
                    ch = handle_str_escape();

                ch = lex_buffer[lex_bufpos];
            } while (ch != '"' && ch != '\n' && ch != '\r');

            if (ch != '"')
                lily_impl_fatal("String without closure.\n");

            word_buffer[word_pos] = '\0';
            /* ...and the ending one too. */
            lex_bufpos++;
            lex_token->tok_type = tk_double_quote;
        }
        else if (group == CC_AT) {
            lex_bufpos++;
            if (lex_buffer[lex_bufpos] == '>')
                lex_token->tok_type = tk_end_tag;
            else
                lily_impl_fatal("Expected '>' after '@'.\n");
        }
        else if (group == CC_EQUAL) {
            lex_bufpos++;
            lex_token->tok_type = tk_equal;
        }
        else if (group == CC_NUMBER) {
            int i, total;

            i = 0;
            total = lex_buffer[lex_bufpos] - '0';
            lex_bufpos++;

            while (i < 9 && isdigit(lex_buffer[lex_bufpos])) {
                total *= (total * 10) + lex_buffer[lex_bufpos] - '0';
                i++;
                lex_bufpos++;
            }
            lex_token->int_val = total;
            lex_token->tok_type = tk_num_int;
        }
        else if (group == CC_NEWLINE || group == CC_SHARP) {
            read_line();
            lex_bufpos = 0;
            continue;
        }
        else
            lex_token->tok_type = tk_invalid;

        return;
    }
}

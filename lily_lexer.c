#include <string.h>
#include <ctype.h>

#include "lily_interp.h"
#include "lily_lexer.h"
#include "lily_impl.h"

#define CC_INVALID        0
#define CC_WORD           1
#define CC_LEFT_PARENTH   2
#define CC_RIGHT_PARENTH  3
#define CC_DOUBLE_QUOTE   4
#define CC_AT             5
#define CC_NEWLINE        6
#define CC_SHARP          7
#define CC_EQUAL          8
#define CC_NUMBER         9
#define CC_DOT           10

/* Add a line from the current page into the buffer. */
static int read_line(lily_lex_data *lex_data)
{
    int ch, i, ok;
    char *lex_buffer = lex_data->lex_buffer;
    FILE *lex_file = lex_data->lex_file;

    i = 0;

    while (1) {
        ch = fgetc(lex_file);
        if (ch == EOF) {
            ok = 0;
            break;
        }
        lex_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            lex_data->lex_bufend = i;
            lex_data->line_num++;
            ok = 1;
            break;
        }
        i++;
    }
    return ok;
}

static char handle_str_escape(char *buffer, int *pos)
{
    /* lex_bufpos points to the first character in the escape. */
    char ch, ret;
    ch = buffer[*pos];

    /* Make sure the buffer position stays ahead. */
    *pos = *pos + 1;

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

static int scan_whole_number(char *buffer, int *start)
{
    int i, pos, total;

    i = 0;
    pos = *start;

    total = buffer[pos] - '0';
    pos++;

    while (i < 9 && isdigit(buffer[pos])) {
        total *= (total * 10) + buffer[pos] - '0';
        i++;
        pos++;
    }

    *start = pos;
    return total;
}

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

void lily_lexer_handle_page_data(lily_lex_data *lex_data)
{
    char c;
    char *lex_buffer, *html_cache;
    int at_file_end, html_bufsize, lbp, htmlp;

    /* htmlp and lbp are used so it's obvious they aren't globals. */
    lex_buffer = lex_data->lex_buffer;
    html_cache = lex_data->html_cache;
    lbp = lex_data->lex_bufpos;
    at_file_end = 0;
    c = lex_buffer[lbp];
    htmlp = 0;
    html_bufsize = lex_data->cache_size;

    /* Send html to the server either when unable to hold more or the lily tag
       is found. */
    while (1) {
        lbp++;
        if (c == '<') {
            if ((lbp + 4) <= lex_data->lex_bufend &&
                strncmp(lex_buffer + lbp, "@lily", 5) == 0) {
                if (htmlp != 0) {
                    /* Don't include the '<', because it goes with <@lily. */
                    html_cache[htmlp] = '\0';
                    lily_impl_send_html(html_cache);
                    htmlp = 0;
                }
                lbp += 5;
                /* Yield control to the lexer. */
                break;
            }
        }
        html_cache[htmlp] = c;
        htmlp++;
        if (htmlp == (html_bufsize - 1)) {
            html_cache[htmlp] = '\0';
            lily_impl_send_html(html_cache);
            htmlp = 0;
        }

        if (c == '\n' || c == '\r') {
            if (read_line(lex_data))
                lbp = 0;
            else {
                at_file_end = 1;
                break;
            }
        }

        c = lex_buffer[lbp];
    }

    if (htmlp != 0) {
        html_cache[htmlp] = '\0';
        lily_impl_send_html(html_cache);
        htmlp = 0;
    }

    if (at_file_end)
        lex_data->token->tok_type = tk_eof;

    lex_data->lex_bufpos = lbp;
}

void lily_include(lily_lex_data *lex_data, char *filename)
{
    FILE *lex_file = fopen(filename, "r");
    if (lex_file == NULL)
        lily_impl_fatal("Failed to open %s.\n", filename);

    lex_data->lex_file = lex_file;

    read_line(lex_data);
    /* Make sure the lexer starts after the <@lily block. */
    lily_lexer_handle_page_data(lex_data);
}

void lily_init_lexer(lily_interp *interp)
{
    lily_lex_data *lex_data = lily_impl_malloc(sizeof(lily_lex_data));
    lex_data->html_cache = lily_impl_malloc(1024 * sizeof(char));
    lex_data->cache_size = 1023;

    lex_data->lex_buffer = lily_impl_malloc(1024 * sizeof(char));
    lex_data->lex_bufpos = 0;
    lex_data->lex_bufsize = 1023;

    lex_data->token = lily_impl_malloc(sizeof(lily_token));
    lex_data->token->word_buffer = lily_impl_malloc(1024 * sizeof(char));

    char *ch_class = lily_impl_malloc(256 * sizeof(char));

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

    ch_class[(unsigned char)'_'] = CC_WORD;
    ch_class[(unsigned char)'('] = CC_LEFT_PARENTH;
    ch_class[(unsigned char)')'] = CC_RIGHT_PARENTH;
    ch_class[(unsigned char)'"'] = CC_DOUBLE_QUOTE;
    ch_class[(unsigned char)'@'] = CC_AT;
    ch_class[(unsigned char)'\r'] = CC_NEWLINE;
    ch_class[(unsigned char)'\n'] = CC_NEWLINE;
    ch_class[(unsigned char)'#'] = CC_SHARP;
    ch_class[(unsigned char)'='] = CC_EQUAL;
    ch_class[(unsigned char)'.'] = CC_DOT;

    lex_data->ch_class = ch_class;
    interp->lex_data = lex_data;
}

void lily_lexer(lily_lex_data *lex_data)
{
    char *ch_class, *lex_buffer;
    int lex_bufpos = lex_data->lex_bufpos;
    lily_token *lex_token = lex_data->token;

    ch_class = lex_data->ch_class;
    lex_buffer = lex_data->lex_buffer;

    while (1) {
        char ch;
        int group;

        ch = lex_buffer[lex_bufpos];
        while (ch == ' ' || ch == '\t') {
            lex_bufpos++;
            ch = lex_buffer[lex_bufpos];
        }

        group = lex_data->ch_class[(unsigned char)ch];

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
                    ch = handle_str_escape(lex_buffer, &lex_bufpos);

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
            int int_total;
            int_total = scan_whole_number(lex_buffer, &lex_bufpos);
            if (lex_buffer[lex_bufpos] == '.') {
                lex_bufpos++;
                double dbl_total = scan_decimal_number(lex_buffer, &lex_bufpos);
                dbl_total += int_total;
                lex_token->dbl_val = dbl_total;
                lex_token->tok_type = tk_num_dbl;
            }
            else {
                lex_token->int_val = int_total;
                lex_token->tok_type = tk_num_int;
            }
        }
        else if (group == CC_DOT) {
            double dbl_total = scan_decimal_number(lex_buffer, &lex_bufpos);
            lex_token->dbl_val = dbl_total;
            lex_token->tok_type = tk_num_dbl;
        }
        else if (group == CC_NEWLINE || group == CC_SHARP) {
            read_line(lex_data);
            lex_bufpos = 0;
            continue;
        }
        else
            lex_token->tok_type = tk_invalid;

        lex_data->lex_bufpos = lex_bufpos;
        return;
    }
}

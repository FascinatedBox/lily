#include <string.h>
#include <ctype.h>

#include "lily_impl.h"
#include "lily_lexer.h"

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
#define CC_COMMA         11
#define CC_PLUS          12
#define CC_MINUS         13
#define CC_STR_NEWLINE   14
#define CC_STR_END       15

static int handle_str_escape(char *buffer, int *pos, char *ch)
{
    /* lex_bufpos points to the first character in the escape. */
    char testch;
    int ret;

    testch = buffer[*pos];
    ret = 1;

    /* Make sure the buffer position stays ahead. */
    *pos = *pos + 1;

    if (testch == 'n')
        *ch = '\n';
    else if (testch == 'r')
        *ch = '\r';
    else if (testch == 't')
        *ch = '\t';
    else if (testch == '\'')
        *ch = '\'';
    else if (testch == '"')
        *ch = '"';
    else if (testch == '\\')
        *ch = '\\';
    else if (testch == 'b')
        *ch = '\b';
    else if (testch == 'a')
        *ch = '\a';
    else
        ret = 0;

    return ret;
}

/* Add a line from the current page into the buffer. */
static int read_line(lily_lex_state *lexer)
{
    int ch, i, ok;
    char *lex_buffer = lexer->lex_buffer;
    FILE *lex_file = lexer->lex_file;

    i = 0;

    while (1) {
        ch = fgetc(lex_file);
        if (ch == EOF) {
            ok = 0;
            break;
        }
        lex_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            lexer->lex_bufend = i;
            lexer->line_num++;
            ok = 1;
            break;
        }
        i++;
    }
    return ok;
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

void lily_free_lex_state(lily_lex_state *lex)
{
    if (lex->lex_file != NULL)
        fclose(lex->lex_file);

    lily_free(lex->html_cache);

    if (lex->save_buffer == NULL)
        lily_free(lex->lex_buffer);
    else
        lily_free(lex->save_buffer);

    lily_free(lex->ch_class);
    lily_free(lex->label);
    lily_free(lex);
}

void lily_load_file(lily_lex_state *lexer, char *filename)
{
    if (lexer->lex_buffer == NULL) {
        /* Change from string-based to file-based. */
        lexer->lex_buffer = lexer->save_buffer;
        lexer->save_buffer = NULL;
        lexer->ch_class[(unsigned char)'\r'] = CC_NEWLINE;
        lexer->ch_class[(unsigned char)'\n'] = CC_NEWLINE;
        lexer->ch_class[0] = CC_INVALID;
    }

    FILE *lex_file = fopen(filename, "r");
    if (lex_file == NULL) {
        lexer->filename = NULL;
        lily_raise(lexer->error, "Failed to open %s.\n", filename);
    }

    lexer->filename = filename;
    lexer->lex_file = lex_file;
    lexer->line_num = 1;

    read_line(lexer);
    /* Make sure the lexer starts after the <@lily block. */
    lily_lexer_handle_page_data(lexer);
}

void lily_lexer(lily_lex_state *lexer)
{
    char *ch_class, *lex_buffer;
    int lex_bufpos = lexer->lex_bufpos;
    lily_token token;

    ch_class = lexer->ch_class;
    lex_buffer = lexer->lex_buffer;

    while (1) {
        char ch;
        int group;

        ch = lex_buffer[lex_bufpos];
        while (ch == ' ' || ch == '\t') {
            lex_bufpos++;
            ch = lex_buffer[lex_bufpos];
        }

        group = lexer->ch_class[(unsigned char)ch];

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
                ch = lex_buffer[lex_bufpos];
            } while (ch_class[(unsigned char)ch] == CC_WORD);
            label[word_pos] = '\0';
            token = tk_word;
        }
        else if (group == CC_LEFT_PARENTH) {
            lex_bufpos++;
            token = tk_left_parenth;
        }
        else if (group == CC_RIGHT_PARENTH) {
            lex_bufpos++;
            token = tk_right_parenth;
        }
        else if (group == CC_DOUBLE_QUOTE) {
            /* todo : Allow multiline strings. */
            int word_pos = 0;
            char *label = lexer->label;

            /* Skip opening quote. */
            lex_bufpos++;
            ch = lex_buffer[lex_bufpos];
            do {
                label[word_pos] = ch;
                word_pos++;
                lex_bufpos++;
                if (ch == '\\') {
                    if (handle_str_escape(lex_buffer, &lex_bufpos, &ch) == 0)
                        lily_raise(lexer->error, "Invalid escape code.\n");
                }

                ch = lex_buffer[lex_bufpos];
            } while (ch != '"' && ch != '\n' && ch != '\r');

            if (ch != '"')
                lily_raise(lexer->error, "String without closure.\n");

            label[word_pos] = '\0';

            /* Prepare the string for the parser. */
            lily_strval *sv = lily_malloc(sizeof(lily_strval));
            if (sv == NULL)
                lily_raise_nomem(lexer->error);

            char *str = lily_malloc(word_pos + 1);
            if (str == NULL) {
                lily_free(sv);
                lily_raise_nomem(lexer->error);
            }

            strcpy(str, label);
            sv->str = str;
            sv->size = word_pos;
            lexer->value.ptr = sv;

            /* ...and the ending one too. */
            lex_bufpos++;
            token = tk_double_quote;
        }
        else if (group == CC_AT) {
            lex_bufpos++;
            /* Disable @> for string-based. */
            if (lex_buffer[lex_bufpos] == '>' && lexer->save_buffer == NULL)
                token = tk_end_tag;
            else
                lily_raise(lexer->error, "Expected '>' after '@'.\n");
        }
        else if (group == CC_EQUAL) {
            lex_bufpos++;
            token = tk_equal;
        }
        else if (group == CC_NUMBER) {
            int integer_total;
            integer_total = scan_whole_number(lex_buffer, &lex_bufpos);
            if (lex_buffer[lex_bufpos] == '.') {
                lex_bufpos++;
                double num_total = scan_decimal_number(lex_buffer, &lex_bufpos);
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
            double number_total = scan_decimal_number(lex_buffer, &lex_bufpos);
            lexer->value.number = number_total;
            token = tk_number;
        }
        else if (group == CC_COMMA) {
            lex_bufpos++;
            token = tk_comma;
        }
        else if (group == CC_PLUS) {
            lex_bufpos++;
            token = tk_plus;
        }
        else if (group == CC_MINUS) {
            lex_bufpos++;
            token = tk_minus;
        }
        else if (group == CC_NEWLINE || group == CC_SHARP) {
            read_line(lexer);
            lex_bufpos = 0;
            continue;
        }
        else if (group == CC_STR_NEWLINE) {
            /* This catches both \r and \n. Make sure that \r\n comes in as one
               newline though. */
            if (ch == '\r' && lex_buffer[lex_bufpos+1] == '\n')
                lex_bufpos += 2;
            else
                lex_bufpos++;

            lexer->line_num++;
            continue;
        }
        else if (group == CC_STR_END)
            token = tk_eof;
        else
            token = tk_invalid;

        lexer->lex_bufpos = lex_bufpos;
        lexer->token = token;
        return;
    }
}

void lily_lexer_handle_page_data(lily_lex_state *lexer)
{
    char c;
    char *lex_buffer, *html_cache;
    int at_file_end, html_bufsize, lbp, htmlp;

    /* htmlp and lbp are used so it's obvious they aren't globals. */
    lex_buffer = lexer->lex_buffer;
    html_cache = lexer->html_cache;
    lbp = lexer->lex_bufpos;
    at_file_end = 0;
    c = lex_buffer[lbp];
    htmlp = 0;
    html_bufsize = lexer->cache_size;

    /* Send html to the server either when unable to hold more or the lily tag
       is found. */
    while (1) {
        lbp++;
        if (c == '<') {
            if ((lbp + 4) <= lexer->lex_bufend &&
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
            if (read_line(lexer))
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
        lexer->token = tk_eof;

    lexer->lex_bufpos = lbp;
}

/* Caller is responsible for free'ing the str. */
void lily_load_str(lily_lex_state *lexer, char *str)
{
    if (lexer->save_buffer == NULL) {
        lexer->save_buffer = lexer->lex_buffer;
        lexer->ch_class[(unsigned char)'\r'] = CC_STR_NEWLINE;
        lexer->ch_class[(unsigned char)'\n'] = CC_STR_NEWLINE;
        lexer->ch_class[0] = CC_STR_END;
        lexer->lex_file = NULL;
        lexer->lex_bufsize = strlen(str);
    }

    /* Line number isn't set, to allow for repl-like use. */
    lexer->lex_buffer = str;

    lexer->filename = "<str>";

    /* String-based has no support for the html skipping, because I can't see
       that being useful. */
}

lily_lex_state *lily_new_lex_state(lily_excep_data *excep_data)
{
    lily_lex_state *s = lily_malloc(sizeof(lily_lex_state));
    char *ch_class;

    if (s == NULL)
        return NULL;

    s->filename = NULL;

    /* File will be set by the loader. */
    s->error = excep_data;
    s->html_cache = lily_malloc(1024 * sizeof(char));
    s->cache_size = 1023;

    s->lex_buffer = lily_malloc(1024 * sizeof(char));
    s->lex_bufpos = 0;
    s->lex_bufsize = 1023;
    s->save_buffer = NULL;

    s->label = lily_malloc(1024 * sizeof(char));
    s->line_num = 1;

    ch_class = lily_malloc(256 * sizeof(char));

    if (ch_class == NULL || s->label == NULL || s->lex_buffer == NULL ||
        s->html_cache == NULL) {
        lily_free(ch_class);
        lily_free(s->label);
        lily_free(s->lex_buffer);
        lily_free(s->html_cache);
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
    /* Prep for file-based, since lex_buffer isn't NULL. */
    ch_class[(unsigned char)'\r'] = CC_NEWLINE;
    ch_class[(unsigned char)'\n'] = CC_NEWLINE;

    s->ch_class = ch_class;
    return s;
}

char *tokname(lily_token t)
{
    static char *toknames[] =
    {"invalid token", "a label", "(", ")", "a string", "an integer", "a number",
     "=", ",", "+", "-", "@>", "end of file"};

    if (t < (sizeof(toknames) / sizeof(toknames[0])))
        return toknames[t];

    return NULL;
}

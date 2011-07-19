#include <stdlib.h>
#include <string.h>

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
#define CC_INVALID  0
#define CC_WORD     1

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

static void handle_page_data()
{
    int html_bufpos = 0;

    char c = lex_buffer[lex_bufpos];
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
            else
                break;
        }

        c = lex_buffer[lex_bufpos];
    }

    if (html_bufpos != 0) {
        html_buffer[html_bufpos] = '\0';
        lily_impl_send_html(html_buffer);
        html_bufpos = 0;
    }
}

void lily_init_lexer(char *filename)
{
    lex_file = fopen(filename, "r");
    if (lex_file == NULL)
        lily_impl_fatal("Couldn't open '%s'.\n", filename);

    html_buffer = malloc(1024 * sizeof(char));
    if (html_buffer == NULL)
        lily_impl_fatal("No memory to init html buffer.\n");

    lex_buffer = malloc(1024 * sizeof(char));
    if (lex_buffer == NULL)
        lily_impl_fatal("No memory to init lexer.\n");

    lex_token = malloc(sizeof(lily_token));
    if (lex_token == NULL)
        lily_impl_fatal("No memory to create lexer token.\n");

    lex_token->word_buffer = malloc(1024 * sizeof(char));
    if (lex_token->word_buffer == NULL)
        lily_impl_fatal("No memory to create lexer token buffer.\n");

    html_bufsize = 1023;
    lex_bufsize = 1023;
    lex_linenum = 0;
    lex_bufpos = 0;

    /* Initialize ch_class, which is used to determine what 'class' a letter
       is in. */
    memset(ch_class, CC_INVALID, sizeof(ch_class));

    int i;
    for (i = 'a';i < 'z';i++)
        ch_class[i] = CC_WORD;

    for (i = 'A';i < 'Z';i++)
        ch_class[i] = CC_WORD;

    ch_class[(unsigned char)'_'] = CC_WORD;

    read_line();
    /* Make sure the lexer starts after the <@lily block. */
    handle_page_data();
}

lily_token *lily_lexer_token()
{
    return lex_token;
}

void lily_lexer(void)
{
    char ch;
    int group;

    ch = lex_buffer[lex_bufpos];
    while (ch == ' ' || ch == '\t') {
        lex_bufpos++;
        ch = lex_buffer[lex_bufpos];
    }

    group = ch_class[(unsigned char)ch];

    if (group == CC_WORD) {
        /* The word and line buffers have the same size, plus \n is not a valid
           word character. So, there's no point in checking for overflow. */
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
    else
        lex_token->tok_type = tk_invalid;
}

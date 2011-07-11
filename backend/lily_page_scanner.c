#include <stdio.h>
#include <stdlib.h>

void lily_be_send_html(char *);
void lily_be_fatal(char *, ...);

static FILE *page;
static char *html_buffer;
static int html_bufsize;
static char *line_buffer;
static int line_buffer_end;
static int line_pos;
static int page_lineno = 0;

/* Add a line from the current page into the buffer. */
static int read_page_line(void)
{
    int ch, i, ok;

    i = 0;

    while (1) {
        ch = fgetc(page);
        if (ch == EOF) {
            ok = 0;
            break;
        }
        line_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            line_buffer_end = i;
            page_lineno++;
            ok = 1;
            break;
        }
        i++;
    }
    return ok;
}

void lily_page_scanner_init(char *filename)
{
    page = fopen(filename, "r");
    if (page == NULL)
        lily_be_fatal("Can't open %s.\n", filename);

    html_buffer = malloc(1028 * sizeof(char));
    html_bufsize = 1027;
    if (html_buffer == NULL)
        lily_be_fatal("No memory for html buffer.");

    line_buffer = malloc(1028 * sizeof(char));
    if (line_buffer == NULL)
        lily_be_fatal("No memory for line buffer.");

    read_page_line();
}

void lily_page_scanner(void)
{
    int html_bufpos = 0;
    int line_bufpos = 0;

    char c = line_buffer[line_pos];
    /* Send html to the server either when unable to hold more or the lily tag
       is found. */
    while (1) {
        line_bufpos++;
        if (c == '<') {
            if ((line_bufpos + 4) <= line_buffer_end &&
                strncmp(line_buffer + line_bufpos, "@lily", 5) == 0) {
                if (html_bufpos != 0) {
                    /* Don't include the '<', because it goes with <@lily. */
                    html_buffer[html_bufpos] = '\0';
                    lily_be_send_html(html_buffer);
                    html_bufpos = 0;
                }
                /* Todo : Parse+Execute lily code. This should finish with the
                 * indexes updated so lily code and html code are not mixed. */
            }
        }
        html_buffer[html_bufpos] = c;
        html_bufpos++;
        if (html_bufpos == (html_bufsize - 1)) {
            html_buffer[html_bufpos] = '\0';
            lily_be_send_html(html_buffer);
            html_bufpos = 0;
        }

        if (c == '\n' || c == '\r') {
            if (read_page_line())
                line_bufpos = 0;
            else
                break;
        }

        c = line_buffer[line_bufpos];
    }

    if (html_bufpos != 0) {
        html_buffer[html_bufpos] = '\0';
        lily_be_send_html(html_buffer);
        html_bufpos = 0;
    }
}

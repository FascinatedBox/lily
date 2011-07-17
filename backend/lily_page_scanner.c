#include <stdio.h>
#include <stdlib.h>

#include "lily_page_scanner.h"

/* Note : The page and line data are shared with lily_lexer.c */
static FILE *scan_file;
static char *html_buffer;
static int html_bufsize;
static char *scan_buffer;
static int scan_bufsize;
static int scan_bufend;
static int scan_bufpos = 0;
static int scan_lineno = 0;

/* Add a line from the current page into the buffer. */
static int read_page_line(void)
{
    int ch, i, ok;

    i = 0;

    while (1) {
        ch = fgetc(scan_file);
        if (ch == EOF) {
            ok = 0;
            break;
        }
        scan_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            scan_bufend = i;
            scan_lineno++;
            ok = 1;
            break;
        }
        i++;
    }
    return ok;
}

void lily_init_page_scanner(lily_page_data *d)
{
    scan_file = d->lex_file;
    scan_buffer = d->lex_buffer;
    scan_bufsize = *d->lex_bufsize;
    scan_bufpos = *d->lex_bufpos;

    html_buffer = malloc(1024 * sizeof(char));
    html_bufsize = 1023;
    if (html_buffer == NULL) {
        lily_impl_fatal("No memory for html buffer.");
        return;
    }

    read_page_line();
}

void lily_page_scanner(void)
{
    int html_bufpos = 0;

    char c = scan_buffer[scan_bufpos];
    /* Send html to the server either when unable to hold more or the lily tag
       is found. */
    while (1) {
        scan_bufpos++;
        if (c == '<') {
            if ((scan_bufpos + 4) <= scan_bufend &&
                strncmp(scan_buffer + scan_bufpos, "@lily", 5) == 0) {
                if (html_bufpos != 0) {
                    /* Don't include the '<', because it goes with <@lily. */
                    html_buffer[html_bufpos] = '\0';
                    lily_impl_send_html(html_buffer);
                    html_bufpos = 0;
                }
                /* Yield to the parser. */
                return;
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
            if (read_page_line())
                scan_bufpos = 0;
            else
                break;
        }

        c = scan_buffer[scan_bufpos];
    }

    if (html_bufpos != 0) {
        html_buffer[html_bufpos] = '\0';
        lily_impl_send_html(html_buffer);
        html_bufpos = 0;
    }
}

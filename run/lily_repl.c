#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lily_alloc.h"
#include "lily_parser.h"

#include "lily_api_options.h"

/*  lily_repl.c
    This implements a REPL for Lily. */

void lily_impl_puts(void *data, char *text)
{
    fputs(text, stdout);
}

int main(int argc, char **argv)
{
    lily_options *options = lily_new_default_options();

    lily_parse_state *parser = lily_new_parse_state(options);
    char scan_buffer[2048];
    int scan_pos = 0;
    fputs("Lily v0.16, made by Jesse Ray Adkins.\n> ", stdout);

    while (1) {
        char ch = fgetc(stdin);
        if (ch == '\n') {
            scan_buffer[scan_pos] = '\0';

            if (scan_pos == 4 && strcmp(scan_buffer, "quit") == 0)
                break;

            if (lily_parse_chunk(parser, scan_buffer) == 0)
                fputs(parser->raiser->msgbuf->message, stdout);

            fputs("> ", stdout);
            scan_pos = 0;
        }
        else {
            scan_buffer[scan_pos] = ch;
            scan_pos++;
        }
    }

    exit(EXIT_SUCCESS);
}

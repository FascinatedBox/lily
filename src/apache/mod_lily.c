/*  mod_lily.c
    This is an apache binding for the Lily language. */

#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "apr_file_io.h"

#include "lily_parser.h"
#include "lily_lexer.h"

void lily_impl_puts(void *data, char *text)
{
    ap_rputs(text, (request_rec *)data);
}

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

static int apache_read_line_fn(lily_lex_entry *entry)
{
    char ch;
    int bufsize, followers, i, ok;
    lily_lex_state *lexer = entry->lexer;
    char *input_buffer = lexer->input_buffer;
    apr_file_t *input_file = (apr_file_t *)entry->source;
    apr_status_t result;
    bufsize = lexer->input_size;
    i = 0;

    while (1) {
        result = apr_file_getc(&ch, input_file);
        if (result != APR_SUCCESS) {
            if ((i + 1) == bufsize) {
                lily_grow_lexer_buffers(lexer);

                input_buffer = lexer->input_buffer;
            }

            lexer->input_buffer[i] = '\n';
            lexer->input_end = i + 1;
            /* If i is 0, then nothing is on this line except eof. Return 0 to
               let the caller know this is the end.
             * If it isn't, then there's stuff with an unexpected eof at the
               end. Return 1, then 0 the next time. */
            ok = !!i;
            /* Make sure the second eof found does not increment the line
               number again. */
            if (lexer->hit_eof == 0) {
                lexer->line_num++;
                lexer->hit_eof = 1;
            }
            break;
        }

        /* i + 2 is used so that when \r\n that the \n can be safely added
           to the buffer. Otherwise, it would be i + 1. */
        if ((i + 2) == bufsize) {
            lily_grow_lexer_buffers(lexer);
            /* Do this in case the realloc decides to use a different block
               instead of growing what it had. */
            input_buffer = lexer->input_buffer;
        }

        lexer->input_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            lexer->input_end = i;
            lexer->line_num++;
            ok = 1;

            if (ch == '\r') {
                apr_file_getc(&ch, input_file);
                if (ch != '\n')
                    apr_file_ungetc(ch, input_file);
                else {
                    /* This is safe: See i + 2 == size comment above. */
                    lexer->input_buffer[i+1] = ch;
                    lexer->input_end++;
                }
            }
            break;
        }
        else if (ch > 127) {
            followers = follower_table[(unsigned int)ch];
            if (followers >= 2) {
                int j;
                i++;
                for (j = 1;j < followers;j++,i++) {
                    result = apr_file_getc(&ch, input_file);
                    if ((unsigned char)ch < 128 || result != APR_SUCCESS) {
                        lily_raise(lexer->raiser, lily_ErrEncoding,
                                   "Invalid utf-8 sequence on line %d.\n",
                                   lexer->line_num);
                    }
                    input_buffer[i] = ch;
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

static void apache_close_fn(lily_lex_entry *entry)
{
    apr_file_close(entry->source);
}

/* The sample content handler */
static int lily_handler(request_rec *r)
{
    if (strcmp(r->handler, "lily"))
        return DECLINED;

    r->content_type = "text/html";

    apr_file_t *lily_file;
    apr_status_t result;
    result = apr_file_open(&lily_file, r->filename, APR_READ, APR_OS_DEFAULT,
            r->pool);

    /* File not found? Give up now. */
    if (result != APR_SUCCESS)
        return DECLINED;

    lily_parse_state *parser = lily_new_parse_state(r, 0, NULL);
    if (parser == NULL)
        return DECLINED;

    lily_parse_special(parser, lily_file, lm_from_file, r->filename,
        apache_read_line_fn, apache_close_fn);

    lily_free_parse_state(parser);

    return OK;
}

static void lily_register_hooks(apr_pool_t *p)
{
    ap_hook_handler(lily_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA lily_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                  /* create per-dir    config structures */
    NULL,                  /* merge  per-dir    config structures */
    NULL,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
    NULL,                  /* table of config file commands       */
    lily_register_hooks    /* register hooks                      */
};


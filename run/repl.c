#include <string.h>

#include "lily.h"
#include "lily_parser.h"

int has_quit = 0;
lily_msgbuf *line_buffer = NULL;
const char *prompts[] = {"@=> ", "*=> "};
#define LILY_VERSION "v" LILY_MAJOR "." LILY_MINOR
#define LILY_TIMESTAMP " (" __DATE__ ", " __TIME__ ")"
#define MAX_PER_LINE 512
#define REPL_NAME "[repl]"

static char *prompt_get_line(char *buffer, int continuing)
{
    const char *prompt = prompts[continuing];

    fputs(prompt, stdout);
    fflush(stdout);
    return fgets(buffer, MAX_PER_LINE, stdin);
}

static int starts_with(const char *str, const char *with)
{
    return strncmp(str, with, strlen(with)) == 0;
}

static int line_is_exit(const char *line)
{
    while (*line && *line == ' ')
        line++;

    int match = strncmp(line, "exit", 4) == 0;

    if (match == 0)
        return 0;

    line += 4;

    while (*line && *line == ' ')
        line++;

    return (*line == '\n');
}

/* Did the last parse fail because it was incomplete? */
int is_complete(lily_state *s)
{
    lily_error_source source = s->raiser->source;

    if (source != err_from_parse &&
        source != err_from_lex)
        /* Emitter, vm, or raw error. These cannot be recovered from. */
        return 1;

    const char *message = lily_error_message(s);

    /* Parser and lexer errors always start with this. */
    message += strlen("SyntaxError: ");

    if (starts_with(message, "Unterminated"))
        /* It's a lambda or string that never finished. */
        return 0;

    if (starts_with(message, "Cannot import"))
        return 1;

    /* If it finished on eof, not complete (keep feeding it more).
       Otherwise stop since there's a syntax error. */
    return (s->gs->parser->lex->token != tk_eof);
}

void content_loop(lily_state *s, const char **output)
{
    char text_from_prompt[MAX_PER_LINE];
    char *line;
    const char *content_to_load = NULL;
    int continuing = 0;

    lily_mb_flush(line_buffer);
    *output = NULL;

    while (1) {
        line = prompt_get_line(text_from_prompt, continuing);

        if (line == NULL) {
            /* Ctrl-D on *nix. Stop the interpreter. */
            *output = NULL;

            /* Leave the console how Ctrl-C leaves it. */
            putc('\n', stdout);
            has_quit = 1;
            break;
        }

        if (continuing == 0)
            content_to_load = text_from_prompt;
        else {
            lily_mb_add(line_buffer, text_from_prompt);
            content_to_load = lily_mb_raw(line_buffer);
        }

        lily_load_string(s, REPL_NAME, content_to_load);

        int result = lily_parse_expr(s, output);

        if (result)
            break;

        /* Content must be loaded again or parsing will immediately bail. */
        lily_load_string(s, REPL_NAME, content_to_load);
        result = lily_parse_content(s);

        if (result == 0) {
            if (continuing == 0) {
                if (line_is_exit(text_from_prompt)) {
                    has_quit = 1;
                    *output = NULL;
                    break;
                }
            }

            if (is_complete(s) == 0) {
                if (continuing == 0)
                    /* Only the first pass skips adding the line. */
                    lily_mb_add(line_buffer, text_from_prompt);

                lily_mb_add_char(line_buffer, '\n');
                continuing = 1;
                continue;
            }

            *output = lily_error_message(s);
            break;
        }

        *output = NULL;
        break;
    }
}

uint8_t lily_repl(lily_state *s)
{
    const char *output = NULL;
    uint8_t exit_code;

    line_buffer = lily_new_msgbuf(64);
    fputs("Lily " LILY_VERSION LILY_TIMESTAMP "\n", stdout);

    while (1) {
        content_loop(s, &output);
        lily_mb_flush(line_buffer);

        if (output && *output)
            fputs(output, stdout);

        if (has_quit || lily_has_exited(s)) {
            exit_code = lily_exit_code(s);
            break;
        }
    }

    lily_free_msgbuf(line_buffer);
    lily_free_state(s);
    return exit_code;
}

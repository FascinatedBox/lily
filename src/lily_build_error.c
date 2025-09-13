#include <string.h>

#include "lily_parser.h"

/* This file handles building error messages that are shown to the user. */

static const char *error_class_name(lily_raiser *raiser)
{
    const char *result;

    switch (raiser->source) {
        case err_from_vm:
            result = raiser->error_class->name;
            break;
        case err_from_parse:
        case err_from_emit:
        case err_from_lex:
            result = "SyntaxError";
            break;
        default:
            result = "Error";
            break;
    }

    return result;
}

static void add_error_header(lily_msgbuf *msgbuf, lily_raiser *raiser)
{
    const char *name = error_class_name(raiser);

    lily_mb_add(msgbuf, name);

    const char *message = lily_mb_raw(raiser->msgbuf);

    if (message[0] != '\0')
        lily_mb_add_fmt(msgbuf, ": %s\n", message);
    else
        lily_mb_add_char(msgbuf, '\n');
}

static int can_show_context(lily_parse_state *parser, lily_error_source es,
        uint16_t line_num)
{
    if (es == err_from_lex) {
        /* Caller says don't bother with context, it won't be useful here. Lexer
           does this for unterminated tokens. */
        if (parser->lex->token_start == NULL)
            return 0;
    }

    if (es != err_from_lex &&
        es != err_from_parse)
        return 0;

    lily_lex_state *lex = parser->lex;

    if (lex->line_num != line_num)
        return 0;
    else if (parser->emit->scope_block->block_type == block_lambda)
        /* Lexer scans lambdas as a large token so parser can jump back in when
           emitter has type information. It's possible to get trace for these,
           but it's tricky. Eventually, but not yet. */
        return 0;

    return 1;
}

static void fill_line_info(char *line_num_str, char *pipe_space,
        uint16_t line_num)
{
    snprintf(line_num_str, 8, "%d", line_num);

    size_t len = strlen(line_num_str) + 1;

    line_num_str[len] = '\0';
    pipe_space[len] = '\0';
}

static char *find_source_start(lily_lex_state *lex)
{
    char *source = lex->source;

    /* Leading whitespace isn't interesting, so skip past it. */
    while (*source == ' ' || *source == '\t')
        source++;

    /* This can happen if a header scan (ex: 'import manifest') fails because of
       space at the front. */
    if (source > lex->token_start)
        source = lex->token_start;

    return source;
}

static int is_source_useful(char *source)
{
    int result = 1;

    if (*source == '\n')
        /* It's an empty line. Don't bother. */
        result = 0;
    else if (*source == '}' && *(source + 1) == '\n')
        /* A match didn't complete. A define didn't return. Pointing to the
           lonely closing brace isn't worth it. */
        result = 0;

    return result;
}

static void add_context(lily_msgbuf *msgbuf,
        lily_parse_state *parser, uint16_t line_num)
{
    lily_lex_state *lex = parser->lex;
    char pipe_space[] = "        ";
    char line_num_str[16];

    fill_line_info(line_num_str, pipe_space, line_num);

    /* Lexer's source always ends with \n\0. */
    char *line = find_source_start(lex);

    if (is_source_useful(line) == 0)
        return;

    char *source_end = lex->token_start;

    /* The context should look like this:

        ```
        | SyntaxError: ...
        |
        |    |
        | 12 | var v = ?
        |    |         ^
        |
        | from somefile.lily:12
        ```
    */

    lily_mb_add_fmt(msgbuf, "\n"
                            "%s |\n"
                            " %d | %s"
                            "%s |", pipe_space, line_num, line, pipe_space);
    lily_mb_repeat_n(msgbuf, ' ', (int)(source_end - line + 1));
    lily_mb_add(msgbuf, "^\n\n");
}

static void add_frontend_trace(lily_msgbuf *msgbuf, lily_parse_state *parser)
{
    uint16_t line_num = parser->lex->line_num;
    lily_error_source es = parser->raiser->source;

    if (es == err_from_emit)
        line_num = parser->raiser->error_ast->line_num;

    if (can_show_context(parser, es, line_num))
        add_context(msgbuf, parser, line_num);

    lily_mb_add_fmt(msgbuf, "    from %s:%d:\n",
            parser->symtab->active_module->path, line_num);
}

static void add_vm_trace(lily_msgbuf *msgbuf, lily_parse_state *parser)
{
    lily_call_frame *frame = parser->vm->call_chain;

    lily_mb_add(msgbuf, "Traceback:\n");

    while (frame->prev) {
        lily_proto *proto = frame->function->proto;

        if (frame->function->code == NULL)
            lily_mb_add_fmt(msgbuf, "    from %s: in %s\n", proto->module_path,
                    proto->name);
        else
            lily_mb_add_fmt(msgbuf, "    from %s:%d: in %s\n",
                    proto->module_path, frame->code[-1], proto->name);

        frame = frame->prev;
    }
}

/* This is called when the interpreter encounters an error. This builds an
   error message that is stored within parser's msgbuf. A runner can later fetch
   this error with `lily_error_message`. */
static void build_error(lily_parse_state *parser)
{
    lily_msgbuf *msgbuf = parser->msgbuf;
    lily_raiser *raiser = parser->raiser;

    lily_mb_flush(parser->msgbuf);

    if (raiser->source == err_from_none)
        return;

    add_error_header(msgbuf, raiser);

    switch (raiser->source) {
        case err_from_emit:
        case err_from_parse:
        case err_from_lex:
            add_frontend_trace(msgbuf, parser);
            break;
        case err_from_vm:
            add_vm_trace(msgbuf, parser);
            break;
        default:
            /* For raw errors (ex: First file failed to open), just show the
               message. */
            break;
    }
}

/* Return a string describing the last error encountered by the interpreter.
   This string is guaranteed to be valid until the next execution of the
   interpreter. */
const char *lily_error_message(lily_state *s)
{
    build_error(s->gs->parser);
    return lily_mb_raw(s->gs->parser->msgbuf);
}

/* Return the message of the last error encountered by the interpreter. */
const char *lily_error_message_no_trace(lily_state *s)
{
    return lily_mb_raw(s->raiser->msgbuf);
}

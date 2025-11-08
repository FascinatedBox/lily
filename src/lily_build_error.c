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
    lily_lex_state *lex = parser->lex;

    if (es == err_from_lex) {
        /* Lexer says context won't be useful (usually unterminated tokens). */
        if (lex->token_start == UINT16_MAX)
            return 0;
    }

    /* It's fine as long as the line is there to show. */
    return lex->line_num == line_num;
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
    char *start = lex->source + lex->token_start;

    /* Leading whitespace isn't interesting, so skip past it. */
    while (*source == ' ' || *source == '\t')
        source++;

    /* This can happen if a header scan (ex: 'import manifest') fails because of
       space at the front. */
    if (source > start)
        source = start;

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

static void patch_line_end(lily_lex_state *lex)
{
    /* Lexer lines always have at least a newline included (never 0). */
    size_t len = strlen(lex->source);
    char *end = lex->source + len - 1;

    if (lex->entry->entry_type != et_lambda ||
        *lex->entry->entry_cursor != '\0')
        /* Patch this off so the format builder can add it for both cases. */
        *end = '\0';
    else
        /* Lambdas have \0 at their end as a convenience for parser. Patch that
           into the ending parenth. */
        *end = ')';
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

    patch_line_end(lex);

    char *source_end = lex->source + lex->token_start;

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
                            " %d | %s\n"
                            "%s |", pipe_space, line_num, line, pipe_space);
    lily_mb_repeat_n(msgbuf, ' ', (int)(source_end - line + 1));
    lily_mb_add(msgbuf, "^\n\n");
}

static void add_frontend_trace(lily_msgbuf *msgbuf, lily_parse_state *parser)
{
    uint16_t line_num = parser->lex->line_num;
    lily_error_source es = parser->raiser->source;

    if (es == err_from_emit) {
        lily_ast *ast = parser->raiser->error_ast;

        line_num = ast->line_num;
        parser->lex->token_start = ast->token_start;
    }

    if (can_show_context(parser, es, line_num))
        add_context(msgbuf, parser, line_num);

    lily_mb_add_fmt(msgbuf, "    from %s:%d:\n",
            parser->symtab->active_module->path, line_num);
}

static void add_vm_trace(lily_msgbuf *msgbuf, lily_call_frame *frame)
{
    lily_mb_add(msgbuf, "Traceback:\n");

    while (frame->prev) {
        lily_proto *proto = frame->function->proto;

        if (proto->code == NULL)
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
            add_vm_trace(msgbuf, parser->vm->call_chain);
            break;
        default:
            /* For raw errors (ex: First file failed to open), just show the
               message. */
            break;
    }
}

const char *lily_ec_exception_message(lily_state *s)
{
    lily_msgbuf *msgbuf = lily_msgbuf_get(s);
    lily_raiser *raiser = s->raiser;

    /* Give the raiser vm's error info. This isn't cleared at the end, because
       exception dispatch will fix it or overwrite it. */
    raiser->source = err_from_vm;
    raiser->error_class = s->exception_cls;
    add_error_header(msgbuf, raiser);

    /* Error callbacks execute in the frame their callback was pushed in. This
       is where the vm actually is. */
    add_vm_trace(msgbuf, s->catch_chain->call_frame);
    return lily_mb_raw(msgbuf);
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

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

static void add_frontend_trace(lily_msgbuf *msgbuf, lily_parse_state *parser)
{
    uint16_t line_num = parser->lex->line_num;

    if (parser->raiser->source == err_from_emit)
        line_num = parser->raiser->error_ast->line_num;

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

#include <stdarg.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_msgbuf.h"
#include "lily_syminfo.h"

lily_msgbuf *lily_new_msgbuf(void)
{
    lily_msgbuf *msgbuf = lily_malloc(sizeof(lily_msgbuf));
    if (msgbuf == NULL)
        return NULL;

    msgbuf->message = lily_malloc(64 * sizeof(char));

    if (msgbuf->message == NULL) {
        lily_free_msgbuf(msgbuf);
        return NULL;
    }

    msgbuf->message[0] = '\0';
    msgbuf->pos = 0;
    msgbuf->size = 64;
    msgbuf->truncated = 0;

    return msgbuf;
}

static int try_resize_msgbuf(lily_msgbuf *msgbuf, int new_size)
{
    char *new_message;
    int ret;

    new_message = lily_realloc(msgbuf->message, new_size);

    if (new_message == NULL) {
        msgbuf->truncated = 1;
        ret = 0;
    }
    else {
        msgbuf->message = new_message;
        msgbuf->size = new_size;
        ret = 1;
    }

    return ret;
}

void lily_free_msgbuf(lily_msgbuf *msgbuf)
{
    lily_free(msgbuf->message);
    lily_free(msgbuf);
}

void lily_msgbuf_add(lily_msgbuf *msgbuf, char *str)
{
    int len = strlen(str);

    if (msgbuf->truncated)
        return;

    if ((msgbuf->pos + len + 1) > msgbuf->size) {
        if (try_resize_msgbuf(msgbuf, msgbuf->pos + len + 1) == 0)
            return;
    }

    strcat(msgbuf->message, str);
    msgbuf->pos += len;
}

void lily_msgbuf_escape_add_str(lily_msgbuf *msgbuf, char *str)
{
    char escape_char;
    int i, len, start;

    len = strlen(str);

    for (i = 0, start = 0;i < len;i++) {
        char ch = str[i];

        if (ch == '\n')
            escape_char = 'n';
        else if (ch == '\r')
            escape_char = 'r';
        else if (ch == '\t')
            escape_char = 't';
        else if (ch == '\'')
            escape_char = '\'';
        else if (ch == '"')
            escape_char = '"';
        else if (ch == '\\')
            escape_char = '\\';
        else if (ch == '\b')
            escape_char = 'b';
        else if (ch == '\a')
            escape_char = 'a';
        else
            escape_char = 0;

        if (escape_char) {
            if (i != start)
                lily_msgbuf_add_text_range(msgbuf, str, start, i);

            lily_msgbuf_add_char(msgbuf, '\\');
            lily_msgbuf_add_char(msgbuf, escape_char);

            start = i + 1;
        }
    }

    if (i != start)
        lily_msgbuf_add_text_range(msgbuf, str, start, i);
}

void lily_msgbuf_add_text_range(lily_msgbuf *msgbuf, char *text, int start,
        int stop)
{
    int range = (stop - start);

    if (msgbuf->truncated)
        return;

    if ((msgbuf->pos + range + 1) > msgbuf->size)
        if (try_resize_msgbuf(msgbuf, msgbuf->pos + range + 1) == 0)
            return;

    memcpy(msgbuf->message + msgbuf->pos, text + start, range);
    msgbuf->pos += range;
    msgbuf->message[msgbuf->pos] = '\0';
}

void lily_msgbuf_add_char(lily_msgbuf *msgbuf, char c)
{
    char ch_buf[2] = {c, '\0'};

    lily_msgbuf_add(msgbuf, ch_buf);
}

void lily_msgbuf_add_int(lily_msgbuf *msgbuf, int i)
{
    char buf[64];
    sprintf(buf, "%d", i);

    lily_msgbuf_add(msgbuf, buf);
}

void lily_msgbuf_add_double(lily_msgbuf *msgbuf, double d)
{
    char buf[64];
    sprintf(buf, "%g", d);

    lily_msgbuf_add(msgbuf, buf);
}

void lily_msgbuf_reset(lily_msgbuf *msgbuf)
{
    msgbuf->pos = 0;
    msgbuf->message[0] = '\0';
    msgbuf->truncated = 0;
}

/*  lily_msgbuf_flush
    This is called by lily_debug to clear the contents of the given msgbuf. The
    truncated indicator is intentionally not reset so that 'show' will stop
    writing output instead writing (likely confusing) bits here and there. */
void lily_msgbuf_flush(lily_msgbuf *msgbuf)
{
    msgbuf->pos = 0;
    msgbuf->message[0] = '\0';
}

void lily_msgbuf_add_sig(lily_msgbuf *msgbuf, lily_sig *sig)
{
    lily_msgbuf_add(msgbuf, sig->cls->name);

    if (sig->cls->id == SYM_CLASS_FUNCTION) {
        lily_msgbuf_add(msgbuf, " (");
        if (sig->siglist[1] != NULL) {
            int i;

            for (i = 1;i < sig->siglist_size - 1;i++) {
                lily_msgbuf_add_sig(msgbuf, sig->siglist[i]);
                lily_msgbuf_add(msgbuf, ", ");
            }

            lily_msgbuf_add_sig(msgbuf, sig->siglist[i]);
            if (sig->flags & SIG_IS_VARARGS)
                lily_msgbuf_add(msgbuf, "...");
        }
        lily_msgbuf_add(msgbuf, "):");
        if (sig->siglist[0] == NULL)
            lily_msgbuf_add(msgbuf, "nil");
        else
            lily_msgbuf_add_sig(msgbuf, sig->siglist[0]);
    }
    else if (sig->cls->id == SYM_CLASS_LIST ||
             sig->cls->id == SYM_CLASS_HASH) {
        int i;
        lily_msgbuf_add(msgbuf, "[");
        for (i = 0;i < sig->cls->template_count;i++) {
            lily_msgbuf_add_sig(msgbuf, sig->siglist[i]);
            if (i != (sig->cls->template_count - 1))
                lily_msgbuf_add(msgbuf, ", ");
        }
        lily_msgbuf_add(msgbuf, "]");
    }
}

/*  msgbuf_add_indent
    msgbuf: The msgbuf to add the data to.
    indent: The number if indents to add. "|    " is added for each indent.

    This is used rather frequently for indenting by the debug part of Lily. */
static void msgbuf_add_indent(lily_msgbuf *msgbuf, int indent)
{
    int i;
    /// todo: go down to 0 to avoid making an 'i'.
    for (i = 0;i < indent;i++)
        lily_msgbuf_add(msgbuf, "|    ");
}

void lily_msgbuf_add_fmt(lily_msgbuf *msgbuf, char *fmt, ...)
{
    char modifier_buf[5];
    int i, len, text_start;
    va_list var_args;

    va_start(var_args, fmt);

    modifier_buf[0] = '%';
    modifier_buf[1] = '\0';
    text_start = 0;
    len = strlen(fmt);

    for (i = 0;i < len;i++) {
        char c = fmt[i];
        if (c == '%') {
            if (i + 1 == len)
                break;

            if (i != text_start)
                lily_msgbuf_add_text_range(msgbuf, fmt, text_start, i);

            i++;
            c = fmt[i];
            if (c >= '0' && c <= '9') {
                modifier_buf[1] = c;
                i++;
                c = fmt[i];
                if (c >= '0' && c <= '9') {
                    modifier_buf[2] = c;
                    modifier_buf[3] = 'd';
                    modifier_buf[4] = '\0';
                    i++;
                    c = fmt[i];
                }
                else {
                    modifier_buf[2] = 'd';
                    modifier_buf[3] = '\0';
                }
            }

            if (c == 's') {
                char *str = va_arg(var_args, char *);
                lily_msgbuf_add(msgbuf, str);
            }
            else if (c == 'd') {
                int d = va_arg(var_args, int);
                if (modifier_buf[1] == '\0')
                    lily_msgbuf_add_int(msgbuf, d);
                else {
                    char buffer[128];
                    snprintf(buffer, 128, modifier_buf, d);
                    lily_msgbuf_add(msgbuf, buffer);
                    modifier_buf[1] = '\0';
                }
            }

            text_start = i+1;
        }
        /* ^ is used to distinguish normal fprintf-like arguments from the
           custom ones used by the msgbuf. */
        else if (c == '^') {
            if (i != text_start)
                lily_msgbuf_add_text_range(msgbuf, fmt, text_start, i);

            i++;
            c = fmt[i];
            if (c == 'T') {
                lily_sig *sig = va_arg(var_args, lily_sig *);
                lily_msgbuf_add_sig(msgbuf, sig);
            }
            else if (c == 'I') {
                int indent = va_arg(var_args, int);
                msgbuf_add_indent(msgbuf, indent);
            }
            else if (c == 'E') {
                char *str = va_arg(var_args, char *);
                lily_msgbuf_escape_add_str(msgbuf, str);
            }

            text_start = i+1;
        }
    }

    if (i != text_start)
        lily_msgbuf_add_text_range(msgbuf, fmt, text_start, i);

    va_end(var_args);
}

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "lily_core_types.h"

#include "lily_api_alloc.h"
#include "lily_api_msgbuf.h"

/* The internals of msgbuf are very simple. Unlike most declarations, this one
   is intentionally not within a .h file. The reasoning behind that, is that
   this prevents other parts of the interpreter from grabbing the message field
   directly. */
typedef struct lily_msgbuf_ {
    /* The message being stored. */
    char *message;
    /* The size that the message currently takes. */
    uint32_t pos;
    /* The buffer space allocated for the message. */
    uint32_t size;
} lily_msgbuf;

lily_msgbuf *lily_new_msgbuf(void)
{
    lily_msgbuf *msgbuf = lily_malloc(sizeof(lily_msgbuf));

    msgbuf->message = lily_malloc(64 * sizeof(char));
    msgbuf->message[0] = '\0';
    msgbuf->pos = 0;
    msgbuf->size = 64;

    return msgbuf;
}

static void resize_msgbuf(lily_msgbuf *msgbuf, int new_size)
{
    msgbuf->message = lily_realloc(msgbuf->message, new_size);
    msgbuf->size = new_size;
}

static void add_escaped_char(lily_msgbuf *msgbuf, char ch)
{
    char buffer[16];
    sprintf(buffer, "%03d", (unsigned char)ch);

    lily_mb_add(msgbuf, buffer);
}

/* Add a safe, escaped version of a given string to the msgbuf. A size is given
   as well so that bytestrings may be sent. */
static void add_escaped_sized(lily_msgbuf *msgbuf, int is_bytestring,
        const char *str, int len)
{
    char escape_char = 0;
    int i, start;

    for (i = 0, start = 0;i < len;i++) {
        char ch = str[i];
        int need_escape = 1;

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
        else if (isprint(ch) ||
            ((unsigned char)ch > 127 && is_bytestring == 0)) {
            need_escape = 0;
            escape_char = 0;
        }

        if (need_escape) {
            if (i != start)
                lily_mb_add_range(msgbuf, str, start, i);

            lily_mb_add_char(msgbuf, '\\');
            if (escape_char)
                lily_mb_add_char(msgbuf, escape_char);
            else
                add_escaped_char(msgbuf, ch);

            start = i + 1;
        }
    }

    if (i != start)
        lily_mb_add_range(msgbuf, str, start, i);

    /* Add a terminating \0 so that the msgbuf is always \0 terminated. */
    if (is_bytestring)
        lily_mb_add_char(msgbuf, '\0');
}

void lily_free_msgbuf(lily_msgbuf *msgbuf)
{
    lily_free(msgbuf->message);
    lily_free(msgbuf);
}

/* This allows getting the contents without knowing the struct. */
const char *lily_mb_get(lily_msgbuf *msgbuf)
{
    return msgbuf->message;
}

void lily_mb_add(lily_msgbuf *msgbuf, const char *str)
{
    int len = strlen(str);

    if ((msgbuf->pos + len + 1) > msgbuf->size)
        resize_msgbuf(msgbuf, msgbuf->pos + len + 1);

    strcat(msgbuf->message, str);
    msgbuf->pos += len;
}

void lily_mb_add_bytestring(lily_msgbuf *msgbuf, const char *str,
        int length)
{
    add_escaped_sized(msgbuf, 1, str, length);
}

/* Add a safe version of a \0 terminated string to a buffer. */
void lily_mb_escape_add_str(lily_msgbuf *msgbuf, const char *str)
{
    add_escaped_sized(msgbuf, 0, str, strlen(str));
}

/* Add a slice of text (start to stop) to the msgbuf. The slice does not need to
   be \0 terminated. However, the result will be \0 terminated. */
void lily_mb_add_range(lily_msgbuf *msgbuf, const char *text,
        int start, int stop)
{
    int range = (stop - start);

    if ((msgbuf->pos + range + 1) > msgbuf->size)
        resize_msgbuf(msgbuf, msgbuf->pos + range + 1);

    memcpy(msgbuf->message + msgbuf->pos, text + start, range);
    msgbuf->pos += range;
    msgbuf->message[msgbuf->pos] = '\0';
}

void lily_mb_add_char(lily_msgbuf *msgbuf, char c)
{
    char ch_buf[2] = {c, '\0'};

    lily_mb_add(msgbuf, ch_buf);
}

void lily_mb_add_boolean(lily_msgbuf *msgbuf, int b)
{
    if (b == 0)
        lily_mb_add(msgbuf, "false");
    else
        lily_mb_add(msgbuf, "true");
}

void lily_mb_add_int(lily_msgbuf *msgbuf, int i)
{
    char buf[64];
    sprintf(buf, "%d", i);

    lily_mb_add(msgbuf, buf);
}

void lily_mb_add_double(lily_msgbuf *msgbuf, double d)
{
    char buf[64];
    sprintf(buf, "%g", d);

    lily_mb_add(msgbuf, buf);
}

/* This erases what the msgbuf currently holds. */
void lily_mb_flush(lily_msgbuf *msgbuf)
{
    msgbuf->pos = 0;
    msgbuf->message[0] = '\0';
}

static void add_type(lily_msgbuf *msgbuf, lily_type *type)
{
    lily_mb_add(msgbuf, type->cls->name);

    if (type->cls->id == SYM_CLASS_FUNCTION) {
        if (type->generic_pos) {
            int i;
            char ch = 'A';
            lily_mb_add(msgbuf, "[");
            for (i = 0;i < type->generic_pos - 1;i++, ch++) {
                lily_mb_add_char(msgbuf, ch);
                lily_mb_add(msgbuf, ", ");
            }

            lily_mb_add_char(msgbuf, ch);
            lily_mb_add(msgbuf, "](");
        }
        else
            lily_mb_add(msgbuf, " (");

        if (type->subtype_count > 1) {
            int i;

            for (i = 1;i < type->subtype_count - 1;i++) {
                add_type(msgbuf, type->subtypes[i]);
                lily_mb_add(msgbuf, ", ");
            }

            if (type->flags & TYPE_IS_VARARGS) {
                /* Varargs are written as 'type ...', but internally are
                   actually 'list[type] ...'. This writes them as they would
                   have been written in (the extra ->subtypes[0] grabs the type
                   within the list. */
                add_type(msgbuf, type->subtypes[i]->subtypes[0]);
                lily_mb_add(msgbuf, "...");
            }
            else
                add_type(msgbuf, type->subtypes[i]);
        }
        if (type->subtypes[0] == NULL)
            lily_mb_add(msgbuf, ")");
        else {
            lily_mb_add(msgbuf, " => ");
            add_type(msgbuf, type->subtypes[0]);
            lily_mb_add(msgbuf, ")");
        }
    }
    else if (type->cls->generic_count != 0) {
        int i;
        int is_optarg = type->cls->id == SYM_CLASS_OPTARG;

        if (is_optarg == 0)
            lily_mb_add(msgbuf, "[");

        for (i = 0;i < type->subtype_count;i++) {
            add_type(msgbuf, type->subtypes[i]);
            if (i != (type->subtype_count - 1))
                lily_mb_add(msgbuf, ", ");
        }

        if (is_optarg == 0)
            lily_mb_add(msgbuf, "]");
    }
}

static void msgbuf_add_indent(lily_msgbuf *msgbuf, int indent)
{
    int i;
    for (i = 0;i < indent;i++)
        lily_mb_add(msgbuf, "|    ");
}

static void msgbuf_add_errno_string(lily_msgbuf *msgbuf, int errno_val)
{
    /* Assume that the message is of a reasonable sort of size. */
    char buffer[128];
#ifdef _WIN32
    strerror_s(buffer, sizeof(buffer), errno_val);
#else
    strerror_r(errno_val, buffer, sizeof(buffer));
#endif
    lily_mb_add(msgbuf, buffer);
}

void lily_mb_add_fmt_va(lily_msgbuf *msgbuf, const char *fmt,
        va_list var_args)
{
    char modifier_buf[5];
    char buffer[128];
    int i, len, text_start;

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
                lily_mb_add_range(msgbuf, fmt, text_start, i);

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
                lily_mb_add(msgbuf, str);
            }
            else if (c == 'd') {
                int d = va_arg(var_args, int);
                if (modifier_buf[1] == '\0')
                    lily_mb_add_int(msgbuf, d);
                else {
                    snprintf(buffer, 128, modifier_buf, d);
                    lily_mb_add(msgbuf, buffer);
                    modifier_buf[1] = '\0';
                }
            }
            else if (c == 'c') {
                char ch = va_arg(var_args, int);
                lily_mb_add_char(msgbuf, ch);
            }
            else if (c == 'p') {
                void *p = va_arg(var_args, void *);
                snprintf(buffer, 128, "%p", p);
                lily_mb_add(msgbuf, buffer);
            }

            text_start = i+1;
        }
        /* ^ is used to distinguish normal fprintf-like arguments from the
           custom ones used by the msgbuf. */
        else if (c == '^') {
            if (i != text_start)
                lily_mb_add_range(msgbuf, fmt, text_start, i);

            i++;
            c = fmt[i];
            if (c == 'T') {
                lily_type *type = va_arg(var_args, lily_type *);
                add_type(msgbuf, type);
            }
            else if (c == 'I') {
                int indent = va_arg(var_args, int);
                msgbuf_add_indent(msgbuf, indent);
            }
            else if (c == 'E') {
                char *str = va_arg(var_args, char *);
                lily_mb_escape_add_str(msgbuf, str);
            }
            else if (c == 'R') {
                int errno_val = va_arg(var_args, int);
                msgbuf_add_errno_string(msgbuf, errno_val);
            }

            text_start = i+1;
        }
    }

    if (i != text_start)
        lily_mb_add_range(msgbuf, fmt, text_start, i);
}

void lily_mb_add_fmt(lily_msgbuf *msgbuf, const char *fmt, ...)
{
    va_list var_args;
    va_start(var_args, fmt);
    lily_mb_add_fmt_va(msgbuf, fmt, var_args);
    va_end(var_args);
}

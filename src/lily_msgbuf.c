#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "lily_alloc.h"
#include "lily_msgbuf.h"
#include "lily_core_types.h"

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

/*  add_escaped_char
    This adds the given */
static void add_escaped_char(lily_msgbuf *msgbuf, char ch)
{
    char buffer[16];
    sprintf(buffer, "%03d", (unsigned char)ch);

    lily_msgbuf_add(msgbuf, buffer);
}

/*  add_escaped_sized
    This adds the string given into the msgbuf. The len is provided in case
    'str' is part of a bytestring which has embedded \0 values.
    This prints out a more readable version of the string given.

    If is_bytestring is 1, then chars > 127 are replaced with numeric escapes
    (\nnn) in case they are not valid utf-8.
    If it is not 1, then chars > 127 are added directly, assuming they are valid
    utf-8.

    Non-printable characters are always replaced with numeric escapes. */
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
        else if (isprint(ch) == 1 ||
            ((unsigned char)ch > 127 && is_bytestring == 0)) {
            need_escape = 0;
            escape_char = 0;
        }

        if (need_escape) {
            if (i != start)
                lily_msgbuf_add_text_range(msgbuf, str, start, i);

            lily_msgbuf_add_char(msgbuf, '\\');
            if (escape_char)
                lily_msgbuf_add_char(msgbuf, escape_char);
            else
                add_escaped_char(msgbuf, ch);

            start = i + 1;
        }
    }

    if (i != start)
        lily_msgbuf_add_text_range(msgbuf, str, start, i);

    /* Add a terminating \0 so that the msgbuf is always \0 terminated. */
    if (is_bytestring)
        lily_msgbuf_add_char(msgbuf, '\0');
}

void lily_free_msgbuf(lily_msgbuf *msgbuf)
{
    lily_free(msgbuf->message);
    lily_free(msgbuf);
}

void lily_msgbuf_add(lily_msgbuf *msgbuf, const char *str)
{
    int len = strlen(str);

    if ((msgbuf->pos + len + 1) > msgbuf->size)
        resize_msgbuf(msgbuf, msgbuf->pos + len + 1);

    strcat(msgbuf->message, str);
    msgbuf->pos += len;
}

/*  lily_msgbuf_escape_add_str
    This is a convenience function for adding a safe, escaped version of the
    given string to the msgbuf. */
void lily_msgbuf_escape_add_str(lily_msgbuf *msgbuf, const char *str)
{
    add_escaped_sized(msgbuf, 0, str, strlen(str));
}

void lily_msgbuf_add_text_range(lily_msgbuf *msgbuf, const char *text,
        int start, int stop)
{
    int range = (stop - start);

    if ((msgbuf->pos + range + 1) > msgbuf->size)
        resize_msgbuf(msgbuf, msgbuf->pos + range + 1);

    memcpy(msgbuf->message + msgbuf->pos, text + start, range);
    msgbuf->pos += range;
    msgbuf->message[msgbuf->pos] = '\0';
}

void lily_msgbuf_add_char(lily_msgbuf *msgbuf, char c)
{
    char ch_buf[2] = {c, '\0'};

    lily_msgbuf_add(msgbuf, ch_buf);
}

static void add_boolean(lily_msgbuf *msgbuf, int b)
{
    if (b == 0)
        lily_msgbuf_add(msgbuf, "false");
    else
        lily_msgbuf_add(msgbuf, "true");
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

/* This marker is used to prevent trying to stringify a value more than once. */
typedef struct tag_ {
    struct tag_ *prev;
    lily_raw_value raw;
} tag;

static void do_add_value(lily_msgbuf *, tag *, lily_value *);

static void add_list_like(lily_msgbuf *msgbuf, tag *t, lily_value *v,
        const char *prefix, const char *suffix)
{
    lily_list_val *lv = v->value.list;
    lily_msgbuf_add(msgbuf, prefix);
    int i;
    for (i = 0;i < lv->num_values - 1;i++) {
        do_add_value(msgbuf, t, lv->elems[i]);
        lily_msgbuf_add(msgbuf, ", ");
    }
    if (i != lv->num_values)
        do_add_value(msgbuf, t, lv->elems[i]);

    lily_msgbuf_add(msgbuf, suffix);
}

static void do_add_value(lily_msgbuf *msgbuf, tag *t, lily_value *v)
{
    if (v->type->flags & TYPE_MAYBE_CIRCULAR) {
        tag *tag_iter = t;
        while (tag_iter) {
            /* Different containers may hold the same underlying values, so make
               sure to NOT test the containers. */
            if (memcmp(&tag_iter->raw, &v->value, sizeof(lily_raw_value)) == 0) {
                lily_msgbuf_add(msgbuf, "[...]");
                return;
            }

            tag_iter = tag_iter->prev;
        }

        tag new_tag = {.prev = t, .raw = v->value};
        t = &new_tag;
    }

    int cls_id = v->type->cls->id;

    if (cls_id == SYM_CLASS_BOOLEAN)
        add_boolean(msgbuf, v->value.integer);
    else if (cls_id == SYM_CLASS_INTEGER)
        lily_msgbuf_add_int(msgbuf, v->value.integer);
    else if (cls_id == SYM_CLASS_DOUBLE)
        lily_msgbuf_add_double(msgbuf, v->value.doubleval);
    else if (cls_id == SYM_CLASS_STRING) {
        lily_msgbuf_add_char(msgbuf, '\"');
        /* Note: This is fine because strings can't contain \0. */
        lily_msgbuf_add(msgbuf, v->value.string->string);
        lily_msgbuf_add_char(msgbuf, '\"');
    }
    else if (cls_id == SYM_CLASS_BYTESTRING) {
        lily_string_val *bytev = v->value.string;
        add_escaped_sized(msgbuf, 1, bytev->string, bytev->size);
    }
    else if (cls_id == SYM_CLASS_FUNCTION) {
        lily_function_val *fv = v->value.function;
        const char *builtin = "";
        const char *class_name = "";
        const char *separator = "";

        if (fv->code == NULL)
            builtin = "built-in ";

        if (fv->class_name) {
            class_name = fv->class_name;
            separator = "::";
        }

        lily_msgbuf_add_fmt(msgbuf, "<%sfunction %s%s%s>", builtin, class_name,
                separator, fv->trace_name);
    }
    else if (v->type->cls->flags & CLS_IS_ENUM)
        do_add_value(msgbuf, t, v->value.any->inner_value);
    else if (cls_id == SYM_CLASS_LIST)
        add_list_like(msgbuf, t, v, "[", "]");
    else if (cls_id == SYM_CLASS_TUPLE)
        add_list_like(msgbuf, t, v, "<[", "]>");
    else if (cls_id == SYM_CLASS_HASH) {
        lily_hash_val *hv = v->value.hash;
        lily_msgbuf_add_char(msgbuf, '[');
        lily_hash_elem *elem = hv->elem_chain;
        while (elem) {
            do_add_value(msgbuf, t, elem->elem_key);
            lily_msgbuf_add(msgbuf, " => ");
            do_add_value(msgbuf, t, elem->elem_value);
            if (elem->next != NULL)
                lily_msgbuf_add(msgbuf, ", ");

            elem = elem->next;
        }
        lily_msgbuf_add_char(msgbuf, ']');
    }
    else if (cls_id == SYM_CLASS_FILE) {
        lily_file_val *fv = v->value.file;
        const char *state = fv->is_open ? "open" : "closed";
        lily_msgbuf_add_fmt(msgbuf, "<%s file at %p>", state, fv);
    }
    else if (v->type->cls->flags & CLS_IS_VARIANT) {
        lily_class *cls = v->type->cls;
        lily_msgbuf_add(msgbuf, cls->name);
        if (cls->variant_type->subtype_count > 1)
            add_list_like(msgbuf, t, v, "(", ")");
    }
    else {
        lily_class *cls;
        const char *package_name = "";
        const char *separator = "";
        if (v->type->cls->is_builtin)
            cls = v->type->cls;
        else
            cls = v->value.instance->true_type->cls;

        if (cls->import->loadname[0] != '\0') {
            package_name = cls->import->loadname;
            separator = "::";
        }

        lily_msgbuf_add_fmt(msgbuf, "<%s%s%s at %p>", package_name, separator,
                cls->name, v->value.generic);
    }
}

void lily_msgbuf_add_value(lily_msgbuf *msgbuf, lily_value *v)
{
    do_add_value(msgbuf, NULL, v);
}

/*  lily_msgbuf_flush
    This is called by to clear the contents of the given msgbuf. */
void lily_msgbuf_flush(lily_msgbuf *msgbuf)
{
    msgbuf->pos = 0;
    msgbuf->message[0] = '\0';
}

void lily_msgbuf_add_type(lily_msgbuf *msgbuf, lily_type *type)
{
    lily_msgbuf_add(msgbuf, type->cls->name);

    if (type->cls->id == SYM_CLASS_FUNCTION) {
        if (type->generic_pos) {
            int i;
            char ch = 'A';
            lily_msgbuf_add(msgbuf, "[");
            for (i = 0;i < type->generic_pos - 1;i++, ch++) {
                lily_msgbuf_add_char(msgbuf, ch);
                lily_msgbuf_add(msgbuf, ", ");
            }

            lily_msgbuf_add_char(msgbuf, ch);
            lily_msgbuf_add(msgbuf, "](");
        }
        else
            lily_msgbuf_add(msgbuf, " (");

        if (type->subtype_count > 1) {
            int i;

            for (i = 1;i < type->subtype_count - 1;i++) {
                lily_msgbuf_add_type(msgbuf, type->subtypes[i]);
                lily_msgbuf_add(msgbuf, ", ");
            }

            lily_msgbuf_add_type(msgbuf, type->subtypes[i]);
            if (type->flags & TYPE_IS_VARARGS)
                lily_msgbuf_add(msgbuf, "...");
        }
        if (type->subtypes[0] == NULL)
            lily_msgbuf_add(msgbuf, ")");
        else {
            lily_msgbuf_add(msgbuf, " => ");
            lily_msgbuf_add_type(msgbuf, type->subtypes[0]);
            lily_msgbuf_add(msgbuf, ")");
        }
    }
    else if (type->cls->id == SYM_CLASS_GENERIC)
        lily_msgbuf_add_char(msgbuf, 'A' + type->generic_pos);
    else if (type->cls->generic_count != 0) {
        int i;
        int is_optarg = type->cls->id == SYM_CLASS_OPTARG;

        if (is_optarg == 0)
            lily_msgbuf_add(msgbuf, "[");

        for (i = 0;i < type->subtype_count;i++) {
            lily_msgbuf_add_type(msgbuf, type->subtypes[i]);
            if (i != (type->subtype_count - 1))
                lily_msgbuf_add(msgbuf, ", ");
        }

        if (is_optarg == 0)
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
    for (i = 0;i < indent;i++)
        lily_msgbuf_add(msgbuf, "|    ");
}

static void msgbuf_add_errno_string(lily_msgbuf *msgbuf, int errno_val)
{
    /* Assume that the message is of a reasonable sort of size. */
    char buffer[128];
    strerror_r(errno_val, buffer, sizeof(buffer));

    lily_msgbuf_add(msgbuf, buffer);
}

void lily_msgbuf_add_fmt_va(lily_msgbuf *msgbuf, const char *fmt,
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
                    snprintf(buffer, 128, modifier_buf, d);
                    lily_msgbuf_add(msgbuf, buffer);
                    modifier_buf[1] = '\0';
                }
            }
            else if (c == 'c') {
                char ch = va_arg(var_args, int);
                lily_msgbuf_add_char(msgbuf, ch);
            }
            else if (c == 'p') {
                void *p = va_arg(var_args, void *);
                snprintf(buffer, 128, "%p", p);
                lily_msgbuf_add(msgbuf, buffer);
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
                lily_type *type = va_arg(var_args, lily_type *);
                lily_msgbuf_add_type(msgbuf, type);
            }
            else if (c == 'I') {
                int indent = va_arg(var_args, int);
                msgbuf_add_indent(msgbuf, indent);
            }
            else if (c == 'E') {
                char *str = va_arg(var_args, char *);
                lily_msgbuf_escape_add_str(msgbuf, str);
            }
            else if (c == 'R') {
                int errno_val = va_arg(var_args, int);
                msgbuf_add_errno_string(msgbuf, errno_val);
            }
            else if (c == 'V') {
                lily_value *v = va_arg(var_args, lily_value *);
                lily_msgbuf_add_value(msgbuf, v);
            }

            text_start = i+1;
        }
    }

    if (i != text_start)
        lily_msgbuf_add_text_range(msgbuf, fmt, text_start, i);
}

void lily_msgbuf_add_fmt(lily_msgbuf *msgbuf, const char *fmt, ...)
{
    va_list var_args;
    va_start(var_args, fmt);
    lily_msgbuf_add_fmt_va(msgbuf, fmt, var_args);
    va_end(var_args);
}

void lily_msgbuf_grow(lily_msgbuf *msgbuf)
{
    resize_msgbuf(msgbuf, msgbuf->size * 2);
}

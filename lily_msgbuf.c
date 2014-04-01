#include <string.h>

#include "lily_impl.h"
#include "lily_msgbuf.h"

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
    msgbuf->truncated = 0;
    msgbuf->message[0] = '\0';
}

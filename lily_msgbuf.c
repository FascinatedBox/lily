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

void lily_free_msgbuf(lily_msgbuf *msgbuf)
{
    lily_free(msgbuf->message);
    lily_free(msgbuf);
}

void lily_msgbuf_add(lily_msgbuf *msgbuf, char *str)
{
    int sl = strlen(str);

    if (msgbuf->truncated)
        return;

    if ((msgbuf->pos + sl + 1) > msgbuf->size) {
        char *new_message;
        int new_size;

        new_size = msgbuf->pos + sl + 1;
        new_message = lily_realloc(msgbuf->message, new_size);
        if (new_message == NULL) {
            msgbuf->truncated = 1;
            return;
        }

        msgbuf->message = new_message;
        msgbuf->size = msgbuf->pos + sl + 1;
    }

    strcat(msgbuf->message, str);
    msgbuf->pos += sl;
}

void lily_msgbuf_add_text_range(lily_msgbuf *msgbuf, char *text, int start,
        int stop)
{
    int range = (stop - start);

    if (msgbuf->truncated)
        return;

    if ((msgbuf->pos + range + 1) > msgbuf->size) {
        char *new_message;
        int new_size;

        new_size = msgbuf->pos + range + 1;
        new_message = lily_realloc(msgbuf->message, new_size);
        if (new_message == NULL) {
            msgbuf->truncated = 1;
            return;
        }

        msgbuf->message = new_message;
        msgbuf->size = msgbuf->pos + range + 1;
    }

    memcpy(msgbuf->message + msgbuf->pos, text + start, range);
    msgbuf->pos += range;
    msgbuf->message[msgbuf->pos] = '\0';
}

void lily_msgbuf_add_int(lily_msgbuf *msgbuf, int i)
{
    char buf[64];
    sprintf(buf, "%d", i);

    lily_msgbuf_add(msgbuf, buf);
}

void lily_msgbuf_add_sig(lily_msgbuf *msgbuf, lily_sig *sig)
{
    lily_msgbuf_add(msgbuf, sig->cls->name);

    if (sig->cls->id == SYM_CLASS_METHOD ||
        sig->cls->id == SYM_CLASS_FUNCTION) {
        lily_call_sig *csig = sig->node.call;
        lily_msgbuf_add(msgbuf, " (");
        int i;
        for (i = 0;i < csig->num_args-1;i++) {
            lily_msgbuf_add_sig(msgbuf, csig->args[i]);
            lily_msgbuf_add(msgbuf, ", ");
        }
        if (i != csig->num_args) {
            lily_msgbuf_add_sig(msgbuf, csig->args[i]);
            if (csig->is_varargs)
                lily_msgbuf_add(msgbuf, "...");
        }
        lily_msgbuf_add(msgbuf, "):");
        if (csig->ret == NULL)
            lily_msgbuf_add(msgbuf, "nil");
        else
            lily_msgbuf_add_sig(msgbuf, csig->ret);
    }
    else if (sig->cls->id == SYM_CLASS_LIST) {
        lily_msgbuf_add(msgbuf, "[");
        lily_msgbuf_add_sig(msgbuf, sig->node.value_sig);
        lily_msgbuf_add(msgbuf, "]");
    }
}

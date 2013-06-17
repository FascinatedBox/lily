#include <string.h>

#include "lily_impl.h"
#include "lily_msgbuf.h"

void lily_free_msgbuf(lily_msgbuf *mb)
{
    lily_free(mb->msg);
    lily_free(mb);
}

lily_msgbuf *lily_new_msgbuf(char *str)
{
    lily_msgbuf *ret = lily_malloc(sizeof(lily_msgbuf));
    if (ret == NULL)
        return NULL;

    int len, size;

    len = strlen(str);
    size = len + 4;

    char *msg = lily_malloc(size);

    if (msg == NULL) {
        lily_free(ret);
        return NULL;
    }

    strcpy(msg, str);

    ret->msg = msg;
    ret->pos = len;
    ret->size = size;
    ret->has_err = 0;

    return ret;
}

void lily_msgbuf_add(lily_msgbuf *mb, char *str)
{
    int sl = strlen(str);

    if (mb->has_err)
        return;

    if ((mb->pos + sl + 1) > mb->size) {
        char *new_msg;
        int new_size;

        new_size = mb->pos + sl + 1;
        new_msg = lily_realloc(mb->msg, new_size);
        if (new_msg == NULL) {
            mb->has_err = 1;
            return;
        }

        mb->msg = new_msg;
        mb->size = mb->pos + sl + 1;
    }

    strcat(mb->msg, str);
    mb->pos += sl;
}

void lily_msgbuf_add_int(lily_msgbuf *mb, int i)
{
    char buf[64];
    sprintf(buf, "%d", i);

    lily_msgbuf_add(mb, buf);
}

void lily_msgbuf_add_sig(lily_msgbuf *mb, lily_sig *sig)
{
    lily_msgbuf_add(mb, sig->cls->name);

    if (sig->cls->id == SYM_CLASS_METHOD ||
        sig->cls->id == SYM_CLASS_FUNCTION) {
        lily_call_sig *csig = sig->node.call;
        lily_msgbuf_add(mb, " (");
        int i;
        for (i = 0;i < csig->num_args-1;i++) {
            lily_msgbuf_add_sig(mb, csig->args[i]);
            lily_msgbuf_add(mb, ", ");
        }
        if (i != csig->num_args) {
            lily_msgbuf_add_sig(mb, csig->args[i]);
            if (csig->is_varargs)
                lily_msgbuf_add(mb, "...");
        }
        lily_msgbuf_add(mb, "):");
        if (csig->ret == NULL)
            lily_msgbuf_add(mb, "nil");
        else
            lily_msgbuf_add_sig(mb, csig->ret);
    }
    else if (sig->cls->id == SYM_CLASS_LIST) {
        lily_msgbuf_add(mb, "[");
        lily_msgbuf_add_sig(mb, sig->node.value_sig);
        lily_msgbuf_add(mb, "]");
    }
}

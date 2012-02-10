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
fprintf(stderr, "realloc.\n");
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

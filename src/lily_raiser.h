#ifndef LILY_RAISER_H
# define LILY_RAISER_H

# include <setjmp.h>

# include "lily.h"
# include "lily_core_types.h"

typedef enum {
    err_from_emit,
    err_from_lex,
    err_from_none,
    err_from_parse,
    err_from_raw,
    err_from_vm,
} lily_error_source;

typedef struct lily_jump_link_ {
    struct lily_jump_link_ *prev;
    struct lily_jump_link_ *next;

    jmp_buf jump;
} lily_jump_link;

struct lily_class_;
struct lily_ast_;

typedef struct lily_raiser_ {
    lily_jump_link *all_jumps;

    /* The error message is stored here. */
    lily_msgbuf *msgbuf;

    /* This is a spare msgbuf for building error messages. */
    lily_msgbuf *aux_msgbuf;

    union {
        struct lily_class_ *error_class;
        struct lily_ast_ *error_ast;
    };

    lily_error_source source: 16;

    /* For non-vm errors: If 0, use lexer's line number. Otherwise, use this. */
    uint16_t override_line_num;
    uint32_t pad;
} lily_raiser;

lily_raiser *lily_new_raiser(void);
void lily_rewind_raiser(lily_raiser *);
void lily_free_raiser(lily_raiser *);

void lily_raise_class(lily_raiser *, struct lily_class_ *);
void lily_raise_lex(lily_raiser *, const char *, ...);
void lily_raise_tree(lily_raiser *, struct lily_ast_ *, const char *, ...);
void lily_raise_syn(lily_raiser *, const char *, ...);
void lily_raise_raw(lily_raiser *, const char *, ...);
lily_jump_link *lily_jump_setup(lily_raiser *);
void lily_release_jump(lily_raiser *);

#endif

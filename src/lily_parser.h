#ifndef LILY_PARSER_H
# define LILY_PARSER_H

# include "lily.h"

# include "lily_raiser.h"
# include "lily_expr.h"
# include "lily_lexer.h"
# include "lily_emitter.h"
# include "lily_symtab.h"
# include "lily_vm.h"
# include "lily_type_maker.h"
# include "lily_buffer_u16.h"
# include "lily_value_stack.h"
# include "lily_generic_pool.h"

struct lily_rewind_state_;
struct lily_import_state_;

typedef struct lily_parse_state_ {
    lily_module_entry *module_start;
    lily_module_entry *module_top;

    lily_module_entry *main_module;

    lily_buffer_u16 *data_stack;

    uint16_t executing;

    /* 1 if there is content to parse, 0 otherwise.
       Used by content loading to block multiple loads and to prevent running
       without any content to run on. */
    uint16_t content_to_parse;

    /* The next import should store temp names here. */
    uint32_t import_pile_current;

    /* Same idea, but for keyword arguments. */
    uint16_t keyarg_current;

    /* 1 if the first import is in template mode, 0 otherwise.
       Used to prevent files in non-template mode from having tags. */
    uint16_t rendering;

    /* 1 if in a static method, 0 otherwise. Static functions can call class
       methods, but they can't send 'self' since they don't have it. */
    uint16_t in_static_call;

    uint16_t pad;

    /* The current expression state. */
    lily_expr_state *expr;

    /* This stores keyword arguments until the prototype can take them. */
    lily_string_pile *keyarg_strings;

    /* Pile strings are stored here. */
    lily_string_pile *expr_strings;

    /* For code like `import (a, b) c`, this stores a and b. */
    lily_string_pile *import_ref_strings;

    /* The parser uses this to hold and register generic classes. */
    lily_generic_pool *generics;

    lily_function_val *toplevel_func;

    lily_type *class_self_type;
    lily_msgbuf *msgbuf;
    lily_type *default_call_type;
    lily_lex_state *lex;
    lily_emit_state *emit;
    lily_symtab *symtab;
    lily_vm_state *vm;
    lily_type_maker *tm;
    lily_raiser *raiser;
    lily_config *config;
    struct lily_rewind_state_ *rs;
    struct lily_import_state_ *ims;
} lily_parse_state;

lily_var *lily_parser_lambda_eval(lily_parse_state *, int, const char *,
        lily_type *);
lily_item *lily_find_or_dl_member(lily_parse_state *, lily_class *,
        const char *);
lily_class *lily_dynaload_exception(lily_parse_state *, const char *);

#endif

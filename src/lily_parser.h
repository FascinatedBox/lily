#ifndef LILY_PARSER_H
# define LILY_PARSER_H

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

# include "lily_api_msgbuf.h"
# include "lily_api_embed.h"

struct lily_rewind_state_;

typedef struct lily_parse_state_ {
    lily_module_entry *module_start;
    lily_module_entry *module_top;

    lily_module_entry *main_module;

    lily_module_entry *last_import;

    lily_buffer_u16 *data_stack;

    uint16_t executing;
    uint16_t first_pass;
    uint32_t pad;

    /* The current expression state. */
    lily_expr_state *expr;

    /* Pile strings are stored here. */
    lily_string_pile *expr_strings;

    /* These are the values of vars that have been dynaloaded. They're stored
       here until the vm is ready to receive them. */
    lily_value_stack *foreign_values;

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
} lily_parse_state;

lily_var *lily_parser_lambda_eval(lily_parse_state *, int, const char *,
        lily_type *);
lily_item *lily_find_or_dl_member(lily_parse_state *, lily_class *,
        const char *);
lily_class *lily_dynaload_exception(lily_parse_state *, const char *);

#endif

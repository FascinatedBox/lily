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

# include "lily_api_msgbuf.h"

typedef struct lily_parse_state_ {
    lily_package *package_start;
    lily_package *package_top;

    lily_module_entry *main_module;

    lily_buffer_u16 *optarg_stack;

    uint16_t executing;
    uint8_t first_pass;
    uint8_t generic_count;
    uint32_t pad;

    /* The current expression state. */
    lily_expr_state *expr;

    /* Pile strings are stored here. */
    lily_string_pile *expr_strings;

    /* The first expression is malloc'd, while the rest are off the stack.
       Holding the first one makes it easier to find and destroy both it and the
       asts under it during teardown. */
    lily_expr_state *first_expr;

    /* These are the values of vars that have been dynaloaded. They're stored
       here until the vm is ready to receive them. */
    lily_value_stack *foreign_values;

    lily_type *class_self_type;
    lily_msgbuf *msgbuf;
    lily_type *default_call_type;
    lily_lex_state *lex;
    lily_emit_state *emit;
    lily_symtab *symtab;
    lily_vm_state *vm;
    lily_type_maker *tm;
    lily_raiser *raiser;
    struct lily_options_ *options;
    void *data;
} lily_parse_state;

lily_var *lily_parser_lambda_eval(lily_parse_state *, int, const char *,
        lily_type *);
lily_sym *lily_parser_interp_eval(lily_parse_state *, int, const char *);
lily_item *lily_find_or_dl_member(lily_parse_state *, lily_class *,
        const char *);
void lily_free_parse_state(lily_parse_state *);
lily_parse_state *lily_new_parse_state(struct lily_options_ *);
int lily_parse_file(lily_parse_state *, lily_lex_mode, const char *);
int lily_parse_string(lily_parse_state *, const char *, lily_lex_mode,
        char *);
lily_class *lily_dynaload_exception(lily_parse_state *, const char *);
void lily_register_package(lily_parse_state *, const char *, const char **,
        lily_loader);
const char *lily_build_error_message(lily_parse_state *);

#endif

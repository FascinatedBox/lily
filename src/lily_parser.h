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

/* This is used to prevent multiple content loads and to make sure content
   handling has content. */
#define PARSER_HAS_CONTENT  0x01

/* Manifest files have different rules. */
#define PARSER_IN_MANIFEST  0x02

/* This is used by symtab's rewind to determine what happens to the new symbols
   from the failed parse. If set, symbols are hidden in case old symbols refer
   to them (unlikely but possible). Otherwise, the symbols are dropped. */
#define PARSER_IS_EXECUTING 0x04

/* This is used to make sure the end token is correct. */
#define PARSER_IS_RENDERING 0x08

/* Don't allow complex expressions (currently only blocks lambdas). */
#define PARSER_SIMPLE_EXPR  0x10

/* Consider `class Error(message: String) < Exception(message) { ... }`. A
   typical expression call would allow calls or subscripts against the
   `Exception` call. This tells expression to not allow that. */
#define PARSER_SUPER_EXPR   0x20

struct lily_rewind_state_;
struct lily_import_state_;

typedef struct lily_parse_state_ {
    lily_module_entry *module_start;
    lily_module_entry *module_top;

    lily_module_entry *main_module;

    /* This stores positions in data_strings for fetching out later. */
    lily_buffer_u16 *data_stack;

    /* The next insertion position into data_strings. */
    uint16_t data_string_pos;

    /* See PARSER_* flags. */
    uint16_t flags;

    uint16_t modifiers;

    uint16_t pad;

    /* The current expression state. */
    lily_expr_state *expr;

    /* Pile strings are stored here. */
    lily_string_pile *expr_strings;

    /* This holds intermediate strings for keyword arguments and import. */
    lily_string_pile *data_strings;

    /* The parser uses this to hold and register generic classes. */
    lily_generic_pool *generics;

    lily_function_val *toplevel_func;

    lily_class *current_class;
    lily_msgbuf *msgbuf;
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

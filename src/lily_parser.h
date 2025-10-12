#ifndef LILY_PARSER_H
# define LILY_PARSER_H

# include "lily.h"
# include "lily_buffer_u16.h"
# include "lily_emitter.h"
# include "lily_expr.h"
# include "lily_generic_pool.h"
# include "lily_lexer.h"
# include "lily_raiser.h"
# include "lily_symtab.h"
# include "lily_type_maker.h"
# include "lily_vm.h"

/* This is used to prevent multiple content loads and to make sure content
   handling has content. */
#define PARSER_HAS_CONTENT  0x01

/* Manifest files have different rules. */
#define PARSER_IN_MANIFEST  0x02

/* This is used by symtab's rewind to determine what happens to the new symbols
   from the failed parse. If set, symbols are hidden in case old symbols refer
   to them (unlikely but possible). Otherwise, the symbols are dropped. */
#define PARSER_IS_EXECUTING 0x04

/* Don't allow complex expressions (currently only blocks lambdas). */
#define PARSER_SIMPLE_EXPR  0x08

/* Consider `class Error(message: String) < Exception(message) { ... }`. A
   typical expression call would allow calls or subscripts against the
   `Exception` call. This tells expression to not allow that. */
#define PARSER_SUPER_EXPR   0x10

/* This is set when parser has a docblock to store when writing the doc section
   for a symbol. */
#define PARSER_HAS_DOCBLOCK 0x20

/* This is set when parser is processing a manifest, or if a parse starts with
   the config struct's extra_info set to 1. If set, parser will store docblocks,
   parameter names, and other useful information for introspection. By default,
   the information is not saved, resulting in some introspection functions
   returning empty strings. */
#define PARSER_EXTRA_INFO   0x40

struct lily_rewind_state_;
struct lily_import_state_;

typedef struct {
    char ***data;
    uint16_t pos;
    uint16_t size;
    uint32_t pad;
} lily_doc_stack;

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
    lily_var *spare_vars;
    lily_doc_stack *doc;
} lily_parse_state;

void lily_parser_hide_match_vars(lily_parse_state *);
void lily_parser_lambda_init(lily_parse_state *, const char *, uint16_t,
      uint16_t);
lily_sym *lily_parser_lambda_eval(lily_parse_state *, lily_type *);
lily_item *lily_find_or_dl_member(lily_parse_state *, lily_class *,
        const char *);
lily_class *lily_dynaload_exception(lily_parse_state *, const char *);
void lily_pa_add_data_string(lily_parse_state *, const char *);

#endif

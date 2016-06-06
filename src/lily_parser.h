#ifndef LILY_PARSER_H
# define LILY_PARSER_H

# include "lily_raiser.h"
# include "lily_ast.h"
# include "lily_lexer.h"
# include "lily_emitter.h"
# include "lily_symtab.h"
# include "lily_vm.h"
# include "lily_msgbuf.h"
# include "lily_type_maker.h"
# include "lily_buffers.h"

typedef struct lily_parse_state_ {
    lily_package *package_start;
    lily_package *package_top;

    lily_module_entry *main_module;

    lily_u16_buffer *optarg_stack;

    uint16_t executing;
    uint8_t first_pass;
    uint8_t generic_count;
    uint32_t pad;

    lily_membuf *ast_membuf;
    lily_type *exception_type;
    lily_type *class_self_type;
    lily_msgbuf *msgbuf;
    lily_type *default_call_type;
    lily_ast_pool *ast_pool;
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
lily_class *lily_maybe_dynaload_class(lily_parse_state *, lily_module_entry *,
        const char *);
void lily_register_package(lily_parse_state *, const char *, const char **,
        lily_loader);
char *lily_build_error_message(lily_parse_state *);

#endif

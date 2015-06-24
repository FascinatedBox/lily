#ifndef LILY_PARSER_H
# define LILY_PARSER_H

# include "lily_raiser.h"
# include "lily_ast.h"
# include "lily_lexer.h"
# include "lily_emitter.h"
# include "lily_symtab.h"
# include "lily_vm.h"
# include "lily_msgbuf.h"

typedef struct lily_path_link_ {
    char *path;
    struct lily_path_link_ *next;
} lily_path_link;

typedef struct lily_parse_state_ {
    lily_type **type_stack;
    uint16_t type_stack_pos;
    uint16_t type_stack_size;
    uint16_t class_depth;
    uint16_t next_lambda_id;

    lily_path_link *import_paths;
    lily_path_link *library_import_paths;

    lily_import_entry *import_top;
    lily_import_entry *import_start;

    uint16_t *optarg_stack;
    uint32_t optarg_stack_pos;
    uint32_t optarg_stack_size;

    lily_type *class_self_type;
    lily_msgbuf *msgbuf;
    lily_type *default_call_type;
    lily_ast_pool *ast_pool;
    lily_lex_state *lex;
    lily_emit_state *emit;
    lily_symtab *symtab;
    lily_vm_state *vm;
    lily_raiser *raiser;
    lily_options *options;
    void *data;
    uint32_t executing;
    uint32_t first_pass;
} lily_parse_state;

lily_options *lily_new_default_options(void);
void lily_free_options(lily_options *);

lily_var *lily_parser_lambda_eval(lily_parse_state *, int, char *, lily_type *,
        int);
lily_var *lily_parser_dynamic_load(lily_parse_state *, lily_class *, char *);
void lily_free_parse_state(lily_parse_state *);
lily_parse_state *lily_new_parse_state(lily_options *);
int lily_parse_file(lily_parse_state *, lily_lex_mode, char *);
int lily_parse_string(lily_parse_state *, char *, lily_lex_mode, char *);
lily_class *lily_maybe_dynaload_class(lily_parse_state *, lily_import_entry *,
        const char *);
void lily_register_import(lily_parse_state *, const char *, const void *,
        var_loader);
char *lily_build_error_message(lily_parse_state *);

#endif

#ifndef LILY_PARSER_H
# define LILY_PARSER_H

# include "lily_raiser.h"
# include "lily_ast.h"
# include "lily_lexer.h"
# include "lily_emitter.h"
# include "lily_symtab.h"
# include "lily_vm.h"
# include "lily_membuf.h"

typedef enum {
    pm_parse,
    pm_execute
} lily_parse_mode;

typedef struct lily_parse_state_t {
    lily_type **type_stack;
    uint16_t type_stack_pos;
    uint16_t type_stack_size;
    uint16_t class_depth;
    uint16_t next_lambda_id;

    char **import_paths;
    lily_import_entry *import_top;
    lily_import_entry *import_start;

    lily_membuf *membuf;
    lily_type *default_call_type;
    lily_ast_pool *ast_pool;
    lily_lex_state *lex;
    lily_emit_state *emit;
    lily_symtab *symtab;
    lily_vm_state *vm;
    lily_raiser *raiser;
    lily_mem_func mem_func;
    void *data;
    lily_parse_mode mode;
    uint32_t first_pass;
} lily_parse_state;

void lily_parser_finish_expr(lily_parse_state *);
lily_var *lily_parser_lambda_eval(lily_parse_state *, int, char *, lily_type *,
        int);
lily_var *lily_parser_dynamic_load(lily_parse_state *, lily_class *, char *);
void lily_free_parse_state(lily_parse_state *);
lily_parse_state *lily_new_parse_state(lily_mem_func, void *, int, char **);
int lily_parse_file(lily_parse_state *, lily_lex_mode, char *);
int lily_parse_string(lily_parse_state *, char *, lily_lex_mode, char *);

void lily_begin_package(lily_parse_state *, char *);
void lily_end_package(lily_parse_state *);

#endif

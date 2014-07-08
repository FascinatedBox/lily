#ifndef LILY_PARSER_H
# define LILY_PARSER_H

# include "lily_raiser.h"
# include "lily_ast.h"
# include "lily_lexer.h"
# include "lily_emitter.h"
# include "lily_symtab.h"
# include "lily_vm.h"

typedef enum {
    pm_init,
    pm_parse,
    pm_execute
} lily_parse_mode;

typedef struct {
    lily_sig **sig_stack;
    int sig_stack_pos;
    int sig_stack_size;
    lily_ast_pool *ast_pool;
    lily_lex_state *lex;
    lily_emit_state *emit;
    lily_symtab *symtab;
    lily_vm_state *vm;
    lily_raiser *raiser;
    void *data;
    lily_parse_mode mode;
} lily_parse_state;

void lily_free_parse_state(lily_parse_state *);
lily_parse_state *lily_new_parse_state(void *, int, char **);
int lily_parse_file(lily_parse_state *, lily_lex_mode, char *);
int lily_parse_string(lily_parse_state *, lily_lex_mode, char *);
int lily_parse_special(lily_parse_state *, lily_lex_mode, void *, char *,
    lily_reader_fn, lily_close_fn);
#endif

#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_ast.h"

typedef struct {
    int next_reg;
} lily_reg_data;

lily_reg_data *lily_init_reg_data(void);
void lily_free_reg_data(lily_reg_data *);
void lily_emit_ast(lily_symbol *, lily_ast *, lily_reg_data *);
void lily_emit_vm_return(lily_symbol *);

#endif

#ifndef LILY_VIRT_H
# define LILY_VIRT_H

# include "lily_value.h"

typedef struct lily_virt_state_ {
    struct lily_function_val_ ***table;
    struct lily_function_val_ **virts;
    uint16_t table_pos;
    uint16_t table_size;
    uint16_t pos;
    uint16_t size;
} lily_virt_state;

lily_virt_state *lily_new_virt_state(void);
void lily_free_virt_state(lily_virt_state *);

void lily_vs_register_virt(lily_virt_state *, lily_var *, uint16_t,
        struct lily_function_val_ *);
void lily_vs_load_parent_virts(lily_virt_state *, lily_class *);
void lily_vs_save_virts(lily_virt_state *, lily_class *);
#define lily_vs_reset_pos(vs) { vs->pos = 0; }

#endif

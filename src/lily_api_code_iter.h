#ifndef LILY_API_CODE_ITER_H
# define LILY_API_CODE_ITER_H

# include <stdint.h>

typedef struct {
    uint16_t *buffer;

    uint16_t offset;
    uint16_t stop;
    uint16_t round_total;
    uint16_t opcode;

    uint16_t line;
    uint16_t special_1;
    uint16_t counter_2;
    uint16_t inputs_3;

    uint16_t special_4;
    uint16_t outputs_5;
    uint16_t special_6;
    uint16_t jumps_7;
} lily_code_iter;

void lily_ci_init(lily_code_iter *, uint16_t *, uint16_t, uint16_t);
int lily_ci_next(lily_code_iter *);

void lily_ci_from_native(lily_code_iter *, struct lily_function_val_ *);

#endif

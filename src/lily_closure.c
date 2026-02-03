#include <string.h>

#include "lily_alloc.h"
#include "lily_closure.h"
#include "lily_code_iter.h"
#include "lily_opcode.h"

/* This sets up the table used to map from a register spot to where that spot is
   in the closure. */
static void setup_for_transform(lily_emit_state *emit,
        lily_function_val *f, int is_backing)
{
    int next_reg_spot = emit->scope_block->next_reg_spot;

    if (emit->transform_size < emit->scope_block->next_reg_spot) {
        emit->transform_table = lily_realloc(emit->transform_table,
                next_reg_spot * sizeof(*emit->transform_table));
        emit->transform_size = emit->scope_block->next_reg_spot;
    }

    memset(emit->transform_table, UINT16_MAX,
            next_reg_spot * sizeof(*emit->transform_table));

    lily_var *func_var = emit->scope_block->scope_var;
    uint16_t line_num = func_var->line_num;
    uint16_t local_count = func_var->type->subtype_count - 1;
    uint16_t i, count = 0;

    for (i = 0;
         i < lily_u16_pos(emit->closure_spots);
         i += 2) {
        if (lily_u16_get(emit->closure_spots, i + 1) == emit->function_depth) {
            uint16_t spot = lily_u16_get(emit->closure_spots, i);
            if (spot < local_count) {
                /* Make sure this parameter always exists in the closure. */
                lily_u16_write_4(emit->closure_aux_code, o_closure_set, i / 2,
                        spot, line_num);
            }

            emit->transform_table[spot] = i / 2;
            count++;
            /* This prevents other closures at this level from thinking this
               local belongs to them. */
            lily_u16_set_at(emit->closure_spots, i + 1, UINT16_MAX);
        }
    }
    /* If there are locals in one of the inner functions, write them down. This
       is later used by the vm to make sure the cells of inner functions are
       fresh. */
    if (is_backing == 0 && count) {
        uint16_t *locals = lily_malloc((count + 1) * sizeof(*locals));

        locals[0] = count + 1;

        int pos = 1;
        for (i = 0;i < next_reg_spot;i++) {
            if (emit->transform_table[i] != UINT16_MAX) {
                locals[pos] = i;
                pos++;
            }
        }

        f->proto->locals = locals;
    }
}

/* This takes a buffer (it's always the patch buffer) and checks for a record of
   'dest' inside somewhere. If not found, then 'dest' is added, along with a
   space after it. These pairings are later used to map from the original
   destination to the new destination.
   This function ensures that jumps that are added are kept in order from lowest
   to highest. The reason for that is it makes it easier for closure transform
   to step through them.
   This is a helper for closure transform: Nothing else should use it. */
static void maybe_add_jump(lily_buffer_u16 *buffer, uint16_t i, uint16_t dest)
{
    uint16_t end = lily_u16_pos(buffer);

    for (;i < end;i += 2) {
        int jump = lily_u16_get(buffer, i);

        /* Make it so jumps are in order from lowest to highest. This allows
           the transform pass to do a check-free increase of the check position
           when a spot is found. */
        if (jump > dest) {
            lily_u16_inject(buffer, i, 0);
            lily_u16_inject(buffer, i, dest);
            return;
        }
        else if (jump == dest)
            return;
    }

    lily_u16_write_2(buffer, dest, 0);
}

/* This is an ugly function that creates a code iter to determine how many
   transforms that an op performed. This information is used when patching the
   jump to an opcode, so that the jump is pulled back to account for
   o_closure_get transforms. */
static int count_transforms(lily_emit_state *emit, int start)
{
    lily_code_iter ci;
    lily_ci_init(&ci, emit->code->data, start, lily_u16_pos(emit->code));
    lily_ci_next(&ci);
    uint16_t *buffer = ci.buffer;
    uint16_t *transform_table = emit->transform_table;
    lily_opcode op = buffer[ci.offset];
    int pos = ci.offset + 1;
    int count = 0;

    if (op == o_call_register &&
        transform_table[buffer[pos]] != UINT16_MAX)
        count++;

    pos += ci.special_1 + ci.counter_2;

    if (ci.inputs_3) {
        int i;
        for (i = 0;i < ci.inputs_3;i++) {
            if (transform_table[buffer[pos + i]] != UINT16_MAX)
                count++;
        }
    }

    return count;
}

uint16_t iter_for_first_line(lily_emit_state *emit, uint16_t pos)
{
    uint16_t result = 0;
    lily_code_iter ci;
    lily_ci_init(&ci, emit->code->data, pos, lily_u16_pos(emit->code));

    while (lily_ci_next(&ci)) {
        if (ci.line_6) {
            pos = ci.offset + ci.round_total - ci.line_6;
            result = ci.buffer[pos];
            break;
        }
    }

    return result;
}

/* This function is called to transform the currently available segment of code
   (emit->block->code_start up to emit->code_pos) into code that will work for
   closures. */
void lily_cl_transform(lily_emit_state *emit, lily_block *scope_block,
        lily_function_val *f)
{
    if (emit->closure_aux_code == NULL)
        emit->closure_aux_code = lily_new_buffer_u16(8);
    else
        lily_u16_set_pos(emit->closure_aux_code, 0);

    uint16_t iter_start = emit->block->code_start;
    int is_backing = (scope_block->flags & BLOCK_CLOSURE_ORIGIN);
    uint16_t first_line = iter_for_first_line(emit, iter_start);

    if (is_backing) {
        /* Put the closure into a new register so the gc can't accidentally
           delete it. */
        uint16_t closure_reg = scope_block->next_reg_spot;

        scope_block->next_reg_spot++;
        lily_u16_write_4(emit->closure_aux_code, o_closure_new,
                lily_u16_pos(emit->closure_spots) / 2, closure_reg,
                first_line);

        lily_storage *self = scope_block->self;

        if (self && self->closure_spot != UINT16_MAX) {
            /* Class constructors don't allow closing over their self and enums
               don't have a constructor. If the backing closure has self inside,
               then it has to come from a class/enum method. Those methods will
               always have self as the first spot, hence the zero. */
            lily_u16_write_4(emit->closure_aux_code, o_closure_set,
                    self->closure_spot, 0, first_line);
        }
    }
    else if (emit->block->self) {
        /* Pull self from the closure into the proper register. */
        lily_storage *block_self = emit->block->self;

        lily_u16_write_4(emit->closure_aux_code, o_closure_get,
                block_self->closure_spot, block_self->reg_spot, first_line);
    }

    setup_for_transform(emit, f, is_backing);

    if (is_backing)
        lily_u16_set_pos(emit->closure_spots, 0);

    lily_code_iter ci;
    lily_ci_init(&ci, emit->code->data, iter_start, lily_u16_pos(emit->code));
    uint16_t *transform_table = emit->transform_table;
    uint16_t jump_adjust = 0;

/* If the input at the position given by 'x' is within the closure, then write
   an instruction to fetch it from the closure first. This makes sure that if
   this local is in the closure, it needs to read from the closure first so that
   any assignment to it as an upvalue will be reflected. */
#define MAYBE_TRANSFORM_INPUT(x, z) \
{ \
    uint16_t id = transform_table[buffer[x]]; \
    if (id != UINT16_MAX) { \
        lily_u16_write_4(emit->closure_aux_code, z, id, \
                buffer[x], first_line); \
        jump_adjust += 4; \
    } \
}

    uint16_t *buffer = ci.buffer;
    uint16_t patch_start = lily_u16_pos(emit->patches);
    int i, pos;

    /* Begin by creating a listing of all jump destinations, which is organized
       from lowest to highest. Each entry has a spot after it to hold where that
       jump is patched to. The spots will be filled during transformation. */
    while (lily_ci_next(&ci)) {
        if (ci.jumps_5) {
            int stop = ci.offset + ci.round_total - ci.line_6;

            for (i = stop - ci.jumps_5;i < stop;i++) {
                int jump = (int16_t)buffer[i];
                /* Catching opcodes write a jump to 0 to let vm know that there
                   is no next catch branch. Do not patch those. */
                if (jump == 0)
                    continue;

                maybe_add_jump(emit->patches, patch_start, ci.offset + jump);
            }
        }
    }

    /* Add an impossible jump to act as a terminator. */
    lily_u16_write_2(emit->patches, UINT16_MAX, 0);

    uint16_t patch_stop = lily_u16_pos(emit->patches);
    uint16_t patch_iter = patch_start;
    uint16_t next_jump = lily_u16_get(emit->patches, patch_iter);

    lily_ci_init(&ci, emit->code->data, iter_start, lily_u16_pos(emit->code));
    while (lily_ci_next(&ci)) {
        int output_start = 0;

        lily_opcode op = buffer[ci.offset];
        /* +1 to skip over the opcode itself. */
        pos = ci.offset + 1;

        if (ci.special_1) {
            switch (op) {
                case o_call_register:
                    MAYBE_TRANSFORM_INPUT(pos, o_closure_get)
                default:
                    pos += ci.special_1;
                    break;
            }
        }

        pos += ci.counter_2;

        if (ci.inputs_3) {
            for (i = 0;i < ci.inputs_3;i++) {
                MAYBE_TRANSFORM_INPUT(pos + i, o_closure_get)
            }

            pos += ci.inputs_3;
        }

        if (ci.outputs_4) {
            output_start = pos;
            pos += ci.outputs_4;
        }

        i = ci.offset;
        if (i == next_jump) {
            /* This op is a jump target. Write where it transform put it, and
               setup to look for the next jump. Remember that there's an
               impossible jump as the terminator, so there's no need for a
               length check here. */
            lily_u16_set_at(emit->patches, patch_iter + 1,
                    lily_u16_pos(emit->closure_aux_code));
            patch_iter += 2;
            next_jump = lily_u16_get(emit->patches, patch_iter);
        }

        int stop = ci.offset + ci.round_total - ci.jumps_5 - ci.line_6;
        for (;i < stop;i++)
            lily_u16_write_1(emit->closure_aux_code, buffer[i]);

        if (ci.jumps_5) {
            for (i = 0;i < ci.jumps_5;i++) {
                /* This is the absolute position of this jump, but within the
                   original buffer. */
                int distance = (int16_t)buffer[stop + i];

                /* Exceptions write 0 as their last jump to note that handling
                   should stop. Don't patch those 0's. */
                if (distance) {
                    int destination = ci.offset + distance;

                    /* Jumps are recorded in pairs. The next pass will take the
                       resulting position, and calculate how to get to the
                       destination.
                       Do note: Jumps are relative to the position of the
                       opcode, for the sake of the vm. So include an offset from
                       the opcode for use in the calculation. */
                    lily_u16_write_2(emit->patches,
                            lily_u16_pos(emit->closure_aux_code),
                            ci.round_total - ci.jumps_5 - ci.line_6 + i);

                    lily_u16_write_1(emit->closure_aux_code, (uint16_t)destination);
                }
                else
                    lily_u16_write_1(emit->closure_aux_code, 0);
            }
        }

        if (ci.line_6)
            lily_u16_write_1(emit->closure_aux_code, buffer[pos]);

        if (ci.outputs_4) {
            int output_stop = output_start + ci.outputs_4;

            for (i = output_start;i < output_stop;i++) {
                MAYBE_TRANSFORM_INPUT(i, o_closure_set)
            }
        }
    }

    /* It's time to patch the unfixed jumps, if there are any. The area from
       patch_stop to the ending position contains jumps to be fixed. */
    uint16_t j;
    for (j = patch_stop;j < lily_u16_pos(emit->patches);j += 2) {
        /* This is where, in the new code, that the jump is located. */
        int aux_pos = lily_u16_get(emit->patches, j);
        /* This has been set to an absolute destination in old code. */
        uint16_t original = lily_u16_get(emit->closure_aux_code, aux_pos);
        int k;

        for (k = patch_start;k < patch_stop;k += 2) {
            if (original == lily_u16_get(emit->patches, k)) {
                int tx_offset = count_transforms(emit, original) * 4;

                /* Note that this is going to be negative for back jumps. */
                int new_jump =
                        /* The new destination */
                        lily_u16_get(emit->patches, k + 1)
                        /* The location */
                        - aux_pos
                        /* The distance between aux_pos and its opcode. */
                        + lily_u16_get(emit->patches, j + 1)
                        /* How far to go back to include upvalue reads. */
                        - tx_offset;

                lily_u16_set_at(emit->closure_aux_code, aux_pos,
                        (int16_t)new_jump);
                break;
            }
        }
    }

    lily_u16_set_pos(emit->patches, patch_start);
}

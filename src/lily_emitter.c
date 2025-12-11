#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "lily_alloc.h"
#include "lily_code_iter.h"
#include "lily_emitter.h"
#include "lily_opcode.h"
#include "lily_parser.h"

extern lily_type *lily_question_type;
extern lily_type *lily_scoop_type;
extern lily_class *lily_self_class;
extern lily_type *lily_unit_type;
extern lily_type *lily_unset_type;

/***
 *      ____       _
 *     / ___|  ___| |_ _   _ _ __
 *     \___ \ / _ \ __| | | | '_ \
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/
 *                          |_|
 */

static lily_proto_stack *new_proto_stack(uint16_t);
static void free_proto_stack(lily_proto_stack *);
static lily_storage_stack *new_storage_stack(uint16_t);
static void free_storage_stack(lily_storage_stack *);
static void clear_storages(lily_storage_stack *, uint16_t);

lily_emit_state *lily_new_emit_state(lily_symtab *symtab, lily_raiser *raiser)
{
    lily_emit_state *emit = lily_malloc(sizeof(*emit));
    lily_block *main_block = lily_malloc(sizeof(*main_block));

    emit->block = main_block;
    emit->closure_aux_code = NULL;
    emit->closure_spots = lily_new_buffer_u16(4);
    emit->code = lily_new_buffer_u16(32);
    emit->expr_num = 1;
    emit->expr_strings = lily_new_string_pile();
    emit->function_depth = 1;
    emit->match_cases = lily_new_buffer_u16(4);
    emit->patches = lily_new_buffer_u16(4);
    emit->protos = new_proto_stack(4);
    emit->raiser = raiser;
    emit->scope_block = main_block;
    emit->storages = new_storage_stack(4);
    emit->self_storages = new_storage_stack(2);
    emit->symtab = symtab;
    emit->tm = lily_new_type_maker();
    emit->transform_size = 0;
    emit->transform_table = NULL;
    emit->ts = lily_new_type_system(emit->tm);

    main_block->block_type = block_file;
    main_block->class_entry = NULL;
    main_block->code_start = 0;
    main_block->forward_class_count = 0;
    main_block->forward_count = 0;
    main_block->generic_start = 0;
    main_block->next = NULL;
    main_block->next_reg_spot = 0;
    main_block->prev = NULL;
    main_block->prev_scope_block = NULL;
    main_block->self = NULL;
    main_block->storage_count = 0;
    main_block->var_count = 0;

    /* This prevents checking for 'self' from going too far. */
    main_block->flags = BLOCK_SELF_ORIGIN;
    return emit;
}

void lily_rewind_emit_state(lily_emit_state *emit)
{
    lily_block *block_iter = emit->scope_block;
    lily_block *main_block = block_iter;
    lily_storage_stack *stack = emit->storages;
    uint16_t total = stack->start + block_iter->storage_count;

    while (block_iter) {
        lily_block *block_next = block_iter->prev_scope_block;

        if (block_next == NULL)
            break;

        block_iter = block_next;
    }

    main_block = block_iter;

    /* Storages above `__main__` need to be cleared, or they'll be reused
       without fixing their ids. */
    stack->start = main_block->storage_count;
    clear_storages(stack, total - stack->start);
    stack->start = 0;
    emit->self_storages->start = 0;

    /* Now the buffers and the emitter. */
    emit->block->forward_class_count = 0;
    emit->block->forward_count = 0;
    emit->block = main_block;
    emit->function_depth = 1;
    emit->scope_block = main_block;
    lily_u16_set_pos(emit->closure_spots, 0);
    lily_u16_set_pos(emit->code, 0);
    lily_u16_set_pos(emit->match_cases, 0);
    lily_u16_set_pos(emit->patches, 0);
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_block *current = emit->block;

    while (current && current->prev)
        current = current->prev;

    while (current) {
        lily_block *temp = current->next;

        lily_free(current);
        current = temp;
    }

    if (emit->closure_aux_code)
        lily_free_buffer_u16(emit->closure_aux_code);

    free_proto_stack(emit->protos);
    free_storage_stack(emit->storages);
    free_storage_stack(emit->self_storages);
    lily_free(emit->transform_table);
    lily_free_buffer_u16(emit->closure_spots);
    lily_free_buffer_u16(emit->code);
    lily_free_buffer_u16(emit->match_cases);
    lily_free_buffer_u16(emit->patches);
    lily_free_string_pile(emit->expr_strings);
    lily_free_type_maker(emit->tm);
    lily_free_type_system(emit->ts);
    lily_free(emit);
}

/***
 *     __        __    _ _   _
 *     \ \      / / __(_) |_(_)_ __   __ _
 *      \ \ /\ / / '__| | __| | '_ \ / _` |
 *       \ V  V /| |  | | |_| | | | | (_| |
 *        \_/\_/ |_|  |_|\__|_|_| |_|\__, |
 *                                   |___/
 */

static lily_storage *get_storage(lily_emit_state *, lily_type *);
static lily_block *find_deepest_loop(lily_emit_state *);
static void eval_tree(lily_emit_state *, lily_ast *, lily_type *);

void lily_emit_write_class_init(lily_emit_state *emit)
{
    lily_storage *self = emit->scope_block->self;

    lily_u16_write_4(emit->code, o_instance_new, self->type->cls_id,
            self->reg_spot, *emit->lex_linenum);
}

void lily_emit_write_shorthand_ctor(lily_emit_state *emit, lily_class *cls,
        lily_var *var_iter)
{
    lily_named_sym *prop_iter = cls->members;
    uint16_t self_reg_spot = emit->scope_block->self->reg_spot;

    /* The class constructor always inserts itself as the first property. Make
       sure to not include that. */

    while (prop_iter->item_kind == ITEM_PROPERTY) {
        while (strcmp(var_iter->name, "") != 0)
            var_iter = var_iter->next;

        lily_u16_write_5(emit->code, o_property_set, prop_iter->reg_spot,
                self_reg_spot, var_iter->reg_spot, *emit->lex_linenum);

        prop_iter->flags &= ~SYM_NOT_INITIALIZED;
        var_iter = var_iter->next;
        prop_iter = prop_iter->next;
    }
}

void lily_emit_write_for_header(lily_emit_state *emit, lily_var *user_loop_var,
        lily_var *for_start, lily_var *for_end, lily_var *for_step,
        uint16_t line_num)
{
    lily_sym *target;
    int need_sync = user_loop_var->flags & VAR_IS_GLOBAL;

    if (need_sync) {
        lily_class *cls = emit->symtab->integer_class;
        /* o_for_integer expects the target register to be a local. Since it
           isn't, do syncing reads before and after to make sure the user's
           loop var is what it should be. */
        target = (lily_sym *)get_storage(emit, cls->self_type);
    }
    else
        target = (lily_sym *)user_loop_var;

    lily_u16_write_6(emit->code, o_for_setup, for_start->reg_spot,
            for_end->reg_spot, for_step->reg_spot, target->reg_spot, line_num);

    if (need_sync) {
        lily_u16_write_4(emit->code, o_global_set, target->reg_spot,
                user_loop_var->reg_spot, line_num);
    }

    /* Fix the start so the continue doesn't reinitialize loop vars. */
    emit->block->code_start = lily_u16_pos(emit->code);

    lily_u16_write_5(emit->code, o_for_integer, for_start->reg_spot,
            for_end->reg_spot, for_step->reg_spot, target->reg_spot);

    lily_u16_write_2(emit->code, 5, line_num);

    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 2);

    if (need_sync) {
        lily_u16_write_4(emit->code, o_global_set, target->reg_spot,
                user_loop_var->reg_spot, line_num);
    }
}

void lily_emit_write_for_of(lily_emit_state *emit, lily_var *for_source,
        lily_var *for_backing, lily_var *for_index, lily_var *for_elem,
        uint16_t line_num)
{
    uint16_t opcode;

    if (for_source->type->cls_id == LILY_ID_LIST)
        opcode = o_for_list_step;
    else
        opcode = o_for_text_step;

    /* This needs to do a negative step like o_for_integer. However, since the
       first index is always 0 and the step is always 1, just start at -1. */
    lily_u16_write_4(emit->code, o_load_integer, (uint16_t)-1,
            for_backing->reg_spot, line_num);

    /* Fix the start so the continue doesn't reinitialize loop vars. */
    emit->block->code_start = lily_u16_pos(emit->code);

    /* The patched jump will need 4 spaces of adjustment. */
    lily_u16_write_6(emit->code, opcode, for_source->reg_spot,
            for_backing->reg_spot, for_elem->reg_spot, 4, line_num);
    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 2);

    if (for_index != for_backing) {
        lily_u16_write_4(emit->code, o_assign_noref, for_backing->reg_spot,
                for_index->reg_spot, line_num);
    }
}

/* This is called before 'continue', 'break', or 'return' is written. It writes
   the appropriate number of try+catch pop instructions to offset the movement.
   A search is done from the current block down to 'stop_block' to find out how
   many try pop's to write. */
static void write_pop_try_blocks_up_to(lily_emit_state *emit,
        lily_block *stop_block)
{
    lily_block *block_iter = emit->block;
    int try_count = 0;

    while (block_iter != stop_block) {
        if (block_iter->block_type == block_try &&
            (block_iter->flags & BLOCK_HAS_BRANCH) == 0)
            try_count++;

        block_iter = block_iter->prev;
    }

    if (try_count) {
        int i;
        for (i = 0;i < try_count;i++)
            lily_u16_write_1(emit->code, o_catch_pop);
    }
}

/* The parser has a 'break' and wants the emitter to write the code. */
int lily_emit_try_write_break(lily_emit_state *emit)
{
    lily_block *block = find_deepest_loop(emit);

    if (block == NULL)
        return 0;

    write_pop_try_blocks_up_to(emit, block);
    lily_u16_write_2(emit->code, o_jump, 1);

    uint16_t patch = lily_u16_pos(emit->code) - 1;

    if (emit->block == block)
        lily_u16_write_1(emit->patches, patch);
    else {
        lily_u16_inject(emit->patches, block->next->patch_start, patch);

        /* The blocks after the one that got the new patch need to have their
           starts adjusted or they'll think it belongs to them. */
        for (block = block->next; block; block = block->next)
            block->patch_start++;
    }

    return 1;
}

/* The parser has a 'continue' and wants the emitter to write the code. */
int lily_emit_try_write_continue(lily_emit_state *emit)
{
    lily_block *loop_block = find_deepest_loop(emit);

    if (loop_block == NULL)
        return 0;

    write_pop_try_blocks_up_to(emit, loop_block);

    uint16_t where = loop_block->code_start - lily_u16_pos(emit->code);
    lily_u16_write_2(emit->code, o_jump, (uint16_t)where);
    return 1;
}

/* Write a conditional jump. 0 means jump if false, 1 means jump if true. The
   ast is the thing to test. */
static void emit_jump_if(lily_emit_state *emit, lily_ast *ast, uint16_t jump_on)
{
    lily_u16_write_4(emit->code, o_jump_if, jump_on, ast->result->reg_spot, 3);

    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);
}

/* If no patches have been written since 'start', this does nothing.

   Otherwise, this fixes patches written after 'start', as well as 'start'
   itself. They're fixed to the current code position. */
static void write_patches_since(lily_emit_state *emit, uint16_t start)
{
    uint16_t pos = lily_u16_pos(emit->code);
    uint16_t stop = lily_u16_pos(emit->patches);
    uint16_t iter;

    for (iter = start;iter != stop;iter++) {
        uint16_t patch = lily_u16_get(emit->patches, iter);

        /* Skip 0's (those are patches that have been optimized out.
           Here's a bit of math: If the vm is at 'x' and wants to get to 'y', it
           can add 'y - x' to 'x', and have 'y'. Cool, right?
           The trouble is that jump positions may be +1 or +2 relative to the
           position of the opcode.
           This problem is worked around by having jumps write down their offset
           to the opcode, and including that in the jump. */
        if (patch != 0) {
            uint16_t adjust = lily_u16_get(emit->code, patch);

            lily_u16_set_at(emit->code, patch, pos + adjust - patch);
        }
    }

    lily_u16_set_pos(emit->patches, start);
}

/***
 *      ____  _
 *     / ___|| |_ ___  _ __ __ _  __ _  ___  ___
 *     \___ \| __/ _ \| '__/ _` |/ _` |/ _ \/ __|
 *      ___) | || (_) | | | (_| | (_| |  __/\__ \
 *     |____/ \__\___/|_|  \__,_|\__, |\___||___/
 *                               |___/
 */

static lily_storage *new_storage(void)
{
    lily_storage *result = lily_malloc(sizeof(*result));

    result->type = NULL;
    result->expr_num = 0;
    result->flags = 0;
    result->item_kind = ITEM_STORAGE;

    return result;
}

/** Storages are used to hold intermediate values. The emitter is responsible
    for handing them out, controlling their position, and making new ones.
    Most of that is done in get_storage. **/
static lily_storage_stack *new_storage_stack(uint16_t initial)
{
    lily_storage_stack *result = lily_malloc(sizeof(*result));
    uint16_t i;

    result->data = lily_malloc(initial * sizeof(*result->data));

    for (i = 0;i < initial;i++) {
        lily_storage *s = new_storage();

        result->data[i] = s;
    }

    result->start = 0;
    result->size = initial;
    return result;
}

static void free_storage_stack(lily_storage_stack *stack)
{
    uint16_t i;

    for (i = 0;i < stack->size;i++)
        lily_free(stack->data[i]);

    lily_free(stack->data);
    lily_free(stack);
}

static void grow_storages(lily_storage_stack *stack)
{
    uint16_t new_size = stack->size * 2;
    lily_storage **new_data = lily_realloc(stack->data,
            sizeof(*new_data) * new_size);
    uint16_t i;

    /* Storages are taken pretty often, so eagerly initialize them for a little
       bit more speed. */
    for (i = stack->size;i < new_size;i++)
        new_data[i] = new_storage();

    stack->data = new_data;
    stack->size = new_size;
}

/* When a callable block exits, the storages need to be cleared. Clearing the
   storages prevents outer functions from using storages with wrong ids, which
   leads to very bad results. */
static void clear_storages(lily_storage_stack *stack, uint16_t count)
{
    uint16_t i;

    for (i = stack->start;i < stack->start + count;i++)
        stack->data[i]->type = NULL;
}

/* This attempts to grab a storage of the given type. It will first attempt to
   get a used storage, then a new one. */
static lily_storage *get_storage(lily_emit_state *emit, lily_type *type)
{
    lily_storage_stack *stack = emit->storages;
    uint32_t expr_num = emit->expr_num;
    uint16_t i;
    lily_storage *s = NULL;

    for (i = stack->start;i < stack->size;i++) {
        s = stack->data[i];

        /* A storage with a type of NULL is not in use and can be claimed. */
        if (s->type == NULL) {
            s->type = type;
            s->flags = SYM_NOT_ASSIGNABLE;

            s->reg_spot = emit->scope_block->next_reg_spot;
            emit->scope_block->next_reg_spot++;

            i++;
            if (i == stack->size)
                grow_storages(emit->storages);

            emit->scope_block->storage_count++;

            break;
        }
        else if (s->type == type &&
                 s->expr_num != expr_num) {
            s->expr_num = expr_num;
            s->flags = SYM_NOT_ASSIGNABLE;
            break;
        }
    }

    s->expr_num = expr_num;

    return s;
}

/***
 *      ____  _            _
 *     | __ )| | ___   ___| | _____
 *     |  _ \| |/ _ \ / __| |/ / __|
 *     | |_) | | (_) | (__|   <\__ \
 *     |____/|_|\___/ \___|_|\_\___/
 *
 */

static void perform_closure_transform(lily_emit_state *, lily_block *,
        lily_function_val *);

static lily_block *next_block(lily_emit_state *emit)
{
    lily_block *new_block;
    if (emit->block->next == NULL) {
        new_block = lily_malloc(sizeof(*new_block));

        emit->block->next = new_block;
        new_block->prev = emit->block;
        new_block->next = NULL;
    }
    else
        new_block = emit->block->next;

    new_block->class_entry = emit->block->class_entry;
    new_block->self = NULL;
    new_block->patch_start = lily_u16_pos(emit->patches);

    /* This can't be 0, or `define f: Integer {}` passes if no code has been
       written before it. */
    new_block->last_exit = UINT16_MAX;
    new_block->flags = 0;
    new_block->var_count = 0;
    new_block->code_start = lily_u16_pos(emit->code);
    new_block->forward_count = 0;

    return new_block;
}

static void setup_scope_block(lily_emit_state *emit, lily_block *new_block)
{
    new_block->prev_scope_block = emit->scope_block;
    new_block->next_reg_spot = 0;
    new_block->storage_count = 0;
    new_block->code_start = lily_u16_pos(emit->code);

    emit->storages->start += emit->scope_block->storage_count;
    emit->scope_block = new_block;
    emit->block = new_block;
}

void lily_emit_enter_anon_block(lily_emit_state *emit)
{
    lily_block *block = next_block(emit);

    block->block_type = block_anon;
    emit->block = block;
}

void lily_emit_enter_class_block(lily_emit_state *emit, lily_var *var)
{
    lily_block *block = next_block(emit);

    block->flags |= BLOCK_SELF_ORIGIN;
    block->block_type = block_class;
    block->scope_var = var;
    block->class_entry = var->parent;
    setup_scope_block(emit, block);
    emit->function_depth++;
}

void lily_emit_enter_define_block(lily_emit_state *emit, lily_var *var,
        uint16_t generic_start)
{
    lily_block *block = next_block(emit);
    lily_block_type scope_block_type = emit->scope_block->block_type;

    if (scope_block_type & (SCOPE_CLASS | SCOPE_ENUM))
        block->flags |= BLOCK_CLOSURE_ORIGIN | BLOCK_SELF_ORIGIN;
    else if (scope_block_type == block_file)
        block->flags |= BLOCK_CLOSURE_ORIGIN;
    else if (scope_block_type == block_define)
        /* This var will need upvalues if it's called. */
        var->flags |= VAR_NEEDS_CLOSURE;

    block->block_type = block_define;
    block->generic_start = generic_start;
    block->scope_var = var;
    setup_scope_block(emit, block);
    emit->function_depth++;

    if (var->parent && (var->flags & VAR_IS_STATIC) == 0) {
        lily_emit_create_block_self(emit, var->parent->self_type);
        lily_emit_activate_block_self(emit);
    }
}

void lily_emit_enter_do_while_block(lily_emit_state *emit)
{
    lily_block *block = next_block(emit);

    block->block_type = block_do_while;
    emit->block = block;
}

void lily_emit_enter_enum_block(lily_emit_state *emit, lily_class *cls)
{
    lily_block *block = next_block(emit);

    /* Enum blocks exist as scope blocks so that enum methods know they're enum
       methods. They don't have a var since they don't execute code. */
    block->block_type = block_enum;
    block->class_entry = cls;
    setup_scope_block(emit, block);
    emit->function_depth++;
}

void lily_emit_enter_expr_match_block(lily_emit_state *emit)
{
    lily_block *block = next_block(emit);

    block->block_type = block_expr_match;
    emit->block = block;
}

void lily_emit_enter_file_block(lily_emit_state *emit, lily_var *var)
{
    lily_block *block = next_block(emit);

    block->forward_class_count = 0;
    block->generic_start = 0;
    block->block_type = block_file;
    block->scope_var = var;
    setup_scope_block(emit, block);
    /* Don't bump depth so these vars are seen as global vars. */
}

void lily_emit_enter_for_in_block(lily_emit_state *emit)
{
    lily_block *block = next_block(emit);

    block->block_type = block_for_in;
    emit->block = block;
}

void lily_emit_enter_if_block(lily_emit_state *emit)
{
    lily_block *block = next_block(emit);

    block->flags |= BLOCK_ALWAYS_EXITS;
    block->block_type = block_if;
    emit->block = block;
}

void lily_emit_enter_lambda_block(lily_emit_state *emit, lily_var *var)
{
    lily_block *block = next_block(emit);
    lily_block_type scope_block_type = emit->scope_block->block_type;

    if (scope_block_type == block_class)
        block->flags |= BLOCK_CLOSURE_ORIGIN | BLOCK_SELF_ORIGIN;
    else if (scope_block_type == block_file)
        block->flags |= BLOCK_CLOSURE_ORIGIN;

    block->block_type = block_lambda;
    block->scope_var = var;
    setup_scope_block(emit, block);
    emit->function_depth++;
}

void lily_emit_enter_match_block(lily_emit_state *emit, lily_sym *sym)
{
    lily_block *block = next_block(emit);

    block->flags |= BLOCK_ALWAYS_EXITS;
    block->block_type = block_match;
    block->last_exit = lily_u16_pos(emit->code);
    block->match_case_start = lily_u16_pos(emit->match_cases);
    block->match_reg = sym->reg_spot;
    block->match_type = sym->type;
    emit->block = block;

    /* Branch switching expects a patch, so write a fake one to skip over. */
    lily_u16_write_1(emit->patches, 0);
}

void lily_emit_enter_try_block(lily_emit_state *emit)
{
    lily_block *block = next_block(emit);

    block->flags |= BLOCK_ALWAYS_EXITS;
    block->block_type = block_try;
    emit->block = block;

    /* Each branch of a try block contains a jump to the next branch. */
    lily_u16_write_3(emit->code, o_catch_push, 1, *emit->lex_linenum);
    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 2);
}

void lily_emit_enter_while_block(lily_emit_state *emit)
{
    lily_block *block = next_block(emit);

    block->block_type = block_while;
    emit->block = block;
}

void lily_emit_enter_with_block(lily_emit_state *emit, lily_sym *sym)
{
    lily_block *block = next_block(emit);

    block->flags |= BLOCK_ALWAYS_EXITS;
    block->block_type = block_with;
    block->last_exit = lily_u16_pos(emit->code);
    block->match_case_start = lily_u16_pos(emit->match_cases);
    block->match_reg = sym->reg_spot;
    block->match_type = sym->type;
    emit->block = block;

    /* Branch switching expects a patch, so write a fake one to skip over. */
    lily_u16_write_1(emit->patches, 0);
}

void lily_emit_leave_block(lily_emit_state *emit)
{
    lily_block *block = emit->block;
    int block_type = block->block_type;

    /* These blocks need to jump back up when the bottom is hit. */
    if (block_type == block_while ||
        block_type == block_for_in) {
        int x = block->code_start - lily_u16_pos(emit->code);
        lily_u16_write_2(emit->code, o_jump, (uint16_t)x);
    }
    else if (block_type == block_match ||
             block_type == block_with)
        lily_u16_set_pos(emit->match_cases, emit->block->match_case_start);
    else if (block_type == block_try) {
        if ((block->flags & BLOCK_HAS_BRANCH) == 0)
            lily_raise_syn(emit->raiser,
                    "try blocks must have at least one except.");

        /* The vm expects that the last except block will have a 'next' of 0 to
           indicate the end of the 'except' chain. Remove the patch that the
           last except block installed so it doesn't get patched. */
        lily_u16_set_at(emit->code, lily_u16_pop(emit->patches), 0);
    }

    if ((block->flags & BLOCK_ALWAYS_EXITS) &&
        (block->flags & BLOCK_FINAL_BRANCH) &&
        block->last_exit == lily_u16_pos(emit->code)) {
        emit->block->prev->last_exit = lily_u16_pos(emit->code);
    }

    write_patches_since(emit, block->patch_start);
    emit->block = emit->block->prev;
}

static void finish_block_code(lily_emit_state *emit)
{
    lily_block *block = emit->scope_block;
    lily_var *var = block->scope_var;
    lily_value *v = lily_literal_at(emit->symtab, var->reg_spot);
    lily_function_val *f = v->value.function;

    uint16_t code_start, code_size;
    uint16_t *source;

    if ((block->flags & BLOCK_MAKE_CLOSURE) == 0) {
        code_start = emit->block->code_start;
        code_size = lily_u16_pos(emit->code) - emit->block->code_start;

        source = emit->code->data;
    }
    else {
        perform_closure_transform(emit, block, f);

        if ((block->flags & BLOCK_CLOSURE_ORIGIN) == 0)
            block->prev_scope_block->flags |= BLOCK_MAKE_CLOSURE;

        code_start = 0;
        code_size = lily_u16_pos(emit->closure_aux_code);
        source = emit->closure_aux_code->data;
    }

    uint16_t *code = lily_malloc((code_size + 1) * sizeof(*code));

    memcpy(code, source + code_start, sizeof(*code) * code_size);

    f->code_len = code_size;
    f->code = code;
    f->proto->code = code;
    f->reg_count = block->next_reg_spot;
}

static int try_write_define_exit(lily_emit_state *emit, lily_type *type,
        uint16_t line_num)
{
    int result = 1;

    if (type == lily_unit_type)
        lily_u16_write_2(emit->code, o_return_unit, line_num);
    else if (type == lily_self_class->self_type)
        /* The implicit 'self' of a class method is always first (at 0). */
        lily_u16_write_3(emit->code, o_return_value, 0, line_num);
    else
        result = 0;

    return result;
}

void lily_emit_leave_class_block(lily_emit_state *emit)
{
    uint16_t self_reg = emit->block->self->reg_spot;

    lily_u16_write_3(emit->code, o_return_value, self_reg, *emit->lex_linenum);
    finish_block_code(emit);
    lily_emit_leave_scope_block(emit);
}

void lily_emit_leave_define_block(lily_emit_state *emit)
{
    lily_block *block = emit->block;
    lily_type *type = block->scope_var->type->subtypes[0];

    if (block->last_exit != lily_u16_pos(emit->code) &&
        try_write_define_exit(emit, type, *emit->lex_linenum) == 0)
        lily_raise_syn(emit->raiser,
                "Missing return statement at end of function.");

    finish_block_code(emit);
    lily_emit_leave_scope_block(emit);
}

void lily_emit_leave_expr_match_block(lily_emit_state *emit)
{
    lily_parser_hide_match_vars(emit->parser);
    emit->block = emit->block->prev;
}

void lily_emit_leave_import_block(lily_emit_state *emit, uint16_t last_line)
{
    lily_var *var = emit->scope_block->scope_var;

    lily_u16_write_2(emit->code, o_return_unit, last_line);
    finish_block_code(emit);

    /* These blocks don't bump the depth since that's used for determining
       globals. Bump the depth to offset the drop that this function does. */
    emit->function_depth++;
    lily_emit_leave_scope_block(emit);

    lily_storage *s = get_storage(emit, lily_unit_type);

    lily_u16_write_5(emit->code, o_call_native, var->reg_spot, 0, s->reg_spot,
            *emit->lex_linenum);
}

void lily_emit_leave_lambda_block(lily_emit_state *emit)
{
    if (emit->block->last_exit != lily_u16_pos(emit->code))
        lily_u16_write_2(emit->code, o_return_unit, *emit->lex_linenum);

    finish_block_code(emit);
    lily_emit_leave_scope_block(emit);
}

void lily_emit_leave_scope_block(lily_emit_state *emit)
{
    lily_block *block = emit->block;

    lily_u16_set_pos(emit->code, block->code_start);
    clear_storages(emit->storages, block->storage_count);
    emit->self_storages->start -= (block->self != NULL);
    emit->scope_block = block->prev_scope_block;
    emit->storages->start -= emit->scope_block->storage_count;

    emit->block = emit->block->prev;
    emit->function_depth--;
}

int lily_emit_try_leave_match_block(lily_emit_state *emit)
{
    lily_block *block = emit->block;

    if (block->flags & BLOCK_FINAL_BRANCH) {
        lily_emit_leave_block(emit);
        return 1;
    }

    lily_class *match_class = block->match_type->cls;
    lily_msgbuf *msgbuf = lily_mb_flush(emit->raiser->aux_msgbuf);

    if ((match_class->item_kind & ITEM_IS_CLASS)) {
        lily_mb_add(msgbuf, "Match against a class must have an 'else' case.");
        return 0;
    }

    lily_named_sym *sym_iter = match_class->members;
    lily_buffer_u16 *match_cases = emit->match_cases;
    uint16_t case_start = block->match_case_start;
    uint16_t case_end = lily_u16_pos(emit->match_cases);
    uint16_t i;

    lily_mb_add(msgbuf,
            "Match pattern not exhaustive. The following case(s) are missing:");

    while (sym_iter) {
        if (sym_iter->item_kind & ITEM_IS_VARIANT) {
            for (i = case_start;i < case_end;i++) {
                if (sym_iter->id == lily_u16_get(match_cases, i))
                    break;
            }

            if (i == case_end)
                lily_mb_add_fmt(msgbuf, "\n* %s", sym_iter->name);
        }

        sym_iter = sym_iter->next;
    }

    return 0;
}

static lily_block *find_deepest_loop(lily_emit_state *emit)
{
    lily_block *block_iter = emit->block;
    lily_block *stop_block = emit->scope_block;
    lily_block *result = NULL;

    for (;block_iter != stop_block;block_iter = block_iter->prev) {
        if (block_iter->block_type == block_while ||
            block_iter->block_type == block_do_while ||
            block_iter->block_type == block_for_in) {
            result = block_iter;
            break;
        }
    }

    return result;
}

/* This handles branch changes for if, match, and try. Each condition finishes
   by writing a jump to the next branch in case of failure. A patch of zero is
   used as a placeholder in case of, for example, a branch that does not have a
   jump because it is always taken. */
void lily_emit_branch_switch(lily_emit_state *emit)
{
    lily_block *block = emit->block;
    uint16_t patch = lily_u16_pop(emit->patches);

    /* The spot in code has an offset for the patch. */
    uint16_t adjust = lily_u16_get(emit->code, patch);

    if (block->last_exit != lily_u16_pos(emit->code)) {
        if ((block->flags & BLOCK_HAS_BRANCH) == 0 &&
            block->block_type == block_try)
            lily_u16_write_1(emit->code, o_catch_pop);

        /* Since the current branch isn't confirmed to exit, write an exit jump.
           This exit jump will persist until the block is done. */
        lily_u16_write_2(emit->code, o_jump, 1);
        lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);
        block->flags &= ~BLOCK_ALWAYS_EXITS;
    }

    if (patch != 0) {
        lily_u16_set_at(emit->code, patch,
                lily_u16_pos(emit->code) + adjust - patch);
    }

    block->flags |= BLOCK_HAS_BRANCH;
}

void lily_emit_branch_finalize(lily_emit_state *emit)
{
    lily_emit_branch_switch(emit);

    emit->block->flags |= BLOCK_FINAL_BRANCH;
}

void lily_emit_except_switch(lily_emit_state *emit, lily_class *except_cls,
        lily_var *except_var)
{
    if (except_cls->id != LILY_ID_EXCEPTION)
        lily_emit_branch_switch(emit);
    else
        lily_emit_branch_finalize(emit);

    lily_u16_write_4(emit->code, o_exception_catch, except_cls->id, 2,
            *emit->lex_linenum);
    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 2);

    if (except_var)
        lily_u16_write_2(emit->code, o_exception_store, except_var->reg_spot);
}

void lily_emit_create_block_self(lily_emit_state *emit, lily_type *self_type)
{
    lily_storage_stack *stack = emit->self_storages;
    lily_storage *self = stack->data[stack->start];

    if (stack->start + 1 == stack->size)
        grow_storages(emit->self_storages);

    stack->start++;

    /* Storages for self are in a different stack to prevent get_storage from
       picking them. They're set with a different item_type to block call result
       from using it. */
    self->item_kind = ITEM_SELF_STORAGE;
    self->type = self_type;
    self->flags = SYM_NOT_ASSIGNABLE;
    self->reg_spot = emit->scope_block->next_reg_spot;
    emit->scope_block->next_reg_spot++;

    /* This isn't cleared by default because it's the only storage that can be
       closed over. */
    self->closure_spot = UINT16_MAX;
    emit->scope_block->self = self;
}

void lily_emit_activate_block_self(lily_emit_state *emit)
{
    lily_block *block = emit->scope_block;
    lily_block_type block_type = block->block_type;
    uint16_t flags;

    if (block_type == block_class)
        flags = SELF_PROPERTY;
    else {
        lily_class *self_cls = block->self->type->cls;

        if (self_cls->item_kind & ITEM_IS_CLASS)
            flags = SELF_KEYWORD | SELF_METHOD | SELF_PROPERTY;
        else
            flags = SELF_KEYWORD | SELF_METHOD;
    }

    block->flags |= flags;
}

#define SELF_FLAGS (SELF_KEYWORD | SELF_PROPERTY | SELF_METHOD)

int lily_emit_can_use_self(lily_emit_state *emit, uint16_t flag)
{
    lily_block *block = emit->scope_block;

    /* This block has a self that supports this action. */
    if (block->flags & flag)
        return 1;

    /* Is there a self that can be pulled from a closure? */
    lily_block *origin = block;

    while ((origin->flags & BLOCK_SELF_ORIGIN) == 0)
        origin = origin->prev_scope_block;

    if ((origin->flags & flag) == 0)
        return 0;

    /* Make sure self is going to be in the closure. */
    lily_storage *origin_self = origin->self;

    if (origin_self->closure_spot == UINT16_MAX) {
        /* The resulting depth for the backing closure is always the same:
           __main__ is 1, class is 2, backing define is 3. */
        uint16_t depth = 3;

        lily_u16_write_2(emit->closure_spots, origin_self->reg_spot, depth);

        uint16_t spot = (lily_u16_pos(emit->closure_spots) - 1) / 2;

        origin_self->closure_spot = spot;
    }

    /* Create a self in this block with information copied over. */
    lily_emit_create_block_self(emit, origin_self->type);
    block->flags |= BLOCK_MAKE_CLOSURE | (origin->flags & SELF_FLAGS);
    block->self->closure_spot = origin_self->closure_spot;

    return 1;
}

#undef SELF_FLAGS

/***
 *       ____ _
 *      / ___| | ___  ___ _   _ _ __ ___  ___
 *     | |   | |/ _ \/ __| | | | '__/ _ \/ __|
 *     | |___| | (_) \__ \ |_| | | |  __/\__ \
 *      \____|_|\___/|___/\__,_|_|  \___||___/
 *
 */

/** Closures are a tricky part of the emitter. The values that are stored in a
    closure are termed its cells. A closure's cells must always be fresh. If a
    variable is assigned to 10 in one scope, then it must be seen as 10 at any
    point in any other scope.

    The current implementation of closures works by 'shadowing' assignments. If
    a value that is closed over is assigned something, then a secondary
    assignment is made to the closure cell. Similarly, if a value is read from
    and closed over, then it needs to be pulled from the closure first. This is
    complicated by not having exact metrics of what is and isn't closed over.
    A define within a define is easy to keep track of, but lambdas are not so
    easy to track. This implementation was chosen because Lily has a
    register-based vm instead of a stack-based one.

    There are other complications. Generics get...strange. If two functions, f
    and g, both take a type A, they may be called such that their A's disagree.
    A barrier has been setup to prevent doing wrong things. Lambdas, however,
    can close over untyped variables because they do not create a generic scope.

    Another tough issue is self. A decision (which may be reverted) is that
    lambdas should be able to close over self. This allows methods to be easily
    called within a lambda, even if the lambda may not be a class member.
    However, it carries a side-effect of making method access need to check for
    self existing either closed over or not. **/

/* This writes o_closure_function which will create a copy of 'func_sym' but
   with closure information. 'target' is a storage where the closed-over copy
   will end up. The result cannot be cached in any way (each invocation should
   get a fresh set of cells). */
static void emit_create_function(lily_emit_state *emit, lily_sym *func_sym,
        lily_storage *target)
{
    lily_u16_write_4(emit->code, o_closure_function, func_sym->reg_spot,
            target->reg_spot, *emit->lex_linenum);
    emit->scope_block->flags |= BLOCK_MAKE_CLOSURE;
}

/* Close over a var, or send a SyntaxError if the var can't be closed over. */
static uint16_t checked_close_over_var(lily_emit_state *emit, lily_ast *ast,
        lily_var *var)
{
    /* Closing over a variable with a generic type is tricky:

       Closures are allowed to have generic types. Inner definitions are not
       quantified when they are called. As a result, an inner definition and its
       outer definition may disagree on what 'A' solves to.

       Lambdas, on the other hand, do get quantified since they exist in the
       scope of the definition using them. So it's fine if they close over vars
       in their parent, but no further. Multiple levels of lambdas is probably
       safe. Given a lack of a use case, that has not been implemented here. */
    lily_block *scope = emit->scope_block;

    if (var->type->flags & TYPE_IS_UNRESOLVED) {
        if (scope->block_type == block_lambda &&
            scope->prev_scope_block->block_type == block_define &&
            var->function_depth == emit->function_depth - 1)
            ;
        else
            lily_raise_tree(emit->raiser, ast,
                    "'%s' cannot be used here, because it has a generic type (^T) from another scope.",
                    var->name, var->type);
    }

    /* Closing over these variables is unnecessary and can cause crashes. */
    if (var->function_depth == 2 && scope->class_entry)
        lily_raise_tree(emit->raiser, ast,
                "Not allowed to close over variables from a class constructor.");

    emit->scope_block->flags |= BLOCK_MAKE_CLOSURE;
    lily_u16_write_2(emit->closure_spots, var->reg_spot, var->function_depth);

    uint16_t spot = (lily_u16_pos(emit->closure_spots) - 1) / 2;

    var->closure_spot = spot;
    return spot;
}


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
static void perform_closure_transform(lily_emit_state *emit,
        lily_block *scope_block, lily_function_val *f)
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

/***
 *      __  __       _       _
 *     |  \/  | __ _| |_ ___| |__
 *     | |\/| |/ _` | __/ __| '_ \
 *     | |  | | (_| | || (__| | | |
 *     |_|  |_|\__,_|\__\___|_| |_|
 *
 */

static void eval_enforce_value(lily_emit_state *, lily_ast *, lily_type *);

/** Match blocks are given a symbol that each case checks for being of a certain
    class. For enums, this pattern matching can include extraction of the
    contents of variants if the variants have values. For classes, match will
    assign class contents to a given variable.

    The code here takes advantage of the following:
    * At vm time, enum values have the id of a variant. Each case starts with
      o_jump_if_not_class, and each of those instructions links to the next one.
      The id test is against the identity of the variant or class that the user
      is interested in.
    * Variants and user classes have the same layout, so o_property_get is
      written to extract variant values that the user is interested in. **/

void lily_emit_write_class_case(lily_emit_state *emit, lily_var *var)
{
    lily_u16_write_4(emit->code, o_assign, emit->block->match_reg,
            var->reg_spot, *emit->lex_linenum);
}

void lily_emit_write_variant_case(lily_emit_state *emit, lily_var *var,
        uint16_t index)
{
    lily_u16_write_5(emit->code, o_property_get, index, emit->block->match_reg,
            var->reg_spot, *emit->lex_linenum);
}

lily_type *lily_emit_type_for_variant(lily_emit_state *emit,
        lily_variant_class *cls)
{
    lily_type *build_type = cls->build_type;
    lily_type *match_type = emit->block->match_type;
    lily_type *result = lily_ts_resolve_by_second(emit->ts, match_type,
            build_type);

    return result;
}

int lily_emit_try_add_match_case(lily_emit_state *emit, lily_class *cls)
{
    lily_block *block = emit->block;

    if (block->flags & BLOCK_FINAL_BRANCH)
        return 0;

    lily_buffer_u16 *cases = emit->match_cases;
    uint16_t start = emit->block->match_case_start;
    uint16_t stop = lily_u16_pos(emit->match_cases);
    uint16_t cls_id = cls->id;
    uint16_t i;
    int result = 1;

    for (i = start;i < stop;i++) {
        uint16_t match_case = lily_u16_get(cases, i);

        if (match_case == cls_id) {
            result = 0;
            break;
        }
    }

    lily_u16_write_1(emit->match_cases, cls->id);

    if ((cls->item_kind & ITEM_IS_VARIANT) == 0)
        return result;

    if ((stop - start + 1) == cls->parent->variant_size)
        block->flags |= BLOCK_FINAL_BRANCH;

    return result;
}

void lily_emit_write_match_switch(lily_emit_state *emit, lily_class *cls)
{
    uint16_t match_reg = emit->block->match_reg;

    if (emit->block->flags & BLOCK_FINAL_BRANCH) {
        /* Class matches are only final in their else, but this is a case. This
           is the last variant of an enum. Enums are closed sets, so don't
           bother checking. Write a placeholder patch in case this is the last
           case of a multi match. */
        lily_u16_write_1(emit->patches, 0);
        return;
    }

    if ((cls->flags & CLS_IS_HAS_VALUE) == 0) {
        /* If this isn't the class, jump to the next branch (or exit). */
        lily_u16_write_4(emit->code, o_jump_if_not_class, cls->id, match_reg,
                3);
        lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);
    }
    else {
        lily_storage *s = get_storage(emit,
                (lily_type *)emit->symtab->integer_class);
        lily_variant_class *variant = (lily_variant_class *)cls;

        /* Value variants are Integer values under the hood, so do a jumping
           compare instead. */
        lily_u16_write_4(emit->code, o_load_readonly, variant->backing_lit,
                s->reg_spot, *emit->lex_linenum);
        lily_u16_write_5(emit->code, o_compare_eq, match_reg,
                s->reg_spot, 3, *emit->lex_linenum);
        lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 2);
    }
}

void lily_emit_write_multi_match_jump(lily_emit_state *emit)
{
    /* This is the jump of the last o_jump_if_not_class. */
    uint16_t patch = lily_u16_pop(emit->patches);
    uint16_t adjust = lily_u16_get(emit->code, patch);

    /* If this branch succeds, it needs to jump to the code section. Write
       a jump to be patched later. */
    lily_u16_write_2(emit->code, o_jump, 1);
    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);

    /* Fix the last o_jump_if_not_class to go here where the new one is. */
    lily_u16_set_at(emit->code, patch,
            lily_u16_pos(emit->code) + adjust - patch);
}

void lily_emit_finish_multi_match(lily_emit_state *emit, uint16_t count)
{
    /* All cases of a multi match case have been found, so code is coming next.
       The last patch is the o_jump_if_not_class of the last case, and it needs
       to be patched to the next match case (or the exit). Behind it are 'count'
       o_jump patches that need to be patched to the code block (here). */
    uint16_t stash_patch = lily_u16_pop(emit->patches);
    uint16_t start = lily_u16_pos(emit->patches) - count;

    write_patches_since(emit, start);
    lily_u16_write_1(emit->patches, stash_patch);
}

/***
 *      _   _      _
 *     | | | | ___| |_ __   ___ _ __ ___
 *     | |_| |/ _ \ | '_ \ / _ \ '__/ __|
 *     |  _  |  __/ | |_) |  __/ |  \__ \
 *     |_| |_|\___|_| .__/ \___|_|  |___/
 *                  |_|
 */

/** These are various helping functions collected together. There's no real
    organization other than that. **/

static lily_proto_stack *new_proto_stack(uint16_t initial)
{
    lily_proto_stack *result = lily_malloc(sizeof(*result));

    result->data = lily_malloc(initial * sizeof(*result->data));
    result->pos = 0;
    result->size = initial;
    return result;
}

static void free_proto_stack(lily_proto_stack *stack)
{
    uint16_t i;
    /* Stop at pos instead of size because there's no eager init here. */
    for (i = 0;i < stack->pos;i++) {
        lily_proto *p = stack->data[i];
        lily_free(p->name);
        lily_free(p->locals);
        lily_free(p->code);

        if (p->keywords) {
            lily_free(p->keywords[0]);
            lily_free(p->keywords);
        }

        lily_free(p);
    }

    lily_free(stack->data);
    lily_free(stack);
}

static void grow_protos(lily_proto_stack *stack)
{
    int new_size = stack->size * 2;
    lily_proto **new_data = lily_realloc(stack->data,
            sizeof(*new_data) * stack->size * 2);

    stack->data = new_data;
    stack->size = new_size;
}

lily_proto *lily_emit_new_proto(lily_emit_state *emit, const char *module_path,
        const char *class_name, const char *name)
{
    lily_proto_stack *protos = emit->protos;

    if (protos->pos == protos->size)
        grow_protos(protos);

    lily_proto *p = lily_malloc(sizeof(*p));
    char *proto_name;

    if (class_name != NULL) {
        if (name[0] != '<') {
            proto_name = lily_malloc(strlen(class_name) + strlen(name) + 2);
            strcpy(proto_name, class_name);
            strcat(proto_name, ".");
            strcat(proto_name, name);
        }
        else {
            /* Instead of Class.<new>, use just Class. */
            proto_name = lily_malloc(strlen(class_name) + 1);
            strcpy(proto_name, class_name);
        }
    }
    else {
        proto_name = lily_malloc(strlen(name) + 1);
        strcpy(proto_name, name);
    }

    p->module_path = module_path;
    p->name = proto_name;
    p->locals = NULL;
    p->code = NULL;
    p->keywords = NULL;

    protos->data[protos->pos] = p;
    protos->pos++;

    return p;
}

lily_proto *lily_emit_proto_for_var(lily_emit_state *emit, lily_var *var)
{
    lily_value *v = lily_literal_at(emit->symtab, var->reg_spot);
    return v->value.function->proto;
}

/* Check if 'type' is something that can be considered truthy/falsey.
   Keep this synced with the vm's o_jump_if calculation.
   Failure: SyntaxError is raised. */
static void ensure_valid_condition_type(lily_emit_state *emit, lily_ast *ast)
{
    uint16_t cls_id = ast->result->type->cls_id;

    if (cls_id != LILY_ID_INTEGER &&
        cls_id != LILY_ID_DOUBLE &&
        cls_id != LILY_ID_STRING &&
        cls_id != LILY_ID_LIST &&
        cls_id != LILY_ID_BOOLEAN)
        lily_raise_tree(emit->raiser, ast,
                "^T is not a valid condition type.", ast->result->type);
}

/* This checks to see if 'index_ast' has a type (and possibly, a value) that is
   a valid index for the type held by 'var_ast'.
   Failure: SyntaxError is raised. */
static void check_valid_subscript(lily_emit_state *emit, lily_ast *var_ast,
        lily_ast *index_ast)
{
    int var_cls_id = var_ast->result->type->cls_id;
    if (var_cls_id == LILY_ID_LIST || var_cls_id == LILY_ID_BYTESTRING ||
        var_cls_id == LILY_ID_STRING) {
        lily_type *index_type = index_ast->result->type;

        if ((index_type->flags & CLS_IS_BASIC_NUMBER) == 0)
            lily_raise_tree(emit->raiser, var_ast,
                    "%s subscripts require a simple numeric index.\n"
                    "Index given: ^T", var_ast->result->type->cls->name,
                    index_ast->result->type);
    }
    else if (var_cls_id == LILY_ID_HASH) {
        lily_type *want_key = var_ast->result->type->subtypes[0];
        lily_type *have_key = index_ast->result->type;

        if (want_key != have_key) {
            lily_raise_tree(emit->raiser, var_ast,
                    "Hash index should be type '^T', not type '^T'.",
                    want_key, have_key);
        }
    }
    else if (var_cls_id == LILY_ID_TUPLE) {
        if (index_ast->tree_type != tree_integer) {
            lily_raise_tree(emit->raiser, var_ast,
                    "Tuple subscripts must be Integer literals.");
        }

        int index_value = index_ast->backing_value;
        lily_type *var_type = var_ast->result->type;
        if (index_value < 0 || index_value >= var_type->subtype_count) {
            lily_raise_tree(emit->raiser, var_ast,
                    "Index %d is out of range for ^T.", index_value, var_type);
        }
    }
    else {
        lily_raise_tree(emit->raiser, var_ast,
                "Cannot subscript type '^T'.",
                var_ast->result->type);
    }
}

/* This returns the result of subscripting 'type' with the value within
   'index_ast'. It should only be called once 'index_ast' has been validated
   with check_valid_subscript.
   This will not fail. */
static lily_type *get_subscript_result(lily_emit_state *emit, lily_type *type,
        lily_ast *index_ast)
{
    lily_type *result;

    if (type->cls_id == LILY_ID_LIST)
        result = type->subtypes[0];
    else if (type->cls_id == LILY_ID_HASH)
        result = type->subtypes[1];
    else if (type->cls_id == LILY_ID_TUPLE) {
        /* check_valid_subscript ensures that this is safe. */
        int literal_index = index_ast->backing_value;
        result = type->subtypes[literal_index];
    }
    else
        result = emit->symtab->byte_class->self_type;

    return result;
}


/* Since o_build_list, o_build_tuple, and o_build_hash are fairly similar (and
   the first two are fairly common), this function writes all of them.

   This function takes a tree, and will walk it up to 'num_values' times. This
   function does not create a storage. Instead, the caller is expected to
   provide a storage of the appropriate type. Said storage should have a spot
   that is 'reg_spot'. */
static void write_build_op(lily_emit_state *emit, uint16_t opcode,
        lily_ast *first_arg, uint16_t line_num, uint16_t num_values,
        lily_storage *s)
{
    lily_ast *arg;

    lily_u16_write_prep(emit->code, 5 + num_values);
    lily_u16_write_1(emit->code, opcode);

    if (opcode == o_build_hash)
        /* The vm the key's id to decide what hashing functions to use. */
        lily_u16_write_1(emit->code, s->type->subtypes[0]->cls_id);

    lily_u16_write_1(emit->code, num_values);

    for (arg = first_arg; arg != NULL; arg = arg->next_arg)
        lily_u16_write_1(emit->code, arg->result->reg_spot);

    lily_u16_write_2(emit->code, s->reg_spot, line_num);
}


/* This checks that 'sym' (either a var or a property) can be used within the
   current scope. If it cannot be, then SyntaxError is raised. */
static void ensure_valid_scope(lily_emit_state *emit, lily_ast *ast)
{
    lily_named_sym *sym = (lily_named_sym *)ast->sym;

    if (sym->flags & (SYM_SCOPE_PRIVATE | SYM_SCOPE_PROTECTED)) {
        lily_class *block_cls = emit->block->class_entry;

        /* Vars and properties have this at the same offset. */
        lily_class *parent = sym->parent;
        int is_private = (sym->flags & SYM_SCOPE_PRIVATE);
        char *name = sym->name;

        if ((is_private && block_cls != parent) ||
            (is_private == 0 &&
             (block_cls == NULL || lily_class_greater_eq(parent, block_cls) == 0))) {
            char *scope_name = is_private ? "private" : "protected";
            lily_raise_tree(emit->raiser, ast,
                       "%s.%s is marked %s, and not available here.",
                       parent->name, name, scope_name);
        }
    }
}

static int can_reroute_tree_result(lily_ast *tree)
{
    int result = 0;

    while (tree->tree_type == tree_parenth)
        tree = tree->arg_start;

    /* Keep these conditions away from each other since each has a different
       reason why they can't be moved. */

    if (tree->tree_type == tree_local_var)
        /* Nothing was written to be moved. */
        ;
    else if (tree->tree_type == tree_binary) {
        uint8_t op = tree->op;

        if (IS_ASSIGN_TOKEN(op))
            ;
        else if (op == tk_logical_and ||
                 op == tk_logical_or ||
                 op == tk_eq_eq ||
                 op == tk_not_eq ||
                 IS_COMPARE_TOKEN(op))
            /* These write in two places, and only one would be moved. */
            ;
        else
            result = 1;
    }
    else if (tree->tree_type == tree_self ||
             tree->tree_type == tree_typecast)
        /* Nothing written to move. */
        ;
    else if (tree->tree_type == tree_ternary_second ||
             tree->tree_type == tree_expr_match)
        /* Multiple writes, only one move. */
        ;
    else
        result = 1;

    return result;
}

/* Check if the ast's result matches the expected type. This does a simple
   equality check before a recursive structural check. */
static int result_matches_type(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    if (ast->result->type == expect)
        return 1;

    return lily_ts_type_greater_eq(emit->ts, expect, ast->result->type);
}

static void unpack_dot_variant(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_class *enum_cls = expect->cls;
    char *oo_name = lily_sp_get(emit->expr_strings, ast->pile_pos);

    /* It's assumed that this will typically be '?' because the user forgot to
       include inference. Based on that assumption, don't write the expected
       type. Hopefully the mention of it not being an enum is enough. */
    if ((enum_cls->item_kind & ITEM_IS_ENUM) == 0)
        lily_raise_tree(emit->raiser, ast,
                "Cannot use member '%s' of non-enum without a value.",
                oo_name);

    ast->variant = lily_find_variant(enum_cls, oo_name);

    if (ast->variant == NULL)
        lily_raise_tree(emit->raiser, ast,
                "%s does not have a variant named '%s'.",
                enum_cls->name, oo_name);
}

/* Since the ast's result doesn't match the expected type, try promoting it.
   Promotion is currently Byte -> Integer (avoid by sending ? for inference).
   This is intended for use by eval functions that produce a value. Caller must
   have ast->result set already. */
static void maybe_promote_value(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_sym *sym = ast->result;

    if (expect->cls_id != LILY_ID_INTEGER ||
        sym->type->cls_id != LILY_ID_BYTE)
        return;

    lily_storage *s = get_storage(emit, expect);

    /* The promoted value needs to be tagged as an integer and in a register for
       Integer values only. This opcode will do the job: `x | x` is always `x`
       and the vm tags the result as an Integer. */
    lily_u16_write_5(emit->code, o_int_bitwise_or, sym->reg_spot, sym->reg_spot,
            s->reg_spot, ast->line_num);
    ast->result = (lily_sym *)s;
}

/***
 *      _____
 *     | ____|_ __ _ __ ___  _ __ ___
 *     |  _| | '__| '__/ _ \| '__/ __|
 *     | |___| |  | | | (_) | |  \__ \
 *     |_____|_|  |_|  \___/|_|  |___/
 *
 */

static void inconsistent_type_error(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect, const char *context)
{
    lily_raise_tree(emit->raiser, ast,
            "%s do not have a consistent type.\n"
            "Expected Type: ^T\n"
            "Received Type: ^T",
            context, expect, ast->result->type);
}

static void bad_assign_error(lily_emit_state *emit, lily_ast *ast,
        lily_type *left_type, lily_type *right_type)
{
    lily_raise_tree(emit->raiser, ast,
            "Cannot assign type '^T' to type '^T'.",
            right_type, left_type);
}

static void incomplete_type_assign_error(lily_emit_state *emit, lily_ast *ast,
        lily_type *right_type)
{
    lily_raise_tree(emit->raiser, ast,
            "Right side of assignment is incomplete type '^T'.",
            right_type);
}

/* This is called when call processing has an argument of the wrong type. This
   generates a syntax error with the call name if that can be located.
   This assumes 'index' is 0-based (argument 0 being the first argument to the
   function given. If 'index' exceeds the number of types available, it's
   assumed that the source is varargs and the last type is used for display. */
static void error_bad_arg(lily_emit_state *emit, lily_ast *ast, lily_ast *arg,
        lily_type *call_type, int index, lily_type *got)
{
    lily_type *expected;

    if ((call_type->flags & TYPE_IS_VARARGS) == 0 ||
        index < call_type->subtype_count - 2)
        expected = call_type->subtypes[index + 1];
    else {
        /* Varargs are represented by having the last argument be a `List` of
           the type that's wanted. That's the type that was really wanted. */
        expected = call_type->subtypes[call_type->subtype_count - 1];
        expected = expected->subtypes[0];
    }

    if (expected->flags & TYPE_IS_UNRESOLVED)
        expected = lily_ts_resolve(emit->ts, expected);

    lily_msgbuf *msgbuf = emit->raiser->aux_msgbuf;
    lily_mb_flush(msgbuf);

    lily_mb_add_fmt(msgbuf, "Argument #%d to ^I is invalid:\n"
            "Expected Type: ^T\n"
            "Received Type: ^T", index + 1, ast->item, expected, got);

    lily_raise_tree(emit->raiser, arg, lily_mb_raw(msgbuf));
}

/* This is called when the tree given doesn't have enough arguments.
   ast:   The tree receiving the call.
   count: The real # of arguments that 'ast' was given.
   min:   The minimum number allowed (lower than max in case of optargs).
   max:   The maximum allowed (UINT16_MAX in case of varargs).

   This is typically called automatically by argument handling if the count is
   wrong. */
static void error_argument_count(lily_emit_state *emit, lily_ast *ast,
        uint16_t count, uint16_t min, uint16_t max)
{
    /* This prints out the number sent, as well as the range of valid counts.
       There are four possibilities, with the last one being exclusively for
       a variant that requires arguments.
       (# for n)
       (# for n+)
       (# for n..m)
       (none for n) */
    const char *div_str = "";
    char arg_str[8], min_str[8] = "", max_str[8] = "";

    if (count != 0 ||
        ast->tree_type != tree_variant)
        snprintf(arg_str, sizeof(arg_str), "%d", count);
    else
        strncpy(arg_str, "none", sizeof(arg_str));

    snprintf(min_str, sizeof(min_str), "%d", min);

    if (min == max)
        div_str = "";
    else if (max == UINT16_MAX)
        div_str = "+";
    else {
        div_str = "..";
        snprintf(max_str, sizeof(max_str), "%d", max);
    }

    lily_raise_tree(emit->raiser, ast,
            "Wrong number of arguments to ^I (%s for %s%s%s).",
            ast->item, arg_str, min_str, div_str, max_str);
}

static void error_keyarg_not_supported(lily_emit_state *emit, lily_ast *ast)
{
    lily_msgbuf *msgbuf = emit->raiser->aux_msgbuf;
    lily_mb_flush(msgbuf);

    lily_mb_add_fmt(msgbuf, "^I", ast->sym);

    if (ast->sym->item_kind == ITEM_DEFINE)
        lily_mb_add(msgbuf,
                " does not specify any keyword arguments.");
    else
        lily_mb_add(msgbuf,
                " is not capable of receiving keyword arguments.");

    lily_raise_tree(emit->raiser, ast, lily_mb_raw(msgbuf));
}

static void error_keyarg_not_valid(lily_emit_state *emit, lily_ast *ast,
        lily_ast *arg)
{
    char *key_name = lily_sp_get(emit->expr_strings, arg->left->pile_pos);

    lily_raise_tree(emit->raiser, arg,
            "^I does not have a keyword named ':%s'.",
            ast->sym, key_name);
}

static void error_keyarg_duplicate(lily_emit_state *emit, lily_ast *ast,
        lily_ast *arg)
{
    char *key_name = lily_sp_get(emit->expr_strings, arg->left->pile_pos);

    lily_raise_tree(emit->raiser, arg,
            "Call to ^I has multiple values for parameter ':%s'.",
            ast->item, key_name);
}

static void error_keyarg_before_posarg(lily_emit_state *emit, lily_ast *arg)
{
    lily_raise_tree(emit->raiser, arg,
            "Positional argument after keyword argument.");
}

static void error_keyarg_missing_params(lily_emit_state *emit, lily_ast *ast,
        lily_type *call_type, char **keywords)
{
    uint16_t i;
    uint16_t stop = call_type->subtype_count - 1;
    lily_ast *arg_start = ast->arg_start;
    lily_msgbuf *msgbuf = emit->raiser->aux_msgbuf;
    lily_type **arg_types = call_type->subtypes;

    lily_mb_flush(msgbuf);
    lily_mb_add_fmt(msgbuf, "Call to ^I is missing parameters:", ast->item);

    if (call_type->flags & TYPE_IS_VARARGS)
        stop--;

    for (i = 0;i != stop;i++) {
        lily_ast *arg_iter = arg_start;
        int skip = 0;

        for (;arg_iter != NULL;arg_iter = arg_iter->next_arg) {
            if (arg_iter->keyword_arg_pos != i)
                continue;

            if (arg_start == arg_iter)
                arg_start = arg_start->next_arg;

            skip = 1;
            break;
        }

        if (skip)
            continue;

        lily_type *t = arg_types[i + 1];

        if (t->cls_id == LILY_ID_OPTARG)
            break;

        char *key = keywords[i];

        if (key[0] != '\0')
            lily_mb_add_fmt(msgbuf, "\n* Parameter #%d (:%s) of type ^T.",
                    i + 1, key, t);
        else
            lily_mb_add_fmt(msgbuf, "\n* Parameter #%d of type ^T.", i + 1, t);
    }

    lily_raise_tree(emit->raiser, ast, lily_mb_raw(msgbuf));
}

/***
 *      __  __                _
 *     |  \/  | ___ _ __ ___ | |__   ___ _ __ ___
 *     | |\/| |/ _ \ '_ ` _ \| '_ \ / _ \ '__/ __|
 *     | |  | |  __/ | | | | | |_) |  __/ |  \__ \
 *     |_|  |_|\___|_| |_| |_|_.__/ \___|_|  |___/
 *
 */

/* Evaluate and verify member access (`<something>.y`). The inner tree (the
   something) is evaluated. This performs a dynaload if necessary, as well as
   ensuring the member access is allowed. This does not load the value, because
   that is usually wrong. Instead, the ast's item is set to either the property
   (ITEM_PROPERTY) or the method (not ITEM_PROPERTY) to access. The item's
   item_kind is returned for convenience. */
static uint16_t eval_oo_access_for_item(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->arg_start->tree_type != tree_local_var)
        eval_tree(emit, ast->arg_start, lily_question_type);

    lily_class *lookup_class = ast->arg_start->result->type->cls;
    char *oo_name = lily_sp_get(emit->expr_strings, ast->pile_pos);
    lily_item *item = lily_find_or_dl_member(emit->parser, lookup_class,
            oo_name);

    if (item == NULL) {
        lily_raise_tree(emit->raiser, ast->arg_start,
                "Class %s does not have a member named %s.", lookup_class->name,
                oo_name);
    }

    if (item->item_kind == ITEM_PROPERTY &&
        ast->arg_start->tree_type == tree_self)
        lily_raise_tree(emit->raiser, ast->arg_start,
                "Use @<name> to get/set properties, not self.<name>.");
    else if (item->item_kind & ITEM_IS_VARIANT)
        lily_raise_tree(emit->raiser, ast->arg_start,
                "Not allowed to access a variant through an enum instance.");

    ast->item = item;
    ensure_valid_scope(emit, ast);
    return item->item_kind;
}

/* This is called on oo trees that have been evaluated and which contain a
   property. This will solve the property's type using the information of the
   inner tree. The inner tree will not be evaluated again. */
static lily_type *get_solved_property_type(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *property_type = ast->property->type;
    if (property_type->flags & TYPE_IS_UNRESOLVED) {
        property_type = lily_ts_resolve_by_second(emit->ts,
                ast->arg_start->result->type, property_type);
    }

    return property_type;
}

/* This is called after eval_oo_access_for_item. It dumps the property or var
   to a storage. */
static void oo_property_read(lily_emit_state *emit, lily_ast *ast)
{
    lily_prop_entry *prop = ast->property;
    lily_type *type = get_solved_property_type(emit, ast);
    lily_storage *result = get_storage(emit, type);

    /* This function is only called on trees of type tree_oo_access which have
       a property into the ast's item. */
    lily_u16_write_5(emit->code, o_property_get, prop->id,
            ast->arg_start->result->reg_spot, result->reg_spot, ast->line_num);

    /* Properties are assignable if their source is assignable. */
    if ((ast->arg_start->result->flags & SYM_NOT_ASSIGNABLE) == 0)
        result->flags &= ~SYM_NOT_ASSIGNABLE;

    ast->result = (lily_sym *)result;
}

/* This is the actual handler for simple 'x.y' accesses. It doesn't do assigns
   though. */
static void eval_oo_access(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    /* The result is a property or class method. */
    if (eval_oo_access_for_item(emit, ast) == ITEM_PROPERTY) {
        oo_property_read(emit, ast);

        /* Promotion is handled here because the other callers (compound op and
           call) have no use for it. */
        if (ast->result->type != expect)
            maybe_promote_value(emit, ast, expect);
    }
    else {
        lily_storage *result = get_storage(emit, ast->sym->type);
        lily_u16_write_4(emit->code, o_load_readonly, ast->sym->reg_spot,
                result->reg_spot, ast->line_num);
        ast->result = (lily_sym *)result;
    }
}

/***
 *      _____                _____            _
 *     |_   _| __ ___  ___  | ____|_   ____ _| |
 *       | || '__/ _ \/ _ \ |  _| \ \ / / _` | |
 *       | || | |  __/  __/ | |___ \ V / (_| | |
 *       |_||_|  \___|\___| |_____| \_/ \__,_|_|
 *
 */

/** Here are most of the functions related to evaluating trees. **/
static void eval_assign(lily_emit_state *, lily_ast *);
static void eval_func_pipe(lily_emit_state *, lily_ast *, lily_type *);
static void eval_logical_op(lily_emit_state *, lily_ast *);
static void eval_plus_plus(lily_emit_state *, lily_ast *);

/* Byte -> Integer autopromotion is encouraged since there's no precision loss.
   Integer -> Double can lose precision, so promotion isn't as permissive. */
static void promote_to_double(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *s = get_storage(emit,
            (lily_type *)emit->symtab->double_class);

    lily_u16_write_4(emit->code, o_double_promotion, ast->result->reg_spot,
            s->reg_spot, ast->line_num);
    ast->result = (lily_sym *)s;
}

static uint16_t eval_for_compare(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->left->tree_type != tree_local_var)
        eval_tree(emit, ast->left, lily_question_type);

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, ast->left->result->type);

    lily_type *left_type = ast->left->result->type;
    lily_type *right_type = ast->right->result->type;

    if (left_type == right_type)
        return ast->op;

    /* Are these types suitable to compare? */

    uint16_t left_id = left_type->cls_id;
    uint16_t right_id = right_type->cls_id;
    uint16_t result = ast->op;

    if (left_id == LILY_ID_BYTE &&
        right_id == LILY_ID_INTEGER)
        /* Yes. Autopromotion failed because the left doesn't have inference.
           The vm groups these together. */
        ;
    /* Autopromotion handles Integer cmp Byte.
       No autopromotion for Integer -> Double, so check both sides. */
    else if (left_id == LILY_ID_INTEGER &&
             right_id == LILY_ID_DOUBLE)
        promote_to_double(emit, ast->left);
    else if (left_id == LILY_ID_DOUBLE &&
             right_id == LILY_ID_INTEGER)
        promote_to_double(emit, ast->right);
    else
        result = UINT16_MAX;

    return result;
}

static void write_compare_or_error(lily_emit_state *emit, lily_ast *ast,
        uint16_t op)
{
    lily_sym *left = ast->left->result;
    lily_sym *right = ast->right->result;
    uint16_t left_id = left->type->cls_id;
    uint16_t opcode = UINT16_MAX;

    if (op == tk_eq_eq)
        opcode = o_compare_eq;
    else if (op == tk_not_eq)
        opcode = o_compare_not_eq;
    else if (left_id == LILY_ID_BYTE ||
             left_id == LILY_ID_DOUBLE ||
             left_id == LILY_ID_INTEGER ||
             left_id == LILY_ID_STRING) {
        if (op == tk_lt_eq) {
            lily_sym *temp = right;
            right = left;
            left = temp;
            opcode = o_compare_greater_eq;
        }
        else if (op == tk_lt) {
            lily_sym *temp = right;
            right = left;
            left = temp;
            opcode = o_compare_greater;
        }
        else if (op == tk_gt_eq)
            opcode = o_compare_greater_eq;
        else if (op == tk_gt)
            opcode = o_compare_greater;
    }

    if (opcode == UINT16_MAX)
        lily_raise_tree(emit->raiser, ast, "Invalid operation: ^T %s ^T.",
                left->type, tokname(ast->op), right->type);

    /* Comparison ops include a jump to take if they fail. In many cases,
       comparisons are done as part of a conditional test. Putting a jump in the
       compare allows omitting a separate o_jump_if_false op. */
    lily_u16_write_5(emit->code, opcode, left->reg_spot, right->reg_spot, 3,
            ast->line_num);
}

static void eval_compare_op(lily_emit_state *emit, lily_ast *ast)
{
    write_compare_or_error(emit, ast, eval_for_compare(emit, ast));

    uint16_t patch = lily_u16_pos(emit->code) - 2;
    lily_storage *s = get_storage(emit, emit->symtab->boolean_class->self_type);

    /* On success, load 'true' and jump over the false section. */
    lily_u16_write_4(emit->code, o_load_boolean, 1, s->reg_spot, ast->line_num);
    lily_u16_write_2(emit->code, o_jump, 1);

    /* The false path starts here. +3 because the jump is 3 away from the op. */
    lily_u16_set_at(emit->code, patch, lily_u16_pos(emit->code) - patch + 3);

    patch = lily_u16_pos(emit->code) - 1;
    lily_u16_write_4(emit->code, o_load_boolean, 0, s->reg_spot, ast->line_num);

    /* Patch the success path to jump here, after the false path. */
    lily_u16_set_at(emit->code, patch, lily_u16_pos(emit->code) - patch + 1);

    ast->result = (lily_sym *)s;
}

static uint16_t arith_group_for(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *left = ast->left->result->type;
    lily_type *right = ast->right->result->type;
    uint16_t group = LILY_ID_NONE;

    if (left->flags & CLS_IS_BASIC_NUMBER) {
        if (right->flags & CLS_IS_BASIC_NUMBER)
            group = LILY_ID_INTEGER;
        else if (right->cls_id == LILY_ID_DOUBLE) {
            promote_to_double(emit, ast->left);
            group = LILY_ID_DOUBLE;
        }
    }
    else if (left->cls_id == LILY_ID_DOUBLE) {
        if (right->cls_id == LILY_ID_DOUBLE)
            group = LILY_ID_DOUBLE;
        else if (right->cls_id == LILY_ID_INTEGER) {
            promote_to_double(emit, ast->right);
            group = LILY_ID_DOUBLE;
        }
    }
    else if (left->cls_id == LILY_ID_BYTE) {
        if (right->flags & CLS_IS_BASIC_NUMBER)
            group = LILY_ID_INTEGER;
    }

    return group;
}

static uint16_t arith_group_to_opcode(uint16_t group, uint16_t op)
{
    uint16_t opcode = UINT16_MAX;

    if (group == LILY_ID_INTEGER) {
        if (op == tk_plus)
            opcode = o_int_add;
        else if (op == tk_minus)
            opcode = o_int_minus;
        else if (op == tk_multiply)
            opcode = o_int_multiply;
        else if (op == tk_divide)
            opcode = o_int_divide;
        else if (op == tk_modulo)
            opcode = o_int_modulo;
        else if (op == tk_left_shift)
            opcode = o_int_left_shift;
        else if (op == tk_right_shift)
            opcode = o_int_right_shift;
        else if (op == tk_bitwise_and)
            opcode = o_int_bitwise_and;
        else if (op == tk_bitwise_or)
            opcode = o_int_bitwise_or;
        else if (op == tk_bitwise_xor)
            opcode = o_int_bitwise_xor;
    }
    else if (group == LILY_ID_DOUBLE) {
        if (op == tk_plus)
            opcode = o_number_add;
        else if (op == tk_minus)
            opcode = o_number_minus;
        else if (op == tk_multiply)
            opcode = o_number_multiply;
        else if (op == tk_divide)
            opcode = o_number_divide;
    }

    return opcode;
}

static void eval_arith_op(lily_emit_state *emit, lily_ast *ast)
{
    /* Send emitter so this first function can do promotion. */
    uint16_t group = arith_group_for(emit, ast);
    uint16_t opcode = arith_group_to_opcode(group, ast->op);
    lily_sym *left = ast->left->result;
    lily_sym *right = ast->right->result;

    if (opcode == UINT16_MAX)
        lily_raise_tree(emit->raiser, ast, "Invalid operation: ^T %s ^T.",
                left->type, tokname(ast->op), right->type);

    /* Use symtab for the type since Byte op Byte == Integer. */
    lily_type *storage_type;

    if (group == LILY_ID_INTEGER)
        storage_type = (lily_type *)emit->symtab->integer_class;
    else
        storage_type = (lily_type *)emit->symtab->double_class;

    lily_storage *s;

    /* Try to use an existing storage again before getting a new one. */
    if (left->item_kind == ITEM_STORAGE &&
        left->type == storage_type)
        s = (lily_storage *)left;
    else if (right->item_kind == ITEM_STORAGE &&
             right->type == storage_type)
        s = (lily_storage *)right;
    else
        s = get_storage(emit, storage_type);

    lily_u16_write_5(emit->code, opcode, left->reg_spot, right->reg_spot,
            s->reg_spot, ast->line_num);
    ast->result = (lily_sym *)s;
}

static void eval_binary_op(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    uint8_t prio = lily_priority_for_token(ast->op);

    /* See `scripts/token.lily` for priority groups. */

    switch (prio) {
        case 1:
            eval_assign(emit, ast);
            break;
        case 2:
        case 3:
            eval_logical_op(emit, ast);
            break;
        case 4:
            eval_compare_op(emit, ast);
            break;
        case 5:
            eval_plus_plus(emit, ast);
            break;
        case 6:
            eval_func_pipe(emit, ast, expect);
            break;
        default:
            if (ast->left->tree_type != tree_local_var)
                eval_tree(emit, ast->left, lily_question_type);

            if (ast->right->tree_type != tree_local_var)
                eval_tree(emit, ast->right, ast->left->result->type);

            eval_arith_op(emit, ast);
            break;
    }
}

static void verify_assign_types(lily_emit_state *emit, lily_ast *ast,
        lily_type *left, lily_type *right)
{
    if (left == right ||
        lily_ts_type_greater_eq(emit->ts, left, right))
        return;

    lily_raise_tree(emit->raiser, ast,
            "Cannot assign type '^T' to type '^T'.",
            right, left);
}

/* Handle the compound op part of any assignment by spoofing a math op
   (ex: `+=` => `+`). Caller must ensure the left's result is initialized. The
   math op will set a result to yield. */
static lily_sym *eval_assign_spoof_op(lily_emit_state *emit, lily_ast *ast)
{
    lily_token save_op = ast->op;
    lily_sym *save_left = ast->left->result;

    /* This converts the tree from X Y= Z to X Y Z. Token generation documents
       that any Y= *must* be one step away from Y. This makes use of that. */
    ast->op--;
    eval_arith_op(emit, ast);
    ast->op = save_op;

    /* If the left was promoted, the type was changed since it couldn't fit.
       Don't change the type, send an error. */
    if (save_left != ast->left->result)
        bad_assign_error(emit, ast, save_left->type, ast->right->result->type);

    return ast->result;
}

/* This section of tree eval handles assignments. The tree type of the left side
   determines which assignment function handles it. The assignment function
   handles simple assignments (`x = y`) as well as compound assignments
   (`x += y`). Assignment chains are handled by the assign handler setting a
   result where appropriate. */

/* Evaluate `x = y` or `x += y` where the left is a local var. Local vars are
   the only ones that can have their assign optimized out. */
static void eval_assign_local(lily_emit_state *emit, lily_ast *ast)
{
    lily_sym *left_sym = ast->left->result;
    lily_sym *right_sym;

    eval_tree(emit, ast->right, left_sym->type);
    right_sym = ast->right->result;
    left_sym->flags &= ~SYM_NOT_INITIALIZED;

    if (right_sym->type->flags & TYPE_TO_BLOCK)
        incomplete_type_assign_error(emit, ast, right_sym->type);

    if (left_sym->type == lily_question_type)
        left_sym->type = right_sym->type;

    if (ast->op == tk_equal)
        verify_assign_types(emit, ast, left_sym->type, right_sym->type);
    else
        right_sym = eval_assign_spoof_op(emit, ast);

    if (can_reroute_tree_result(ast->right)) {
        /* These always end with a result, then a line number. */
        uint16_t pos = lily_u16_pos(emit->code) - 2;

        lily_u16_set_at(emit->code, pos, left_sym->reg_spot);
    }
    else {
        uint16_t left_id = left_sym->type->cls_id;
        uint16_t opcode;

        if (left_id == LILY_ID_INTEGER ||
            left_id == LILY_ID_DOUBLE)
            opcode = o_assign_noref;
        else
            opcode = o_assign;

        lily_u16_write_4(emit->code, opcode, right_sym->reg_spot,
                left_sym->reg_spot, ast->line_num);
    }

    ast->result = right_sym;
}

/* Evaluate `x = y` or `x += y` where left is a global var. */
static void eval_assign_global(lily_emit_state *emit, lily_ast *ast)
{
    lily_sym *left_sym = ast->left->result;
    lily_sym *right_sym;

    eval_tree(emit, ast->right, left_sym->type);
    right_sym = ast->right->result;
    left_sym->flags &= ~SYM_NOT_INITIALIZED;

    if (right_sym->type->flags & TYPE_TO_BLOCK)
        incomplete_type_assign_error(emit, ast, right_sym->type);

    if (left_sym->type == lily_question_type)
        left_sym->type = right_sym->type;

    if (ast->op == tk_equal)
        verify_assign_types(emit, ast, left_sym->type, right_sym->type);
    else {
        eval_tree(emit, ast->left, lily_question_type);
        right_sym = eval_assign_spoof_op(emit, ast);
    }

    lily_u16_write_4(emit->code, o_global_set, right_sym->reg_spot,
            left_sym->reg_spot, ast->line_num);
    ast->result = right_sym;
}

/* Evaluate `@x = y` or `@x += y`. Parser ensures the left has a property, so
   there's no need to check if it's a method. Since this operates within a class
   constructor or method, the property *must not* be solved. */
static void eval_assign_property(lily_emit_state *emit, lily_ast *ast)
{
    ensure_valid_scope(emit, ast->left);

    lily_prop_entry *left_property = ast->left->property;
    lily_sym *right_sym;

    eval_tree(emit, ast->right, left_property->type);
    right_sym = ast->right->result;
    left_property->flags &= ~SYM_NOT_INITIALIZED;

    if (left_property->type == lily_question_type)
        left_property->type = right_sym->type;

    if (right_sym->type->flags & TYPE_TO_BLOCK)
        incomplete_type_assign_error(emit, ast, right_sym->type);

    if (ast->op == tk_equal)
        verify_assign_types(emit, ast, left_property->type, right_sym->type);
    else {
        eval_tree(emit, ast->left, lily_question_type);
        right_sym = eval_assign_spoof_op(emit, ast);
    }

    lily_u16_write_5(emit->code, o_property_set, left_property->id,
            emit->scope_block->self->reg_spot, right_sym->reg_spot,
            ast->line_num);
    ast->result = right_sym;
}

/* Evaluate `x.y = z` or `x.y += z`. The assignment needs to be done against the
   property's type (solved using the instance type). */
static void eval_assign_oo(lily_emit_state *emit, lily_ast *ast)
{
    if (eval_oo_access_for_item(emit, ast->left) != ITEM_PROPERTY)
        lily_raise_tree(emit->raiser, ast,
                "Left side of %s is not assignable.", tokname(ast->op));

    lily_prop_entry *left_property = ast->left->property;
    lily_type *real_left_type = get_solved_property_type(emit, ast->left);
    lily_sym *right_sym;

    eval_tree(emit, ast->right, real_left_type);
    right_sym = ast->right->result;

    if (right_sym->type->flags & TYPE_TO_BLOCK)
        incomplete_type_assign_error(emit, ast, right_sym->type);

    if (ast->op == tk_equal)
        verify_assign_types(emit, ast, real_left_type, right_sym->type);
    else {
        oo_property_read(emit, ast->left);
        right_sym = eval_assign_spoof_op(emit, ast);
    }

    lily_u16_write_5(emit->code, o_property_set, left_property->id,
            ast->left->arg_start->result->reg_spot, right_sym->reg_spot,
            ast->line_num);
    ast->result = right_sym;
}

/* Evaluate `x = y` or `x += y` where the left side is an upvalue. */
static void eval_assign_upvalue(lily_emit_state *emit, lily_ast *ast)
{
    lily_var *left_var = (lily_var *)ast->left->sym;

    eval_tree(emit, ast->right, left_var->type);

    /* This must be captured *after* the above. Since the left is initialized,
       the right may involve the left (ex: `x = x ++ y ++ z`) and give it an
       initial closure spot. */
    uint16_t spot = left_var->closure_spot;
    lily_sym *right_sym = ast->right->result;

    if (spot == UINT16_MAX)
        spot = checked_close_over_var(emit, ast->left, left_var);

    if (ast->op == tk_equal)
        verify_assign_types(emit, ast, left_var->type, right_sym->type);
    else {
        eval_tree(emit, ast->left, lily_question_type);
        right_sym = eval_assign_spoof_op(emit, ast);
    }

    lily_u16_write_4(emit->code, o_closure_set, spot, right_sym->reg_spot,
            ast->line_num);
    ast->result = right_sym;
}

/* Evaluate `x[y] = z` or `x[y] += z`. The left side of this is a subscript,
   wherein the first tree is the subject, and the second is the index. This is
   the most complex of the assignments. */
static void eval_assign_sub(lily_emit_state *emit, lily_ast *ast)
{
    lily_ast *var_ast = ast->left->arg_start;
    lily_ast *index_ast = var_ast->next_arg;

    if (var_ast->tree_type != tree_local_var) {
        eval_tree(emit, var_ast, lily_question_type);
        if (var_ast->result->flags & SYM_NOT_ASSIGNABLE) {
            lily_raise_tree(emit->raiser, ast,
                    "Left side of %s is not assignable.", tokname(ast->op));
        }
    }

    /* The index is usually a literal or a var and therefore has no need for
       inference. Since fetching inference would be a little annoying and
       unlikely to be useful, don't bother sending any. */
    if (index_ast->tree_type != tree_local_var)
        eval_tree(emit, index_ast, lily_question_type);

    lily_type *var_type = var_ast->result->type;

    check_valid_subscript(emit, var_ast, index_ast);

    if (var_type->cls_id == LILY_ID_STRING)
        lily_raise_tree(emit->raiser, ast,
                "Subscript assignment on a String is not allowed.");

    lily_type *elem_type = get_subscript_result(emit, var_type,
            index_ast);

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, elem_type);

    lily_sym *right_sym = ast->right->result;

    if (right_sym->type->flags & TYPE_TO_BLOCK)
        incomplete_type_assign_error(emit, ast, right_sym->type);

    if (ast->op == tk_equal)
        verify_assign_types(emit, ast, elem_type, right_sym->type);
    else {
        /* Subscript eval has to do most of the above business again, so just
           inline it here. */
        lily_storage *s = get_storage(emit, elem_type);

        lily_u16_write_5(emit->code, o_subscript_get, var_ast->result->reg_spot,
                index_ast->result->reg_spot, s->reg_spot, ast->line_num);
        ast->left->result = (lily_sym *)s;
        right_sym = eval_assign_spoof_op(emit, ast);
    }

    lily_u16_write_5(emit->code, o_subscript_set, var_ast->result->reg_spot,
            index_ast->result->reg_spot, right_sym->reg_spot, ast->line_num);
    ast->result = right_sym;
}

/* This handles the common parts of assignments, using helpers as needed. */
static void eval_assign(lily_emit_state *emit, lily_ast *ast)
{
    lily_tree_type left_tt = ast->left->tree_type;

    if (left_tt == tree_local_var)
        eval_assign_local(emit, ast);
    else if (left_tt == tree_global_var)
        eval_assign_global(emit, ast);
    else if (left_tt == tree_property)
        eval_assign_property(emit, ast);
    else if (left_tt == tree_oo_access)
        eval_assign_oo(emit, ast);
    else if (left_tt == tree_upvalue)
        eval_assign_upvalue(emit, ast);
    else if (left_tt == tree_subscript)
        eval_assign_sub(emit, ast);
    else
        lily_raise_tree(emit->raiser, ast,
                "Left side of %s is not assignable.", tokname(ast->op));

    if (ast->parent == NULL)
        /* Block conditions from using the result of an assignment. */
        ast->result = NULL;
    /* This allows assignment chains. */
    else if (ast->parent->tree_type == tree_binary &&
        IS_ASSIGN_TOKEN(ast->parent->op))
        ;
    else
        lily_raise_tree(emit->raiser, ast,
                "Cannot nest an assignment within an expression.");
}

/* This handles access to properties (@<name>). */
static void eval_property(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    ensure_valid_scope(emit, ast);

    lily_storage *result = get_storage(emit, ast->property->type);

    lily_u16_write_5(emit->code, o_property_get, ast->property->id,
            emit->scope_block->self->reg_spot,
            result->reg_spot, ast->line_num);

    result->flags &= ~SYM_NOT_ASSIGNABLE;
    ast->result = (lily_sym *)result;

    if (result->type != expect)
        maybe_promote_value(emit, ast, expect);
}

/* When parser first sees a lambda, it collects the lambda as a blob of text.
   Emitter now has an idea of what type to expect back from the lambda, so go
   back into parser with that information. */
static void eval_lambda_to_parse(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    int save_expr_num = emit->expr_num;
    char *lambda_body = lily_sp_get(emit->expr_strings, ast->pile_pos);

    if (expect->cls_id == LILY_ID_FUNCTION)
        ;
    else if (expect == lily_question_type)
        expect = lily_unset_type;
    else
        /* This is a better error than letting the parameters fail to infer. */
        lily_raise_tree(emit->raiser, ast,
                "Lambda given where non-Function value expected (^T).",
                expect);

    /* Enter the lambda and adjust the starting position. Emitter should not
       interact with lexer. Have parser do that. */
    lily_parser_lambda_init(emit->parser, lambda_body, ast->line_num,
            ast->lambda_offset);

    lily_sym *lambda_result = lily_parser_lambda_eval(emit->parser, expect);

    /* Lambdas may run 1+ expressions. Restoring the expression count to what it
       was prevents grabbing expressions that are currently in use. */
    emit->expr_num = save_expr_num;

    lily_storage *s = get_storage(emit, lambda_result->type);

    if ((emit->scope_block->flags & BLOCK_MAKE_CLOSURE) == 0)
        lily_u16_write_4(emit->code, o_load_readonly, lambda_result->reg_spot,
                s->reg_spot, ast->line_num);
    else
        emit_create_function(emit, lambda_result, s);

    ast->result = (lily_sym *)s;
}

/* This takes care of binary || and &&. */
static void eval_logical_op(lily_emit_state *emit, lily_ast *ast)
{
    uint16_t jump_on = (ast->op == tk_logical_or);
    lily_storage *result;
    uint16_t andor_start;

    /* The top-most and/or will start writing patches, and then later write down
       all of those patches. This is okay to do, because the current block
       cannot exit during this and/or branching. */
    if (ast->parent == NULL ||
        (ast->parent->tree_type != tree_binary || ast->parent->op != ast->op))
        andor_start = lily_u16_pos(emit->patches);
    else
        andor_start = UINT16_MAX;

    if (ast->left->tree_type != tree_local_var)
        eval_tree(emit, ast->left, lily_question_type);

    /* If the left is the same as this tree, then it's already checked itself
       and doesn't need a retest. However, and/or are opposites, so they have
       to check each other (so the op has to be exactly the same). */
    if ((ast->left->tree_type == tree_binary && ast->left->op == ast->op) == 0) {
        ensure_valid_condition_type(emit, ast->left);
        emit_jump_if(emit, ast->left, jump_on);
    }

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, lily_question_type);

    ensure_valid_condition_type(emit, ast->right);
    emit_jump_if(emit, ast->right, jump_on);

    if (andor_start != UINT16_MAX) {
        lily_symtab *symtab = emit->symtab;
        uint16_t truthy = (ast->op == tk_logical_and);
        uint16_t save_pos;

        result = get_storage(emit, symtab->boolean_class->self_type);
        lily_u16_write_4(emit->code, o_load_boolean, truthy, result->reg_spot,
                ast->line_num);

        /* The jump will be patched as soon as patches are written, so don't
           bother writing a count. */
        lily_u16_write_2(emit->code, o_jump, 0);
        save_pos = lily_u16_pos(emit->code) - 1;

        write_patches_since(emit, andor_start);

        lily_u16_write_4(emit->code, o_load_boolean, !truthy, result->reg_spot,
                ast->line_num);

        /* Fix the jump that was written. Normally, patches have an offset in
           them that accounts for the header. But the jump of o_jump is always
           1 away from the opcode. So add + 1 to below so the relative jump is
           written properly. */
        lily_u16_set_at(emit->code, save_pos,
                lily_u16_pos(emit->code) + 1 - save_pos);
        ast->result = (lily_sym *)result;
    }
}

/* This runs a subscript, including validation of the indexes. */
static void eval_subscript(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_ast *var_ast = ast->arg_start;
    lily_ast *index_ast = var_ast->next_arg;
    if (var_ast->tree_type != tree_local_var)
        eval_tree(emit, var_ast, lily_question_type);

    if (index_ast->tree_type != tree_local_var)
        eval_tree(emit, index_ast, lily_question_type);

    check_valid_subscript(emit, var_ast, index_ast);

    lily_type *type_for_result;
    type_for_result = get_subscript_result(emit, var_ast->result->type,
            index_ast);

    lily_storage *result = get_storage(emit, type_for_result);

    /* Subscripts are assignable if their source is assignable. */
    if ((var_ast->result->flags & SYM_NOT_ASSIGNABLE) == 0)
        result->flags &= ~SYM_NOT_ASSIGNABLE;

    lily_u16_write_5(emit->code, o_subscript_get, var_ast->result->reg_spot,
            index_ast->result->reg_spot, result->reg_spot, ast->line_num);

    ast->result = (lily_sym *)result;

    if (result->type != expect)
        maybe_promote_value(emit, ast, expect);
}

static void eval_typecast(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *cast_type = ast->arg_start->next_arg->type;
    lily_ast *right_tree = ast->arg_start;

    eval_tree(emit, right_tree, cast_type);

    ast->result = right_tree->result;

    if (result_matches_type(emit, ast->right, cast_type) == 0)
        lily_raise_tree(emit->raiser, ast,
                "Cannot cast type '^T' to type '^T'.",
                ast->result->type, cast_type);
}

static void eval_unary_op(lily_emit_state *emit, lily_ast *ast)
{
    /* Inference shouldn't be necessary for something so simple. */
    if (ast->left->tree_type != tree_local_var)
        eval_tree(emit, ast->left, lily_question_type);

    lily_class *lhs_class = ast->left->result->type->cls;
    uint16_t opcode = 0, lhs_id = lhs_class->id;
    lily_storage *storage;

    lily_token op = ast->op;

    if (lhs_id == LILY_ID_BOOLEAN) {
        if (op == tk_not)
            opcode = o_unary_not;
    }
    else if (lhs_id == LILY_ID_INTEGER) {
        if (op == tk_minus)
            opcode = o_unary_minus;
        else if (op == tk_not)
            opcode = o_unary_not;
        else if (op == tk_tilde)
            opcode = o_unary_bitwise_not;
    }
    else if (lhs_id == LILY_ID_DOUBLE) {
        if (op == tk_minus)
            opcode = o_unary_minus;
    }

    if (opcode == 0)
        lily_raise_tree(emit->raiser, ast,
                "Invalid operation: %s%s.",
                tokname(ast->op), lhs_class->name);

    storage = get_storage(emit, lhs_class->self_type);

    lily_u16_write_4(emit->code, opcode, ast->left->result->reg_spot,
            storage->reg_spot, ast->line_num);

    ast->result = (lily_sym *)storage;
}

static void eval_build_tuple(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    if (ast->args_collected == 0)
        lily_raise_tree(emit->raiser, ast, "Cannot create an empty Tuple.");

    if (expect->cls_id != LILY_ID_TUPLE ||
        ast->args_collected > expect->subtype_count)
        expect = lily_question_type;

    uint16_t i;
    lily_ast *arg;

    for (i = 0, arg = ast->arg_start;
         arg != NULL;
         i++, arg = arg->next_arg) {
        lily_type *elem_type = lily_question_type;

        /* It's important to do this for each pass because it allows the inner
           trees to infer types that this tree's parent may want. */
        if (expect != lily_question_type)
            elem_type = expect->subtypes[i];

        eval_tree(emit, arg, elem_type);
    }

    for (i = 0, arg = ast->arg_start;
         i < ast->args_collected;
         i++, arg = arg->next_arg) {
        lily_tm_add(emit->tm, arg->result->type);
    }

    lily_type *new_type = lily_tm_make(emit->tm, emit->symtab->tuple_class, i);
    lily_storage *s = get_storage(emit, new_type);

    write_build_op(emit, o_build_tuple, ast->arg_start, ast->line_num,
            ast->args_collected, s);
    ast->result = (lily_sym *)s;
}

static void emit_literal(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *s = get_storage(emit, ast->type);
    uint16_t op = o_load_readonly;

    if (ast->type->cls_id == LILY_ID_BYTESTRING)
        op = o_load_bytestring_copy;

    lily_u16_write_4(emit->code, op, ast->literal_reg_spot,
            s->reg_spot, ast->line_num);

    ast->result = (lily_sym *)s;
}

static void ensure_safe_global_func(lily_emit_state *emit, lily_ast *ast,
        lily_var *var)
{
    lily_tree_type tt;

    if (ast->parent == NULL) {
        /* The value will be put into a storage that the user can't reference,
           unless this is a lambda exiting. Lambda exit can't quantify, so it
           can't have global generics. */
        if (emit->scope_block->flags & BLOCK_LAMBDA_RESULT)
            tt = tree_local_var;
        else
            return;
    }
    else
        tt = ast->parent->tree_type;

    uint16_t flags = var->type->flags;

    /* Scoop cannot be quantified so it can never be let out. */
    if (flags & TYPE_HAS_SCOOP)
        lily_raise_tree(emit->raiser, ast,
                "^I must be called, since it uses type $1.", var);

    /* Calls will quantify arguments that are not unquantified, to allow code
       such as `xyz.map(Option.unwrap)` to succeed. */
    if (tt == tree_call || tt == tree_named_call)
        return;

    /* Allowing global generic functions to escape also causes issues. */
    lily_raise_tree(emit->raiser, ast,
            "^I has generics, and must be called or be a call argument.", var);
}

/* Local var eval is unique in that the result is already set. There's nothing
   to do unless the type needs to be promoted. */
static void emit_local_var(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    if (expect == lily_question_type || ast->result->type == expect)
        return;

    maybe_promote_value(emit, ast, expect);
}

/* This handles loading globals, upvalues, and static functions. */
static void emit_nonlocal_var(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    uint16_t opcode, spot;
    lily_sym *sym = ast->sym;
    lily_storage *ret = get_storage(emit, sym->type);

    switch (ast->tree_type) {
        case tree_global_var:
            opcode = o_global_get;
            spot = sym->reg_spot;
            ret->flags &= ~SYM_NOT_ASSIGNABLE;
            break;
        case tree_upvalue: {
            opcode = o_closure_get;
            lily_var *v = (lily_var *)sym;

            spot = v->closure_spot;
            if (spot == UINT16_MAX)
                spot = checked_close_over_var(emit, ast, v);

            emit->scope_block->flags |= BLOCK_MAKE_CLOSURE;
            ret->flags &= ~SYM_NOT_ASSIGNABLE;
            break;
        }
        case tree_static_func:
            ensure_valid_scope(emit, ast);
        case tree_defined_func:
            if (sym->type->flags & (TYPE_HAS_SCOOP | TYPE_IS_UNRESOLVED))
                ensure_safe_global_func(emit, ast, (lily_var *)sym);
        default:
            spot = sym->reg_spot;
            opcode = o_load_readonly;
            break;
    }

    if ((sym->flags & VAR_NEEDS_CLOSURE) == 0 ||
        ast->tree_type == tree_upvalue) {
        lily_u16_write_4(emit->code, opcode, spot, ret->reg_spot,
                ast->line_num);
        ast->result = (lily_sym *)ret;

        if (sym->type != expect)
            maybe_promote_value(emit, ast, expect);
    }
    else {
        emit_create_function(emit, sym, ret);
        ast->result = (lily_sym *)ret;
    }
}

static void emit_byte(lily_emit_state *emit, lily_ast *ast, lily_type *expect)
{
    lily_type *t = emit->symtab->byte_class->self_type;
    uint16_t opcode = o_load_byte;

    if (expect->cls_id == LILY_ID_INTEGER) {
        t = expect;
        opcode = o_load_integer;
    }

    lily_storage *s = get_storage(emit, t);

    lily_u16_write_4(emit->code, opcode, ast->backing_value, s->reg_spot,
            ast->line_num);

    ast->result = (lily_sym *)s;
}

static void emit_integer(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    if (expect->cls_id == LILY_ID_BYTE) {
        if (ast->backing_value >= 0 &&
            ast->backing_value <= UINT8_MAX) {
            emit_byte(emit, ast, expect);
            return;
        }
        else
            /* The cast prevents msgbuf from writing it wrong. */
            lily_raise_tree(emit->raiser, ast,
                    "Value %ld is out of range for a Byte.",
                    (int64_t)ast->backing_value);
    }

    lily_storage *s = get_storage(emit, emit->symtab->integer_class->self_type);

    lily_u16_write_4(emit->code, o_load_integer, ast->backing_value,
            s->reg_spot, ast->line_num);

    ast->result = (lily_sym *)s;
}

static void emit_boolean(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *s = get_storage(emit, emit->symtab->boolean_class->self_type);

    lily_u16_write_4(emit->code, o_load_boolean, ast->backing_value, s->reg_spot,
            ast->line_num);

    ast->result = (lily_sym *)s;
}

static void eval_self(lily_emit_state *emit, lily_ast *ast)
{
    ast->result = (lily_sym *)emit->scope_block->self;
}

/* Either the truthy or falsey branch of ternary is done. The branch result
   needs to be in a storage so it can be rerouted. */
static uint16_t ternary_branch_fixup(lily_emit_state *emit, lily_ast *ast)
{
    if (can_reroute_tree_result(ast) == 0)
        /* Finish it with an assignment (which can be rerouted). */
        lily_u16_write_4(emit->code, o_assign, ast->result->reg_spot, 0,
                ast->line_num);

    /* Trees always finish with a line number and a result. */
    return lily_u16_pos(emit->code) - 2;
}

/* Unify, like type checking, expects that the right side will be the same or
   more than the right. However, in this case, order is not important. All
   that's important is that there's a common bottom type toward the elements. So
   run unify in both directions. */
static lily_type *bidirectional_unify(lily_type_system *ts,
        lily_type *left_type, lily_type *right_type)
{
    lily_type *result = lily_ts_unify(ts, left_type, right_type);

    if (result == NULL)
        result = lily_ts_unify(ts, right_type, left_type);

    return result;
}

static void eval_ternary(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    if (expect->flags & TYPE_HAS_SCOOP)
        expect = lily_question_type;

    /* Ternary is tricky because both sides need to eval into the same storage
       and must have the same type. The latter is tougher when there are
       incomplete types floating around. */
    lily_ast *cond_ast = ast->arg_start;
    lily_ast *truthy_ast = cond_ast->next_arg;
    lily_ast *falsey_ast = truthy_ast->next_arg;
    uint16_t truthy_escape, truthy_patch_pos;

    /* The condition doesn't get inference because there are several truthy
       types. At the end, write a jump to take if false (fall to truthy). */
    eval_tree(emit, cond_ast, lily_question_type);
    ensure_valid_condition_type(emit, cond_ast);
    emit_jump_if(emit, cond_ast, 0);
    eval_tree(emit, truthy_ast, expect);

    truthy_patch_pos = ternary_branch_fixup(emit, truthy_ast);
    lily_u16_write_2(emit->code, o_jump, 1);
    truthy_escape = lily_u16_pos(emit->code) - 1;

    /* Truthy is done, so falsey lands here. */
    uint16_t patch = lily_u16_pop(emit->patches);
    uint16_t adjust = lily_u16_get(emit->code, patch);

    lily_u16_set_at(emit->code, patch,
            lily_u16_pos(emit->code) + adjust - patch);

    /* Since the branches need to agree on a type, have the falsey side use the
       truthy side for inference. This seems right. */
    eval_tree(emit, falsey_ast, truthy_ast->result->type);

    uint16_t falsey_patch_pos = ternary_branch_fixup(emit, falsey_ast);

    lily_u16_set_at(emit->code, truthy_escape,
            lily_u16_pos(emit->code) + 1 - truthy_escape);

    lily_type *storage_type = bidirectional_unify(emit->ts, truthy_ast->result->type,
            falsey_ast->result->type);

    if (storage_type == NULL)
        lily_raise_tree(emit->raiser, ast,
                "Ternary branches have different types:\n"
                "First: ^T\n"
                "Second: ^T",
                truthy_ast->result->type,
                falsey_ast->result->type);

    lily_storage *result = get_storage(emit, storage_type);

    lily_u16_set_at(emit->code, falsey_patch_pos, result->reg_spot);
    lily_u16_set_at(emit->code, truthy_patch_pos, result->reg_spot);
    ast->result = (lily_sym *)result;
}

static lily_variant_class *start_expr_match_case(lily_emit_state *emit,
        lily_ast *ast, lily_type *match_type)
{
    const char *name = lily_sp_get(emit->expr_strings, ast->pile_pos);
    lily_class *enum_cls = match_type->cls;
    lily_class *cls = (lily_class *)lily_find_variant(enum_cls, name);

    if (cls == NULL)
        lily_raise_tree(emit->raiser, ast,
                "Enum %s does not have a variant named %s.", enum_cls->name,
                name);

    lily_emit_branch_switch(emit);

    if (lily_emit_try_add_match_case(emit, cls) == 0)
        lily_raise_tree(emit->raiser, ast, "Already have a case for %s.", name);

    lily_emit_write_match_switch(emit, cls);
    return (lily_variant_class *)cls;
}

static void expr_branch_else(lily_emit_state *emit, lily_ast *ast)
{
    if (emit->block->flags & BLOCK_FINAL_BRANCH)
        lily_raise_tree(emit->raiser, ast, "else in exhaustive match.");

    lily_emit_branch_finalize(emit);
}

static void unpack_match_case(lily_emit_state *emit, lily_ast *ast,
        lily_variant_class *cls)
{
    lily_class *enum_cls = cls->parent;

    if (cls->item_kind == ITEM_VARIANT_EMPTY) {
        if (ast->match_var_count)
            lily_raise_tree(emit->raiser, ast,
                    "Variant %s of enum %s does not take arguments.", cls->name,
                    enum_cls->name);

        return;
    }

    lily_type *case_type = lily_emit_type_for_variant(emit, cls);

    /* The builder type is a Function that returns the enum. Drop by -1 to
       account for the return at [0]. */
    uint16_t end = case_type->subtype_count - 1;

    if (ast->match_var_count != end) {
        lily_raise_tree(emit->raiser, ast,
                "Variant %s of enum %s expects %d parameters (%d given).",
                cls->name, enum_cls->name, end, ast->match_var_count);
    }

    /* Vars are linked last -> first, so they get unpacked that way too. */
    lily_var *var_iter = (lily_var *)ast->sym;
    uint16_t match_reg = emit->block->match_reg;

    for (;end > 0; end--, var_iter = var_iter->next) {
        /* Vars to be skipped ("_") are saved in parser as blank names, to
           prevent them from being referenced. */
        if (var_iter->name[0] == '\0')
            continue;

        var_iter->type = case_type->subtypes[end];

        lily_u16_write_5(emit->code, o_property_get, end - 1,
                match_reg, var_iter->reg_spot, var_iter->line_num);
    }
}

static void verify_match_case_result(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    if (result_matches_type(emit, ast, expect))
        return;

    lily_raise_tree(emit->raiser, ast,
            "Mismatched type in expression match branch:\n"
            "Expected Type: ^T\n"
            "Received Type: ^T", expect, ast->result->type);
}

static void redirect_match_case_result(lily_emit_state *emit, lily_ast *ast,
        lily_ast *arg)
{
    if (can_reroute_tree_result(arg)) {
        uint16_t pos = lily_u16_pos(emit->code) - 2;

        lily_u16_set_at(emit->code, pos, ast->result->reg_spot);
    }
    else
        lily_u16_write_4(emit->code, o_assign, arg->result->reg_spot,
                ast->result->reg_spot, ast->line_num);
}

static void eval_expr_match(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_ast *arg = ast->arg_start;
    lily_sym *target = lily_eval_for_result(emit, arg);
    lily_type *t = target->type;

    /* Revert the bump by the above eval, just to be safe. */
    emit->expr_num--;

    /* Enums are the most useful subject to match against. Class matching is not
       included because `case somemodule.Classxyz` would not work. */
    if ((t->cls->item_kind & ITEM_IS_ENUM) == 0)
        lily_raise_tree(emit->raiser, arg,
                "Match expressions only work with enum values.\n"
                "Received: ^T", t);

    if (t->flags & TYPE_IS_INCOMPLETE)
        lily_raise_tree(emit->raiser, arg,
            "Match expression value cannot be an incomplete type.\n"
            "Received: ^T", t);

    lily_emit_enter_match_block(emit, target);
    ast->result = NULL;

    /* Match is collected as pairs of a branch kind (case or else), and expr. */
    for (arg = arg->next_arg; arg != NULL; arg = arg->next_arg) {
        if (arg->tree_type == tree_expr_match_case) {
            lily_variant_class *c = start_expr_match_case(emit, arg, t);

            unpack_match_case(emit, arg, c);
        }
        else
            expr_branch_else(emit, arg);

        arg = arg->next_arg;
        eval_tree(emit, arg, expect);

        if (expect != lily_question_type)
            verify_match_case_result(emit, arg, expect);
        else
            expect = arg->result->type;

        if (ast->result == NULL)
            ast->result = (lily_sym *)get_storage(emit, expect);

        redirect_match_case_result(emit, ast, arg);
    }

    if (lily_emit_try_leave_match_block(emit) == 0)
        lily_raise_tree(emit->raiser, ast,
                lily_mb_raw(emit->raiser->aux_msgbuf));

    lily_emit_leave_expr_match_block(emit);
}

/***
 *      _     _     _             _   _           _
 *     | |   (_)___| |_     _    | | | | __ _ ___| |__
 *     | |   | / __| __|  _| |_  | |_| |/ _` / __| '_ \
 *     | |___| \__ \ |_  |_   _| |  _  | (_| \__ \ | | |
 *     |_____|_|___/\__|   |_|   |_| |_|\__,_|___/_| |_|
 *
 */

static void ensure_valid_key_type(lily_emit_state *emit, lily_ast *ast,
        lily_type *key_type)
{
    uint16_t key_id = key_type->cls_id;

    if (key_id != LILY_ID_INTEGER &&
        key_id != LILY_ID_STRING)
        lily_raise_tree(emit->raiser, ast,
                "Type '^T' is not a valid key for Hash.", key_type);
}

/* Build an empty something. It's an empty hash only if the caller wanted a
   hash. In any other case, it becomes an empty list. Use ? as a default where
   it's needed. The purpose of this function is to make it so list and hash
   build do not need to worry about missing information. */
static void make_empty_list_or_hash(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_class *cls;
    uint16_t num, op;

    if (expect->cls_id == LILY_ID_HASH) {
        lily_type *key_type = expect->subtypes[0];
        lily_type *value_type = expect->subtypes[1];
        ensure_valid_key_type(emit, ast, key_type);

        lily_tm_add(emit->tm, key_type);
        lily_tm_add(emit->tm, value_type);

        cls = emit->symtab->hash_class;
        op = o_build_hash;
        num = 2;
    }
    else {
        lily_type *elem_type = lily_question_type;

        if (expect->cls_id == LILY_ID_LIST) {
            elem_type = expect->subtypes[0];

            /* This is a rare case that's possible with `List.zip` since that
               method sends down the scoop type for inference.
               The scoop type is only for methods, because it bypasses type
               checking. Send `Unit` instead. */
            if (elem_type == lily_scoop_type)
                elem_type = lily_unit_type;
        }

        lily_tm_add(emit->tm, elem_type);

        cls = emit->symtab->list_class;
        op = o_build_list;
        num = 1;
    }

    lily_storage *s = get_storage(emit, lily_tm_make(emit->tm, cls, num));
    write_build_op(emit, op, ast->arg_start, ast->line_num, 0, s);
    ast->result = (lily_sym *)s;
}

static void eval_build_hash(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_ast *tree_iter;

    lily_type *key_type, *value_type;

    if (expect->cls_id == LILY_ID_HASH) {
        key_type = expect->subtypes[0];
        value_type = expect->subtypes[1];

        uint16_t cls_id = key_type->cls_id;

        /* Parser allows keys to be generic. That's fine, as long as emitter
           doesn't build a Hash that's invalid. To prevent building an invalid
           Hash, require that the key be one of the known valid types. */
        if (cls_id != LILY_ID_INTEGER &&
            cls_id != LILY_ID_STRING)
            key_type = lily_question_type;
    }
    else {
        key_type = lily_question_type;
        value_type = lily_question_type;
    }

    for (tree_iter = ast->arg_start;
         tree_iter != NULL;
         tree_iter = tree_iter->next_arg->next_arg) {

        lily_ast *key_tree, *value_tree;
        lily_type *unify_type;

        key_tree = tree_iter;
        value_tree = tree_iter->next_arg;

        eval_tree(emit, key_tree, key_type);

        lily_type *key_result = key_tree->result->type;

        /* The only valid types for Hash keys are monomorphic so == can be used
           for equality testing. */
        if (key_type != key_result) {
            if (tree_iter == ast->arg_start) {
                key_type = key_result;
                ensure_valid_key_type(emit, key_tree, key_type);
            }
            else
                inconsistent_type_error(emit, key_tree, key_type, "Hash keys");
        }

        eval_tree(emit, value_tree, value_type);
        unify_type = bidirectional_unify(emit->ts, value_type,
                value_tree->result->type);

        if (unify_type == NULL) {
            if (tree_iter == ast->arg_start)
                unify_type = value_tree->result->type;
            else
                inconsistent_type_error(emit, value_tree, value_type,
                        "Hash values");
        }

        value_type = unify_type;
    }

    lily_class *hash_cls = emit->symtab->hash_class;
    lily_tm_add(emit->tm, key_type);
    lily_tm_add(emit->tm, value_type);
    lily_type *new_type = lily_tm_make(emit->tm, hash_cls, 2);

    lily_storage *s = get_storage(emit, new_type);

    write_build_op(emit, o_build_hash, ast->arg_start, ast->line_num,
            ast->args_collected, s);
    ast->result = (lily_sym *)s;
}

static void eval_build_list(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    if (ast->args_collected == 0) {
        make_empty_list_or_hash(emit, ast, expect);
        return;
    }

    lily_type *elem_type = lily_question_type;
    lily_ast *arg;

    if (expect->cls_id == LILY_ID_LIST)
        elem_type = expect->subtypes[0];

    if (elem_type == lily_scoop_type)
        elem_type = lily_question_type;

    for (arg = ast->arg_start;arg != NULL;arg = arg->next_arg) {
        eval_tree(emit, arg, elem_type);

        lily_type *new_elem_type = bidirectional_unify(emit->ts, elem_type,
                arg->result->type);

        if (new_elem_type == NULL) {
            if (arg == ast->arg_start)
                new_elem_type = arg->result->type;
            else
                inconsistent_type_error(emit, arg, elem_type, "List elements");
        }

        elem_type = new_elem_type;
    }

    lily_tm_add(emit->tm, elem_type);
    lily_type *new_type = lily_tm_make(emit->tm, emit->symtab->list_class,
            1);

    lily_storage *s = get_storage(emit, new_type);

    write_build_op(emit, o_build_list, ast->arg_start, ast->line_num,
            ast->args_collected, s);
    ast->result = (lily_sym *)s;
}

/***
 *       ____      _ _
 *      / ___|__ _| | |___
 *     | |   / _` | | / __|
 *     | |__| (_| | | \__ \
 *      \____\__,_|_|_|___/
 *
 */

/** Lily allows the following kinds of calls:

    * x()
    * x.y()
    * @y
    * x[0]()
    * x()()
    * Some(10)
    * (|| 10) ()
    * [1, 2, 3].y()
    * x |> y
    * .UserVariant()

    The different kinds of functions allowed in Lily mean that a call could have
    many kinds of sources. Once that hurdle has been passed, most calls support
    optional arguments as well as varargs. The functions in this section handle
    calls.

    Calls need to be able to send down inference as well as to check the result
    of an argument eval. Most of that work is done in ts (type system). Argument
    eval uses ts to solve types with ?, so that arguments can fill in types
    piece by piece. This is particularly useful for variants where one argument
    may have half of the type, and another have the other.

    This area has some strategies to make calls easier:

    * Call piping rewrites the tree so that it looks like a call and dispatches
      to call. This prevents bugs that would otherwise be caused by differing
      implementations. The same is also true for variant shorthand.

    * For non-variant targets, the source tree has the 'sym' field set to the
      target that will be called.

    * For all targets, there will always be a source to write against. Variants
      put their enum parent's self type where a function would put its return
      type. This allows variants that take arguments to be treated like any
      other kind of tree, even though there's no sym since they have no backing
      function.
    **/

static void get_func_min_max(lily_type *call_type, uint16_t *min, uint16_t *max)
{
    *min = call_type->subtype_count - 1;
    *max = *min;

    if (call_type->flags & TYPE_HAS_OPTARGS) {
        uint16_t i;
        for (i = 1;i < call_type->subtype_count;i++) {
            if (call_type->subtypes[i]->cls_id == LILY_ID_OPTARG)
                break;
        }
        *min = i - 1;
    }

    if (call_type->flags & TYPE_IS_VARARGS) {
        *max = UINT16_MAX;

        if ((call_type->flags & TYPE_HAS_OPTARGS) == 0)
            *min = *min - 1;
    }
}

/* This function is called before a call is actually written down. It finds a
   result for the ast to write down into so that call writing doesn't have to
   worry about that. */
static void setup_call_result(lily_emit_state *emit, lily_ast *ast,
        lily_type *return_type)
{
    if (return_type == lily_self_class->self_type)
        ast->result = ast->arg_start->result;
    else if (ast->first_tree_type == tree_inherited_new)
        ast->result = (lily_sym *)emit->scope_block->self;
    else {
        lily_ast *arg = ast->arg_start;

        if (return_type->flags & (TYPE_IS_UNRESOLVED | TYPE_HAS_SCOOP))
            return_type = lily_ts_resolve_unscoop(emit->ts, return_type);

        lily_storage *s = NULL;

        for (;arg;arg = arg->next_arg) {
            if (arg->result->item_kind == ITEM_STORAGE &&
                arg->result->type == return_type) {
                s = (lily_storage *)arg->result;
                break;
            }
        }

        if (s == NULL)
            s = get_storage(emit, return_type);

        ast->result = (lily_sym *)s;
    }
}

/* The call's subtrees have been evaluated now. Write the instruction to do the
   call and make a storage to put the result in (if needed). */
static void write_call(lily_emit_state *emit, lily_ast *ast,
        uint16_t argument_count, lily_storage *vararg_s)
{
    lily_ast *arg;
    uint16_t i;

    lily_u16_write_3(emit->code, ast->call_op, ast->call_source_reg,
            argument_count + (vararg_s != NULL));

    for (i = 0, arg = ast->arg_start;
         i < argument_count;
         i++, arg = arg->next_arg)
        lily_u16_write_1(emit->code, arg->result->reg_spot);

    if (vararg_s)
        lily_u16_write_1(emit->code, vararg_s->reg_spot);

    lily_u16_write_2(emit->code, ast->result->reg_spot, ast->line_num);
}

/* This handles writing a call when keyword arguments and optional arguments
   intersect. This is different (and more difficult) because of potential unset
   values between arguments given. */
static void write_call_keyopt(lily_emit_state *emit, lily_ast *ast,
        lily_type *call_type, lily_storage *vararg_s)
{
    lily_storage *s = get_storage(emit, lily_unset_type);

    /* This register can be reused as much as needed since it's only a
       placeholder. */
    s->expr_num = 0;

    lily_u16_write_3(emit->code, ast->call_op, ast->call_source_reg, 0);

    uint16_t arg_count_spot = lily_u16_pos(emit->code) - 1;
    uint16_t args_written = 0;
    uint16_t unset_reg_spot = s->reg_spot;
    uint16_t pos = ast->arg_start->keyword_arg_pos;
    uint16_t va_pos;
    lily_ast *arg = ast->arg_start;

    /* -2 because the return type is at 0 and arg positions start at 0 for the
       first. */
    if (call_type->flags & TYPE_IS_VARARGS)
        va_pos = call_type->subtype_count - 2;
    else
        va_pos = UINT16_MAX;

    while (1) {
        if (pos != args_written) {
            /* Fill the missing arguments with the unset register. */
            while (pos != args_written) {
                lily_u16_write_1(emit->code, unset_reg_spot);
                args_written++;
            }
        }

        args_written++;

        if (pos == va_pos) {
            lily_u16_write_1(emit->code, vararg_s->reg_spot);
            break;
        }
        else {
            lily_u16_write_1(emit->code, arg->result->reg_spot);
            arg = arg->next_arg;

            if (arg)
                pos = arg->keyword_arg_pos;
            else if (va_pos != UINT16_MAX && vararg_s != NULL)
                /* vararg_s may be NULL if the function is varargs but no
                   varargs were actually passed. */
                pos = va_pos;
            else
                /* Missing parameters are to the right of what's been written.
                   That's the usual case for optargs, so unset-padding isn't
                   necessary here. */
                break;
        }
    }

    lily_u16_set_at(emit->code, arg_count_spot, args_written);
    lily_u16_write_2(emit->code, ast->result->reg_spot, ast->line_num);
}

/* Evaluate the call argument 'arg'. The type 'want_type' is -not- solved by the
   caller, and thus must be solved here.
   Returns 1 if successful, 0 otherwise. */
static int eval_call_arg(lily_emit_state *emit, lily_ast *arg,
        lily_type *want_type)
{
    if (want_type->cls_id == LILY_ID_OPTARG)
        want_type = want_type->subtypes[0];

    lily_type *eval_type = want_type;
    if (eval_type->flags & TYPE_IS_UNRESOLVED)
        eval_type = lily_ts_resolve(emit->ts, want_type);

    eval_tree(emit, arg, eval_type);
    lily_type *result_type = arg->result->type;

    /* For these two, the generics are not confined to the current scope.
       Instead, the generics should be treated as being unquantified. */
    if ((result_type->flags & TYPE_IS_UNRESOLVED) &&
        (arg->tree_type == tree_static_func ||
         arg->tree_type == tree_defined_func))
    {
        /* Example:
           `[Some(1)].map(Option.unwrap)`

           The type wanted is `Function (A => B)`, with A solved as
           `Option[Integer]`. It'll solve with B as `?`.

           The type given (`Option.unwrap`) is `Function (Option[A] => A)`.

           The check will solve the type given using the solved result above.
           That will produce `Function (Option[Integer] => Integer)` which
           satisfies the constraint. */

        lily_type *solved_want = lily_ts_resolve(emit->ts, want_type);

        lily_ts_save_point p;
        lily_ts_scope_save(emit->ts, &p);
        lily_ts_check(emit->ts, result_type, solved_want);
        lily_type *solved_result = lily_ts_resolve(emit->ts, result_type);
        lily_ts_scope_restore(emit->ts, &p);

        /* Don't assume it succeeded, because it worsens the error message in
           the case that it didn't. */
        if (solved_result == solved_want ||
            lily_ts_type_greater_eq(emit->ts, solved_want, solved_result))
            result_type = solved_result;
    }

    /* This is important. If the callee wants something generic, it HAS to be
       a resolving match. Otherwise, it HAS to be a strict >= type match.
       Example: Callee wants list[A], and A = integer. Giving an actual list[A]
       is wrong. */
    if (((want_type->flags & TYPE_IS_UNRESOLVED) &&
          lily_ts_check(emit->ts, want_type, result_type))
        ||
        (((want_type->flags & TYPE_IS_UNRESOLVED) == 0) &&
         lily_ts_type_greater_eq(emit->ts, want_type, result_type)))
        return 1;
    else
        return 0;
}

/* This is the main body of argument handling. This begins after ts has had
   generics set aside for this function. This function verifies the argument
   count, sets the result up, and does the call to write values out. */
static void run_call(lily_emit_state *emit, lily_ast *ast, lily_type *call_type)
{
    lily_ast *arg = ast->arg_start;
    uint16_t num_args = ast->args_collected;
    uint16_t min, max;

    get_func_min_max(call_type, &min, &max);

    if (num_args < min || num_args > max)
        error_argument_count(emit, ast, num_args, min, max);

    lily_type **arg_types = call_type->subtypes;
    uint16_t i, stop;

    if ((call_type->flags & TYPE_IS_VARARGS) == 0 ||
        call_type->subtype_count - 1 > num_args)
        stop = num_args;
    else
        stop = call_type->subtype_count - 2;

    for (i = 0; i < stop; i++, arg = arg->next_arg) {
        if (eval_call_arg(emit, arg, arg_types[i + 1]) == 0)
            error_bad_arg(emit, ast, arg, call_type, i, arg->result->type);
    }

    lily_storage *vararg_s = NULL;

    /* The second check prevents running varargs when there are unfilled
       optional arguments that come before the varargs. */
    if (call_type->flags & TYPE_IS_VARARGS &&
        (num_args + 2) >= call_type->subtype_count) {

        /* Don't solve this yet, because eval_call_arg solves it (and double
           solving is bad). */
        lily_type *vararg_type = arg_types[i + 1];
        lily_type *original_vararg = vararg_type;
        int is_optarg = 0;

        /* Varargs are presented as a `List` of their inner values, so use
           subtypes[0] to get the real type. If this vararg is optional, then do
           a double unwrap. */
        if (vararg_type->cls_id == LILY_ID_OPTARG) {
            is_optarg = 1;
            original_vararg = original_vararg->subtypes[0];
            vararg_type = original_vararg->subtypes[0];
        }
        else
            vararg_type = vararg_type->subtypes[0];

        lily_ast *vararg_iter = arg;
        uint16_t vararg_i;

        for (vararg_i = i;
             arg != NULL;
             arg = arg->next_arg, vararg_i++) {
            if (eval_call_arg(emit, arg, vararg_type) == 0)
                error_bad_arg(emit, ast, arg, call_type, vararg_i,
                        arg->result->type);
        }

        if (vararg_type->flags & TYPE_IS_UNRESOLVED)
            vararg_type = lily_ts_resolve(emit->ts, vararg_type);

        if (vararg_i != i || is_optarg == 0) {
            vararg_s = get_storage(emit, original_vararg);
            lily_u16_write_2(emit->code, o_build_list, vararg_i - i);
            for (;vararg_iter;vararg_iter = vararg_iter->next_arg)
                lily_u16_write_1(emit->code, vararg_iter->result->reg_spot);

            lily_u16_write_2(emit->code, vararg_s->reg_spot, ast->line_num);
        }
    }

    setup_call_result(emit, ast, arg_types[0]);
    write_call(emit, ast, stop, vararg_s);
}

static void init_call_state(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_item *call_item;
    lily_ast *first_arg = ast->arg_start;

    switch (first_arg->tree_type) {
        case tree_method:
            ensure_valid_scope(emit, first_arg);
            first_arg->result = (lily_sym *)emit->scope_block->self;
            first_arg->tree_type = tree_cached;
            call_item = first_arg->item;
            break;
        case tree_static_func:
            ensure_valid_scope(emit, first_arg);
            call_item = first_arg->item;
            break;
        case tree_oo_access:
            if (eval_oo_access_for_item(emit, first_arg) == ITEM_PROPERTY)
                oo_property_read(emit, first_arg);
            else {
                first_arg->result = first_arg->arg_start->result;
                first_arg->tree_type = tree_cached;
            }

            call_item = first_arg->item;
            break;
        case tree_dot_variant:
            unpack_dot_variant(emit, first_arg, expect);
            first_arg->tree_type = tree_variant;
            /* Fallthrough to typical variant handling. */
        case tree_variant: {
            lily_variant_class *variant = first_arg->variant;
            if (variant->item_kind == ITEM_VARIANT_EMPTY)
                lily_raise_tree(emit->raiser, ast,
                        "%s is an empty variant that should not be called.",
                        variant->name);

            call_item = (lily_item *)variant;
            break;
        }
        case tree_global_var:
        case tree_upvalue:
            eval_tree(emit, first_arg, lily_question_type);
        case tree_local_var:
        case tree_defined_func:
        case tree_inherited_new:
            call_item = first_arg->item;
            break;
        default:
            eval_tree(emit, first_arg, lily_question_type);
            call_item = (lily_item *)first_arg->result;
            break;
    }

    ast->item = call_item;
}

static lily_type *start_call(lily_emit_state *emit, lily_ast *ast)
{
    uint16_t call_source_reg;
    lily_type *call_type;
    uint16_t call_op = o_call_register;
    lily_item *call_item = ast->item;
    lily_ast *first_arg = ast->arg_start;

    switch (call_item->item_kind) {
        case ITEM_DEFINE:
        case ITEM_VAR: {
            lily_var *v = (lily_var *)call_item;

            if (v->flags & VAR_NEEDS_CLOSURE) {
                lily_storage *s = get_storage(emit, v->type);

                emit_create_function(emit, (lily_sym *)v, s);
                call_source_reg = s->reg_spot;
            }
            else if (call_item->flags & VAR_IS_FOREIGN_FUNC) {
                call_op = o_call_foreign;
                call_source_reg = v->reg_spot;
            }
            else if (call_item->flags & VAR_IS_READONLY) {
                call_op = o_call_native;
                call_source_reg = v->reg_spot;
            }
            else
                call_source_reg = first_arg->result->reg_spot;

            ast->sym = (lily_sym *)v;
            call_type = v->type;
            break;
        }
        case ITEM_VARIANT_FILLED: {
            lily_variant_class *variant = (lily_variant_class *)call_item;

            ast->variant = variant;
            call_op = o_build_variant;
            call_source_reg = variant->cls_id;
            call_type = variant->build_type;
            break;
        }
        case ITEM_PROPERTY:
            ast->sym = first_arg->sym;
            call_source_reg = first_arg->result->reg_spot;
            call_type = first_arg->result->type;
            break;
        case ITEM_STORAGE:
        default: {
            lily_storage *s = (lily_storage *)call_item;

            if (s->type->flags & TYPE_IS_INCOMPLETE)
                lily_raise_tree(emit->raiser, ast,
                        "Cannot call an incomplete type (^T).", s->type);

            ast->sym = (lily_sym *)first_arg->result;
            call_source_reg = first_arg->result->reg_spot;
            call_type = s->type;
            break;
        }
    }

    if (call_type->cls_id != LILY_ID_FUNCTION &&
        (call_item->item_kind & ITEM_IS_VARIANT) == 0) {
        lily_raise_tree(emit->raiser, ast,
                "Cannot anonymously call resulting type '^T'.",
                call_type);
    }

    ast->call_source_reg = call_source_reg;
    ast->call_op = call_op;
    ast->first_tree_type = first_arg->tree_type;

    if (first_arg->tree_type != tree_cached) {
        ast->arg_start = ast->arg_start->next_arg;
        ast->args_collected--;
    }

    return call_type;
}

/* This is called when a call has a type that references generics in some way.
   It must be called after the ts scope is setup. There are two purposes to
   this function:

   Inference. Try to pull inference information by matching the expected type
   against the call's result.

   Quantification. Generics within a definition will be solved to some unknown
   type when the definition is used. If, for example, a definition takes a
   Function that maps from A to B, it is only guaranteed to work on the A given
   during invocation. */
static void setup_typing_for_call(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect, lily_type *call_type)
{
    lily_tree_type first_tt = ast->first_tree_type;

    if (first_tt == tree_local_var ||
        first_tt == tree_upvalue ||
        first_tt == tree_inherited_new)
        /* Simulate quantification by self-solving generics. Mandatory for the
           first two.
           For tree_inherited_new, this is a matter of simplifying generics. By
           nailing A to A, the A of a base class is always A no matter how far
           the inheritance stretches. */
        lily_ts_check(emit->ts, call_type, call_type);
    else
        /* This function is guarded by a check for the call type having
           generics. It's a safe assumption that there will be generics in the
           result calculation. So if there's a type being sent to infer
           against, try it.
           Do not check the result. If this fails, there will be a more clear
           error generated somewhere else further down. */
        lily_ts_check(emit->ts, call_type->subtypes[0], expect);
}

/* This does everything a call needs from start to finish. At the end of this,
   the 'ast' given will have a result set and code written. */
static void eval_call(lily_emit_state *emit, lily_ast *ast, lily_type *expect)
{
    lily_ts_save_point p;

    init_call_state(emit, ast, expect);

    lily_type *call_type = start_call(emit, ast);

    /* Scope save MUST happen after the call is started, because evaluating the
       call may trigger a dynaload. That dynaload may then cause the number of
       generics to be seen to increase. But since the scope was registered
       before the increase, there may be types from a different scope (they
       blast on entry) that are improperly visible. */
    lily_ts_scope_save(emit->ts, &p);

    if (call_type->flags & TYPE_IS_UNRESOLVED)
        setup_typing_for_call(emit, ast, expect, call_type);

    run_call(emit, ast, call_type);
    lily_ts_scope_restore(emit->ts, &p);
}

static uint16_t keyarg_to_pos(char **keywords, const char *to_find)
{
    uint16_t i = 0;

    while (1) {
        char *key = keywords[i];

        if (key == NULL) {
            i = UINT16_MAX;
            break;
        }

        if (key[0] && strcmp(key, to_find) == 0)
            break;

        i++;
    }

    return i;
}

static lily_type *get_va_type(lily_type *call_type)
{
    lily_type *va_type;

    if (call_type->flags & TYPE_IS_VARARGS) {
        va_type = call_type->subtypes[call_type->subtype_count - 1];

        if (va_type->cls_id == LILY_ID_OPTARG)
            va_type = va_type->subtypes[0];
    }
    else
        /* The type won't be used if there aren't varargs to use it. Returning
           this allows safely grabbing the contents. */
        va_type = call_type;

    return va_type;
}

/* This is called when receiving a keyword argument that isn't varargs. This
   inserts 'new_ast' into the chain starting at 'source_ast'. The insertion goes
   from lowest position to highest.
   The result of this function is the new head of the chain, in case that
   'new_ast' comes before 'source_ast'. */
static lily_ast *relink_arg(lily_ast *source_ast, lily_ast *new_ast)
{
    if (source_ast->keyword_arg_pos > new_ast->keyword_arg_pos) {
        new_ast->next_arg = source_ast;
        new_ast->left = source_ast->left;
        source_ast = new_ast;
    }
    else {
        lily_ast *iter_ast = source_ast;
        while (1) {
            lily_ast *next_ast = iter_ast->next_arg;
            if (next_ast->keyword_arg_pos > new_ast->keyword_arg_pos) {
                new_ast->next_arg = next_ast;
                iter_ast->next_arg = new_ast;
                break;
            }

            iter_ast = next_ast;
        }
    }

    return source_ast;
}

static char **get_keyarg_names(lily_emit_state *emit, lily_ast *ast)
{
    char **names = NULL;

    if (ast->sym->item_kind == ITEM_DEFINE) {
        lily_var *var = (lily_var *)ast->sym;
        lily_proto *p = lily_emit_proto_for_var(emit, var);

        names = p->keywords;
    }
    else if (ast->sym->item_kind & ITEM_IS_VARIANT)
        names = ast->variant->keywords;

    return names;
}

static int keyarg_at_pos(lily_ast *ast, lily_ast *arg_stop, int pos)
{
    int found = 0;
    lily_ast *arg;

    for (arg = ast->arg_start; arg != arg_stop; arg = arg->next_arg) {
        if (arg->keyword_arg_pos == pos) {
            found = 1;
            break;
        }
    }

    return found;
}

static void keyargs_mark_and_verify(lily_emit_state *emit, lily_ast *ast,
        lily_type *call_type)
{
    lily_ast *arg = ast->arg_start;
    uint16_t num_args = ast->args_collected;
    uint16_t va_pos = UINT16_MAX;
    int have_keyargs = 0;

    if (call_type->flags & TYPE_IS_VARARGS)
        va_pos = call_type->subtype_count - 2;

    char **keywords = get_keyarg_names(emit, ast);

    if (keywords == NULL)
        error_keyarg_not_supported(emit, ast);

    for (uint16_t i = 0; arg != NULL; i++, arg = arg->next_arg) {
        uint16_t pos;

        if (arg->tree_type == tree_binary &&
            arg->op == tk_keyword_arg) {
            have_keyargs = 1;
            char *key_name = lily_sp_get(emit->expr_strings,
                    arg->left->pile_pos);

            pos = keyarg_to_pos(keywords, key_name);

            if (pos == UINT16_MAX)
                error_keyarg_not_valid(emit, ast, arg);

            if (va_pos <= pos)
                num_args--;
            else if (keyarg_at_pos(ast, arg, pos))
                error_keyarg_duplicate(emit, ast, arg);
        }
        else if (have_keyargs == 0) {
            if (i > va_pos)
                pos = va_pos;
            else
                pos = i;
        }
        else
            error_keyarg_before_posarg(emit, arg);

        arg->keyword_arg_pos = pos;
    }

    uint16_t min, max;

    get_func_min_max(call_type, &min, &max);

    if (min > num_args)
        error_keyarg_missing_params(emit, ast, call_type, keywords);
}

static void verify_keyopt_call(lily_emit_state *emit, lily_ast *ast,
        lily_type *call_type, uint16_t min)
{
    if (min == 0)
        return;

    /* The call has at least 'min' args in total. For keyopt, make sure it has
       at least 'min' *required* arguments. Eval has linked them and has tagged
       each with an argument position. */
    lily_ast *arg_iter = ast->arg_start;

    for (uint16_t i = 0;i < min;i++) {
        /* The interpreter defaults to not storing argument names, so reference
           the position instead. */
        if (arg_iter->keyword_arg_pos != i) {
            lily_raise_tree(emit->raiser, ast,
                    "Call to ^I is missing parameters:\n"
                    "* Parameter #%d (^T)", ast->item, i + 1,
                    call_type->subtypes[i + 1]);
        }

        arg_iter = arg_iter->next_arg;
    }
}

static void run_named_call(lily_emit_state *emit, lily_ast *ast,
        lily_type *call_type)
{
    uint16_t num_args = ast->args_collected;
    uint16_t min, max;

    get_func_min_max(call_type, &min, &max);

    /* Let the mark and verify step handle too few arguments, since it will
       print what positions are missing. */
    if (num_args > max)
        error_argument_count(emit, ast, num_args, min, max);

    /* This writes down a position for every argument passed. If there are
       issues (duplicate keys, invalid key, etc.), then this will raise an
       error. */
    keyargs_mark_and_verify(emit, ast, call_type);

    /* Now that the call is known to be good, the arguments can be evaluated.
       Arguments are evaluated in the order they're provided, in case that order
       is relevant to type inference.
       After evaluation, the argument is then broken away and relinked to one
       of the two _head chains found below. This is so that functions written
       with positional args/values in mind don't need to be altered. */

    lily_type **arg_types = call_type->subtypes;
    lily_type *va_elem_type = get_va_type(call_type)->subtypes[0];
    lily_ast *arg = ast->arg_start;
    lily_ast *next_arg = arg->next_arg;
    lily_ast *basic_arg_head = NULL;
    lily_ast *var_arg_head = NULL;
    uint16_t base_count = 0, va_count = 0, va_pos = UINT16_MAX;

    if (call_type->flags & TYPE_IS_VARARGS)
        va_pos = call_type->subtype_count - 2;

    while (arg != NULL) {
        lily_ast *real_arg = arg;
        int is_vararg = 0;
        uint16_t pos = arg->keyword_arg_pos;

        if (arg->tree_type == tree_binary && arg->op == tk_keyword_arg)
            real_arg = arg->right;

        lily_type *arg_type;

        if (va_pos > pos)
            arg_type = arg_types[pos + 1];
        else {
            arg_type = va_elem_type;
            is_vararg = 1;
        }

        if (eval_call_arg(emit, real_arg, arg_type) == 0)
            error_bad_arg(emit, ast, arg, call_type, pos,
                    real_arg->result->type);

        /* For keyargs, this lifts the result into the proper tree. */
        arg->result = real_arg->result;

        /* Unlink the argument from the original argument chain. */
        next_arg = arg->next_arg;
        arg->next_arg = NULL;

        /* Undo potential damage to the keyword pos field. */
        arg->keyword_arg_pos = pos;

        /* This links the argument back to the appropriate chain.
           Both chains use the 'left' field to store their last argument for
           convenience. It's okay to do so because the arguments in question
           have already been evaluated. */
        if (is_vararg == 0) {
            /* Basic arguments are sorted by keyword_arg_pos, with the lowest
               value being the head. */
            if (basic_arg_head == NULL) {
                basic_arg_head = arg;
                basic_arg_head->left = arg;
            }
            else if (basic_arg_head->left->keyword_arg_pos
                     < arg->keyword_arg_pos) {
                basic_arg_head->left->next_arg = arg;
                basic_arg_head->left = arg;
            }
            else
                basic_arg_head = relink_arg(basic_arg_head, arg);

            base_count++;
        }
        else {
            /* Varargs, on the other hand, are sorted in order of appearance. */
            if (var_arg_head == NULL) {
                var_arg_head = arg;
                var_arg_head->left = arg;
            }
            else {
                var_arg_head->left->next_arg = arg;
                var_arg_head->left = arg;
            }

            va_count++;
        }

        arg = next_arg;
    }

    lily_storage *vararg_s = NULL;

    if (va_pos != UINT16_MAX) {
        lily_type *va_type = call_type->subtypes[call_type->subtype_count - 1];

        if (var_arg_head ||
            va_type->cls_id != LILY_ID_OPTARG) {
            lily_type *va_list_type = get_va_type(call_type);

            if (va_list_type->flags & TYPE_IS_UNRESOLVED)
                va_list_type = lily_ts_resolve(emit->ts, va_list_type);

            vararg_s = get_storage(emit, va_list_type);
            lily_u16_write_2(emit->code, o_build_list, va_count);

            for (;var_arg_head;var_arg_head = var_arg_head->next_arg)
                lily_u16_write_1(emit->code, var_arg_head->result->reg_spot);

            lily_u16_write_2(emit->code, vararg_s->reg_spot, ast->line_num);
        }
    }

    ast->arg_start = basic_arg_head;
    setup_call_result(emit, ast, arg_types[0]);

    if ((call_type->flags & TYPE_HAS_OPTARGS) == 0)
        write_call(emit, ast, base_count, vararg_s);
    else {
        verify_keyopt_call(emit, ast, call_type, min);
        write_call_keyopt(emit, ast, call_type, vararg_s);
    }
}

static void eval_named_call(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_ts_save_point p;

    init_call_state(emit, ast, expect);

    lily_type *call_type = start_call(emit, ast);

    /* Scope save MUST happen after the call is started, because evaluating the
       call may trigger a dynaload. That dynaload may then cause the number of
       generics to be seen to increase. But since the scope was registered
       before the increase, there may be types from a different scope (they
       blast on entry) that are improperly visible. */
    lily_ts_scope_save(emit->ts, &p);

    if (call_type->flags & TYPE_IS_UNRESOLVED)
        setup_typing_for_call(emit, ast, expect, call_type);

    run_named_call(emit, ast, call_type);
    lily_ts_scope_restore(emit->ts, &p);
}

/* This handles variants that are used when they don't receive arguments. Any
   variants that are passed arguments are handled by call processing. */
static void eval_variant(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_variant_class *variant = ast->variant;
    /* Did this need arguments? It was used incorrectly if so. */
    if (variant->item_kind == ITEM_VARIANT_FILLED) {
        uint16_t min, max;
        get_func_min_max(variant->build_type, &min, &max);
        error_argument_count(emit, ast, 0, min, max);
    }

    /* An empty variant's build type is the enum self type with any generics
       being solved with ?. Use that unless there's inference to pull from. */
    lily_type *storage_type = variant->build_type;
    int is_value_enum = (storage_type->cls->parent != NULL);

    if (storage_type->flags & TYPE_IS_INCOMPLETE &&
        expect->cls == variant->parent)
        storage_type = expect;

    uint16_t op, what;

    if (is_value_enum == 0) {
        op = o_load_empty_variant;
        what = variant->cls_id;
    }
    else {
        if (expect->cls_id == LILY_ID_INTEGER)
            storage_type = expect;

        op = o_load_readonly;
        what = variant->backing_lit;
    }

    lily_storage *s = get_storage(emit, storage_type);

    lily_u16_write_4(emit->code, op, what, s->reg_spot, ast->line_num);
    ast->result = (lily_sym *)s;
}

static void eval_dot_variant(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    unpack_dot_variant(emit, ast, expect);
    eval_variant(emit, ast, expect);
}

/* This handles function pipes by faking them as calls and running them as a
   call. The job of this is to turn `f |> g` into `g(f)`.
   Shoutout to F#, which inspired this idea. */
static void eval_func_pipe(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    /* lily_ast has 'right' and 'arg_start' in a union. 'right' is also the
       thing to be called. So, by itself, that's quite a bit already done. For
       calls to be sufficiently faked, the left needs to first be hooked up to
       follow the right. */
    ast->right->next_arg = ast->left;
    /* No matter what, say there are just two arguments. It's up to calls to
       find out if there are really two arguments or not. */
    ast->args_collected = 2;
    /* This particular operation is a special case. In nearly any other case,
       evaluating a tree should not damage the subtrees in any way. I'm doing it
       this way only because calls are hard. */
    ast->tree_type = tree_call;
    eval_call(emit, ast, expect);
}

static void eval_plus_plus(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->left->tree_type != tree_local_var)
        eval_tree(emit, ast->left, lily_question_type);

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, lily_question_type);

    if (ast->parent == NULL ||
        (ast->parent->tree_type != tree_binary ||
         ast->parent->op != tk_plus_plus)) {
        lily_u16_write_2(emit->code, o_interpolation, 0);

        uint16_t fix_spot = lily_u16_pos(emit->code) - 1;
        lily_ast *iter_ast = ast->left;
        lily_storage *s = get_storage(emit,
                emit->symtab->string_class->self_type);

        while (1) {
            if (iter_ast->tree_type != tree_binary ||
                iter_ast->op != tk_plus_plus)
                break;

            iter_ast = iter_ast->left;
        }

        iter_ast = iter_ast->parent;
        lily_u16_write_1(emit->code, iter_ast->left->result->reg_spot);

        while (iter_ast != ast) {
            lily_u16_write_1(emit->code, iter_ast->right->result->reg_spot);
            iter_ast = iter_ast->parent;
        }

        lily_u16_write_1(emit->code, iter_ast->right->result->reg_spot);

        lily_u16_set_at(emit->code, fix_spot,
                lily_u16_pos(emit->code) - fix_spot - 1);

        lily_u16_write_2(emit->code, s->reg_spot, ast->line_num);

        ast->result = (lily_sym *)s;
    }
}

/***
 *         _    ____ ___
 *        / \  |  _ \_ _|
 *       / _ \ | |_) | |
 *      / ___ \|  __/| |
 *     /_/   \_\_|  |___|
 *
 */

/** The rest are functions that are used by parser (or eval_tree which helps
    them) that don't seem to fit anywhere else. Most of these deal with running
    an expression. **/

/* Evaluate 'ast' with 'expect' for inference. If caller has no inference to
   give, then 'expect' should be 'lily_question_type'. */
static void eval_tree(lily_emit_state *emit, lily_ast *ast, lily_type *expect)
{
    if (ast->tree_type == tree_global_var ||
        ast->tree_type == tree_defined_func ||
        ast->tree_type == tree_static_func ||
        ast->tree_type == tree_method ||
        ast->tree_type == tree_inherited_new ||
        ast->tree_type == tree_upvalue)
        emit_nonlocal_var(emit, ast, expect);
    else if (ast->tree_type == tree_local_var)
        emit_local_var(emit, ast, expect);
    else if (ast->tree_type == tree_literal)
        emit_literal(emit, ast);
    else if (ast->tree_type == tree_integer)
        emit_integer(emit, ast, expect);
    else if (ast->tree_type == tree_byte)
        emit_byte(emit, ast, expect);
    else if (ast->tree_type == tree_boolean)
        emit_boolean(emit, ast);
    else if (ast->tree_type == tree_call)
        eval_call(emit, ast, expect);
    else if (ast->tree_type == tree_binary)
        eval_binary_op(emit, ast, expect);
    else if (ast->tree_type == tree_parenth) {
        lily_ast *start = ast->arg_start;

        eval_tree(emit, start, expect);
        ast->result = start->result;
   }
    else if (ast->tree_type == tree_unary)
        eval_unary_op(emit, ast);
    else if (ast->tree_type == tree_list)
        eval_build_list(emit, ast, expect);
    else if (ast->tree_type == tree_hash)
        eval_build_hash(emit, ast, expect);
    else if (ast->tree_type == tree_tuple)
        eval_build_tuple(emit, ast, expect);
    else if (ast->tree_type == tree_subscript)
        eval_subscript(emit, ast, expect);
    else if (ast->tree_type == tree_typecast)
        eval_typecast(emit, ast);
    else if (ast->tree_type == tree_oo_access)
        eval_oo_access(emit, ast, expect);
    else if (ast->tree_type == tree_property)
        eval_property(emit, ast, expect);
    else if (ast->tree_type == tree_variant)
        eval_variant(emit, ast, expect);
    else if (ast->tree_type == tree_lambda)
        eval_lambda_to_parse(emit, ast, expect);
    else if (ast->tree_type == tree_self)
        eval_self(emit, ast);
    else if (ast->tree_type == tree_named_call)
        eval_named_call(emit, ast, expect);
    else if (ast->tree_type == tree_dot_variant)
        eval_dot_variant(emit, ast, expect);
    else if (ast->tree_type == tree_ternary_second)
        eval_ternary(emit, ast, expect);
    else if (ast->tree_type == tree_expr_match)
        eval_expr_match(emit, ast, expect);
}

static void eval_enforce_value(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    eval_tree(emit, ast, expect);
    emit->expr_num++;

    if (ast->result == NULL)
        lily_raise_syn(emit->raiser,
                "Expected a value, but got an assignment instead.");
}

/* This evaluates an expression at the root of the given pool, then resets the
   pool for the next expression. */
void lily_eval_expr(lily_emit_state *emit, lily_expr_state *es)
{
    eval_tree(emit, es->root, lily_question_type);
    emit->expr_num++;
}

lily_sym *lily_eval_for_result(lily_emit_state *emit, lily_ast *ast)
{
    eval_tree(emit, ast, lily_question_type);
    emit->expr_num++;

    if (ast->result == NULL)
        lily_raise_syn(emit->raiser,
                "Expected a value, but got an assignment instead.");

    return ast->result;
}

void lily_eval_optarg(lily_emit_state *emit, lily_ast *ast)
{
    /* Optional arguments are implemented as assignments. */
    uint16_t target_reg = ast->left->sym->reg_spot;

    /* The jump is 2 spots away from the current code pos. */
    uint16_t patch = lily_u16_pos(emit->code) + 2;

    /* The offset is 2, but it technically doesn't need to be written because
       the offset is added as + 2 below. */
    lily_u16_write_3(emit->code, o_jump_if_set, target_reg, 2);

    eval_tree(emit, ast, lily_question_type);
    emit->expr_num++;

    /* If this optional argument was initialized, jump to now. */
    lily_u16_set_at(emit->code, patch, lily_u16_pos(emit->code) - patch + 2);
}

static void verify_for_expr_source_type(lily_emit_state *emit, lily_type *t)
{
    if (t->flags & TYPE_IS_INCOMPLETE)
        lily_raise_syn(emit->raiser,
                "For expression has incomplete type ^T.", t);

    uint16_t cls_id = t->cls_id;

    if (cls_id == LILY_ID_LIST ||
        cls_id == LILY_ID_BYTESTRING ||
        cls_id == LILY_ID_STRING)
        return;

    lily_raise_syn(emit->raiser,
            "For expression is not iterable:\n"
            "Expected: ByteString, List, or String\n"
            "Received: ^T", t);
}

void lily_eval_for_of(lily_emit_state *emit, lily_expr_state *es,
        lily_var *for_source, lily_var *elem_var)
{
    lily_ast *ast = es->root;

    if (ast->tree_type == tree_binary &&
        IS_ASSIGN_TOKEN(ast->op)) {
        lily_raise_syn(emit->raiser,
                   "For expression contains an assignment.");
    }

    eval_tree(emit, ast, lily_question_type);
    emit->expr_num++;

    lily_type *t = ast->result->type;

    verify_for_expr_source_type(emit, t);

    /* Opcode writing uses this to determine which one to write. */
    for_source->type = t;

    if (t->cls_id == LILY_ID_LIST)
        t = t->subtypes[0];
    else
        t = (lily_type *)emit->symtab->byte_class;

    if (elem_var->type == lily_question_type)
        /* Fix parser's placeholder type. */
        elem_var->type = t;
    else if (lily_ts_type_greater_eq(emit->ts, elem_var->type, t) == 0)
        lily_raise_syn(emit->raiser,
            "For list element type is not compatible:\n"
            "Expected (by %s): ^T\n"
            "Received: ^T",
            elem_var->name, elem_var->type, t);

    lily_u16_write_4(emit->code, o_assign, ast->result->reg_spot,
            for_source->reg_spot, ast->line_num);
}

void lily_eval_to_loop_var(lily_emit_state *emit, lily_expr_state *es,
        lily_var *var)
{
    lily_ast *ast = es->root;

    /* Don't allow assigning expressions, since that just looks weird.
       ex: for i in a += 10..5
       Also, it makes no real sense to do that. */
    if (ast->tree_type == tree_binary &&
        IS_ASSIGN_TOKEN(ast->op)) {
        lily_raise_syn(emit->raiser,
                   "For range value expression contains an assignment.");
    }

    /* Send the type to allow autopromotion. */
    eval_tree(emit, ast, var->type);
    emit->expr_num++;

    if (ast->result->type != var->type) {
        lily_raise_syn(emit->raiser,
                   "Expected type 'Integer', but got type '^T'.",
                   ast->result->type);
    }

    lily_u16_write_4(emit->code, o_assign_noref, ast->result->reg_spot,
            var->reg_spot, ast->line_num);
}

static int is_false_tree(lily_ast *ast)
{
    int result = 0;

    if (ast->tree_type == tree_boolean ||
        ast->tree_type == tree_byte ||
        ast->tree_type == tree_integer)
        result = (ast->backing_value != 0);

    return result;
}

static int is_compare_tree(lily_ast *ast)
{
    int result = 0;

    if (ast->tree_type == tree_binary &&
        IS_COMPARE_TOKEN(ast->op))
        result = 1;

    return result;
}

/* Evaluate an expression that falls through if truthy, or jumps to the next
   branch if falsey. */
void lily_eval_entry_condition(lily_emit_state *emit, lily_expr_state *es)
{
    lily_ast *ast = es->root;

    if (is_false_tree(ast)) {
        /* Write a fake jump for block transition to skip over. */
        lily_u16_write_1(emit->patches, 0);
        return;
    }

    if (is_compare_tree(ast)) {
        /* Do exactly the start of eval_compare_op. */
        write_compare_or_error(emit, ast, eval_for_compare(emit, ast));

        /* Comparison ops end with a jump, then a line number. Use that jump. */
        lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 2);
        return;
    }

    eval_enforce_value(emit, ast, lily_question_type);
    ensure_valid_condition_type(emit, ast);

    /* Jump if false (0) to the next branch. The branch will be patched when the
       next condition comes in.  */
    emit_jump_if(emit, ast, 0);
}

/* Evaluate an expression that exits if truthy, or jumps back up if falsey. */
void lily_eval_exit_condition(lily_emit_state *emit, lily_expr_state *es)
{
    lily_ast *ast = es->root;

    if (is_false_tree(ast)) {
        uint16_t location = lily_u16_pos(emit->code) - emit->block->code_start;
        lily_u16_write_2(emit->code, o_jump, (uint16_t)-location);
        return;
    }

    eval_enforce_value(emit, ast, lily_question_type);
    ensure_valid_condition_type(emit, ast);

    uint16_t location = lily_u16_pos(emit->code) - emit->block->code_start;
    lily_u16_write_4(emit->code, o_jump_if, 1, ast->result->reg_spot,
            (uint16_t)-location);
}

void lily_eval_lambda_exit(lily_emit_state *emit)
{
    if (emit->block->last_exit == lily_u16_pos(emit->code))
        /* Unreachable, so nothing more to do. */
        return;

    /* Lambdas use the result type as the var type until they're done. If this
       was a define, the result would be the first subtype of this type. */
    lily_type *expect = emit->scope_block->scope_var->type;

    /* It's unset if the parent tree has no inference (and will thus accept
       anythign). '?' occurs when the parent expects a generic that does not
       have a solution yet. */
    if (expect == lily_unset_type ||
        expect == lily_question_type ||
        expect == lily_unit_type) {
        lily_u16_write_2(emit->code, o_return_unit, *emit->lex_linenum);
        emit->block->last_exit = lily_u16_pos(emit->code);
        return;
    }

    lily_raise_syn(emit->raiser,
            "Lambda result should be type '^T', but none given.", expect);
}

/* This is called by parser to evaluate a lambda's last expression, which will
   become the type it returns. */
lily_type *lily_eval_lambda_result(lily_emit_state *emit, lily_expr_state *es)
{
    lily_block *scope_block = emit->scope_block;
    lily_type *expect = scope_block->scope_var->type;

    /* Calls infer their arguments by solving their result against what is
       expected. Unset should never be witnessed, and Unit results in terrible
       inference (nobody wants a List[Unit]). */
    if (expect == lily_unset_type ||
        expect == lily_unit_type)
        expect = lily_question_type;

    scope_block->flags |= BLOCK_LAMBDA_RESULT;
    eval_tree(emit, es->root, expect);
    scope_block->flags &= ~BLOCK_LAMBDA_RESULT;

    lily_sym *root_result = es->root->result;
    lily_type *result_type;

    if (root_result) {
        lily_u16_write_3(emit->code, o_return_value, root_result->reg_spot,
                es->root->line_num);
        emit->block->last_exit = lily_u16_pos(emit->code);
        result_type = root_result->type;
    }
    else {
        /* This only happens if it was an assignment. */
        lily_u16_write_2(emit->code, o_return_unit, es->root->line_num);
        emit->block->last_exit = lily_u16_pos(emit->code);
        result_type = lily_unit_type;
    }

    if (expect == result_type ||
        expect == lily_question_type)
        return result_type;

    lily_type *unify_type = lily_ts_unify(emit->ts, expect, result_type);

    if (unify_type == NULL)
        lily_raise_tree(emit->raiser, es->root,
                "Lambda result should be type '^T', but got type '^T'.",
                expect, result_type);

    return unify_type;
}

static void update_lambda_return(lily_emit_state *emit, lily_type *result_type)
{
    lily_var *scope_var = emit->scope_block->scope_var;
    lily_type *expect = scope_var->type;
    lily_type *unify_type = lily_ts_unify(emit->ts, expect, result_type);

    /* Shouldn't happen, but be on the safe side. */
    if (unify_type)
        scope_var->type = unify_type;
}

/* This handles the 'return' keyword. If parser has the pool filled with some
   expression, then run that expression (checking the result). The pool will be
   cleared out if there was an expression. */
void lily_eval_return(lily_emit_state *emit, lily_expr_state *es,
        lily_type *return_type)
{
    lily_ast *ast = es->root;

    eval_enforce_value(emit, ast, return_type);

    if (result_matches_type(emit, ast, return_type))
        ;
    else if (return_type->cls == lily_self_class &&
             ast->tree_type == tree_self)
        ;
    else
        lily_raise_tree(emit->raiser, ast,
                "return expected type '^T' but got type '^T'.", return_type,
                ast->result->type);

    write_pop_try_blocks_up_to(emit, emit->scope_block);
    lily_u16_write_3(emit->code, o_return_value, ast->result->reg_spot,
            ast->line_num);
    emit->block->last_exit = lily_u16_pos(emit->code);

    if (return_type->flags & TYPE_IS_INCOMPLETE)
        update_lambda_return(emit, ast->result->type);
}

void lily_eval_raise(lily_emit_state *emit, lily_expr_state *es)
{
    lily_ast *ast = es->root;
    eval_enforce_value(emit, ast, lily_question_type);

    lily_class *result_cls = ast->result->type->cls;
    if (lily_class_greater_eq_id(LILY_ID_EXCEPTION, result_cls) == 0) {
        lily_raise_tree(emit->raiser, ast,
                "Invalid class '%s' given to raise.", result_cls->name);
    }

    lily_u16_write_3(emit->code, o_exception_raise, ast->result->reg_spot,
            ast->line_num);
    emit->block->last_exit = lily_u16_pos(emit->code);
}

void lily_eval_unit_return(lily_emit_state *emit)
{
    write_pop_try_blocks_up_to(emit, emit->scope_block);
    lily_u16_write_2(emit->code, o_return_unit, *emit->lex_linenum);
    emit->block->last_exit = lily_u16_pos(emit->code);
}

/* This prepares __main__ to be called and sets up the next pass. */
void lily_prepare_main(lily_emit_state *emit, lily_function_val *main_func)
{
    uint16_t register_count = emit->block->next_reg_spot;

    lily_u16_write_1(emit->code, o_vm_exit);

    main_func->code_len = lily_u16_pos(emit->code);
    main_func->code = emit->code->data;
    main_func->proto->code = main_func->code;
    main_func->reg_count = register_count;

    /* Emitter won't write code until the next pass comes around. Set it up so
       that it will start writing over the old instructions with new ones. */
    emit->code->pos = 0;
}

void lily_clear_main(lily_emit_state *emit)
{
    emit->code->pos = 0;
}

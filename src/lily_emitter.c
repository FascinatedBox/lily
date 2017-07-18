#include <string.h>
#include <stdint.h>

#include "lily_alloc.h"
#include "lily_expr.h"
#include "lily_emitter.h"
#include "lily_parser.h"

#include "lily_int_opcode.h"
#include "lily_int_code_iter.h"

# define IS_LOOP_BLOCK(b) (b == block_while || \
                           b == block_do_while || \
                           b == block_for_in)

# define lily_raise_adjusted(r, adjust, message, ...) \
{ \
    r->line_adjust = adjust; \
    lily_raise_syn(r, message, __VA_ARGS__); \
}

extern lily_class *lily_self_class;
extern lily_type *lily_unit_type;

/***
 *      ____       _
 *     / ___|  ___| |_ _   _ _ __
 *     \___ \ / _ \ __| | | | '_ \
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/
 *                          |_|
 */

static lily_storage_stack *new_storage_stack(int);
static void free_storage_stack(lily_storage_stack *);
lily_function_val *new_native_function_val(char *, char *);
lily_function_val *new_foreign_function_val(lily_foreign_func, const char *,
        const char *);

lily_emit_state *lily_new_emit_state(lily_symtab *symtab, lily_raiser *raiser)
{
    lily_emit_state *emit = lily_malloc(sizeof(*emit));

    emit->patches = lily_new_buffer_u16(4);
    emit->match_cases = lily_malloc(sizeof(*emit->match_cases) * 4);
    emit->tm = lily_new_type_maker();
    emit->ts = lily_new_type_system(emit->tm, symtab->dynamic_class->self_type,
            symtab->question_class->self_type);
    emit->code = lily_new_buffer_u16(32);
    emit->closure_aux_code = NULL;

    emit->closure_spots = lily_new_buffer_u16(4);

    emit->storages = new_storage_stack(4);

    emit->transform_table = NULL;
    emit->transform_size = 0;

    emit->expr_strings = lily_new_string_pile();

    /* tm uses Dynamic's type as a special default, so it needs that. */
    emit->tm->dynamic_class_type = symtab->dynamic_class->self_type;
    emit->tm->question_class_type = symtab->question_class->self_type;

    emit->match_case_pos = 0;
    emit->match_case_size = 4;

    emit->block = NULL;

    emit->function_depth = 0;

    emit->raiser = raiser;
    emit->expr_num = 1;

    lily_block *main_block = lily_malloc(sizeof(*main_block));

    main_block->prev = NULL;
    main_block->next = NULL;
    main_block->block_type = block_file;
    main_block->class_entry = NULL;
    main_block->self = NULL;
    main_block->code_start = 0;
    main_block->next_reg_spot = 0;
    main_block->storage_start = 0;
    main_block->var_count = 0;
    main_block->flags = 0;
    emit->block = main_block;
    emit->function_depth++;
    emit->main_block = main_block;
    emit->function_block = main_block;
    emit->class_block_depth = 0;

    return emit;
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_block *current, *temp;
    current = emit->block;
    while (current && current->prev)
        current = current->prev;

    while (current) {
        temp = current->next;
        lily_free(current);
        current = temp;
    }

    free_storage_stack(emit->storages);

    lily_free_string_pile(emit->expr_strings);
    lily_free_type_maker(emit->tm);
    lily_free(emit->transform_table);
    lily_free_type_system(emit->ts);
    lily_free(emit->match_cases);
    if (emit->closure_aux_code)
        lily_free_buffer_u16(emit->closure_aux_code);
    lily_free_buffer_u16(emit->closure_spots);
    lily_free_buffer_u16(emit->patches);
    lily_free_buffer_u16(emit->code);
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
static void inject_patch_into_block(lily_emit_state *, lily_block *, uint16_t);
static void eval_tree(lily_emit_state *, lily_ast *, lily_type *);

/* This is called from parser to get emitter to write a function call targeting
   a var. The var should always be an __import__ function. */
void lily_emit_write_import_call(lily_emit_state *emit, lily_var *var)
{
    uint16_t spot = lily_emit_get_storage_spot(emit, lily_unit_type);
    lily_u16_write_5(emit->code, o_native_call, var->reg_spot, 0, spot,
            *emit->lex_linenum);
}

void lily_emit_eval_optarg(lily_emit_state *emit, lily_ast *ast)
{
    eval_tree(emit, ast, NULL);
    emit->expr_num++;

    uint16_t patch_spot = lily_u16_pop(emit->patches);

    lily_u16_set_at(emit->code, patch_spot,
            lily_u16_pos(emit->code) - patch_spot + 1);
}

void lily_emit_write_optarg_header(lily_emit_state *emit, lily_type *type,
        int *count)
{
    /* Optargs are always together and always after any required arguments.
       This figures out where they start, and begins writing o_jump_if_not_class
       instructions. As it goes along, jumps are written to target the
       initialization expressions that optargs wrote. Given this:

       ```
       define f(a: *Integer=0, b: *String="", c: *Option[Integer]=None)
       ```

       The result looks like:

       if a is set
           if b is set
               if c is set
                   jump to done
               else
                   jump to init c
           else
               jump to init b
       else
           jump to init a

       init a
       init b
       init c
       */

    int i;
    for (i = type->subtype_count - 1;i > 0;i--) {
        lily_type *inner = type->subtypes[i];
        if (inner->cls->id != LILY_OPTARG_ID)
            break;
    }

    int patch_start = lily_u16_pos(emit->patches);
    uint16_t first_reg = (uint16_t)i;

    i = type->subtype_count - i - 1;

    for (;i > 0;i--, first_reg++) {
        /* If this value is NOT unset, jump to the next test. */
        lily_u16_write_4(emit->code, o_jump_if_not_class, first_reg, 0, 6);

        /* Otherwise jump to the assign table. */
        lily_u16_write_2(emit->code, o_jump, 1);
        lily_u16_inject(emit->patches, patch_start,
                lily_u16_pos(emit->code) - 1);
    }

    *count = lily_u16_pos(emit->patches) - patch_start;

    /* Write one final jump for when all branches succeed. */
    lily_u16_write_2(emit->code, o_jump, 1);
    lily_u16_inject(emit->patches, patch_start,
            lily_u16_pos(emit->code) - 1);

    /* Patches are injected making them first in first out (instead of last
       out). The first patch is a jump to 'init a', which happens when none of
       the optarg values are set. That's right now, so that patch can be taken
       care of. */

    uint16_t patch_spot = lily_u16_pop(emit->patches);
    lily_u16_set_at(emit->code, patch_spot,
            lily_u16_pos(emit->code) - patch_spot + 1);
}

void lily_emit_write_class_header(lily_emit_state *emit, lily_type *self_type,
        uint16_t line_num)
{
    lily_storage *self = get_storage(emit, self_type);

    emit->block->self = self;
    lily_u16_write_4(emit->code, o_new_instance_basic, self_type->cls->id,
            self->reg_spot, line_num);
}

void lily_emit_write_shorthand_ctor(lily_emit_state *emit, lily_class *cls,
        lily_var *var_iter, uint16_t line_num)
{
    lily_named_sym *prop_iter = cls->members;
    uint16_t self_reg_spot = emit->block->self->reg_spot;

    /* The class constructor always inserts itself as the first property. Make
       sure to not include that. */

    while (prop_iter->item_kind == ITEM_TYPE_PROPERTY) {
        while (strcmp(var_iter->name, "") != 0)
            var_iter = var_iter->next;

        lily_u16_write_5(emit->code, o_set_property, prop_iter->reg_spot,
                self_reg_spot, var_iter->reg_spot, *emit->lex_linenum);

        var_iter = var_iter->next;
        prop_iter = prop_iter->next;
    }
}

/* This function writes the code necessary to get a for <var> in x...y style
   loop to work. */
void lily_emit_finalize_for_in(lily_emit_state *emit, lily_var *user_loop_var,
        lily_var *for_start, lily_var *for_end, lily_sym *for_step,
        int line_num)
{
    lily_sym *target;
    int need_sync = user_loop_var->flags & VAR_IS_GLOBAL;

    if (need_sync) {
        lily_class *cls = emit->symtab->integer_class;
        /* o_integer_for expects the target register to be a local. Since it
           isn't, do syncing reads before and after to make sure the user's
           loop var is what it should be. */
        target = (lily_sym *)get_storage(emit, cls->self_type);
    }
    else
        target = (lily_sym *)user_loop_var;

    lily_u16_write_6(emit->code, o_for_setup, for_start->reg_spot,
            for_end->reg_spot, for_step->reg_spot, target->reg_spot, line_num);

    if (need_sync) {
        lily_u16_write_4(emit->code, o_set_global, target->reg_spot,
                user_loop_var->reg_spot, line_num);
    }

    /* Fix the start so the continue doesn't reinitialize loop vars. */
    emit->block->code_start = lily_u16_pos(emit->code);

    lily_u16_write_5(emit->code, o_integer_for, for_start->reg_spot,
            for_end->reg_spot, for_step->reg_spot, target->reg_spot);

    lily_u16_write_2(emit->code, 5, line_num);

    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 2);

    if (need_sync) {
        lily_u16_write_4(emit->code, o_set_global, target->reg_spot,
                user_loop_var->reg_spot, line_num);
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
        if (block_iter->block_type == block_try)
            try_count++;

        block_iter = block_iter->prev;
    }

    if (try_count) {
        int i;
        for (i = 0;i < try_count;i++)
            lily_u16_write_1(emit->code, o_pop_try);
    }
}

/* The parser has a 'break' and wants the emitter to write the code. */
void lily_emit_break(lily_emit_state *emit)
{
    lily_block *loop_block = find_deepest_loop(emit);

    if (loop_block == NULL)
        lily_raise_syn(emit->raiser, "'break' used outside of a loop.");

    write_pop_try_blocks_up_to(emit, loop_block);

    /* Write the jump, then figure out where to put it. */
    lily_u16_write_2(emit->code, o_jump, 1);

    inject_patch_into_block(emit, loop_block, lily_u16_pos(emit->code) - 1);
}

/* The parser has a 'continue' and wants the emitter to write the code. */
void lily_emit_continue(lily_emit_state *emit)
{
    lily_block *loop_block = find_deepest_loop(emit);

    if (loop_block == NULL)
        lily_raise_syn(emit->raiser, "'continue' used outside of a loop.");

    write_pop_try_blocks_up_to(emit, loop_block);

    int where = emit->block->code_start - lily_u16_pos(emit->code);
    lily_u16_write_2(emit->code, o_jump, (uint16_t)where);
}

/* The parser has a 'try' and wants the emitter to write the code. */
void lily_emit_try(lily_emit_state *emit, int line_num)
{
    lily_u16_write_3(emit->code, o_push_try, 1, line_num);

    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 2);
}

/* The parser has an 'except' clause and wants emitter to write code for it. */
void lily_emit_except(lily_emit_state *emit, lily_type *except_type,
        lily_var *except_var, int line_num)
{
    if (except_var) {
        /* There's a register to dump the result into, so use this opcode to let
           the vm know to copy down the information to this var. */
        lily_u16_write_4(emit->code, o_catch, line_num,
                except_var->type->cls->id, 3);
        lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);
        lily_u16_write_2(emit->code, o_store_exception, except_var->reg_spot);
    }
    else {
        lily_u16_write_4(emit->code, o_catch, line_num,
                except_type->cls->id, 3);
        lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);
    }
}

/* Write a conditional jump. 0 means jump if false, 1 means jump if true. The
   ast is the thing to test. */
static void emit_jump_if(lily_emit_state *emit, lily_ast *ast, int jump_on)
{
    lily_u16_write_4(emit->code, o_jump_if, jump_on, ast->result->reg_spot, 3);

    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);
}

/* This writes patches down until 'to' is reached. The patches are written so
   they target the current code position. */
static void write_patches_since(lily_emit_state *emit, int to)
{
    int from = emit->patches->pos - 1;
    int pos = lily_u16_pos(emit->code);

    for (;from >= to;from--) {
        uint16_t patch = lily_u16_pop(emit->patches);

        /* Skip 0's (those are patches that have been optimized out.
           Here's a bit of math: If the vm is at 'x' and wants to get to 'y', it
           can add 'y - x' to 'x', and have 'y'. Cool, right?
           The trouble is that jump positions may be +1 or +2 relative to the
           position of the opcode.
           This problem is worked around by having jumps write down their offset
           to the opcode, and including that in the jump. */
        if (patch != 0) {
            int adjust = lily_u16_get(emit->code, patch);
            lily_u16_set_at(emit->code, patch, pos + adjust - patch);
        }
    }
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
    result->item_kind = ITEM_TYPE_STORAGE;

    return result;
}

/** Storages are used to hold intermediate values. The emitter is responsible
    for handing them out, controlling their position, and making new ones.
    Most of that is done in get_storage. **/
static lily_storage_stack *new_storage_stack(int initial)
{
    lily_storage_stack *result = lily_malloc(sizeof(*result));
    result->data = lily_malloc(initial * sizeof(*result->data));
    int i;
    for (i = 0;i < initial;i++) {
        lily_storage *s = new_storage();

        result->data[i] = s;
    }

    result->scope_end = 0;
    result->size = initial;

    return result;
}

static void free_storage_stack(lily_storage_stack *stack)
{
    int i;
    for (i = 0;i < stack->size;i++) {
        lily_free(stack->data[i]);
    }

    lily_free(stack->data);
    lily_free(stack);
}

static void grow_storages(lily_storage_stack *stack)
{
    int i;
    int new_size = stack->size * 2;
    lily_storage **new_data = lily_realloc(stack->data,
            sizeof(*new_data) * stack->size * 2);

    /* Storages are taken pretty often, so eagerly initialize them for a little
       bit more speed. */
    for (i = stack->size;i < new_size;i++)
        new_data[i] = new_storage();

    stack->data = new_data;
    stack->size = new_size;
}

/* This attempts to grab a storage of the given type. It will first attempt to
   get a used storage, then a new one. */
static lily_storage *get_storage(lily_emit_state *emit, lily_type *type)
{
    lily_storage_stack *stack = emit->storages;
    int expr_num = emit->expr_num;
    int i;
    lily_storage *s = NULL;

    for (i = emit->function_block->storage_start;
         i < stack->size;
         i++) {
        s = stack->data[i];

        /* A storage with a type of NULL is not in use and can be claimed. */
        if (s->type == NULL) {
            s->type = type;

            s->reg_spot = emit->function_block->next_reg_spot;
            emit->function_block->next_reg_spot++;

            i++;
            if (i == stack->size)
                grow_storages(emit->storages);

            /* This prevents inner functions from using the storages of outer
               functions as their own. */
            stack->scope_end = i;

            break;
        }
        else if (s->type == type &&
                 s->expr_num != expr_num) {
            s->expr_num = expr_num;
            break;
        }
    }

    s->expr_num = expr_num;
    s->flags &= ~SYM_NOT_ASSIGNABLE;

    return s;
}

/* This function attempts to get a storage of a particular type that has not
   been used before. This is used by closures. */
lily_storage *get_unique_storage(lily_emit_state *emit, lily_type *type)
{
    int next_spot = emit->function_block->next_reg_spot;
    lily_storage *s = NULL;

    do {
        s = get_storage(emit, type);
    } while (emit->function_block->next_reg_spot == next_spot);

    return s;
}

uint16_t lily_emit_get_storage_spot(lily_emit_state *emit, lily_type *type)
{
    lily_storage *s = get_storage(emit, type);
    return s->reg_spot;
}

/***
 *      ____  _            _
 *     | __ )| | ___   ___| | _____
 *     |  _ \| |/ _ \ / __| |/ / __|
 *     | |_) | | (_) | (__|   <\__ \
 *     |____/|_|\___/ \___|_|\_\___/
 *
 */

static void inject_patch_into_block(lily_emit_state *, lily_block *, uint16_t);
static void write_final_code_for_block(lily_emit_state *, lily_block *);

/** The emitter's blocks keep track of the current context of things. Is the
    current block an if with or without an else? Where do storages start? Were
    any vars created in this scope?

    Blocks are currently in a rough state. They've accidentally grown fat, and
    likely carry too much info. The same thing represents both a defined
    function, an if block, etc. Some blocks don't necessarily use all of the
    items that are inside. **/

static lily_block *block_enter_common(lily_emit_state *emit)
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
    new_block->self = emit->block->self;
    new_block->patch_start = emit->patches->pos;
    new_block->last_exit = -1;
    new_block->flags = 0;
    new_block->var_count = 0;

    return new_block;
}

/* This enters a block of the given block type. A block's vars are considered to
   be any vars created after the block has been entered. Information such as the
   current class entered and self's location is inferred from existing info. */
void lily_emit_enter_block(lily_emit_state *emit, lily_block_type block_type)
{
    lily_block *new_block = block_enter_common(emit);
    new_block->block_type = block_type;
    new_block->flags |= BLOCK_ALWAYS_EXITS;

    if (block_type == block_enum)
        /* Enum entries are not considered function-like, because they do
            not have a class .new. */
        new_block->class_entry = emit->symtab->active_module->class_chain;

    emit->block = new_block;
}

void lily_emit_enter_call_block(lily_emit_state *emit,
        lily_block_type block_type, lily_var *call_var)
{
    lily_block *new_block = block_enter_common(emit);
    new_block->block_type = block_type;

    if (block_type == block_class) {
        new_block->class_entry = emit->symtab->active_module->class_chain;
        emit->class_block_depth = emit->function_depth + 1;
    }

    /* Nested functions are marked this way so that any call to them is
       guaranteed to give them the upvalues they need. */
    if (emit->block->block_type == block_define)
        call_var->flags |= VAR_NEEDS_CLOSURE;

    /* This causes vars within this imported file to be seen as global
       vars, instead of locals. Without this, the interpreter gets confused
       and thinks the imported file's globals are really upvalues. */
    if (block_type != block_file) {
        if (block_type == block_lambda) {
            /* A lambda cannot be guaranteed to have the 'self' of a class
               as the first parameter. If it wants 'self', it can close over
               it when it needs to. */
            new_block->self = NULL;
        }
        emit->function_depth++;
    }

    new_block->prev_function_block = emit->function_block;

    emit->function_block = new_block;

    new_block->next_reg_spot = 0;
    new_block->storage_start = emit->storages->scope_end;
    new_block->function_var = call_var;
    new_block->code_start = lily_u16_pos(emit->code);

    emit->block = new_block;
}

void lily_emit_leave_call_block(lily_emit_state *emit, uint16_t line_num)
{
    lily_block *block = emit->block;

    if (block->block_type == block_class)
        lily_u16_write_3(emit->code, o_return_val, block->self->reg_spot,
                line_num);
    else if (block->last_exit != lily_u16_pos(emit->code)) {
        lily_type *type = block->function_var->type->subtypes[0];

        if (type == lily_unit_type ||
            type == lily_self_class->self_type)
            lily_u16_write_2(emit->code, o_return_unit, line_num);
        else
            lily_raise_syn(emit->raiser,
                    "Missing return statement at end of function.");
    }

    write_final_code_for_block(emit, block);

    int i;
    for (i = block->storage_start;i < emit->storages->scope_end;i++)
        emit->storages->data[i]->type = NULL;

    emit->storages->scope_end = block->storage_start;

    if (emit->block->block_type == block_class)
        emit->class_block_depth = 0;

    emit->function_block = block->prev_function_block;

    /* File 'blocks' do not bump up the depth because that's used to determine
       if something is a global or not. */
    if (block->block_type != block_file)
        emit->function_depth--;

    emit->block = emit->block->prev;
}

void lily_emit_leave_block(lily_emit_state *emit)
{
    lily_block *block;
    int block_type;

    if (emit->block->prev == NULL)
        lily_raise_syn(emit->raiser, "'}' outside of a block.");

    block = emit->block;
    block_type = block->block_type;

    /* These blocks need to jump back up when the bottom is hit. */
    if (block_type == block_while || block_type == block_for_in) {
        int x = block->code_start - lily_u16_pos(emit->code);
        lily_u16_write_2(emit->code, o_jump, (uint16_t)x);
    }
    else if (block_type == block_match)
        emit->match_case_pos = emit->block->match_case_start;
    else if (block_type == block_try ||
             block_type == block_try_except ||
             block_type == block_try_except_all) {
        /* The vm expects that the last except block will have a 'next' of 0 to
           indicate the end of the 'except' chain. Remove the patch that the
           last except block installed so it doesn't get patched. */
        lily_u16_set_at(emit->code, lily_u16_pop(emit->patches), 0);
    }

    if ((block_type == block_if_else ||
         block_type == block_match ||
         block_type == block_try_except_all) &&
        block->flags & BLOCK_ALWAYS_EXITS &&
        block->last_exit == lily_u16_pos(emit->code)) {
        emit->block->prev->last_exit = lily_u16_pos(emit->code);
    }

    write_patches_since(emit, block->patch_start);
    emit->block = emit->block->prev;
}

static lily_block *find_deepest_loop(lily_emit_state *emit)
{
    lily_block *block, *ret;
    ret = NULL;

    for (block = emit->block; block; block = block->prev) {
        if (IS_LOOP_BLOCK(block->block_type)) {
            ret = block;
            break;
        }
        else if (block->block_type >= block_define) {
            ret = NULL;
            break;
        }
    }

    return ret;
}

/* This is called when a patch needs to be put into a particular block. The
   given block may or may not be the current block. */
static void inject_patch_into_block(lily_emit_state *emit, lily_block *block,
        uint16_t patch)
{
    /* This is the most recent block, so add the patch to the top. */
    if (emit->block == block)
        lily_u16_write_1(emit->patches, patch);
    else {
        lily_u16_inject(emit->patches, block->next->patch_start, patch);

        /* The blocks after the one that got the new patch need to have their
           starts adjusted or they'll think it belongs to them. */
        for (block = block->next; block; block = block->next)
            block->patch_start++;
    }
}


/* This function is called from parser to change the current block into a block
   of the given type. The emitter does some checks to make sure that the change
   is valid, as well as jump patching. */
void lily_emit_change_block_to(lily_emit_state *emit, int new_type)
{
    lily_block *block = emit->block;
    lily_block_type current_type = block->block_type;

    if (block->last_exit != lily_u16_pos(emit->code))
        block->flags &= ~BLOCK_ALWAYS_EXITS;

    if (new_type == block_if_elif || new_type == block_if_else) {
        char *block_name;
        if (new_type == block_if_elif)
            block_name = "elif";
        else
            block_name = "else";

        if (current_type == block_if_else)
            lily_raise_syn(emit->raiser, "'%s' after 'else'.", block_name);
    }
    else if (new_type == block_try_except || new_type == block_try_except_all) {
        if (current_type == block_try_except_all)
            lily_raise_syn(emit->raiser, "'except' clause is unreachable.");

        /* If nothing in the 'try' block raises an error, the vm needs to be
           told to unregister the 'try' block since will become unreachable
           when the jump below occurs. */
        if (current_type == block_try)
            lily_u16_write_1(emit->code, o_pop_try);
    }

    int save_jump;

    if (block->last_exit != lily_u16_pos(emit->code)) {
        /* Write a jump at the end of this branch. It will be patched to target
           the if/try's exit. */
        lily_u16_write_2(emit->code, o_jump, 1);
        save_jump = lily_u16_pos(emit->code) - 1;
    }
    else
        /* This branch has code that is confirmed to return, continue, raise, or
           do some other action that prevents it from reaching here. Don't
           bother writing a jump that will never be seen. */
        save_jump = -1;

    /* The last jump of the previous branch wants to know where the check for
       the next branch starts. It's right now. */
    uint16_t patch = lily_u16_pop(emit->patches);

    if (patch != 0) {
        int patch_adjust = lily_u16_get(emit->code, patch);
        lily_u16_set_at(emit->code, patch,
                lily_u16_pos(emit->code) + patch_adjust - patch);
    }
    /* else it's a fake branch from a condition that was optimized out. */

    if (save_jump != -1)
        lily_u16_write_1(emit->patches, save_jump);

    emit->block->block_type = new_type;
}

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

static void close_over_sym(lily_emit_state *emit, uint16_t depth, lily_sym *sym)
{
    lily_u16_write_2(emit->closure_spots, sym->reg_spot, depth);
    sym->flags |= SYM_CLOSED_OVER;
    emit->function_block->flags |= BLOCK_MAKE_CLOSURE;
}

/* This writes o_create_function which will create a copy of 'func_sym' but
   with closure information. 'target' is a storage where the closed-over copy
   will end up. The result cannot be cached in any way (each invocation should
   get a fresh set of cells). */
static void emit_create_function(lily_emit_state *emit, lily_sym *func_sym,
        lily_storage *target)
{
    lily_u16_write_4(emit->code, o_create_function, func_sym->reg_spot,
            target->reg_spot, *emit->lex_linenum);
    emit->function_block->flags |= BLOCK_MAKE_CLOSURE;
}

/* This function closes over a var, but has a requirement. The requirement is
   that it cannot be an unsolved type from a higher-up scope. This requirement
   exists because Lily does not currently understand how to have two different
   kinds of a generic that are not equivalent / scoped generics. */
static uint16_t checked_close_over_var(lily_emit_state *emit, lily_var *var)
{
    if (emit->function_block->block_type == block_define &&
        emit->function_block->prev->block_type == block_define &&
        var->type->flags & TYPE_IS_UNRESOLVED)
        lily_raise_syn(emit->raiser,
                "Cannot close over a var of an incomplete type in this scope.");

    if (var->function_depth == emit->class_block_depth)
        lily_raise_syn(emit->raiser,
                "Not allowed to close over variables from a class constructor.",
                "");

    close_over_sym(emit, var->function_depth, (lily_sym *)var);
    return (lily_u16_pos(emit->closure_spots) - 1) / 2;
}

/* See if the given sym has been closed over.
   Success: The spot
   Failure: -1 */
static int find_closed_sym_spot_raw(lily_emit_state *emit, uint16_t depth,
        uint16_t spot)
{
    int result = -1, i;

    for (i = 0;
         i < lily_u16_pos(emit->closure_spots);
         i += 2) {
        if (lily_u16_get(emit->closure_spots, i) == spot &&
            lily_u16_get(emit->closure_spots, i + 1) == depth) {
            result = i / 2;
            break;
        }
    }

    return result;
}

#define find_closed_sym_spot(emit, depth, sym) \
find_closed_sym_spot_raw(emit, depth, (sym)->reg_spot)

/* Called if the current block is a lambda. If `self` can be closed over, then
   do that. */
static void maybe_close_over_class_self(lily_emit_state *emit, lily_ast *ast)
{
    uint16_t depth = emit->function_depth;
    lily_block *block = emit->function_block->prev_function_block;
    while (block->block_type != block_class) {
        block = block->prev_function_block;
        depth--;
    }

    block = block->next;

    if (block->block_type != block_define) {
        lily_raise_adjusted(emit->raiser, ast->line_num,
                "Not allowed to close over self in a class constructor.",
                "");
    }

    lily_sym *self = (lily_sym *)block->self;

    if (find_closed_sym_spot(emit, depth, self) == -1)
        close_over_sym(emit, depth, self);

    if (emit->block->self == NULL)
        emit->block->self = get_storage(emit, self->type);

    emit->function_block->flags |= BLOCK_MAKE_CLOSURE;
}

/* This sets up the table used to map from a register spot to where that spot is
   in the closure. */
static void setup_for_transform(lily_emit_state *emit,
        lily_function_val *f, int is_backing)
{
    int next_reg_spot = emit->function_block->next_reg_spot;

    if (emit->transform_size < emit->function_block->next_reg_spot) {
        emit->transform_table = lily_realloc(emit->transform_table,
                next_reg_spot * sizeof(*emit->transform_table));
        emit->transform_size = emit->function_block->next_reg_spot;
    }

    memset(emit->transform_table, (uint16_t)-1,
            next_reg_spot * sizeof(*emit->transform_table));

    lily_var *func_var = emit->function_block->function_var;
    uint16_t line_num = func_var->line_num;
    uint16_t local_count = func_var->type->subtype_count - 1;
    int i, count = 0;

    for (i = 0;
         i < lily_u16_pos(emit->closure_spots);
         i += 2) {
        if (lily_u16_get(emit->closure_spots, i + 1) == emit->function_depth) {
            uint16_t spot = lily_u16_get(emit->closure_spots, i);

            if (spot == (uint16_t)-1)
                continue;
            else if (spot < local_count) {
                /* Make sure this parameter always exists in the closure. */
                lily_u16_write_4(emit->closure_aux_code, o_set_upvalue, i / 2,
                    spot, line_num);
            }

            emit->transform_table[spot] = i / 2;
            count++;
            /* This prevents other closures at this level from thinking this
               local belongs to them. */
            lily_u16_set_at(emit->closure_spots, i + 1, (uint16_t)-1);
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
            if (emit->transform_table[i] != (uint16_t) -1) {
                locals[pos] = i;
                pos++;
            }
        }

        f->locals = locals;
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
static void maybe_add_jump(lily_buffer_u16 *buffer, int i, int dest)
{
    int end = lily_u16_pos(buffer);

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
   o_get_upvalue transforms. */
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

    if (op == o_function_call &&
        transform_table[buffer[pos]] != (uint16_t)-1)
        count++;

    pos += ci.special_1 + ci.counter_2;

    if (ci.inputs_3) {
        int i;
        for (i = 0;i < ci.inputs_3;i++) {
            if (transform_table[buffer[pos + i]] != (uint16_t)-1)
                count++;
        }
    }

    return count;
}

/* This function is called to transform the currently available segment of code
   (emit->block->code_start up to emit->code_pos) into code that will work for
   closures. */
static void perform_closure_transform(lily_emit_state *emit,
        lily_block *function_block, lily_function_val *f)
{
    if (emit->closure_aux_code == NULL)
        emit->closure_aux_code = lily_new_buffer_u16(8);
    else
        lily_u16_set_pos(emit->closure_aux_code, 0);

    int iter_start = emit->block->code_start;

    lily_block *prev_block = function_block->prev_function_block;
    int is_backing = (prev_block->block_type == block_class ||
                      prev_block->block_type == block_file);

    if (is_backing) {
        /* Put the backing closure into a register so it's not lost in a gc
           sweep. */
        lily_storage *s = get_unique_storage(emit,
                function_block->function_var->type);

        lily_u16_write_4(emit->closure_aux_code, o_create_closure,
                lily_u16_pos(emit->closure_spots) / 2, s->reg_spot,
                f->line_num);

        if (function_block->self) {
            /* Use raw search because 'self' is always at 0. */
            uint16_t self_spot = find_closed_sym_spot_raw(emit,
                    emit->function_depth, 0);
            /* Load register 0 (self) into the closure. */
            if (self_spot != (uint16_t)-1) {
                lily_u16_write_4(emit->closure_aux_code, o_set_upvalue,
                        self_spot, 0, f->line_num);
            }
        }
    }
    else if (emit->block->block_type == block_lambda) {
        lily_storage *lambda_self = emit->block->self;
        if (lambda_self) {
            while (prev_block->block_type != block_class)
                prev_block = prev_block->prev_function_block;

            prev_block = prev_block->next;

            uint16_t self_spot = find_closed_sym_spot(emit,
                    emit->class_block_depth, (lily_sym *)prev_block->self);
            if (self_spot != (uint16_t)-1)
                lily_u16_write_4(emit->closure_aux_code, o_get_upvalue,
                        self_spot, lambda_self->reg_spot, f->line_num);
        }
    }

    setup_for_transform(emit, f, is_backing);

    if (is_backing)
        lily_u16_set_pos(emit->closure_spots, 0);

    lily_code_iter ci;
    lily_ci_init(&ci, emit->code->data, iter_start, lily_u16_pos(emit->code));
    uint16_t *transform_table = emit->transform_table;
    int jump_adjust = 0;

/* If the input at the position given by 'x' is within the closure, then write
   an instruction to fetch it from the closure first. This makes sure that if
   this local is in the closure, it needs to read from the closure first so that
   any assignment to it as an upvalue will be reflected. */
#define MAYBE_TRANSFORM_INPUT(x, z) \
{ \
    uint16_t id = transform_table[buffer[x]]; \
    if (id != (uint16_t)-1) { \
        lily_u16_write_4(emit->closure_aux_code, z, id, \
                buffer[x], f->line_num); \
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
        if (ci.jumps_7) {
            int stop = ci.offset + ci.round_total - ci.line_8;

            for (i = stop - ci.jumps_7;i < stop;i++) {
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
                case o_function_call:
                    MAYBE_TRANSFORM_INPUT(pos, o_get_upvalue)
                default:
                    pos += ci.special_1;
                    break;
            }
        }

        pos += ci.counter_2;

        if (ci.inputs_3) {
            for (i = 0;i < ci.inputs_3;i++) {
                MAYBE_TRANSFORM_INPUT(pos + i, o_get_upvalue)
            }

            pos += ci.inputs_3;
        }

        if (ci.special_4) {
            switch (op) {
                case o_create_function:
                    pos += ci.special_4;
                    break;
                case o_jump_if_not_class:
                    pos += 1;
                    break;
                default:
                    lily_raise_syn(emit->raiser,
                            "Special value #4 for opcode %d not handled.", op);
            }
        }

        if (ci.outputs_5) {
            output_start = pos;
            pos += ci.outputs_5;
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

        int stop = ci.offset + ci.round_total - ci.jumps_7 - ci.line_8;
        for (;i < stop;i++)
            lily_u16_write_1(emit->closure_aux_code, buffer[i]);

        if (ci.jumps_7) {
            for (i = 0;i < ci.jumps_7;i++) {
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
                            ci.round_total - ci.jumps_7 - ci.line_8 + i);

                    lily_u16_write_1(emit->closure_aux_code, destination);
                }
                else
                    lily_u16_write_1(emit->closure_aux_code, 0);
            }
        }

        if (ci.line_8)
            lily_u16_write_1(emit->closure_aux_code, 5);

        if (ci.outputs_5) {
            int stop = output_start + ci.outputs_5;

            for (i = output_start;i < stop;i++) {
                MAYBE_TRANSFORM_INPUT(i, o_set_upvalue)
            }
        }
    }

    /* It's time to patch the unfixed jumps, if there are any. The area from
       patch_stop to the ending position contains jumps to be fixed. */
    int j;
    for (j = patch_stop;j < lily_u16_pos(emit->patches);j += 2) {
        /* This is where, in the new code, that the jump is located. */
        int aux_pos = lily_u16_get(emit->patches, j);
        /* This has been set to an absolute destination in old code. */
        int original = lily_u16_get(emit->closure_aux_code, aux_pos);
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

/* This makes the function value that will be needed by the current code
   block. If the current function is a closure, then the appropriate transform
   is done to it. */
static void write_final_code_for_block(lily_emit_state *emit,
        lily_block *function_block)
{
    lily_var *var = function_block->function_var;
    lily_value *v = lily_vs_nth(emit->symtab->literals, var->reg_spot);
    lily_function_val *f = v->value.function;

    int code_start, code_size;
    uint16_t *source, *code;

    if ((function_block->flags & BLOCK_MAKE_CLOSURE) == 0) {
        code_start = emit->block->code_start;
        code_size = lily_u16_pos(emit->code) - emit->block->code_start;

        source = emit->code->data;
    }
    else {
        lily_block *prev = function_block->prev_function_block;

        perform_closure_transform(emit, function_block, f);

        if (prev->block_type != block_file)
            prev->flags |= BLOCK_MAKE_CLOSURE;

        code_start = 0;
        code_size = lily_u16_pos(emit->closure_aux_code);
        source = emit->closure_aux_code->data;
    }

    code = lily_malloc((code_size + 1) * sizeof(*code));
    memcpy(code, source + code_start, sizeof(*code) * code_size);

    f->code_len = code_size;
    f->code = code;
    f->reg_count = function_block->next_reg_spot;

    lily_u16_set_pos(emit->code, function_block->code_start);
}

/***
 *      __  __       _       _
 *     |  \/  | __ _| |_ ___| |__
 *     | |\/| |/ _` | __/ __| '_ \
 *     | |  | | (_| | || (__| | | |
 *     |_|  |_|\__,_|\__\___|_| |_|
 *
 */

static void eval_enforce_value(lily_emit_state *, lily_ast *, lily_type *,
        const char *);

/** Match blocks are given a symbol that each case checks for being of a certain
    class. For enums, this pattern matching can include extraction of the
    contents of variants if the variants have values. For classes, match will
    assign class contents to a given variable.

    The code here takes advantage of the following:
    * At vm time, enum values have the id of a variant. Each case starts with
      o_jump_if_not_class, and each of those instructions links to the next one.
      The id test is against the identity of the variant or class that the user
      is interested in.
    * Variants and user classes have the same layout, so o_get_property is
      written to extract variant values that the user is interested in. **/

static void grow_match_cases(lily_emit_state *emit)
{
    emit->match_case_size *= 2;
    emit->match_cases = lily_realloc(emit->match_cases,
        sizeof(*emit->match_cases) * emit->match_case_size);
}

/* This is written when match wants a value from the source. When matching on an
   enum, 'index' is a spot to pull from. For matches on a user-class or a
   Dynamic, this only needs to store the value over into another register. The
   assign there is necessary so that the source var will have the right type. */
void lily_emit_decompose(lily_emit_state *emit, lily_sym *match_sym, int index,
        uint16_t pos)
{
    /* Note: 'pos' is the target of a local var, so no global/upvalue checks are
       necessary here. */

    if (match_sym->type->cls->flags & CLS_IS_ENUM)
        lily_u16_write_5(emit->code, o_get_property, index, match_sym->reg_spot,
                pos, *emit->lex_linenum);
    else
        lily_u16_write_4(emit->code, o_assign, match_sym->reg_spot, pos,
                *emit->lex_linenum);
}

int lily_emit_is_duplicate_case(lily_emit_state *emit, lily_class *cls)
{
    if (emit->match_case_pos >= emit->match_case_size)
        grow_match_cases(emit);

    lily_block *block = emit->block;
    int cls_id = cls->id, ret = 0;
    int i;

    for (i = block->match_case_start;i < emit->match_case_pos;i++) {
        if (emit->match_cases[i] == cls_id) {
            ret = 1;
            break;
        }
    }

    return ret;
}

void lily_emit_write_match_case(lily_emit_state *emit, lily_sym *match_sym,
        lily_class *cls)
{
    emit->match_cases[emit->match_case_pos] = cls->id;
    emit->match_case_pos++;

    lily_u16_write_4(emit->code, o_jump_if_not_class, match_sym->reg_spot,
            cls->id, 3);

    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);
}

void lily_emit_change_match_branch(lily_emit_state *emit)
{
    lily_block *block = emit->block;

    if (block->match_case_start != emit->match_case_pos) {
        if (emit->block->last_exit != lily_u16_pos(emit->code))
            emit->block->flags &= ~BLOCK_ALWAYS_EXITS;

        /* This is the jump of the last o_jump_if_not_class. */
        int pos = lily_u16_pop(emit->patches);
        int adjust = lily_u16_get(emit->code, pos);

        /* Write a pending exit jump for the previous case. */
        lily_u16_write_2(emit->code, o_jump, 1);
        lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);

        /* The last o_jump_if_not_class will go here. */
        lily_u16_set_at(emit->code, pos,
                lily_u16_pos(emit->code) + adjust - pos);
    }
}

/* This evaluates the expression to be sent to 'match' The resulting value is
   checked for returning a value that is a valid enum. The match block's state
   is then prepared.
   The pool is cleared out for the next expression after this. */
void lily_emit_eval_match_expr(lily_emit_state *emit, lily_expr_state *es)
{
    lily_ast *ast = es->root;
    lily_block *block = emit->block;
    eval_enforce_value(emit, ast, NULL, "Match expression has no value.");

    block->match_case_start = emit->match_case_pos;

    lily_class *match_class = ast->result->type->cls;

    if (match_class->id == LILY_DYNAMIC_ID) {
        lily_storage *s = get_storage(emit, emit->ts->question_class_type);

        /* Dynamic is laid out like a class with the content in slot 0. Extract
           it out to match against. */
        lily_u16_write_5(emit->code, o_get_property, 0, ast->result->reg_spot,
                s->reg_spot, ast->line_num);

        ast->result = (lily_sym *)s;
    }
    else if ((match_class->flags & CLS_IS_ENUM) == 0 &&
             (match_class->flags & CLS_IS_BUILTIN))
        lily_raise_syn(emit->raiser, "Match expression is not an enum value.");

    /* Each case pops the last jump and writes in their own. */
    lily_u16_write_1(emit->patches, 0);
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

/* This creates a new function value that wraps over a foreign (C) function. */
lily_function_val *new_foreign_function_val(lily_foreign_func func,
        const char *class_name, const char *name)
{
    lily_function_val *f = lily_malloc(sizeof(*f));

    /* This won't get a ref bump from being moved/assigned since all functions
       are marked as literals. Start at 1 ref, not 0. */
    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = func;
    f->code = NULL;
    /* Closures can have zero upvalues, so use -1 to mean no upvalues at all. */
    f->num_upvalues = (uint16_t) -1;
    f->upvalues = NULL;
    f->gc_entry = NULL;
    f->reg_count = -1;
    f->locals = NULL;
    return f;
}

/* This creates a new function value representing a native function. */
lily_function_val *new_native_function_val(char *class_name, char *name)
{
    lily_function_val *f = lily_malloc(sizeof(*f));

    /* This won't get a ref bump from being moved/assigned since all functions
       are marked as literals. Start at 1 ref, not 0. */
    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = NULL;
    f->code = NULL;
    /* Closures can have zero upvalues, so use -1 to mean no upvalues at all. */
    f->num_upvalues = (uint16_t)-1;
    f->upvalues = NULL;
    f->gc_entry = NULL;
    f->reg_count = -1;
    f->locals = NULL;
    return f;
}

/* Return a string representation of the given op. */
static const char *opname(lily_expr_op op)
{
    static const char *opnames[] =
    {"+", "++", "-", "==", "<", "<=", ">", ">=", "!=", "%", "*", "/", "<<",
     ">>", "&", "|", "^", "!", "-", "&&", "||", "|>", "=", "+=", "-=", "%=",
     "*=", "/=", "<<=", ">>=", "&=", "|=", "^="};

    return opnames[op];
}

/* Check if 'type' is something that can be considered truthy/falsey.
   Keep this synced with the vm's o_jump_if calculation.
   Failure: SyntaxError is raised. */
static void ensure_valid_condition_type(lily_emit_state *emit, lily_type *type)
{
    int cls_id = type->cls->id;

    if (cls_id != LILY_INTEGER_ID &&
        cls_id != LILY_DOUBLE_ID &&
        cls_id != LILY_STRING_ID &&
        cls_id != LILY_LIST_ID &&
        cls_id != LILY_BOOLEAN_ID)
        lily_raise_syn(emit->raiser, "^T is not a valid condition type.", type);
}

/* This checks to see if 'index_ast' has a type (and possibly, a value) that is
   a valid index for the type held by 'var_ast'.
   Failure: SyntaxError is raised. */
static void check_valid_subscript(lily_emit_state *emit, lily_ast *var_ast,
        lily_ast *index_ast)
{
    int var_cls_id = var_ast->result->type->cls->id;
    if (var_cls_id == LILY_LIST_ID || var_cls_id == LILY_BYTESTRING_ID) {
        if (index_ast->result->type->cls->id != LILY_INTEGER_ID)
            lily_raise_adjusted(emit->raiser, var_ast->line_num,
                    "%s index is not an integer.",
                    var_ast->result->type->cls->name);
    }
    else if (var_cls_id == LILY_HASH_ID) {
        lily_type *want_key = var_ast->result->type->subtypes[0];
        lily_type *have_key = index_ast->result->type;

        if (want_key != have_key) {
            lily_raise_adjusted(emit->raiser, var_ast->line_num,
                    "hash index should be type '^T', not type '^T'.",
                    want_key, have_key);
        }
    }
    else if (var_cls_id == LILY_TUPLE_ID) {
        if (index_ast->tree_type != tree_integer) {
            lily_raise_adjusted(emit->raiser, var_ast->line_num,
                    "tuple subscripts must be integer literals.", "");
        }

        int index_value = index_ast->backing_value;
        lily_type *var_type = var_ast->result->type;
        if (index_value < 0 || index_value >= var_type->subtype_count) {
            lily_raise_adjusted(emit->raiser, var_ast->line_num,
                    "Index %d is out of range for ^T.", index_value, var_type);
        }
    }
    else {
        lily_raise_adjusted(emit->raiser, var_ast->line_num,
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
    if (type->cls->id == LILY_LIST_ID)
        result = type->subtypes[0];
    else if (type->cls->id == LILY_HASH_ID)
        result = type->subtypes[1];
    else if (type->cls->id == LILY_TUPLE_ID) {
        /* check_valid_subscript ensures that this is safe. */
        int literal_index = index_ast->backing_value;
        result = type->subtypes[literal_index];
    }
    else if (type->cls->id == LILY_BYTESTRING_ID)
        result = emit->symtab->byte_class->self_type;
    else
        /* Won't happen, but keeps the compiler from complaining. */
        result = NULL;

    return result;
}


/* Since o_build_list, o_build_tuple, and o_build_hash are fairly similar (and
   the first two are fairly common), this function writes all of them.

   This function takes a tree, and will walk it up to 'num_values' times. This
   function does not create a storage. Instead, the caller is expected to
   provide a storage of the appropriate type. Said storage should have a spot
   that is 'reg_spot'. */
static void write_build_op(lily_emit_state *emit, int opcode,
        lily_ast *first_arg, int line_num, int num_values, lily_storage *s)
{
    int i;
    lily_ast *arg;
    lily_u16_write_prep(emit->code, 5 + num_values);

    lily_u16_write_1(emit->code, opcode);

    if (opcode == o_build_hash)
        /* The vm the key's id to decide what hashing functions to use. */
        lily_u16_write_1(emit->code, s->type->subtypes[0]->cls->id);

    lily_u16_write_1(emit->code, num_values);

    for (i = 0, arg = first_arg; arg != NULL; arg = arg->next_arg, i++)
        lily_u16_write_1(emit->code, arg->result->reg_spot);

    lily_u16_write_2(emit->code, s->reg_spot, line_num);
}


/* This checks that 'sym' (either a var or a property) can be used within the
   current scope. If it cannot be, then SyntaxError is raised. */
static void ensure_valid_scope(lily_emit_state *emit, lily_sym *sym)
{
    if (sym->flags & (SYM_SCOPE_PRIVATE | SYM_SCOPE_PROTECTED)) {
        lily_class *block_class = emit->block->class_entry;
        lily_class *parent;
        int is_private = (sym->flags & SYM_SCOPE_PRIVATE);
        char *name;

        if (sym->item_kind == ITEM_TYPE_PROPERTY) {
            lily_prop_entry *prop = (lily_prop_entry *)sym;
            parent = prop->cls;
            name = prop->name;
        }
        else {
            lily_var *v = (lily_var *)sym;
            parent = v->parent;
            name = v->name;
        }

        if ((is_private && block_class != parent) ||
            (is_private == 0 &&
             (block_class == NULL || lily_class_greater_eq(parent, block_class) == 0))) {
            char *scope_name = is_private ? "private" : "protected";
            lily_raise_syn(emit->raiser,
                       "%s.%s is marked %s, and not available here.",
                       parent->name, name, scope_name);
        }
    }
}

/* Subscript assign is in a curious position. The correct order (determined by
   checking other languages) is that the right evaluates before the left. This
   causes a chicken-and-egg problem though: The right needs to infer based off
   of the left, but the left needs to run to generate that.

   The emitter has no support for rewinding. As a bandage, this function does
   what would be done by doing the eval, except no storages are created. If the
   left's type is invalid, then NULL will be returned. Otherwise, this returns
   a type against which the right side should infer. */
static lily_type *determine_left_type(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *result_type = NULL;

    if (ast->tree_type == tree_global_var ||
        ast->tree_type == tree_local_var)
        result_type = ast->sym->type;
    else if (ast->tree_type == tree_subscript) {
        lily_ast *var_tree = ast->arg_start;
        lily_ast *index_tree = var_tree->next_arg;

        result_type = determine_left_type(emit, var_tree);

        if (result_type != NULL) {
            if (result_type->cls->id == LILY_HASH_ID)
                result_type = result_type->subtypes[1];
            else if (result_type->cls->id == LILY_TUPLE_ID) {
                if (index_tree->tree_type != tree_integer)
                    result_type = NULL;
                else {
                    int literal_index = index_tree->backing_value;
                    if (literal_index < 0 ||
                        literal_index > result_type->subtype_count)
                        result_type = NULL;
                    else
                        result_type = result_type->subtypes[literal_index];
                }
            }
            else if (result_type->cls->id == LILY_LIST_ID)
                result_type = result_type->subtypes[0];
            else if (result_type->cls->id == LILY_BYTESTRING_ID)
                result_type = emit->symtab->byte_class->self_type;
        }
    }
    else if (ast->tree_type == tree_oo_access) {
        result_type = determine_left_type(emit, ast->arg_start);
        if (result_type != NULL) {
            char *oo_name = lily_sp_get(emit->expr_strings, ast->pile_pos);
            lily_class *lookup_class = result_type->cls;
            lily_type *lookup_type = result_type;

            lily_prop_entry *prop = lily_find_property(lookup_class, oo_name);

            if (prop) {
                result_type = prop->type;
                if (result_type->flags & TYPE_IS_UNRESOLVED) {
                    result_type = lily_ts_resolve_by_second(emit->ts,
                            lookup_type, result_type);
                }
            }
            else
                result_type = NULL;
        }
    }
    else if (ast->tree_type == tree_property)
        result_type = ast->property->type;
    /* All other are either invalid for the left side of an assignment. */
    else
        result_type = NULL;

    return result_type;
}

/* This checks what the parent of an assignment is. It has been decided that
   assignment chains are okay, but assignments inside of non-assignment chains
   are not okay. */
static void assign_post_check(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->parent &&
         (ast->parent->tree_type != tree_binary ||
          ast->parent->op < expr_assign)) {
        lily_raise_syn(emit->raiser,
                "Cannot nest an assignment within an expression.");
    }
    else if (ast->parent == NULL) {
        /* This prevents conditions from using the result of an assignment. */
        ast->result = NULL;
    }
}

/* Does an assignment -really- have to be written, or can the last tree's result
   be rewritten to target the left side? Given a tree (the whole assign), this
   figures that out.
   Note: Only valid for basic assignments. Upvalue/subscript/etc. do not belong
   here. */
static int assign_optimize_check(lily_ast *ast)
{
    int can_optimize = 1;

    do {
        /* assigning to a global is done differently than with a local, so it
           can't be optimized. */
        if (ast->left->tree_type == tree_global_var) {
            can_optimize = 0;
            break;
        }

        lily_ast *right_tree = ast->right;

        /* Parenths don't write anything, so dive to the bottom of them. */
        while (right_tree->tree_type == tree_parenth)
            right_tree = right_tree->arg_start;

        /* Gotta do basic assignments. */
        if (right_tree->tree_type == tree_local_var) {
            can_optimize = 0;
            break;
        }

        /* && and || work by having one set of cases write down to one storage,
           and the other set write down to another storage. Because of that, it
           can't be folded, or the first set of cases will target a storage
           while the second target the var. */
        if (right_tree->tree_type == tree_binary &&
            (right_tree->op == expr_logical_and ||
             right_tree->op == expr_logical_or)) {
            can_optimize = 0;
            break;
        }

        /* Also check if the right side is an assignment or compound op. */
        if (right_tree->tree_type == tree_binary &&
            right_tree->op >= expr_assign) {
            can_optimize = 0;
            break;
        }
    } while (0);

    return can_optimize;
}

/* This is a simple function that checks if the result in 'right' is suitable
   to be the type required by 'want_type'. Returns 1 if yes, 0 if no. */
static int type_matchup(lily_emit_state *emit, lily_type *want_type,
        lily_ast *right)
{
    int ret;
    lily_type *right_type = right->result->type;

    if (want_type == right_type ||
        lily_ts_type_greater_eq(emit->ts, want_type, right_type))
        ret = 1;
    else
        ret = 0;

    return ret;
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
    lily_raise_adjusted(emit->raiser, ast->line_num,
            "%s do not have a consistent type.\n"
            "Expected Type: ^T\n"
            "Received Type: ^T",
            context, expect, ast->result->type);
}

static void bad_assign_error(lily_emit_state *emit, int line_num,
        lily_type *left_type, lily_type *right_type)
{
    lily_raise_adjusted(emit->raiser, line_num,
            "Cannot assign type '^T' to type '^T'.",
            right_type, left_type);
}

static void add_call_name_to_msgbuf(lily_msgbuf *msgbuf, lily_ast *ast)
{
    if (ast->tree_type != tree_variant)
        ast = ast->arg_start;

    switch (ast->tree_type) {
        case tree_method:
        case tree_static_func: {
            lily_var *v = (lily_var *)ast->item;
            lily_mb_add_fmt(msgbuf, "%s.%s", v->parent->name, v->name);
            break;
        }
        case tree_inherited_new: {
            lily_var *v = (lily_var *)ast->item;
            lily_mb_add_fmt(msgbuf, "%s", v->parent->name);
            break;
        }
        case tree_oo_access: {
            if (ast->item->item_kind == ITEM_TYPE_VAR) {
                lily_var *v = (lily_var *)ast->item;
                lily_mb_add_fmt(msgbuf, "%s.%s", v->parent->name, v->name);
            }
            else {
                lily_prop_entry *p = (lily_prop_entry *)ast->item;
                lily_mb_add_fmt(msgbuf, "%s.%s", p->cls->name, p->name);
            }
            break;
        }
        case tree_defined_func:
        case tree_variant:
        case tree_local_var: {
            lily_var *v = (lily_var *)ast->item;
            lily_mb_add_fmt(msgbuf, "%s", v->name);
            break;
        }
        case tree_call:
            lily_mb_add(msgbuf, "(anonymous)");
            break;
        default:
            lily_mb_add(msgbuf, "(?)");
            break;
    }
}

/* This is called when call processing has an argument of the wrong type. This
   generates a syntax error with the call name if that can be located.
   This assumes 'index' is 0-based (argument 0 being the first argument to the
   function given. If 'index' exceeds the number of types available, it's
   assumed that the source is varargs and the last type is used for display. */
static void error_bad_arg(lily_emit_state *emit, lily_ast *ast,
        lily_type *call_type, int index, lily_type *got)
{
    /* Ensure that generics that did not get a valid value are replaced with the
       ? type (instead of NULL, which will cause a problem). */
    lily_ts_resolve_as_question(emit->ts);
    lily_type *question = emit->ts->question_class_type;

    lily_type *expected;

    if (index >= call_type->subtype_count)
        expected = call_type->subtypes[call_type->subtype_count];
    else
        expected = call_type->subtypes[index + 1];

    if (expected->flags & TYPE_IS_UNRESOLVED)
        expected = lily_ts_resolve_with(emit->ts, expected, question);

    lily_msgbuf *msgbuf = emit->raiser->aux_msgbuf;
    lily_mb_flush(msgbuf);

    lily_mb_add_fmt(msgbuf, "Argument #%d to ", index + 1);
    add_call_name_to_msgbuf(msgbuf, ast);
    lily_mb_add_fmt(msgbuf,
            " is invalid:\n"
            "Expected Type: ^T\n"
            "Received Type: ^T", expected, got);

    lily_raise_adjusted(emit->raiser, ast->line_num, lily_mb_get(msgbuf), "");
}

/* This is called when the tree given doesn't have enough arguments. The count
   given should include implicit values from tree_oo_access/tree_method. The
   values for min and max should come from `get_func_min_max`.
   This function includes a special case: If 'count' is -1, then "none" will be
   printed. This is used in cases like `Some` (since `Some` requires arguments
   and variants don't allow 0 argument calls). */
static void error_argument_count(lily_emit_state *emit, lily_ast *ast,
        int count, int min, int max)
{
    lily_ast *first_arg = ast->arg_start;

    /* Don't count the implicit self that these functions receive. */
    if (ast->tree_type != tree_variant &&
        (first_arg->tree_type == tree_method ||
         (first_arg->tree_type == tree_oo_access &&
          first_arg->sym->item_kind == ITEM_TYPE_VAR))) {
        min--;
        count--;
        if (max != -1)
            max--;
    }

    /* This prints out the number sent, as well as the range of valid counts.
       There are four possibilities, with the last one being exclusively for
       a variant that requires arguments.
       (# for n)
       (# for n+)
       (# for n..m)
       (none for n) */
    const char *div_str = "";
    char arg_str[8], min_str[8] = "", max_str[8] = "";

    if (count == -1)
        strncpy(arg_str, "none", sizeof(arg_str));
    else
        snprintf(arg_str, sizeof(arg_str), "%d", count);

    snprintf(min_str, sizeof(min_str), "%d", min);

    if (min == max)
        div_str = "";
    else if (max == -1)
        div_str = "+";
    else {
        div_str = "..";
        snprintf(max_str, sizeof(max_str), "%d", max);
    }

    lily_msgbuf *msgbuf = emit->raiser->aux_msgbuf;
    lily_mb_flush(msgbuf);

    lily_mb_add(msgbuf, "Wrong number of arguments to ");
    add_call_name_to_msgbuf(msgbuf, ast);
    lily_mb_add_fmt(msgbuf, " (%s for %s%s%s).", arg_str, min_str, div_str,
            max_str);

    lily_raise_adjusted(emit->raiser, ast->line_num, lily_mb_get(msgbuf),
            "");
}

/***
 *      __  __                _
 *     |  \/  | ___ _ __ ___ | |__   ___ _ __ ___
 *     | |\/| |/ _ \ '_ ` _ \| '_ \ / _ \ '__/ __|
 *     | |  | |  __/ | | | | | |_) |  __/ |  \__ \
 *     |_|  |_|\___|_| |_| |_|_.__/ \___|_|  |___/
 *
 */

static void emit_op_for_compound(lily_emit_state *, lily_ast *);

/** Member access for classes is quite annoying. The parser doesn't have type
    information, so it packages them as having a tree and a name. It seems like
    it would be as simple as 'evaluate the tree, lookup the name', but it isn't.

    The name could be a method that needs to be dynaloaded, a class member, or a
    method that has already been loaded. The thing that is loaded must dump a
    value to a storage.

    Problems begin with 'x.y = z' (oo/prop assign). This kind of access needs to
    know what type that it is targeting. Running the 'x.y' access is a waste,
    because it's not possible to assign to a storage (that fails if y is an
    integer property). Additionally, 'y' might be a property that isn't solved
    yet, so that's another issue. 'x.y += z' is also tricky. **/

/* This runs an 'x.y' kind of access. The inner tree is evaluated, but no result
   is set. Instead, the ast's item is set to either the var or the storage that
   is returned.

   SyntaxError is raised if the name specified doesn't exist, or is not
   available in the current scope. */
static void eval_oo_access_for_item(lily_emit_state *emit, lily_ast *ast)
{
    if (emit->function_block->block_type == block_lambda &&
        ast->arg_start->tree_type == tree_self)
        maybe_close_over_class_self(emit, ast);

    if (ast->arg_start->tree_type != tree_local_var)
        eval_tree(emit, ast->arg_start, NULL);


    lily_class *lookup_class = ast->arg_start->result->type->cls;
    /* This allows variant values to use enum methods. */
    if (lookup_class->item_kind == ITEM_TYPE_VARIANT)
        lookup_class = lookup_class->parent;

    char *oo_name = lily_sp_get(emit->expr_strings, ast->pile_pos);
    lily_item *item = lily_find_or_dl_member(emit->parser, lookup_class,
            oo_name);

    if (item == NULL) {
        lily_raise_adjusted(emit->raiser, ast->arg_start->line_num,
                "Class %s has no method or property named %s.",
                lookup_class->name, oo_name);
    }
    else if (item->item_kind == ITEM_TYPE_PROPERTY &&
             ast->arg_start->tree_type == tree_self) {
        lily_raise_adjusted(emit->raiser, ast->arg_start->line_num,
                "Use @<name> to get/set properties, not self.<name>.", "");
    }
    else
        ast->item = item;

    ensure_valid_scope(emit, (lily_sym *)item);
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
    lily_u16_write_5(emit->code, o_get_property, prop->id,
            ast->arg_start->result->reg_spot, result->reg_spot, ast->line_num);

    ast->result = (lily_sym *)result;
}

/* This is the actual handler for simple 'x.y' accesses. It doesn't do assigns
   though. */
static void eval_oo_access(lily_emit_state *emit, lily_ast *ast)
{
    eval_oo_access_for_item(emit, ast);
    /* An 'x.y' access will either yield a property or a class method. */
    if (ast->item->item_kind == ITEM_TYPE_PROPERTY)
        oo_property_read(emit, ast);
    else {
        lily_storage *result = get_storage(emit, ast->sym->type);
        lily_u16_write_4(emit->code, o_get_readonly, ast->sym->reg_spot,
                result->reg_spot, ast->line_num);
        ast->result = (lily_sym *)result;
    }
}

/* This handles 'x.y = z' kinds of assignments. */
static void eval_oo_assign(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *left_type;

    eval_oo_access_for_item(emit, ast->left);
    ensure_valid_scope(emit, ast->left->sym);
    if (ast->left->item->item_kind != ITEM_TYPE_PROPERTY)
        lily_raise_adjusted(emit->raiser, ast->line_num,
                "Left side of %s is not assignable.", opname(ast->op));

    left_type = get_solved_property_type(emit, ast->left);

    eval_tree(emit, ast->right, left_type);

    lily_sym *rhs = ast->right->result;
    lily_type *right_type = rhs->type;

    if (left_type != right_type &&
        type_matchup(emit, left_type, ast->right) == 0) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, left_type, right_type);
    }

    if (ast->op > expr_assign) {
        oo_property_read(emit, ast->left);
        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    lily_u16_write_5(emit->code, o_set_property, ast->left->property->id,
            ast->left->arg_start->result->reg_spot, rhs->reg_spot,
            ast->line_num);

    ast->result = rhs;
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

/* This handles simple binary ops (no assign, &&/||, |>, or compounds. This
   assumes that both sides have already been evaluated. */
static void emit_binary_op(lily_emit_state *emit, lily_ast *ast)
{
    lily_sym *lhs_sym = ast->left->result;
    lily_sym *rhs_sym = ast->right->result;
    lily_class *lhs_class = lhs_sym->type->cls;
    lily_class *rhs_class = rhs_sym->type->cls;
    int opcode = -1;
    lily_storage *s;

    if (lhs_sym->type == rhs_sym->type) {
        int lhs_id = lhs_class->id;
        int op = ast->op;

        if (lhs_id == LILY_INTEGER_ID) {
            if (op == expr_plus)
                opcode = o_integer_add;
            else if (op == expr_minus)
                opcode = o_integer_minus;
            else if (op == expr_multiply)
                opcode = o_integer_mul;
            else if (op == expr_divide)
                opcode = o_integer_div;
            else if (op == expr_modulo)
                opcode = o_modulo;
            else if (op == expr_left_shift)
                opcode = o_left_shift;
            else if (op == expr_right_shift)
                opcode = o_right_shift;
            else if (op == expr_bitwise_and)
                opcode = o_bitwise_and;
            else if (op == expr_bitwise_or)
                opcode = o_bitwise_or;
            else if (op == expr_bitwise_xor)
                opcode = o_bitwise_xor;
        }
        else if (lhs_id == LILY_DOUBLE_ID) {
            if (op == expr_plus)
                opcode = o_double_add;
            else if (op == expr_minus)
                opcode = o_double_minus;
            else if (op == expr_multiply)
                opcode = o_double_mul;
            else if (op == expr_divide)
                opcode = o_double_div;
        }

        if (lhs_id == LILY_INTEGER_ID ||
            lhs_id == LILY_DOUBLE_ID ||
            lhs_id == LILY_STRING_ID) {
            if (op == expr_lt_eq) {
                lily_sym *temp = rhs_sym;
                rhs_sym = lhs_sym;
                lhs_sym = temp;
                opcode = o_greater_eq;
            }
            else if (op == expr_lt) {
                lily_sym *temp = rhs_sym;
                rhs_sym = lhs_sym;
                lhs_sym = temp;
                opcode = o_greater;
            }
            else if (op == expr_gr_eq)
                opcode = o_greater_eq;
            else if (op == expr_gr)
                opcode = o_greater;
        }

        if (op == expr_eq_eq)
            opcode = o_is_equal;
        else if (op == expr_not_eq)
            opcode = o_not_eq;
    }

    if (opcode == -1)
        lily_raise_adjusted(emit->raiser, ast->line_num,
                   "Invalid operation: ^T %s ^T.", ast->left->result->type,
                   opname(ast->op), ast->right->result->type);

    lily_class *storage_class;
    switch (ast->op) {
        case expr_plus:
        case expr_minus:
        case expr_multiply:
        case expr_divide:
            storage_class = lhs_sym->type->cls;
            break;
        case expr_eq_eq:
        case expr_lt:
        case expr_lt_eq:
        case expr_gr:
        case expr_gr_eq:
        case expr_not_eq:
            storage_class = emit->symtab->boolean_class;
            break;
        default:
            storage_class = emit->symtab->integer_class;
    }

    /* Can we reuse a storage instead of making a new one? It's a simple check,
       but every register the vm doesn't need to make helps. */
    if (lhs_sym->item_kind == ITEM_TYPE_STORAGE &&
        lhs_class == storage_class)
        s = (lily_storage *)lhs_sym;
    else if (rhs_sym->item_kind == ITEM_TYPE_STORAGE &&
             rhs_class == storage_class)
        s = (lily_storage *)rhs_sym;
    else {
        s = get_storage(emit, storage_class->self_type);
        s->flags |= SYM_NOT_ASSIGNABLE;
    }

    lily_u16_write_5(emit->code, opcode, lhs_sym->reg_spot, rhs_sym->reg_spot,
            s->reg_spot, ast->line_num);

    ast->result = (lily_sym *)s;
}

/* This takes a tree and will change the op from an 'X Y= Z' to 'X Y Z'. The
   tree is run as a binary op, then fixed back. This is how compound operations
   are broken down.

   emit_binary_op assumes that the left and right have already been evaluated,
   so this won't double-eval. */
static void emit_op_for_compound(lily_emit_state *emit, lily_ast *ast)
{
    int save_op = ast->op;
    int spoof_op;

    if (ast->op == expr_div_assign)
        spoof_op = expr_divide;
    else if (ast->op == expr_mul_assign)
        spoof_op = expr_multiply;
    else if (ast->op == expr_modulo_assign)
        spoof_op = expr_modulo;
    else if (ast->op == expr_plus_assign)
        spoof_op = expr_plus;
    else if (ast->op == expr_minus_assign)
        spoof_op = expr_minus;
    else if (ast->op == expr_left_shift_assign)
        spoof_op = expr_left_shift;
    else if (ast->op == expr_right_shift_assign)
        spoof_op = expr_right_shift;
    else if (ast->op == expr_bitwise_and_assign)
        spoof_op = expr_bitwise_and;
    else if (ast->op == expr_bitwise_or_assign)
        spoof_op = expr_bitwise_or;
    else {
        lily_raise_syn(emit->raiser, "Invalid compound op: %s.",
                opname(ast->op));
        spoof_op = -1;
    }

    ast->op = spoof_op;
    emit_binary_op(emit, ast);
    ast->op = save_op;
}

/* This handles basic assignments (locals and globals). */
static void eval_assign(lily_emit_state *emit, lily_ast *ast)
{
    int can_optimize = 1, left_cls_id, opcode;
    lily_sym *left_sym, *right_sym;
    opcode = -1;

    if (ast->left->tree_type != tree_global_var &&
        ast->left->tree_type != tree_local_var) {
        /* If the left is complex and valid, it would have been sent off to a
           different assign. Ergo, it must be invalid. */
        lily_raise_adjusted(emit->raiser, ast->line_num,
                "Left side of %s is not assignable.", opname(ast->op));
    }

    eval_tree(emit, ast->right, ast->left->result->type);

    /* For 'var <name> = ...', fix the type. */
    if (ast->left->result->type == NULL)
        ast->left->result->type = ast->right->result->type;

    ast->left->result->flags &= ~SYM_NOT_INITIALIZED;

    left_sym = ast->left->result;
    right_sym = ast->right->result;
    left_cls_id = left_sym->type->cls->id;

    if (left_sym->type != right_sym->type &&
        type_matchup(emit, ast->left->result->type, ast->right) == 0)
        bad_assign_error(emit, ast->line_num, left_sym->type, right_sym->type);

    if (opcode == -1) {
        if (left_cls_id == LILY_INTEGER_ID ||
            left_cls_id == LILY_DOUBLE_ID)
            opcode = o_fast_assign;
        else
            opcode = o_assign;
    }

    if (ast->op > expr_assign) {
        if (ast->left->tree_type == tree_global_var)
            eval_tree(emit, ast->left, NULL);

        emit_op_for_compound(emit, ast);
        right_sym = ast->result;
    }

    if (ast->left->tree_type == tree_global_var)
        opcode = o_set_global;

    /* If assign can be optimized out, then rewrite the last result to point to
       the left side. */
    if (can_optimize && assign_optimize_check(ast)) {
        /* Trees always finish by writing a result and then the line number.
           Optimize out by patching the result to target the left side. */
        int pos = lily_u16_pos(emit->code) - 2;
        lily_u16_set_at(emit->code, pos, left_sym->reg_spot);
    }
    else {
        lily_u16_write_4(emit->code, opcode, right_sym->reg_spot,
                left_sym->reg_spot, ast->line_num);
    }
    ast->result = right_sym;
}

/* This handles ```@<name>```. Properties, unlike member access, are validated
   at parse-time. */
static void eval_property(lily_emit_state *emit, lily_ast *ast)
{
    ensure_valid_scope(emit, ast->sym);
    if (emit->function_block->block_type == block_lambda)
        maybe_close_over_class_self(emit, ast);

    if (ast->property->flags & SYM_NOT_INITIALIZED)
        lily_raise_adjusted(emit->raiser, ast->line_num,
                "Invalid use of uninitialized property '@%s'.",
                ast->property->name);

    lily_storage *result = get_storage(emit, ast->property->type);

    lily_u16_write_5(emit->code, o_get_property, ast->property->id,
            emit->block->self->reg_spot, result->reg_spot, ast->line_num);

    ast->result = (lily_sym *)result;
}

/* This handles assignments to a property. It's similar in spirit to oo assign,
   but not as complicated. */
static void eval_property_assign(lily_emit_state *emit, lily_ast *ast)
{
    if (emit->function_block->block_type == block_lambda)
        maybe_close_over_class_self(emit, ast);

    ensure_valid_scope(emit, ast->left->sym);
    lily_type *left_type = ast->left->property->type;
    lily_sym *rhs;

    eval_tree(emit, ast->right, left_type);

    lily_type *right_type = ast->right->result->type;
    lily_prop_entry *left_prop = ast->left->property;

    /* For 'var @<name> = ...', fix the type of the property. */
    if (left_prop->flags & SYM_NOT_INITIALIZED) {
        left_prop->flags &= ~SYM_NOT_INITIALIZED;

        if (left_type == NULL) {
            left_prop->type = right_type;
            left_type = right_type;
        }
    }

    if (left_type != ast->right->result->type &&
        type_matchup(emit, left_type, ast->right) == 0) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, left_type, right_type);
    }

    rhs = ast->right->result;

    if (ast->op > expr_assign) {
        eval_tree(emit, ast->left, NULL);
        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    lily_u16_write_5(emit->code, o_set_property, ast->left->property->id,
            emit->block->self->reg_spot, rhs->reg_spot, ast->line_num);

    ast->result = rhs;
}

/* This evaluates a lambda. The parser sent the lambda over as a blob of text
   since it didn't know what the types were. Now that the types are known, pass
   it back to the parser to, umm, parse. */
static void eval_lambda(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    int save_expr_num = emit->expr_num;
    char *lambda_body = lily_sp_get(emit->expr_strings, ast->pile_pos);

    if (expect && expect->cls->id != LILY_FUNCTION_ID)
        expect = NULL;

    lily_sym *lambda_result = (lily_sym *)lily_parser_lambda_eval(emit->parser,
            ast->line_num, lambda_body, expect);

    /* Lambdas may run 1+ expressions. Restoring the expression count to what it
       was prevents grabbing expressions that are currently in use. */
    emit->expr_num = save_expr_num;

    lily_storage *s = get_storage(emit, lambda_result->type);

    if ((emit->function_block->flags & BLOCK_MAKE_CLOSURE) == 0)
        lily_u16_write_4(emit->code, o_get_readonly, lambda_result->reg_spot,
                s->reg_spot, ast->line_num);
    else
        emit_create_function(emit, lambda_result, s);

    ast->result = (lily_sym *)s;
}

/* This handles assignments to things that are marked as upvalues. */
static void eval_upvalue_assign(lily_emit_state *emit, lily_ast *ast)
{
    eval_tree(emit, ast->right, NULL);

    lily_var *left_var = (lily_var *)ast->left->sym;
    int spot = find_closed_sym_spot(emit, left_var->function_depth,
            (lily_sym *)left_var);
    if (spot == -1)
        spot = checked_close_over_var(emit, left_var);

    lily_sym *rhs = ast->right->result;

    if (ast->op > expr_assign) {
        lily_storage *s = get_storage(emit, ast->left->sym->type);
        lily_u16_write_4(emit->code, o_get_upvalue, spot, s->reg_spot,
                ast->line_num);
        ast->left->result = (lily_sym *)s;
        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    lily_u16_write_4(emit->code, o_set_upvalue, spot, rhs->reg_spot,
            ast->line_num);

    ast->result = ast->right->result;
}

/* This takes care of binary || and &&. */
static void eval_logical_op(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *result;
    int andor_start;
    int jump_on = (ast->op == expr_logical_or);

    /* The top-most and/or will start writing patches, and then later write down
       all of those patches. This is okay to do, because the current block
       cannot exit during this and/or branching. */
    if (ast->parent == NULL ||
        (ast->parent->tree_type != tree_binary || ast->parent->op != ast->op))
        andor_start = lily_u16_pos(emit->patches);
    else
        andor_start = -1;

    if (ast->left->tree_type != tree_local_var)
        eval_tree(emit, ast->left, NULL);

    /* If the left is the same as this tree, then it's already checked itself
       and doesn't need a retest. However, and/or are opposites, so they have
       to check each other (so the op has to be exactly the same). */
    if ((ast->left->tree_type == tree_binary && ast->left->op == ast->op) == 0)
        emit_jump_if(emit, ast->left, jump_on);

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, NULL);

    emit_jump_if(emit, ast->right, jump_on);

    if (andor_start != -1) {
        int save_pos;
        lily_symtab *symtab = emit->symtab;

        result = get_storage(emit, symtab->boolean_class->self_type);

        int truthy = (ast->op == expr_logical_and);

        lily_u16_write_4(emit->code, o_get_boolean, truthy, result->reg_spot,
                ast->line_num);

        /* The jump will be patched as soon as patches are written, so don't
           bother writing a count. */
        lily_u16_write_2(emit->code, o_jump, 0);
        save_pos = lily_u16_pos(emit->code) - 1;

        write_patches_since(emit, andor_start);

        lily_u16_write_4(emit->code, o_get_boolean, !truthy, result->reg_spot,
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
        eval_tree(emit, var_ast, NULL);

    if (index_ast->tree_type != tree_local_var)
        eval_tree(emit, index_ast, NULL);

    check_valid_subscript(emit, var_ast, index_ast);

    lily_type *type_for_result;
    type_for_result = get_subscript_result(emit, var_ast->result->type,
            index_ast);

    lily_storage *result = get_storage(emit, type_for_result);

    lily_u16_write_5(emit->code, o_get_item, var_ast->result->reg_spot,
            index_ast->result->reg_spot, result->reg_spot, ast->line_num);

    if (var_ast->result->flags & SYM_NOT_ASSIGNABLE)
        result->flags |= SYM_NOT_ASSIGNABLE;

    ast->result = (lily_sym *)result;
}

/* This handles subscript assign. Subscript assign has two issues that make it
   a bit tough:

   * The right side must go first (other languages do this), but should have
     inference from the left. This is implemented a bit hackish.
   * Similar to oo access, the subscript target needs to be verified, but
     without doing an eval of the inner tree. Such is needed because evaluating
     the inner tree would make a junk storage. */
static void eval_sub_assign(lily_emit_state *emit, lily_ast *ast)
{
    lily_ast *var_ast = ast->left->arg_start;
    lily_ast *index_ast = var_ast->next_arg;
    lily_sym *rhs;
    lily_type *elem_type;

    /* This gets the type that the left will be without actually evaluating it.
       It is important to not run the left before the right, because assigns
       should be right to left. */
    lily_type *left_type = determine_left_type(emit, ast->left);

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, left_type);

    rhs = ast->right->result;

    if (var_ast->tree_type != tree_local_var) {
        eval_tree(emit, var_ast, NULL);
        if (var_ast->result->flags & SYM_NOT_ASSIGNABLE) {
            lily_raise_adjusted(emit->raiser, ast->line_num,
                    "Left side of %s is not assignable.", opname(ast->op));
        }
    }

    if (index_ast->tree_type != tree_local_var)
        eval_tree(emit, index_ast, NULL);

    check_valid_subscript(emit, var_ast, index_ast);

    elem_type = get_subscript_result(emit, var_ast->result->type, index_ast);

    if (type_matchup(emit, elem_type, ast->right) == 0) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, elem_type, rhs->type);
    }

    rhs = ast->right->result;

    if (ast->op > expr_assign) {
        /* For a compound assignment to work, the left side must be subscripted
           to get the value held. */

        lily_storage *subs_storage = get_storage(emit, elem_type);

        lily_u16_write_5(emit->code, o_get_item, var_ast->result->reg_spot,
                index_ast->result->reg_spot, subs_storage->reg_spot,
                ast->line_num);

        ast->left->result = (lily_sym *)subs_storage;

        /* Run the compound op now that ->left is set properly. */
        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    lily_u16_write_5(emit->code, o_set_item, var_ast->result->reg_spot,
            index_ast->result->reg_spot, rhs->reg_spot, ast->line_num);

    ast->result = rhs;
}

static void eval_typecast(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *boxed_type = ast->arg_start->next_arg->type;
    lily_ast *right_tree = ast->arg_start;
    lily_type *cast_type = boxed_type->subtypes[0];

    eval_tree(emit, right_tree, cast_type);

    lily_type *var_type = right_tree->result->type;

    lily_raise_adjusted(emit->raiser, ast->line_num,
            "Cannot cast type '^T' to type '^T'.", var_type, cast_type);
}

static void eval_unary_op(lily_emit_state *emit, lily_ast *ast)
{
    /* Inference shouldn't be necessary for something so simple. */
    if (ast->left->tree_type != tree_local_var)
        eval_tree(emit, ast->left, NULL);

    int opcode = -1;
    lily_class *lhs_class = ast->left->result->type->cls;
    lily_storage *storage;

    lily_expr_op op = ast->op;

    if (lhs_class == emit->symtab->boolean_class) {
        if (op == expr_unary_not)
            opcode = o_unary_not;
    }
    else if (lhs_class == emit->symtab->integer_class) {
        if (op == expr_unary_minus)
            opcode = o_unary_minus;
        else if (op == expr_unary_not)
            opcode = o_unary_not;
    }

    if (opcode == -1)
        lily_raise_adjusted(emit->raiser, ast->line_num,
                "Invalid operation: %s%s.",
                opname(ast->op), lhs_class->name);

    storage = get_storage(emit, lhs_class->self_type);
    storage->flags |= SYM_NOT_ASSIGNABLE;

    lily_u16_write_4(emit->code, opcode, ast->left->result->reg_spot,
            storage->reg_spot, ast->line_num);

    ast->result = (lily_sym *)storage;
}

/* This handles building tuples ```<[1, "2", 3.3]>```. Tuples are structures
   that allow varying types, but with a fixed size. */
static void eval_build_tuple(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    if (ast->args_collected == 0)
        lily_raise_syn(emit->raiser, "Cannot create an empty tuple.");

    if (expect != NULL &&
        (expect->cls->id != LILY_TUPLE_ID ||
         ast->args_collected > expect->subtype_count))
        expect = NULL;

    int i;
    lily_ast *arg;

    for (i = 0, arg = ast->arg_start;
         arg != NULL;
         i++, arg = arg->next_arg) {
        lily_type *elem_type = NULL;

        /* It's important to do this for each pass because it allows the inner
           trees to infer types that this tree's parent may want. */
        if (expect)
            elem_type = expect->subtypes[i];

        eval_tree(emit, arg, elem_type);

        if (elem_type && elem_type != arg->result->type)
            /* Attempt to fix the type to what's wanted. If it fails, the parent
               tree will note a type mismatch. Can't do anything else here
               though. */
            type_matchup(emit, elem_type, arg);
    }

    for (i = 0, arg = ast->arg_start;
         i < ast->args_collected;
         i++, arg = arg->next_arg) {
        lily_tm_add(emit->tm, arg->result->type);
    }

    lily_type *new_type = lily_tm_make(emit->tm, 0, emit->symtab->tuple_class,
            i);
    lily_storage *s = get_storage(emit, new_type);

    write_build_op(emit, o_build_tuple, ast->arg_start, ast->line_num,
            ast->args_collected, s);
    ast->result = (lily_sym *)s;
}

static void emit_literal(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *s = get_storage(emit, ast->type);

    lily_u16_write_4(emit->code, o_get_readonly, ast->literal_reg_spot,
            s->reg_spot, ast->line_num);

    ast->result = (lily_sym *)s;
}

/* This handles loading globals, upvalues, and static functions. */
static void emit_nonlocal_var(lily_emit_state *emit, lily_ast *ast)
{
    int opcode;
    uint16_t spot;
    lily_sym *sym = ast->sym;
    lily_storage *ret = get_storage(emit, sym->type);

    switch (ast->tree_type) {
        case tree_global_var:
            opcode = o_get_global;
            spot = sym->reg_spot;
            break;
        case tree_upvalue: {
            opcode = o_get_upvalue;
            lily_var *v = (lily_var *)sym;

            spot = find_closed_sym_spot(emit, v->function_depth, (lily_sym *)v);
            if (spot == (uint16_t)-1)
                spot = checked_close_over_var(emit, v);

            emit->function_block->flags |= BLOCK_MAKE_CLOSURE;
            break;
        }
        case tree_static_func:
            ensure_valid_scope(emit, ast->sym);
        default:
            ret->flags |= SYM_NOT_ASSIGNABLE;
            spot = sym->reg_spot;
            opcode = o_get_readonly;
            break;
    }

    if ((sym->flags & VAR_NEEDS_CLOSURE) == 0 ||
        ast->tree_type == tree_upvalue) {
        lily_u16_write_4(emit->code, opcode, spot, ret->reg_spot,
                ast->line_num);
    }
    else
        emit_create_function(emit, sym, ret);

    ast->result = (lily_sym *)ret;
}

static void emit_integer(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *s = get_storage(emit, emit->symtab->integer_class->self_type);

    lily_u16_write_4(emit->code, o_get_integer, ast->backing_value, s->reg_spot,
            ast->line_num);

    ast->result = (lily_sym *)s;
}

static void emit_boolean(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *s = get_storage(emit, emit->symtab->boolean_class->self_type);

    lily_u16_write_4(emit->code, o_get_boolean, ast->backing_value, s->reg_spot,
            ast->line_num);

    ast->result = (lily_sym *)s;
}

static void emit_byte(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *s = get_storage(emit, emit->symtab->byte_class->self_type);

    lily_u16_write_4(emit->code, o_get_byte, ast->backing_value, s->reg_spot,
            ast->line_num);

    ast->result = (lily_sym *)s;
}

static void eval_self(lily_emit_state *emit, lily_ast *ast)
{
    ast->result = (lily_sym *)emit->block->self;
}

/***
 *      _     _     _             _   _           _
 *     | |   (_)___| |_     _    | | | | __ _ ___| |__
 *     | |   | / __| __|  _| |_  | |_| |/ _` / __| '_ \
 *     | |___| \__ \ |_  |_   _| |  _  | (_| \__ \ | | |
 *     |_____|_|___/\__|   |_|   |_| |_|\__,_|___/_| |_|
 *
 */

/** The eval for these two is broken away from the others because it is fairly
    difficult. The bulk of the problem comes with trying to find a 'bottom type'
    of all the values that were entered. This process is termed 'unification',
    and it isn't easy here.

    Unification is currently done after lists and hashes have evaluated all of
    their members. This is unfortunate, because it means that certain cases will
    not work as well as they could. Another problem with the current unification
    is that it only works for enums and variants. Anything else gets sent
    straight to type Dynamic. It's unfortunate. **/

/* Make sure that 'key_type' is a valid key. It may be NULL or ? depending on
   inference. If 'key_type' is not suitable to be a hash key, then raise a
   syntax error. */
static void ensure_valid_key_type(lily_emit_state *emit, lily_ast *ast,
        lily_type *key_type)
{
    if (key_type == NULL || key_type->cls->id == LILY_QUESTION_ID)
        key_type = emit->symtab->dynamic_class->self_type;

    if (key_type == NULL || (key_type->cls->flags & CLS_VALID_HASH_KEY) == 0)
        lily_raise_adjusted(emit->raiser, ast->line_num,
                "Type '^T' is not a valid hash key.", key_type);
}

/* Build an empty something. It's an empty hash only if the caller wanted a
   hash. In any other case, it becomes an empty list. Use Dynamic as a default
   where it's needed. The purpose of this function is to make it so list and
   hash build do not need to worry about missing information. */
static void make_empty_list_or_hash(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_type *dynamic_type = emit->symtab->dynamic_class->self_type;
    lily_class *cls;
    int num, op;

    if (expect && expect->cls->id == LILY_HASH_ID) {
        lily_type *key_type = expect->subtypes[0];
        lily_type *value_type = expect->subtypes[1];
        ensure_valid_key_type(emit, ast, key_type);

        if (value_type == NULL || value_type->cls->id == LILY_QUESTION_ID)
            value_type = dynamic_type;

        lily_tm_add(emit->tm, key_type);
        lily_tm_add(emit->tm, value_type);

        cls = emit->symtab->hash_class;
        op = o_build_hash;
        num = 2;
    }
    else {
        lily_type *elem_type;
        if (expect && expect->cls->id == LILY_LIST_ID &&
            expect->subtypes[0]->cls->id != LILY_QUESTION_ID) {
            elem_type = expect->subtypes[0];
        }
        else
            elem_type = dynamic_type;

        lily_tm_add(emit->tm, elem_type);

        cls = emit->symtab->list_class;
        op = o_build_list;
        num = 1;
    }

    lily_storage *s = get_storage(emit, lily_tm_make(emit->tm, 0, cls, num));
    write_build_op(emit, op, ast->arg_start, ast->line_num, 0, s);
    ast->result = (lily_sym *)s;
}

static void eval_build_hash(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_ast *tree_iter;

    lily_type *key_type, *question_type, *value_type;
    question_type = emit->symtab->question_class->self_type;

    if (expect && expect->cls->id == LILY_HASH_ID) {
        key_type = expect->subtypes[0];
        value_type = expect->subtypes[1];
        if (key_type == NULL)
            key_type = question_type;

        if (value_type == NULL)
            value_type = question_type;
    }
    else {
        key_type = question_type;
        value_type = question_type;
    }

    for (tree_iter = ast->arg_start;
         tree_iter != NULL;
         tree_iter = tree_iter->next_arg->next_arg) {

        lily_ast *key_tree, *value_tree;
        key_tree = tree_iter;
        value_tree = tree_iter->next_arg;

        lily_type *unify_type;

        eval_tree(emit, key_tree, key_type);

        unify_type = lily_ts_unify(emit->ts, key_type, key_tree->result->type);
        if (unify_type == NULL)
            inconsistent_type_error(emit, key_tree, key_type, "Hash keys");
        else {
            ensure_valid_key_type(emit, ast, unify_type);
            key_type = unify_type;
        }

        eval_tree(emit, value_tree, value_type);
        unify_type = lily_ts_unify(emit->ts, value_type,
                value_tree->result->type);
        if (unify_type == NULL)
            inconsistent_type_error(emit, value_tree, value_type,
                    "Hash values");
        else
            value_type = unify_type;
    }

    if (value_type->flags & TYPE_IS_INCOMPLETE)
        value_type = lily_tm_make_dynamicd_copy(emit->tm, value_type);

    lily_class *hash_cls = emit->symtab->hash_class;
    lily_tm_add(emit->tm, key_type);
    lily_tm_add(emit->tm, value_type);
    lily_type *new_type = lily_tm_make(emit->tm, 0, hash_cls, 2);

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

    lily_type *elem_type = NULL;
    lily_ast *arg;

    if (expect && expect->cls->id == LILY_LIST_ID)
        elem_type = expect->subtypes[0];

    if (elem_type == NULL || elem_type->cls->id == LILY_SCOOP_1_ID)
        elem_type = emit->ts->question_class_type;

    for (arg = ast->arg_start;arg != NULL;arg = arg->next_arg) {
        eval_tree(emit, arg, elem_type);

        lily_type *new_elem_type = lily_ts_unify(emit->ts, elem_type,
                arg->result->type);
        if (new_elem_type == NULL)
            inconsistent_type_error(emit, arg, elem_type, "List elements");

        elem_type = new_elem_type;
    }

    if (elem_type->flags & TYPE_IS_INCOMPLETE)
        elem_type = lily_tm_make_dynamicd_copy(emit->tm, elem_type);

    lily_tm_add(emit->tm, elem_type);
    lily_type *new_type = lily_tm_make(emit->tm, 0, emit->symtab->list_class,
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
    * {|| 10} ()
    * [1, 2, 3].y()
    * x |> y

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
      implementations.

    * For non-variant targets, the source tree has the 'sym' field set to the
      target that will be called.

    * For all targets, there will always be a source to write against. Variants
      put their enum parent's self type where a function would put its return
      type. This allows variants that take arguments to be treated like any
      other kind of tree, even though there's no sym since they have no backing
      function.
    **/

static void get_func_min_max(lily_type *call_type, unsigned int *min,
        unsigned int *max)
{
    *min = call_type->subtype_count - 1;
    *max = *min;

    if (call_type->flags & TYPE_HAS_OPTARGS) {
        int i;
        for (i = 1;i < call_type->subtype_count;i++) {
            if (call_type->subtypes[i]->cls->id == LILY_OPTARG_ID)
                break;
        }
        *min = i - 1;
    }

    if (call_type->flags & TYPE_IS_VARARGS) {
        *max = (unsigned int)-1;

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
    else if (ast->arg_start->tree_type == tree_inherited_new)
        ast->result = (lily_sym *)emit->block->self;
    else {
        lily_ast *arg = ast->arg_start;

        if (arg->tree_type != tree_variant) {
            if (return_type->flags & TYPE_IS_UNRESOLVED) {
                /* Force incomplete solutions to become Dynamic. */
                lily_ts_default_incomplete_solves(emit->ts);

                return_type = lily_ts_resolve(emit->ts, return_type);
            }
        }
        else {
            /* Variants are allowed to be incomplete. Doing so allows code such
               as `[None, None, Some(1)]` to work. */
            if (return_type->flags & TYPE_IS_UNRESOLVED)
                return_type = lily_ts_resolve_with(emit->ts, return_type,
                        emit->ts->question_class_type);

            /* Variant trees don't have a result so skip over them. */
            arg = arg->next_arg;
        }

        lily_storage *s = NULL;

        for (;arg;arg = arg->next_arg) {
            if (arg->result->item_kind == ITEM_TYPE_STORAGE &&
                arg->result->type == return_type) {
                s = (lily_storage *)arg->result;
                break;
            }
        }

        if (s == NULL) {
            s = get_storage(emit, return_type);
            s->flags |= SYM_NOT_ASSIGNABLE;
        }

        ast->result = (lily_sym *)s;
    }
}

/* The call's subtrees have been evaluated now. Write the instruction to do the
   call and make a storage to put the result in (if needed). */
static void write_call(lily_emit_state *emit, lily_ast *ast,
        int argument_count, lily_storage *vararg_s)
{
    lily_ast *arg = ast->arg_start;
    lily_tree_type first_tt = arg->tree_type;
    uint16_t opcode = 0;
    uint16_t target = 0;

    if (first_tt != tree_variant) {
        lily_sym *call_sym = ast->sym;

        if (call_sym->flags & VAR_IS_READONLY) {
            if (call_sym->flags & VAR_IS_FOREIGN_FUNC)
                opcode = o_foreign_call;
            else
                opcode = o_native_call;
        }
        else
            opcode = o_function_call;

        target = call_sym->reg_spot;
    }
    else {
        opcode = o_build_enum;
        target = arg->variant->cls_id;
    }

    lily_u16_write_3(emit->code, opcode, target,
            argument_count + (vararg_s != NULL));

    int i = 0;

    if (first_tt == tree_oo_access) {
        i++;
        lily_u16_write_1(emit->code, arg->result->reg_spot);
    }
    else if (first_tt == tree_method) {
        i++;
        lily_u16_write_1(emit->code, emit->block->self->reg_spot);
    }

    for (arg = arg->next_arg;
         i < argument_count;
         i++, arg = arg->next_arg)
        lily_u16_write_1(emit->code, arg->result->reg_spot);

    if (vararg_s)
        lily_u16_write_1(emit->code, vararg_s->reg_spot);

    lily_u16_write_2(emit->code, ast->result->reg_spot, ast->line_num);
}

/* Evaluate the call argument 'arg'. The type 'want_type' is -not- solved by the
   caller, and thus must be solved here.
   Returns 1 if successful, 0 otherwise. */
static int eval_call_arg(lily_emit_state *emit, lily_ast *arg,
        lily_type *want_type)
{
    if (want_type->cls->id == LILY_OPTARG_ID)
        want_type = want_type->subtypes[0];

    lily_type *eval_type = want_type;
    if (eval_type->flags & TYPE_IS_UNRESOLVED) {
        eval_type = lily_ts_resolve_with(emit->ts, want_type,
                emit->ts->question_class_type);
    }

    eval_tree(emit, arg, eval_type);
    lily_type *result_type = arg->result->type;

    /* Here's an interesting case where the result type doesn't match but where
       the result is some global generic function. Since the result function is
       global, the generics inside of it are unquantified. For this special
       case, see if the generic function provided can narrow down to be what is
       wanted. */
    if ((result_type->flags & TYPE_IS_UNRESOLVED) &&
        (arg->tree_type == tree_static_func ||
         arg->tree_type == tree_defined_func))
    {
        lily_type *question_type = emit->ts->question_class_type;
        /* Figure out what the caller REALLY wants, and make sure to do it
           BEFORE changing scope. */
        lily_type *solved_want = lily_ts_resolve_with(emit->ts, want_type,
                question_type);

        lily_ts_save_point p;
        lily_ts_scope_save(emit->ts, &p);
        lily_ts_check(emit->ts, solved_want, result_type);
        lily_type *solved_result = lily_ts_resolve_with(emit->ts, result_type,
                question_type);
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
static void run_call(lily_emit_state *emit, lily_ast *ast,
        lily_type *call_type, lily_type *expect)
{
    lily_ast *arg = ast->arg_start;
    /* NOTE: This works for both calls and func pipe because the arg_start and
       right fields of lily_ast are in a union together. */
    lily_tree_type first_tt = arg->tree_type;
    /* The first tree is counted as an argument. However, most trees don't
       actually add the first argument. In fact, only two will:
       tree_method will inject self as a first argument.
       tree_oo_access will inject the left of the dot (a.x() adds 'a'). */
    int count_first = (first_tt == tree_oo_access || first_tt == tree_method);
    int num_args = ast->args_collected - 1 + count_first;

    unsigned int min, max;

    get_func_min_max(call_type, &min, &max);

    if (num_args < min || num_args > max)
        error_argument_count(emit, ast, num_args, min, max);

    lily_type **arg_types = call_type->subtypes;

    if (arg->tree_type == tree_oo_access) {
        if (lily_ts_check(emit->ts, arg_types[1], arg->result->type) == 0)
            error_bad_arg(emit, ast, call_type, 0, arg->result->type);
    }

    int stop;
    if ((call_type->flags & TYPE_IS_VARARGS) == 0 ||
        call_type->subtype_count - 1 > num_args)
        stop = num_args;
    else
        stop = call_type->subtype_count - 2;

    int i;
    for (i = count_first, arg = arg->next_arg;
         i < stop;
         i++, arg = arg->next_arg) {
        if (eval_call_arg(emit, arg, arg_types[i + 1]) == 0)
            error_bad_arg(emit, ast, call_type, i, arg->result->type);
    }

    lily_storage *vararg_s = NULL;

    /* The second check prevents running varargs when there are unfilled
       optional arguments that come before the varargs. */
    if (call_type->flags & TYPE_IS_VARARGS &&
        (num_args + 2) >= call_type->subtype_count) {

        /* Don't solve this yet, because eval_call_arg solves it (and double
           solving is bad). */
        lily_type *vararg_type = arg_types[i + 1];
        int is_optarg = 0;

        /* Varargs are presented as a `List` of their inner values, so use
           subtypes[0] to get the real type. If this vararg is optional, then do
           a double unwrap. */
        if (vararg_type->cls->id == LILY_OPTARG_ID) {
            is_optarg = 1;
            vararg_type = vararg_type->subtypes[0]->subtypes[0];
        }
        else
            vararg_type = vararg_type->subtypes[0];

        lily_ast *vararg_iter = arg;

        int vararg_i;
        for (vararg_i = i;
             arg != NULL;
             arg = arg->next_arg, vararg_i++) {
            if (eval_call_arg(emit, arg, vararg_type) == 0)
                error_bad_arg(emit, ast, call_type, vararg_i,
                        arg->result->type);
        }

        if (vararg_type->flags & TYPE_IS_UNRESOLVED)
            vararg_type = lily_ts_resolve(emit->ts, vararg_type);

        if (vararg_i != i || is_optarg == 0) {
            vararg_s = get_storage(emit, vararg_type);
            lily_u16_write_2(emit->code, o_build_list, vararg_i - i);
            for (;vararg_iter;vararg_iter = vararg_iter->next_arg)
                lily_u16_write_1(emit->code, vararg_iter->result->reg_spot);

            lily_u16_write_2(emit->code, vararg_s->reg_spot, ast->line_num);
        }
    }

    setup_call_result(emit, ast, arg_types[0]);
    write_call(emit, ast, stop, vararg_s);
}

/* This is a prelude to running a call. This function is responsible for
   determining what the source of the call is, and evaluating that source if it
   needs that.
   'ast' is the calling tree, with the first argument being the target. If the
   calling tree is not a variant, then the sym field will be set to the call
   target. Variants go through a different path than other trees when they are
   written, and thus do not need the sym field set.
   'call_type' must be the address of a non-NULL pointer. It will be set to the
   type of the sym, or the build type of the variant.  */
static void begin_call(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect, lily_type **call_type)
{
    lily_ast *first_arg = ast->arg_start;
    lily_tree_type first_tt = first_arg->tree_type;
    lily_sym *call_sym = NULL;

    switch (first_tt) {
        case tree_method:
            if (emit->block->self == NULL)
                maybe_close_over_class_self(emit, ast);

        case tree_defined_func:
        case tree_inherited_new:
            call_sym = first_arg->sym;
            if (call_sym->flags & VAR_NEEDS_CLOSURE) {
                lily_storage *s = get_storage(emit, first_arg->sym->type);
                emit_create_function(emit, first_arg->sym, s);
                call_sym = (lily_sym *)s;
            }
            break;
        case tree_static_func:
            ensure_valid_scope(emit, first_arg->sym);
            call_sym = first_arg->sym;
            break;
        case tree_oo_access:
            eval_oo_access_for_item(emit, first_arg);
            if (first_arg->item->item_kind == ITEM_TYPE_PROPERTY) {
                oo_property_read(emit, first_arg);
                call_sym = (lily_sym *)first_arg->result;
            }
            else
                call_sym = first_arg->sym;

            /* Calls expect that each argument tree has a result prepared. When
               evaluating `x.y`, the call source is the y but x is sent as the
               first argument. Do this so that tree_oo_access isn't a special
               case. */
            first_arg->result = first_arg->arg_start->result;
            break;
        case tree_variant: {
            lily_variant_class *variant = first_arg->variant;
            if (variant->flags & CLS_EMPTY_VARIANT)
                lily_raise_syn(emit->raiser, "Variant %s should not get args.",
                        variant->name);

            *call_type = first_arg->variant->build_type;
            break;
        }
        default:
            eval_tree(emit, ast->arg_start, NULL);
            call_sym = (lily_sym *)ast->arg_start->result;
            break;
    }

    if (call_sym) {
        ast->sym = call_sym;
        *call_type = call_sym->type;

        if (call_sym->type->cls->id != LILY_FUNCTION_ID)
            lily_raise_adjusted(emit->raiser, ast->line_num,
                    "Cannot anonymously call resulting type '^T'.",
                    call_sym->type);
    }
}

/* This does everything a call needs from start to finish. At the end of this,
   the 'ast' given will have a result set and code written. */
static void eval_call(lily_emit_state *emit, lily_ast *ast, lily_type *expect)
{
    lily_type *call_type = NULL;
    begin_call(emit, ast, expect, &call_type);

    lily_ts_save_point p;
    /* Scope save MUST happen after the call is started, because evaluating the
       call may trigger a dynaload. That dynaload may then cause the number of
       generics to be seen to increase. But since the scope was registered
       before the increase, there may be types from a different scope (they
       blast on entry) that are improperly visible. */
    lily_ts_scope_save(emit->ts, &p);

    if (call_type->flags & TYPE_IS_UNRESOLVED) {
        lily_tree_type first_tt = ast->arg_start->tree_type;

        if (first_tt == tree_local_var ||
            first_tt == tree_upvalue ||
            first_tt == tree_inherited_new)
            /* The first two cases simulate quantification by solving generics
               as themselves.
               The last case forces class inheritance to keep the same generic
               order (A of one class is always A of a superclass). It helps to
               make generic solving a lot simpler. */
            lily_ts_check(emit->ts, call_type, call_type);
        else if (expect &&
                 expect->cls->id == call_type->subtypes[0]->cls->id)
            /* Grab whatever inference is possible from the result. Don't do
               error checking. Instead, let a type mismatch show up that's
               closer to the problem source. */
            lily_ts_check(emit->ts, call_type->subtypes[0], expect);
    }

    run_call(emit, ast, call_type, expect);
    lily_ts_scope_restore(emit->ts, &p);
}

/* This handles variants that are used when they don't receive arguments. Any
   variants that are passed arguments are handled by call processing. */
static void eval_variant(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_variant_class *variant = ast->variant;
    /* Did this need arguments? It was used incorrectly if so. */
    if ((variant->flags & CLS_EMPTY_VARIANT) == 0) {
        unsigned int min, max;
        get_func_min_max(variant->build_type, &min, &max);
        error_argument_count(emit, ast, -1, min, max);
    }

    lily_u16_write_2(emit->code, o_get_empty_variant, variant->cls_id);

    lily_type *storage_type;

    if (variant->parent->generic_count) {
        lily_type *self_type = variant->parent->self_type;
        lily_ts_save_point p;
        lily_ts_scope_save(emit->ts, &p);

        /* Since the variant has no opinion on generics, try to pull any
           inference possible before defaulting to ?. */
        if (expect && expect->cls == variant->parent)
            lily_ts_check(emit->ts, self_type, expect);

        storage_type = lily_ts_resolve_with(emit->ts, self_type,
                emit->ts->question_class_type);

        lily_ts_scope_restore(emit->ts, &p);
    }
    else
        storage_type = variant->parent->self_type;

    lily_storage *s = get_storage(emit, storage_type);
    lily_u16_write_2(emit->code, s->reg_spot, ast->line_num);
    ast->result = (lily_sym *)s;
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
    eval_call(emit, ast, expect);
}

static void eval_plus_plus(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->left->tree_type != tree_local_var)
        eval_tree(emit, ast->left, NULL);

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, NULL);

    if (ast->parent == NULL ||
        (ast->parent->tree_type != tree_binary ||
         ast->parent->op != expr_plus_plus)) {
        lily_u16_write_2(emit->code, o_interpolation, 0);

        int fix_spot = lily_u16_pos(emit->code) - 1;
        lily_ast *iter_ast = ast->left;
        lily_storage *s = get_storage(emit,
                emit->symtab->string_class->self_type);

        while (1) {
            if (iter_ast->tree_type != tree_binary ||
                iter_ast->op != expr_plus_plus)
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

/* Evaluate 'ast' using 'expect' for inference. If unsure (or have no opinion of
   what 'ast' should be), 'expect' can be NULL. */
static void eval_tree(lily_emit_state *emit, lily_ast *ast, lily_type *expect)
{
    if (ast->tree_type == tree_global_var ||
        ast->tree_type == tree_defined_func ||
        ast->tree_type == tree_static_func ||
        ast->tree_type == tree_method ||
        ast->tree_type == tree_inherited_new ||
        ast->tree_type == tree_upvalue)
        emit_nonlocal_var(emit, ast);
    else if (ast->tree_type == tree_literal)
        emit_literal(emit, ast);
    else if (ast->tree_type == tree_integer)
        emit_integer(emit, ast);
    else if (ast->tree_type == tree_byte)
        emit_byte(emit, ast);
    else if (ast->tree_type == tree_boolean)
        emit_boolean(emit, ast);
    else if (ast->tree_type == tree_call)
        eval_call(emit, ast, expect);
    else if (ast->tree_type == tree_binary) {
        if (ast->op >= expr_assign) {
            lily_tree_type left_tt = ast->left->tree_type;
            if (left_tt == tree_local_var ||
                left_tt == tree_global_var)
                eval_assign(emit, ast);
            else if (left_tt == tree_subscript)
                eval_sub_assign(emit, ast);
            else if (left_tt == tree_oo_access)
                eval_oo_assign(emit, ast);
            else if (left_tt == tree_property)
                eval_property_assign(emit, ast);
            else if (left_tt == tree_upvalue)
                eval_upvalue_assign(emit, ast);
            else
                /* Let eval_assign say that it's wrong. */
                eval_assign(emit, ast);

            assign_post_check(emit, ast);
        }
        else if (ast->op == expr_logical_or || ast->op == expr_logical_and)
            eval_logical_op(emit, ast);
        else if (ast->op == expr_func_pipe)
            eval_func_pipe(emit, ast, expect);
        else if (ast->op == expr_plus_plus)
            eval_plus_plus(emit, ast);
        else {
            if (ast->left->tree_type != tree_local_var)
                eval_tree(emit, ast->left, NULL);

            if (ast->right->tree_type != tree_local_var)
                eval_tree(emit, ast->right, ast->left->result->type);

            emit_binary_op(emit, ast);
        }
    }
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
        eval_oo_access(emit, ast);
    else if (ast->tree_type == tree_property)
        eval_property(emit, ast);
    else if (ast->tree_type == tree_variant)
        eval_variant(emit, ast, expect);
    else if (ast->tree_type == tree_lambda)
        eval_lambda(emit, ast, expect);
    else if (ast->tree_type == tree_self)
        eval_self(emit, ast);
}

/* Evaluate a tree with 'expect' sent for inference. If the tree does not return
   a value, then SyntaxError is raised with 'message'. */
static void eval_enforce_value(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect, const char *message)
{
    eval_tree(emit, ast, expect);
    emit->expr_num++;

    if (ast->result == NULL)
        lily_raise_syn(emit->raiser, message);
}

/* This evaluates an expression at the root of the given pool, then resets the
   pool for the next expression. */
void lily_emit_eval_expr(lily_emit_state *emit, lily_expr_state *es)
{
    eval_tree(emit, es->root, NULL);
    emit->expr_num++;
}

lily_sym *lily_emit_eval_interp_expr(lily_emit_state *emit, lily_expr_state *es)
{
    eval_tree(emit, es->root, NULL);
    return es->root->result;
}

/* This is used by 'for...in'. It evaluates an expression, then writes an
   assignment that targets 'var'.
   Since this is used by 'for...in', it checks to make sure that the expression
   returns a value of type integer. If it does not, then SyntaxError is raised.
   The pool given will be cleared. */
void lily_emit_eval_expr_to_var(lily_emit_state *emit, lily_expr_state *es,
        lily_var *var)
{
    lily_ast *ast = es->root;

    eval_tree(emit, ast, NULL);
    emit->expr_num++;

    if (ast->result->type->cls->id != LILY_INTEGER_ID) {
        lily_raise_syn(emit->raiser,
                   "Expected type 'integer', but got type '^T'.",
                   ast->result->type);
    }

    /* Note: This works because the only time this is called is to handle
             for..in range expressions, which are always integers. */
    lily_u16_write_4(emit->code, o_fast_assign, ast->result->reg_spot,
            var->reg_spot, ast->line_num);
}

/* Evaluate the root of the given pool, making sure that the result is something
   that can be truthy/falsey. SyntaxError is raised if the result isn't.
   Since this is called to evaluate conditions, this also writes any needed jump
   or patch necessary. */
void lily_emit_eval_condition(lily_emit_state *emit, lily_expr_state *es)
{
    lily_ast *ast = es->root;
    lily_block_type current_type = emit->block->block_type;

    if (((ast->tree_type == tree_boolean ||
          ast->tree_type == tree_byte ||
          ast->tree_type == tree_integer) &&
          ast->backing_value != 0) == 0) {
        eval_enforce_value(emit, ast, NULL,
                "Conditional expression has no value.");
        ensure_valid_condition_type(emit, ast->result->type);

        /* For a do...while block, on success the target jumps back up and thus
           stays within the loop. Everything else checks for failure, and will
           jump to the next branch on failure. */
        emit_jump_if(emit, ast, current_type == block_do_while);
    }
    else {
        if (current_type != block_do_while) {
            /* Code that handles if/elif/else transitions expects each branch to
               write a jump. There's no easy way to tell it that none was made...
               so give it a fake jump. */
            lily_u16_write_1(emit->patches, 0);
        }
        else {
            /* A do-while block is negative because it jumps back up. */
            int location = lily_u16_pos(emit->code) - emit->block->code_start;
            lily_u16_write_2(emit->code, o_jump, (uint16_t)-location);
        }
    }
}

/* This is called from parser to evaluate the last expression that is within a
   lambda. This is rather tricky, because 'full_type' is supposed to describe
   the full type of the lambda, but may be NULL. If it isn't NULL, then use that
   to infer what the result of the lambda should be. */
void lily_emit_eval_lambda_body(lily_emit_state *emit, lily_expr_state *es,
        lily_type *full_type)
{
    lily_type *wanted_type = NULL;
    if (full_type)
        wanted_type = full_type->subtypes[0];

    /* If full_type is NULL, then the parent is considered to have no particular
       opinion as to if the lambda should or should not return a value. Default
       to returning something. In the case that the parent has an opinion, and
       the opinion is to not return anything, respect that. */
    int return_wanted = (full_type == NULL || full_type->subtypes[0] != NULL);

    eval_tree(emit, es->root, wanted_type);
    lily_sym *root_result = es->root->result;

    if (return_wanted && root_result != NULL) {
        /* If the caller doesn't want a return, then don't give one...regardless
           of if there is one available. */
        lily_u16_write_3(emit->code, o_return_val, es->root->result->reg_spot,
                es->root->line_num);
        emit->block->last_exit = lily_u16_pos(emit->code);
    }
    else if (return_wanted == 0)
        es->root->result = NULL;
}

/* This handles the 'return' keyword. If parser has the pool filled with some
   expression, then run that expression (checking the result). The pool will be
   cleared out if there was an expression. */
void lily_emit_eval_return(lily_emit_state *emit, lily_expr_state *es,
        lily_type *return_type)
{
    if (return_type != lily_unit_type) {
        lily_ast *ast = es->root;

        eval_enforce_value(emit, ast, return_type,
                "'return' expression has no value.");

        if (ast->result->type != return_type &&
            type_matchup(emit, return_type, ast) == 0) {
            lily_raise_adjusted(emit->raiser, ast->line_num,
                    "return expected type '^T' but got type '^T'.", return_type,
                    ast->result->type);
        }

        write_pop_try_blocks_up_to(emit, emit->function_block);
        lily_u16_write_3(emit->code, o_return_val, ast->result->reg_spot,
                ast->line_num);
        emit->block->last_exit = lily_u16_pos(emit->code);
    }
    else {
        write_pop_try_blocks_up_to(emit, emit->function_block);
        lily_u16_write_2(emit->code, o_return_unit, *emit->lex_linenum);
    }
}

/* Evaluate the given tree, then try to write instructions that will raise the
   result of the tree.
   SyntaxError happens if the tree's result is not raise-able. */
void lily_emit_raise(lily_emit_state *emit, lily_expr_state *es)
{
    lily_ast *ast = es->root;
    eval_enforce_value(emit, ast, NULL, "'raise' expression has no value.");

    lily_class *result_cls = ast->result->type->cls;
    if (lily_class_greater_eq_id(LILY_EXCEPTION_ID, result_cls) == 0) {
        lily_raise_syn(emit->raiser, "Invalid class '%s' given to raise.",
                result_cls->name);
    }

    lily_u16_write_3(emit->code, o_raise, ast->result->reg_spot, ast->line_num);
    emit->block->last_exit = lily_u16_pos(emit->code);
}

/* This resets __main__'s code position for the next pass. Only tagged mode
   needs this. */
void lily_reset_main(lily_emit_state *emit)
{
    emit->code->pos = 0;
}


/* This function is to be called before executing __main__. It makes sure that
   __main__'s code ends with exiting the vm, and that the code itself is
   linked to emitter's code. */
void lily_prepare_main(lily_emit_state *emit, lily_function_val *main_func)
{
    int register_count = emit->main_block->next_reg_spot;

    lily_u16_write_1(emit->code, o_return_from_vm);

    main_func->code_len = lily_u16_pos(emit->code);
    main_func->code = emit->code->data;
    main_func->reg_count = register_count;
}

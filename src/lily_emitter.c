#include <string.h>
#include <stdint.h>

#include "lily_ast.h"
#include "lily_emitter.h"
#include "lily_opcode.h"
#include "lily_emit_table.h"
#include "lily_parser.h"
#include "lily_opcode_table.h"

#include "lily_api_alloc.h"
#include "lily_api_value_ops.h"

# define IS_LOOP_BLOCK(b) (b == block_while || \
                           b == block_do_while || \
                           b == block_for_in)

# define lily_raise_adjusted(r, adjust, error_code, message, ...) \
{ \
    r->line_adjust = adjust; \
    lily_raise(r, error_code, message, __VA_ARGS__); \
}

/* (Temporary) What this does is rewind the membuf that parser and emitter share
   since the strings are no longer useful. It also rewinds the pool that  */
#define RESET_POOL(pool) \
emit->ast_membuf->pos = pool->membuf_start; \
lily_ast_reset_pool(pool);

/***
 *      ____       _
 *     / ___|  ___| |_ _   _ _ __
 *     \___ \ / _ \ __| | | | '_ \
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/
 *                          |_|
 */

static void add_call_state(lily_emit_state *);
static void add_storage(lily_emit_state *);

lily_emit_state *lily_new_emit_state(lily_symtab *symtab, lily_raiser *raiser)
{
    lily_emit_state *emit = lily_malloc(sizeof(lily_emit_state));

    emit->patches = lily_new_buffer_u16(4);
    emit->match_cases = lily_malloc(sizeof(int) * 4);
    emit->tm = lily_new_type_maker();
    emit->ts = lily_new_type_system(emit->tm, symtab->dynamic_class->type,
            symtab->question_class->type);
    emit->code = lily_new_buffer_u16(32);
    emit->closed_syms = lily_malloc(sizeof(lily_sym *) * 4);
    emit->transform_table = NULL;
    emit->transform_size = 0;

    emit->ast_membuf = lily_membuf_new();

    /* tm uses Dynamic's type as a special default, so it needs that. */
    emit->tm->dynamic_class_type = symtab->dynamic_class->type;
    emit->tm->question_class_type = symtab->question_class->type;

    emit->call_values = lily_malloc(sizeof(lily_sym *) * 8);
    emit->call_state = NULL;

    emit->call_values_pos = 0;
    emit->call_values_size = 8;

    emit->closed_pos = 0;
    emit->closed_size = 4;

    emit->match_case_pos = 0;
    emit->match_case_size = 4;

    emit->block = NULL;
    emit->unused_storage_start = NULL;
    emit->all_storage_start = NULL;
    emit->all_storage_top = NULL;

    emit->function_depth = 0;

    emit->raiser = raiser;
    emit->expr_num = 1;

    add_call_state(emit);

    return emit;
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_block *current, *temp;
    lily_storage *current_store, *temp_store;
    current = emit->block;
    while (current && current->prev)
        current = current->prev;

    while (current) {
        temp = current->next;
        lily_free(current);
        current = temp;
    }

    current_store = emit->all_storage_start;
    while (current_store) {
        temp_store = current_store->next;
        lily_free(current_store);
        current_store = temp_store;
    }

    lily_emit_call_state *call_iter = emit->call_state;
    if (call_iter) {
        while (call_iter->prev != NULL)
            call_iter = call_iter->prev;

        lily_emit_call_state *call_next;
        while (call_iter) {
            call_next = call_iter->next;
            lily_free(call_iter);
            call_iter = call_next;
        }
    }

    lily_membuf_free(emit->ast_membuf);
    lily_free(emit->transform_table);
    lily_free(emit->closed_syms);
    lily_free(emit->call_values);
    lily_free_type_system(emit->ts);
    lily_free(emit->match_cases);
    lily_free_buffer_u16(emit->patches);
    lily_free_buffer_u16(emit->code);
    lily_free(emit);
}

/* This is called once during parser init. It creates the first storage, and
   enters the block that represents __main__. */
void lily_emit_enter_main(lily_emit_state *emit)
{
    add_storage(emit);

    /* This creates the type for __main__. __main__ is a function that takes 0
       arguments and does not return anything. */
    lily_tm_add(emit->tm, NULL);
    lily_type *main_type = lily_tm_make(emit->tm, 0,
            emit->symtab->function_class, 1);

    lily_var *main_var = lily_new_raw_var(emit->symtab, main_type, "__main__");
    main_var->reg_spot = 0;
    main_var->function_depth = 1;
    main_var->flags |= VAR_IS_READONLY;
    emit->symtab->next_readonly_spot++;

    lily_block *main_block = lily_malloc(sizeof(lily_block));
    lily_function_val *main_function = lily_new_native_function_val(
            NULL, main_var->name);

    emit->symtab->main_var = main_var;
    emit->symtab->main_function = main_function;

    /* __main__ needs two refs because it goes through a custom deref. Most
       function values hold (copies of) the names of their vars inside for show
       to print. However, __main__ does not, because __main__'s vars are global
       and alive. Because of this, __main__ has a special deref. */
    main_function->refcount++;
    lily_tie_function(emit->symtab, main_var, main_function);

    /* Everything is set manually because creating a block requires taking info
       from a previous block (for things such as self). */
    main_block->prev = NULL;
    main_block->next = NULL;
    main_block->block_type = block_define;
    main_block->function_var = main_var;
    main_block->storage_start = emit->all_storage_start;
    main_block->class_entry = NULL;
    main_block->self = NULL;
    main_block->code_start = 0;
    main_block->jump_offset = 0;
    main_block->next_reg_spot = 0;
    main_block->loop_start = -1;
    main_block->make_closure = 0;
    emit->top_var = main_var;
    emit->top_function_ret = NULL;
    emit->block = main_block;
    emit->function_depth++;
    emit->main_block = main_block;
    emit->function_block = main_block;
}

/***
 *     __     __
 *     \ \   / /_ _ _ __ ___
 *      \ \ / / _` | '__/ __|
 *       \ V / (_| | |  \__ \
 *        \_/ \__,_|_|  |___/
 *
 */

/** Lily's vm is a register-based vm. This means that when a var is created, it
    occupies a certain slot and will retain that slot through the lifetime of
    the function. This has the benefit of having to set type information once,
    but some drawbacks too. For one, it makes variable creation tougher. Just
    creating a var is not possible.

    One problem that this creates is that temporary values (termed storages) are
    in one area (emitter), and vars are in another (symtab). The emitter must
    make sure that vars always retain their position, but allow storages to be
    used repeatedly.

    One trick that is employed by Lily is that all imports capture their
    toplevel code into a function called __import__. This allows error tracking
    to know what file had a problem. It's important that vars within __import__
    are available in other scopes, so __import__'s vars are registered as
    globals instead of locals.

    Lily also has an artificial requirement that define-d functions not be
    altered. Vars that are associated with a define are put into a separate
    table, and not loaded into any register (they'll never go out of scope,
    after all). These vars have a register spot that is actually a spot in a
    special read-only table within vm.

    To simplify this, emitter is in charge of setting register spots for vars
    and for storages too. Different kinds of vars will have different needs,
    however, and thus have different entry functions. **/

/* This is the most commonly-used function for creating a new var. This creates
   a new var that will be destroyed when the current block is complete. */
lily_var *lily_emit_new_scoped_var(lily_emit_state *emit, lily_type *type,
        const char *name)
{
    lily_var *new_var = lily_new_raw_var(emit->symtab, type, name);

    if (emit->function_depth == 1) {
        new_var->reg_spot = emit->main_block->next_reg_spot;
        emit->main_block->next_reg_spot++;
        new_var->flags |= VAR_IS_GLOBAL;
    }
    else {
        new_var->reg_spot = emit->function_block->next_reg_spot;
        emit->function_block->next_reg_spot++;
    }

    new_var->function_depth = emit->function_depth;

    return new_var;
}

/* This creates a new var that will be associated with a 'define'. */
lily_var *lily_emit_new_define_var(lily_emit_state *emit, lily_type *type,
        const char *name)
{
    lily_var *new_var = lily_new_raw_var(emit->symtab, type, name);

    new_var->reg_spot = emit->symtab->next_readonly_spot;
    emit->symtab->next_readonly_spot++;
    new_var->function_depth = 1;
    new_var->flags |= VAR_IS_READONLY;

    return new_var;
}

/* This is used to create a var that goes into a particular scope, and which has
   a foreign function associated with it. */
lily_var *lily_emit_new_tied_dyna_var(lily_emit_state *emit,
        lily_foreign_func func, lily_item *source, lily_type *type,
        const char *name)
{
    lily_var *new_var = lily_new_raw_unlinked_var(emit->symtab, type, name);

    new_var->function_depth = 1;
    new_var->flags |= VAR_IS_READONLY | VAR_IS_FOREIGN_FUNC;
    new_var->reg_spot = emit->symtab->next_readonly_spot;
    emit->symtab->next_readonly_spot++;

    lily_function_val *func_val;

    if (source->item_kind == ITEM_TYPE_MODULE) {
        lily_module_entry *module = (lily_module_entry *)source;

        new_var->next = module->var_chain;
        module->var_chain = new_var;

        func_val = lily_new_foreign_function_val(func, NULL, name);
        func_val->cid_table = ((lily_module_entry *)source)->cid_table;
    }
    else {
        lily_class *cls = (lily_class *)source;

        new_var->next = (lily_var *)cls->members;
        cls->members = (lily_named_sym *)new_var;
        new_var->parent = cls;

        func_val = lily_new_foreign_function_val(func, cls->name, name);
        func_val->cid_table = ((lily_class *)source)->module->cid_table;
    }

    lily_tie_function(emit->symtab, new_var, func_val);
    return new_var;
}

/* This creates a var that will be put into some special non-current space. This
   is used so that dynamically-loaded vars will be loaded once (and only once)
   into their appropriate scope. */
lily_var *lily_emit_new_dyna_var(lily_emit_state *emit,
        lily_module_entry *module, lily_type *type, const char *name)
{
    lily_var *new_var = lily_new_raw_unlinked_var(emit->symtab, type, name);

    new_var->reg_spot = emit->main_block->next_reg_spot;
    emit->main_block->next_reg_spot++;
    new_var->function_depth = 1;
    new_var->flags |= VAR_IS_GLOBAL;

    new_var->next = module->var_chain;
    module->var_chain = new_var;

    return new_var;
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

/* This is called from parser to get emitter to write a function call targeting
   a var. The var should always be an __import__ function. */
void lily_emit_write_import_call(lily_emit_state *emit, lily_var *var)
{
    lily_u16_write_5(emit->code, o_native_call, *emit->lex_linenum,
            var->reg_spot, 0, 0);
}

/* This takes the stack of optional arguments and writes out the jumping
   necessary at the top. */
void lily_emit_write_optargs(lily_emit_state *emit, lily_buffer_u16 *optargs,
        int start)
{
    /* Optional arguments are sent in pairs of class id and some data (usually
       the register spot of a value). The arguments have been added from left to
       right. The thinking is that registers at vm-time will be scanned from
       right to left.

       Supposing that there are 4 locals, the goal is to have something like
       this happen:

       switch(#regs)
           case 0:
               assign reg #1
           case 1:
               assign reg #2
           case 2:
               no-op
       } */
    int stop = optargs->pos;
    uint16_t *stack = optargs->data;
    uint16_t line_num = *emit->lex_linenum;
    int count = ((stop - start) / 3) + 1;
    int i;

    /* This writes down the most recent register and the count. The count is
       sent because the vm doesn't have an easy way to know how many to scan. */
    lily_u16_write_3(emit->code, o_optarg_dispatch,
            emit->block->next_reg_spot - 1, count);

    /* Write a block of zeroes that will be patched later. */
    for (i = 0;i < count - 1;i++)
        lily_u16_write_1(emit->code, 0);

    int jump_target = lily_u16_pos(emit->code);

    /* The last jump is the 'case 2' from above.  */
    lily_u16_write_1(emit->code, lily_u16_pos(emit->code) + 2);

    for (i = start;i != stop;i += 3, jump_target--) {
        int target_reg = stack[i];
        int opcode = stack[i + 1];
        int value = stack[i + 2];
        lily_u16_insert(emit->code, jump_target, lily_u16_pos(emit->code) -
                emit->block->jump_offset);
        lily_u16_write_4(emit->code, opcode, line_num, value, target_reg);
    }

    /* The first jump will be cascading down all of the default assigns. */
    lily_u16_insert(emit->code, jump_target, lily_u16_pos(emit->code) -
            emit->block->jump_offset);
}

/* This function writes the code necessary to get a for <var> in x...y style
   loop to work. */
void lily_emit_finalize_for_in(lily_emit_state *emit, lily_var *user_loop_var,
        lily_var *for_start, lily_var *for_end, lily_sym *for_step,
        int line_num)
{
    lily_class *cls = emit->symtab->integer_class;

    /* If no step is provided, provide '1' as a step. */
    if (for_step == NULL) {
        for_step = (lily_sym *)lily_emit_new_scoped_var(emit, cls->type,
                "(for step)");
        lily_u16_write_4(emit->code, o_get_integer, line_num, 1,
                for_step->reg_spot);
    }

    lily_sym *target;
    if (user_loop_var->function_depth == 1)
        /* DO NOT use a storage here. Global operations are equivalent to local
           operations when in just __main__. Besides, a storage would almost
           certainly be repurposed. At the writing of this comment, there is no
           way to lock a storage to prevent other uses of it. */
        target = (lily_sym *)lily_emit_new_scoped_var(emit, cls->type,
                "(for temp)");
    else
        target = (lily_sym *)user_loop_var;

    lily_u16_write_6(emit->code, o_for_setup, line_num, target->reg_spot,
            for_start->reg_spot, for_end->reg_spot, for_step->reg_spot);
    lily_u16_write_1(emit->code, 0);

    if (target != (lily_sym *)user_loop_var) {
        lily_u16_write_4(emit->code, o_set_global, line_num, target->reg_spot,
                user_loop_var->reg_spot);
    }
    /* for..in is entered right after 'for' is seen. However, range values can
       be expressions. This needs to be fixed, or the loop will jump back up to
       re-eval those expressions. */
    emit->block->loop_start = lily_u16_pos(emit->code);

    lily_u16_write_5(emit->code, o_integer_for, line_num, target->reg_spot,
            for_end->reg_spot, for_step->reg_spot);

    /* Might as well write the patch, since code is in just the right spot. */
    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code));

    lily_u16_write_2(emit->code, 0, for_start->reg_spot);

    if (target != (lily_sym *)user_loop_var) {
        lily_u16_write_4(emit->code, o_set_global, line_num, target->reg_spot,
                user_loop_var->reg_spot);
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
    if (emit->block->loop_start == (uint16_t)-1) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "'break' used outside of a loop.\n");
    }

    lily_block *loop_block = find_deepest_loop(emit);

    write_pop_try_blocks_up_to(emit, loop_block);

    /* Write the jump, then figure out where to put it. */
    lily_u16_write_2(emit->code, o_jump, 0);

    inject_patch_into_block(emit, loop_block, lily_u16_pos(emit->code) - 1);
}

/* The parser has a 'continue' and wants the emitter to write the code. */
void lily_emit_continue(lily_emit_state *emit)
{
    if (emit->block->loop_start == (uint16_t)-1) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "'continue' used outside of a loop.\n");
    }

    write_pop_try_blocks_up_to(emit, find_deepest_loop(emit));

    lily_u16_write_2(emit->code, o_jump, emit->block->loop_start);
}

/* The parser has a 'try' and wants the emitter to write the code. */
void lily_emit_try(lily_emit_state *emit, int line_num)
{
    lily_u16_write_3(emit->code, o_push_try, line_num, 0);

    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);
}

/* The parser has an 'except' clause and wants emitter to write code for it. */
void lily_emit_except(lily_emit_state *emit, lily_type *except_type,
        lily_var *except_var, int line_num)
{
    int spot = lily_u16_pos(emit->code) + 3;
    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) + 3);

    if (except_var)
        /* There's a register to dump the result into, so use this opcode to let
           the vm know to copy down the information to this var. */
        lily_u16_write_5(emit->code, o_except_catch, line_num,
                except_var->type->cls->id, 0, except_var->reg_spot);
    else
        /* It doesn't matter, so the vm shouldn't bother fixing up the exception
           stack. The last 0 is very important, because for both of these
           opcodes, the vm grabs the register at spot. Without setting a zero,
           the register would depend on the next opcode (or a condition check
           would be needed). */
        lily_u16_write_5(emit->code, o_except_ignore, line_num,
                except_type->cls->id, 0, 0);

    if (emit->code->data[spot] != 0)
        fprintf(stderr, "value of patch spot is %d.\n", emit->code->data[spot]);
}

/* Write a conditional jump. 0 means jump if false, 1 means jump if true. The
   ast is the thing to test. */
static void emit_jump_if(lily_emit_state *emit, lily_ast *ast, int jump_on)
{
    lily_u16_write_4(emit->code, o_jump_if, jump_on, ast->result->reg_spot, 0);

    lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);
}

/* This takes all patches that exist in the current block and makes them target
   'pos'. The patches are removed. */
void write_block_patches(lily_emit_state *emit, int pos)
{
    int from = emit->patches->pos - 1;
    int to = emit->block->patch_start;

    for (;from >= to;from--) {
        /* Skip -1's, which are fake patches from conditions that were
            optimized out. */
        uint16_t patch = lily_u16_pop(emit->patches);

        if (patch != (uint16_t)-1)
            lily_u16_insert(emit->code, patch, pos);
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

/** Storages are used to hold intermediate values. The emitter is responsible
    for handing them out, controlling their position, and making new ones.
    Most of that is done in get_storage. **/

static void add_storage(lily_emit_state *emit)
{
    lily_storage *storage = lily_malloc(sizeof(lily_storage));

    storage->type = NULL;
    storage->next = NULL;
    storage->expr_num = 0;
    storage->flags = 0;
    storage->item_kind = ITEM_TYPE_STORAGE;

    if (emit->all_storage_start == NULL)
        emit->all_storage_start = storage;
    else
        emit->all_storage_top->next = storage;

    emit->all_storage_top = storage;
    emit->unused_storage_start = storage;
}

/* This attempts to grab a storage of the given type. It will first attempt to
   get a used storage, then a new one. */
static lily_storage *get_storage(lily_emit_state *emit, lily_type *type)
{
    lily_storage *storage_iter = emit->block->storage_start;
    int expr_num = emit->expr_num;

    while (storage_iter) {
        /* A storage with a type of NULL is not in use and can be claimed. */
        if (storage_iter->type == NULL) {
            storage_iter->type = type;

            storage_iter->reg_spot = emit->function_block->next_reg_spot;
            emit->function_block->next_reg_spot++;

            if (storage_iter->next)
                emit->unused_storage_start = storage_iter->next;

            break;
        }
        else if (storage_iter->type == type &&
                 storage_iter->expr_num != expr_num) {
            storage_iter->expr_num = expr_num;
            break;
        }

        storage_iter = storage_iter->next;
    }

    storage_iter->expr_num = expr_num;
    /* This ensures that there is always a valid extra storage. It makes setting
       the storage starting position easier. */
    if (storage_iter->next == NULL)
        add_storage(emit);

    storage_iter->flags &= ~SYM_NOT_ASSIGNABLE;
    return storage_iter;
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

/***
 *      ____  _            _
 *     | __ )| | ___   ___| | _____
 *     |  _ \| |/ _ \ / __| |/ / __|
 *     | |_) | | (_) | (__|   <\__ \
 *     |____/|_|\___/ \___|_|\_\___/
 *
 */

static void inject_patch_into_block(lily_emit_state *, lily_block *, uint16_t);
static lily_function_val *create_code_block_for(lily_emit_state *, lily_block *);
static void ensure_proper_match_block(lily_emit_state *);

/** The emitter's blocks keep track of the current context of things. Is the
    current block an if with or without an else? Where do storages start? Were
    any vars created in this scope?

    Blocks are currently in a rough state. They've accidentally grown fat, and
    likely carry too much info. The same thing represents both a defined
    function, an if block, etc. Some blocks don't necessarily use all of the
    items that are inside. **/

/* This enters a block of the given block type. A block's vars are considered to
   be any vars created after the block has been entered. Information such as the
   current class entered and self's location is inferred from existing info. */
void lily_emit_enter_block(lily_emit_state *emit, lily_block_type block_type)
{
    lily_block *new_block;
    if (emit->block->next == NULL) {
        new_block = lily_malloc(sizeof(lily_block));

        emit->block->next = new_block;
        new_block->prev = emit->block;
        new_block->next = NULL;
    }
    else
        new_block = emit->block->next;

    new_block->block_type = block_type;
    new_block->var_start = emit->symtab->active_module->var_chain;
    new_block->class_entry = emit->block->class_entry;
    new_block->self = emit->block->self;
    new_block->patch_start = emit->patches->pos;
    new_block->last_exit = -1;
    new_block->loop_start = emit->block->loop_start;
    new_block->make_closure = 0;

    if (block_type < block_define) {
        /* Non-functions will continue using the storages that the parent uses.
           Additionally, the same technique is used to allow loop starts to
           bubble upward until a function gets in the way. */
        new_block->storage_start = emit->block->storage_start;
        new_block->jump_offset = emit->block->jump_offset;
        new_block->all_branches_exit = 1;

        if (IS_LOOP_BLOCK(block_type))
            new_block->loop_start = lily_u16_pos(emit->code);
        else if (block_type == block_enum) {
            /* Enum entries are not considered function-like, because they do
               not have a class .new. */
            new_block->class_entry = emit->symtab->active_module->class_chain;
            new_block->loop_start = -1;
        }
    }
    else {
        lily_var *v = emit->symtab->active_module->var_chain;
        if (block_type == block_class)
            new_block->class_entry = emit->symtab->active_module->class_chain;

        v->parent = new_block->class_entry;

        /* This only happens when a define occurs within another define. The
           inner define is marked as needing closures. This makes it so all
           calls to the inner define will create a copy with closures.
           The last line exists to prevent dynaloaded class constructors from
           unnecessarily being marked as closures. */
        if (emit->function_depth >= 2 &&
            emit->block->block_type != block_class &&
            new_block->block_type != block_class)
            v->flags |= VAR_NEEDS_CLOSURE;

        new_block->next_reg_spot = 0;

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
        emit->function_block = new_block;

        new_block->storage_start = emit->unused_storage_start;
        new_block->function_var = v;
        new_block->code_start = lily_u16_pos(emit->code);
        new_block->jump_offset = lily_u16_pos(emit->code);
        new_block->loop_start = -1;

        emit->top_var = v;
    }

    emit->block = new_block;
}

/* This is called when a function-like block is exiting. The function value
   that the block needs is made, the register info is made, and storages are
   freed up. */
static void finalize_function_block(lily_emit_state *emit,
        lily_block *function_block)
{
    /* This must run before the rest, because if this call needs to be a
       closure, it will require a unique storage. */
    lily_function_val *f = create_code_block_for(emit, function_block);

    int register_count = emit->function_block->next_reg_spot;
    lily_storage *storage_iter = function_block->storage_start;

    if (emit->function_depth > 1) {
        lily_var *var_stop = function_block->function_var;
        /* todo: Reuse the var shells instead of destroying. Seems petty, but
                 malloc isn't cheap if there are a lot of vars. */
        lily_var *var_iter = emit->symtab->active_module->var_chain;
        lily_var *var_temp;
        while (var_iter != var_stop) {
            var_temp = var_iter->next;
            if ((var_iter->flags & VAR_IS_READONLY) == 0) {
                lily_free(var_iter->name);
                lily_free(var_iter);
            }
            else {
                /* This is a function declared within the current function. Hide it
                   in symtab's old functions since it's going out of scope. */
                var_iter->next = emit->symtab->old_function_chain;
                emit->symtab->old_function_chain = var_iter;
            }

            /* The function value now owns the var names, so don't free them. */
            var_iter = var_temp;
        }
    }

    storage_iter = function_block->storage_start;
    while (storage_iter) {
        storage_iter->type = NULL;
        storage_iter = storage_iter->next;
    }

    emit->unused_storage_start = function_block->storage_start;

    f->reg_count = register_count;
}

static void leave_function(lily_emit_state *emit, lily_block *block)
{
    if (block->block_type == block_class) {
        int class_flags = block->class_entry->flags;

        if (class_flags & (CLS_GC_SPECULATIVE | CLS_GC_TAGGED)) {
            uint16_t opcode;
            if (class_flags & CLS_GC_SPECULATIVE)
                opcode = o_new_instance_speculative;
            else
                opcode = o_new_instance_tagged;

            lily_u16_insert(emit->code, block->code_start, opcode);
        }

        lily_u16_write_3(emit->code, o_return_val, *emit->lex_linenum,
                block->self->reg_spot);
    }
    else {
        /* A lambda's return is whatever the last expression returns. */
        if (block->block_type == block_lambda)
            emit->top_function_ret = emit->top_var->type->subtypes[0];
        if (emit->top_function_ret == NULL)
            lily_u16_write_2(emit->code, o_return_noval, *emit->lex_linenum);
        else if (block->block_type == block_define &&
                 block->last_exit != lily_u16_pos(emit->code)) {
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Missing return statement at end of function.\n");
        }
    }

    finalize_function_block(emit, block);

    /* Information must be pulled from and saved to the last function-like
       block. This loop is because of lambdas. */
    lily_block *last_func_block = block->prev;
    while (last_func_block->block_type < block_define)
        last_func_block = last_func_block->prev;

    lily_var *v = last_func_block->function_var;

    /* If this function was the .new for a class, move it over into that class
       since the class is about to close. */
    if (emit->block->block_type == block_class) {
        lily_class *cls = emit->block->class_entry;

        emit->symtab->active_module->var_chain = block->function_var;
        lily_add_class_method(emit->symtab, cls, block->function_var);
    }
    else if (emit->block->block_type != block_file)
        emit->symtab->active_module->var_chain = block->function_var;
    /* For file 'blocks', don't fix the var_chain or all of the toplevel
       functions in that block will vanish! */

    emit->top_var = v;
    emit->top_function_ret = v->type->subtypes[0];
    emit->function_block = last_func_block;

    lily_u16_set_pos(emit->code, block->code_start);

    /* File 'blocks' do not bump up the depth because that's used to determine
       if something is a global or not. */
    if (block->block_type != block_file) {
        emit->function_depth--;

        /* If this block needed to make a closure, then mark the upward block
           as that too. This ensures that necessary closure data is not
           forgotten. But...don't do that to __main__ or things get weird. */
        if (block->make_closure == 1 && emit->function_block->prev != NULL)
            emit->function_block->make_closure = 1;
    }
}

/* This leaves the current block. If there is a function-like entity that has
   been left, then it's prepared and cleaned up. */
void lily_emit_leave_block(lily_emit_state *emit)
{
    lily_var *v;
    lily_block *block;
    int block_type;

    if (emit->block->prev == NULL)
        lily_raise(emit->raiser, lily_SyntaxError, "'}' outside of a block.\n");

    block = emit->block;
    block_type = block->block_type;

    /* These blocks need to jump back up when the bottom is hit. */
    if (block_type == block_while || block_type == block_for_in)
        lily_u16_write_2(emit->code, o_jump,
                block->loop_start - block->jump_offset);
    else if (block_type == block_match) {
        ensure_proper_match_block(emit);
        emit->match_case_pos = emit->block->match_case_start;
    }
    else if (block_type == block_try ||
             block_type == block_try_except ||
             block_type == block_try_except_all) {
        /* The vm expects that the last except block will have a 'next' of 0 to
           indicate the end of the 'except' chain. Remove the patch that the
           last except block installed so it doesn't get patched. */
        emit->patches->pos--;
    }

    if ((block_type == block_if_else ||
         block_type == block_match ||
         block_type == block_try_except_all) &&
        block->all_branches_exit &&
        block->last_exit == lily_u16_pos(emit->code)) {
        emit->block->prev->last_exit = lily_u16_pos(emit->code);
    }

    v = block->var_start;

    if (block_type < block_define) {
        write_block_patches(emit, lily_u16_pos(emit->code) -
                block->jump_offset);

        lily_hide_block_vars(emit->symtab, v);
    }
    else
        leave_function(emit, block);

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
    lily_block_type current_type = emit->block->block_type;
    int save_jump;

    if (emit->block->last_exit != lily_u16_pos(emit->code))
        emit->block->all_branches_exit = 0;

    if (new_type == block_if_elif || new_type == block_if_else) {
        char *block_name;
        if (new_type == block_if_elif)
            block_name = "elif";
        else
            block_name = "else";

        if (current_type != block_if && current_type != block_if_elif)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "'%s' without 'if'.\n", block_name);

        if (current_type == block_if_else)
            lily_raise(emit->raiser, lily_SyntaxError, "'%s' after 'else'.\n",
                    block_name);
    }
    else if (new_type == block_try_except || new_type == block_try_except_all) {
        if (current_type == block_try_except_all)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "'except' clause is unreachable.\n");
        else if (current_type != block_try && current_type != block_try_except)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "'except' outside 'try'.\n");

        /* If nothing in the 'try' block raises an error, the vm needs to be
           told to unregister the 'try' block since will become unreachable
           when the jump below occurs. */
        if (current_type == block_try)
            lily_u16_write_1(emit->code, o_pop_try);
    }

    lily_var *v = emit->block->var_start;
    if (v != emit->symtab->active_module->var_chain)
        lily_hide_block_vars(emit->symtab, v);

    /* Transitioning between blocks is simple: First write a jump at the end of
       the current branch. This will get patched to the if/try's exit. */
    lily_u16_write_2(emit->code, o_jump, 0);
    save_jump = lily_u16_pos(emit->code) - 1;

    /* The last jump of the previous branch wants to know where the check for
       the next branch starts. It's right now. */
    uint16_t patch = lily_u16_pop(emit->patches);

    if (patch != (uint16_t)-1)
        lily_u16_insert(emit->code, patch,
                lily_u16_pos(emit->code) - emit->block->jump_offset);
    /* else it's a fake branch from a condition that was optimized out. */

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

static void grow_closed_syms(lily_emit_state *emit)
{
    emit->closed_size *= 2;
    emit->closed_syms = lily_realloc(emit->closed_syms,
        sizeof(lily_sym *) * emit->closed_size);
}

static void close_over_sym(lily_emit_state *emit, lily_sym *sym)
{
    if (emit->closed_pos == emit->closed_size)
        grow_closed_syms(emit);

    emit->closed_syms[emit->closed_pos] = sym;
    emit->closed_pos++;
    sym->flags |= SYM_CLOSED_OVER;
    emit->function_block->make_closure = 1;
}

/* This writes o_create_function which will create a copy of 'func_sym' but
   with closure information. 'target' is a storage where the closed-over copy
   will end up. The result cannot be cached in any way (each invocation should
   get a fresh set of cells). */
static void emit_create_function(lily_emit_state *emit, lily_sym *func_sym,
        lily_storage *target)
{
    lily_u16_write_4(emit->code, o_create_function, 0, func_sym->reg_spot,
            target->reg_spot);
    inject_patch_into_block(emit, emit->function_block,
            lily_u16_pos(emit->code) - 3);
    emit->function_block->make_closure = 1;
}

/* This function closes over a var, but has a requirement. The requirement is
   that it cannot be an unsolved type from a higher-up scope. This requirement
   exists because Lily does not currently understand how to have two different
   kinds of a generic that are not equivalent / scoped generics. */
static void checked_close_over_var(lily_emit_state *emit, lily_var *var)
{
    if (emit->function_block->block_type == block_define &&
        emit->function_block->prev->block_type == block_define &&
        var->type->flags & TYPE_IS_UNRESOLVED)
        lily_raise(emit->raiser, lily_SyntaxError,
                "Cannot close over a var of an incomplete type in this scope.\n");

    close_over_sym(emit, (lily_sym *)var);
}

/* See if the given sym has been closed over.
   Success: The spot
   Failure: -1 */
static int find_closed_sym_spot(lily_emit_state *emit, lily_sym *sym)
{
    int result = -1, i;
    for (i = 0;i < emit->closed_pos;i++) {
        if (emit->closed_syms[i] == sym) {
            result = i;
            break;
        }
    }

    return result;
}

/* Like find_closed_sym_spot, but checks for 'self' instead. */
static int find_closed_self_spot(lily_emit_state *emit)
{
    int i, result = -1;
    for (i = 0;i < emit->closed_pos;i++) {
        lily_sym *s = emit->closed_syms[i];
        if (s && s->item_kind != ITEM_TYPE_VAR) {
            result = i;
            break;
        }
    }

    return result;
}

/* If 'self' isn't close over, then close over it. */
static void maybe_close_over_class_self(lily_emit_state *emit)
{
    lily_block *block = emit->block;
    while (block->block_type != block_class)
        block = block->prev;

    lily_sym *self = (lily_sym *)block->self;
    if (find_closed_sym_spot(emit, self) == -1)
        close_over_sym(emit, self);

    if (emit->block->self == NULL)
        emit->block->self = get_storage(emit, self->type);

    emit->function_block->make_closure = 1;
}


/* This function walks over the opcodes from 'pos' to 'end'. As it walks through
   opcodes, the values are put into three relative groups:
   * This is a local read (needs to be shadowed over).
   * This is a local write (shadow)
   * This is a jump (it needs to be adjusted OR held)
   * I don't care about this.

   Raw code is walked over, emitting new code in the process. Before this is
   called, the one caller makes sure to do a write_prep that will prevent
   emit->code from being resized while being iterated over.

   This helps ensure that the closure always has fresh cells. */
static void transform_code(lily_emit_state *emit, lily_function_val *f,
        int pos, int end, int starting_adjust)
{
    uint16_t *transform_table = emit->transform_table;
    int jump_adjust = starting_adjust;
    int jump_pos = -1, jump_end;
    int output_pos = -1, output_end;
    /* Do not create a local copy of emit->code here, because the write_4's
       may cause it to be realloc'd. */
    int patch_start = emit->patches->pos;

    while (pos != end) {
        int j = 0, op = emit->code->data[pos];
        int c, count, call_type, i, line_num;
        const int *opcode_data = opcode_table[op];
        int patch_i;

        /* If there are any jumps that were stored that target the current
           position, they can be written now. This doesn't pop out jumps that
           are written, since this isn't considered to be a hot path. */
        for (patch_i = patch_start;patch_i != emit->patches->pos;patch_i += 2) {
            int where = emit->patches->data[patch_i + 1];
            if (pos == where)
                emit->code->data[emit->patches->data[patch_i]] = pos + jump_adjust;
        }

        for (i = 1;i <= opcode_data[1];i++) {
            c = opcode_data[i + 1];
            if (c == C_LINENO)
                line_num = emit->code->data[pos + i + j];
            else if ((c == C_INPUT || c == C_MATCH_INPUT ||
                      (c == C_CALL_INPUT && call_type == 0)) &&
                     op != o_create_function) {
                int spot = emit->code->data[pos + i + j];
                if (transform_table[spot] != (uint16_t)-1) {
                    lily_u16_write_4(emit->code, o_get_upvalue, line_num,
                            transform_table[spot], spot);
                    jump_adjust += 4;
                }
            }
            else if (c == C_OUTPUT) {
                int spot = emit->code->data[pos + i + j];
                if (spot != (uint16_t)-1 &&
                    transform_table[spot] != (uint16_t)-1) {
                    output_pos = i + j;
                    output_end = output_pos + 1;
                }
            }
            else if (c == C_COUNT)
                count = emit->code->data[pos + i + j];
            else if (c == C_NOP)
                break;
            else if (c == C_CALL_TYPE)
                call_type = emit->code->data[pos + i + j];
            else if (c == C_COUNT_OUTPUTS) {
                output_pos = i + j;
                output_end = output_pos + count;
                j += count - 1;
            }
            else if (c == C_JUMP) {
                /* All of the o_except cases of a single try block are linked
                   together. The last one has a jump position of 0 to mean that
                   it's at the end. Make sure that is preserved. */
                if (op != o_except_catch &&
                    op != o_except_ignore &&
                    emit->code->data[pos + i + j] != 0) {
                    jump_pos = i + j;
                    jump_end = jump_pos + 1;
                }
            }
            else if (c == C_COUNT_JUMPS) {
                jump_pos = i + j;
                jump_end = jump_pos + count;
                j += count - 1;
            }
            else if (c == C_COUNT_LIST) {
                for (j = 0;j < count;j++) {
                    int spot = emit->code->data[pos + i + j];
                    if (transform_table[spot] != (uint16_t)-1) {
                        lily_u16_write_4(emit->code, o_get_upvalue, line_num,
                                transform_table[spot], spot);
                        jump_adjust += 4;
                    }
                }
                j--;
            }
            else if (c == C_COUNT_OUTPUTS) {
                output_pos = i + j;
                output_end = output_pos + count;
                j += count - 1;
            }
            else if (c == C_COUNT_OPTARGS) {
                count = emit->code->data[pos + i + j];
                /* Optargs is unique in that it contains two kinds of things.
                   The first half are literals, and the second half are register
                   outputs. */
                output_pos = i + j + 1 + (count / 2);
                output_end = i + j + 1 + count;
                /* Do not do count - 1 because this one includes the size with
                   it since there's no standalone non-counted optargs. */
                j += count;
            }
        }

        int move = i + j;

        lily_u16_write_prep(emit->code, move);
        memcpy(emit->code->data + lily_u16_pos(emit->code), emit->code->data + pos,
               move * sizeof(uint16_t));

        if (jump_pos != -1) {
            for (;jump_pos < jump_end;jump_pos++) {
                if (emit->code->data[lily_u16_pos(emit->code) + jump_pos] > pos) {
                    /* This is a jump to a future place. Don't patch this now,
                       because there may be more adjustments made between now
                       and the target location. Mark it down for later. */
                    lily_u16_write_2(emit->patches,
                            lily_u16_pos(emit->code) + jump_pos,
                            emit->code->data[lily_u16_pos(emit->code) + jump_pos]);
                }
                else
                    emit->code->data[lily_u16_pos(emit->code) + jump_pos] += jump_adjust;
            }

            jump_pos = -1;
        }

        lily_u16_set_pos(emit->code, emit->code->pos + move);

        if (output_pos != -1) {
            for (;output_pos < output_end;output_pos++) {
                int spot = emit->code->data[pos + output_pos];
                if (spot != (uint16_t)-1 &&
                    transform_table[spot] != (uint16_t)-1) {
                    lily_u16_write_4(emit->code, o_set_upvalue, line_num,
                            transform_table[spot], spot);
                    jump_adjust += 4;
                }
            }
            output_pos = -1;
        }

        pos += move;
    }

    emit->patches->pos = patch_start;
}

/* The parameters of a function are never assigned anywhere. As a result, the
   values won't have a shadowing assignment. If any parameters are closed over,
   this will write a direct upvalue assignment to them. This ensures that they
   will be in the closure. */
static void ensure_params_in_closure(lily_emit_state *emit)
{
    lily_var *function_var = emit->block->function_var;
    int local_count = function_var->type->subtype_count - 1;
    if (local_count == 0)
        return;

    lily_class *optarg_class = emit->symtab->optarg_class;
    /* The vars themselves aren't marked optargs, because that would be silly.
       To know if something has optargs, prod the function's types. */
    lily_type **real_param_types = function_var->type->subtypes;

    lily_var *var_iter = emit->symtab->active_module->var_chain;
    while (var_iter != function_var) {
        if (var_iter->flags & SYM_CLOSED_OVER &&
            var_iter->reg_spot < local_count) {
            lily_type *real_type = real_param_types[var_iter->reg_spot + 1];
            if (real_type->cls != optarg_class)
                lily_u16_write_4(emit->code, o_set_upvalue,
                        function_var->line_num,
                        find_closed_sym_spot(emit, (lily_sym *)var_iter),
                        var_iter->reg_spot);
        }

        var_iter = var_iter->next;
    }
}

/* This sets up the table used to map from a register spot to where that spot is
   in the closure. */
static void setup_transform_table(lily_emit_state *emit)
{
    if (emit->transform_size < emit->function_block->next_reg_spot) {
        emit->transform_table = lily_realloc(emit->transform_table,
                emit->function_block->next_reg_spot * sizeof(uint16_t));
        emit->transform_size = emit->function_block->next_reg_spot;
    }

    memset(emit->transform_table, (uint16_t)-1,
           sizeof(uint16_t) * emit->function_block->next_reg_spot);

    int i;

    for (i = 0;i < emit->closed_pos;i++) {
        lily_sym *s = (lily_sym *)emit->closed_syms[i];
        if (s && s->item_kind == ITEM_TYPE_VAR) {
            lily_var *v = (lily_var *)s;
            if (v->function_depth == emit->function_depth) {
                emit->transform_table[v->reg_spot] = i;
                /* Each var can only be transformed once, and within the scope
                   it was declared. This prevents two nested functions from
                   trying to transform the same (now-dead) vars. */
                emit->closed_syms[i] = NULL;
            }
        }
    }
}

/* Some functions are closures and recursive. Calling a recursive function
   should not result in local values being mutated. This function solves that by
   'zapping' the cells on the current level. This forces the closed-over
   function to make new cells. */
static void write_closure_zap(lily_emit_state *emit)
{
    int spot = lily_u16_pos(emit->code);
    /* This will be patched with the length later. */
    lily_u16_write_1(emit->code, 0);
    int count = 0;

    int i;
    for (i = 0;i < emit->closed_pos;i++) {
        lily_sym *sym = emit->closed_syms[i];
        if (sym && sym->item_kind == ITEM_TYPE_VAR) {
            lily_var *var = (lily_var *)sym;
            if (var->function_depth == emit->function_depth) {
                lily_u16_write_1(emit->code, i);
                count++;
            }
        }
    }

    lily_u16_insert(emit->code, spot, count);
}

/* This function is called to transform the currently available segment of code
   (emit->block->code_start up to emit->code_pos) into code that will work for
   closures.
   there are a couple things to do before the transform:
   * The first part is to setup the emitter's "transform table". This table will
     map from a var's position in the current function's locals to the position
     it has in the current closure. This will be used by transform_code.
   * Depending on where this function is (is it a class method, a nested
     function, or the top-most function), a different opcode will get written
     that will become the top of the transformed code. */
static void closure_code_transform(lily_emit_state *emit, lily_function_val *f,
        int *new_start, int *new_size)
{
    int transform_start = emit->block->code_start;
    int start = transform_start;
    int end = lily_u16_pos(emit->code);
    *new_start = lily_u16_pos(emit->code);
    int save_code_pos = lily_u16_pos(emit->code);

    /* To make sure that the closure information is not unexpectedly destroyed,
       it is stored into a register. get_unique_storage is custom made for this,
       and will grab a storage that nothing else is using. */
    lily_storage *s = get_unique_storage(emit, emit->block->function_var->type);

    int closed_self_spot = find_closed_self_spot(emit);
    /* Take note that the new code start will be the current code end + 1.
       Anything written from here until the code transform will appear at the
       top of the transformed code. */
    if (emit->function_depth == 2) {
        /* A depth of 2 means that this is the very top function. It will need
           to create the closure that gets passed down. This is really easy. */
        lily_u16_write_4(emit->code, o_create_closure, f->line_num,
                emit->closed_pos, s->reg_spot);

        if (emit->block->block_type == block_class) {
            emit->block->class_entry->flags |= CLS_GC_TAGGED;

            /* Classes are slightly tricky. There are (up to) three different
               things that really want to be at the top of the code:
               o_new_instance_*, o_setup_optargs, and o_native_call (in the
               event that there is an inherited new).
               Inject o_new_instance_mark, then patch that out of the header so
               that transform doesn't write it in again. */

            uint16_t linenum = emit->code->data[start + 1];
            uint16_t cls_id = emit->code->data[start + 2];
            uint16_t self_reg_spot = emit->code->data[start + 3];
            /* The class is holding a Function, so it's getting tagged. */
            lily_u16_write_4(emit->code, o_new_instance_tagged, linenum, cls_id,
                    self_reg_spot);

            transform_start += 4;

            /* The closure only needs to hold self if there was a lambda that
               used self (because the lambda doesn't automatically get self). */
            if (closed_self_spot != -1) {
                lily_u16_write_4(emit->code, o_set_upvalue, linenum,
                        closed_self_spot, self_reg_spot);
                /* This class is going out of scope, so the 'self' it contians
                   is going away as well. */
                emit->closed_syms[closed_self_spot] = NULL;
            }

            lily_class *cls = emit->block->class_entry;
            /* This is only set if a class method needed to access some part of
               the closure through the class. This is likely to be the case, but
               may not always be (ex: the class only contains lambdas). */
            lily_prop_entry *closure_prop;
            closure_prop = lily_find_property(cls, "*closure");

            if (closure_prop) {
                lily_u16_write_5(emit->code, o_set_property, linenum,
                        self_reg_spot, closure_prop->id, s->reg_spot);
            }
        }
    }
    else if (emit->block->prev &&
             emit->block->prev->block_type == block_class) {
        if (emit->block->block_type != block_lambda) {
            lily_class *cls = emit->block->class_entry;
            lily_prop_entry *closure_prop = lily_find_property(cls, "*closure");
            lily_class *parent = cls->parent;
            if (closure_prop == NULL ||
                /* This should yield a closure stored in THIS class, not one
                   that may be in a parent class. */
                (parent && closure_prop->id <= parent->prop_count)) {
                closure_prop = lily_add_class_property(emit->symtab, cls,
                    s->type, "*closure", 0);
            }

            lily_u16_write_5(emit->code, o_load_class_closure, f->line_num,
                    emit->block->self->reg_spot, closure_prop->id, s->reg_spot);
        }
        else {
            /* Lambdas don't get 'self' as their first argument: They instead
               need to pull it out of the closure.
               Lambdas do not need to write in a zap for their level of
               upvalues because they cannot be called by name twice. */
            lily_u16_write_4(emit->code, o_load_closure, f->line_num, 0,
                    s->reg_spot);

            lily_storage *lambda_self = emit->block->self;
            if (lambda_self) {
                lily_u16_write_4(emit->code, o_get_upvalue, *emit->lex_linenum,
                        closed_self_spot, lambda_self->reg_spot);
            }
        }
    }
    else {
        lily_u16_write_2(emit->code, o_load_closure, (uint16_t)f->line_num);
        write_closure_zap(emit);
        lily_u16_write_1(emit->code, s->reg_spot);
    }

    ensure_params_in_closure(emit);
    setup_transform_table(emit);

    if (emit->function_depth == 2)
        emit->closed_pos = 0;

    /* Closures create patches when they write o_create_function. Fix those
       patches with the spot of the closure (since they need to draw closure
       info but won't have it just yet). */
    if (emit->block->patch_start != emit->patches->pos)
        write_block_patches(emit, s->reg_spot);

    /* Since jumps reference absolute locations, they need to be adjusted
       for however much bytecode is written as a header. The
       transform - code_start is so that class closures are accounted for as
       well (since the o_new_instance is rewritten). */
    int starting_adjust = (lily_u16_pos(emit->code) - save_code_pos) +
            (transform_start - emit->block->code_start);
    transform_code(emit, f, transform_start, end, starting_adjust);
    *new_size = lily_u16_pos(emit->code) - *new_start;
}

/* This makes the function value that will be needed by the current code
   block. If the current function is a closure, then the appropriate transform
   is done to it. */
static lily_function_val *create_code_block_for(lily_emit_state *emit,
        lily_block *function_block)
{
    char *class_name;
    if (function_block->class_entry)
        class_name = function_block->class_entry->name;
    else
        class_name = NULL;

    lily_var *var = function_block->function_var;
    lily_function_val *f = lily_new_native_function_val(class_name,
            var->name);

    lily_tie_function(emit->symtab, var, f);

    int code_start, code_size;

    if (function_block->make_closure == 0) {
        code_start = emit->block->code_start;
        code_size = lily_u16_pos(emit->code) - emit->block->code_start;
    }
    else {
        closure_code_transform(emit, f, &code_start, &code_size);
    }

    uint16_t *code = lily_malloc((code_size + 1) * sizeof(uint16_t));
    memcpy(code, emit->code->data + code_start, sizeof(uint16_t) * code_size);

    f->code = code;
    return f;
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

/** Match is a weird block. It needs to be exhaustive, and it needs to be able
    to decompose variant values. There's plenty of other code that deals with
    enums and variants, but this focuses on match and decomposition. This isn't
    terrible, but it's not great either. **/

static void grow_match_cases(lily_emit_state *emit)
{
    emit->match_case_size *= 2;
    emit->match_cases = lily_realloc(emit->match_cases,
        sizeof(int) * emit->match_case_size);
}

/* This checks that the current match block (which is exiting) is exhaustive. IF
   it is not, then a SyntaxError is raised. */
static void ensure_proper_match_block(lily_emit_state *emit)
{
    lily_block *block = emit->block;
    int error = 0;
    lily_msgbuf *msgbuf = emit->raiser->aux_msgbuf;
    int i;
    lily_class *match_class = block->match_sym->type->cls;

    for (i = block->match_case_start;i < emit->match_case_pos;i++) {
        if (emit->match_cases[i] == 0) {
            if (error == 0) {
                lily_msgbuf_add(msgbuf,
                        "Match pattern not exhaustive. The following case(s) are missing:\n");
                error = 1;
            }

            lily_msgbuf_add_fmt(msgbuf, "* %s\n",
                    match_class->variant_members[i]->name);
        }
    }

    if (error)
        lily_raise(emit->raiser, lily_SyntaxError, msgbuf->message);
}

/* This writes a decomposition for a given variant type. As for the values, it
   pulls from recently-declared vars and assumes those vars should be the
   targets. */
void lily_emit_variant_decompose(lily_emit_state *emit, lily_type *variant_type)
{
    int value_count = variant_type->subtype_count - 1;
    int i;

    lily_u16_write_4(emit->code, o_variant_decompose, *emit->lex_linenum,
            emit->block->match_sym->reg_spot, value_count);

    /* Since this function is called immediately after declaring the last var
       that will receive the decompose, it's safe to pull the vars directly
       from symtab's var chain. */
    lily_var *var_iter = emit->symtab->active_module->var_chain;

    /* Go down because the vars are linked from newest -> oldest. If this isn't
       done, then the first var will get the last value in the variant, the
       second will get the next-to-last value, etc. */
    for (i = value_count - 1;i >= 0;i--) {
        lily_u16_write_1(emit->code, var_iter->reg_spot);
        var_iter = var_iter->next;
    }
}

/* This adds a match case to the current match block. 'pos' is the index of a
   valid variant within the current match enum type. This will hide the current
   block's vars. If 'pos' has been seen twice, then SyntaxError is raised. */
int lily_emit_add_match_case(lily_emit_state *emit, int pos)
{
    int block_offset = emit->block->match_case_start;
    int is_first_case = 1, ret = 1;
    int i;

    for (i = emit->block->match_case_start;
         i < emit->match_case_pos;
         i++) {
        if (emit->match_cases[i] == 1) {
            is_first_case = 0;
            break;
        }
    }

    if (emit->block->last_exit != lily_u16_pos(emit->code) &&
        is_first_case == 0)
        emit->block->all_branches_exit = 0;

    if (emit->match_cases[block_offset + pos] == 0) {
        emit->match_cases[block_offset + pos] = 1;

        /* Every case added after the first needs to write an exit jump before
           any code. This makes it so the previous branch jumps outside the
           match instead of falling through (very bad, in this case). */
        if (is_first_case == 0) {
            lily_u16_write_2(emit->code, o_jump, 0);

            lily_u16_write_1(emit->patches, lily_u16_pos(emit->code) - 1);
        }

        /* Patch the o_match_dispatch spot the corresponds with this class
           so that it will jump to the current location.
           Oh, and make sure to do it AFTER writing the jump, or the dispatch
           will go to the exit jump. */
        lily_u16_insert(emit->code, emit->block->match_code_start + pos,
                lily_u16_pos(emit->code) - emit->block->jump_offset);

        /* This is necessary to keep vars created from the decomposition of one
           class from showing up in subsequent cases. */
        lily_var *v = emit->block->var_start;
        if (v != emit->symtab->active_module->var_chain)
            lily_hide_block_vars(emit->symtab, v);
    }
    else
        ret = 0;

    return ret;
}

/* This evaluates the expression to be sent to 'match' The resulting value is
   checked for returning a value that is a valid enum. The match block's state
   is then prepared.
   The pool is cleared out for the next expression after this. */
void lily_emit_eval_match_expr(lily_emit_state *emit, lily_ast_pool *ap)
{
    lily_ast *ast = ap->root;
    lily_block *block = emit->block;
    eval_enforce_value(emit, ast, NULL, "Match expression has no value.\n");

    if ((ast->result->type->cls->flags & CLS_IS_ENUM) == 0 ||
        ast->result->type->cls->id == SYM_CLASS_DYNAMIC) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "Match expression is not an enum value.\n");
    }

    int match_cases_needed = ast->result->type->cls->variant_size;
    if (emit->match_case_pos + match_cases_needed > emit->match_case_size)
        grow_match_cases(emit);

    block->match_case_start = emit->match_case_pos;

    /* This is how the emitter knows that no cases have been given yet. */
    int i;
    for (i = 0;i < match_cases_needed;i++)
        emit->match_cases[emit->match_case_pos + i] = 0;

    emit->match_case_pos += match_cases_needed;

    block->match_code_start = lily_u16_pos(emit->code) + 4;
    block->match_sym = (lily_sym *)ast->result;

    lily_u16_write_prep(emit->code, 4 + match_cases_needed);

    lily_u16_write_4(emit->code, o_match_dispatch, *emit->lex_linenum,
            ast->result->reg_spot, match_cases_needed);

    for (i = 0;i < match_cases_needed;i++)
        lily_u16_write_1(emit->code, 0);

    RESET_POOL(ap)
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

/* Return a string representation of the given op. */
static const char *opname(lily_expr_op op)
{
    static const char *opnames[] =
    {"+", "-", "==", "<", "<=", ">", ">=", "!=", "%", "*", "/", "<<", ">>", "&",
     "|", "^", "!", "-", "&&", "||", "|>", "=", "+=", "-=", "%=", "*=", "/=",
     "<<=", ">>="};

    return opnames[op];
}

/* Check if 'type' is something that can be considered truthy/falsey.
   Keep this synced with the vm's o_jump_if calculation.
   Failure: SyntaxError is raised. */
static void ensure_valid_condition_type(lily_emit_state *emit, lily_type *type)
{
    int cls_id = type->cls->id;

    if (cls_id != SYM_CLASS_INTEGER &&
        cls_id != SYM_CLASS_DOUBLE &&
        cls_id != SYM_CLASS_STRING &&
        cls_id != SYM_CLASS_LIST &&
        cls_id != SYM_CLASS_BOOLEAN)
        lily_raise(emit->raiser, lily_SyntaxError,
                "^T is not a valid condition type.\n", type);
}

/* This checks to see if 'index_ast' has a type (and possibly, a value) that is
   a valid index for the type held by 'var_ast'.
   Failure: SyntaxError is raised. */
static void check_valid_subscript(lily_emit_state *emit, lily_ast *var_ast,
        lily_ast *index_ast)
{
    int var_cls_id = var_ast->result->type->cls->id;
    if (var_cls_id == SYM_CLASS_LIST || var_cls_id == SYM_CLASS_STRING) {
        if (index_ast->result->type->cls->id != SYM_CLASS_INTEGER)
            lily_raise_adjusted(emit->raiser, var_ast->line_num,
                    lily_SyntaxError, "%s index is not an integer.\n",
                    var_ast->result->type->cls->name);
    }
    else if (var_cls_id == SYM_CLASS_HASH) {
        lily_type *want_key = var_ast->result->type->subtypes[0];
        lily_type *have_key = index_ast->result->type;

        if (want_key != have_key) {
            lily_raise_adjusted(emit->raiser, var_ast->line_num, lily_SyntaxError,
                    "hash index should be type '^T', not type '^T'.\n",
                    want_key, have_key);
        }
    }
    else if (var_cls_id == SYM_CLASS_TUPLE) {
        if (index_ast->tree_type != tree_integer) {
            lily_raise_adjusted(emit->raiser, var_ast->line_num, lily_SyntaxError,
                    "tuple subscripts must be integer literals.\n", "");
        }

        int index_value = index_ast->backing_value;
        lily_type *var_type = var_ast->result->type;
        if (index_value < 0 || index_value >= var_type->subtype_count) {
            lily_raise_adjusted(emit->raiser, var_ast->line_num,
                    lily_SyntaxError, "Index %d is out of range for ^T.\n",
                    index_value, var_type);
        }
    }
    else {
        lily_raise_adjusted(emit->raiser, var_ast->line_num, lily_SyntaxError,
                "Cannot subscript type '^T'.\n",
                var_ast->result->type);
    }
}

/* This returns the result of subscripting 'type' with the value within
   'index_ast'. It should only be called once 'index_ast' has been validated
   with check_valid_subscript.
   This will not fail. */
static lily_type *get_subscript_result(lily_type *type, lily_ast *index_ast)
{
    lily_type *result;
    if (type->cls->id == SYM_CLASS_LIST)
        result = type->subtypes[0];
    else if (type->cls->id == SYM_CLASS_HASH)
        result = type->subtypes[1];
    else if (type->cls->id == SYM_CLASS_TUPLE) {
        /* check_valid_subscript ensures that this is safe. */
        int literal_index = index_ast->backing_value;
        result = type->subtypes[literal_index];
    }
    else if (type->cls->id == SYM_CLASS_STRING)
        result = type;
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
        lily_ast *first_arg, int line_num, int num_values, int reg_spot)
{
    int i;
    lily_ast *arg;
    lily_u16_write_prep(emit->code, 4 + num_values);

    lily_u16_write_3(emit->code, opcode, line_num, num_values);

    for (i = 0, arg = first_arg; arg != NULL; arg = arg->next_arg, i++)
        lily_u16_write_1(emit->code, arg->result->reg_spot);

    lily_u16_write_1(emit->code, reg_spot);
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
            lily_raise(emit->raiser, lily_SyntaxError,
                       "%s.%s is marked %s, and not available here.\n",
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
            if (result_type->cls->id == SYM_CLASS_HASH)
                result_type = result_type->subtypes[1];
            else if (result_type->cls->id == SYM_CLASS_TUPLE) {
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
            else if (result_type->cls->id == SYM_CLASS_LIST)
                result_type = result_type->subtypes[0];
            /* Strings don't allow for subscript assign, so don't bother
               checking for that here. */
        }
    }
    else if (ast->tree_type == tree_oo_access) {
        result_type = determine_left_type(emit, ast->arg_start);
        if (result_type != NULL) {
            char *oo_name = lily_membuf_get(emit->ast_membuf, ast->membuf_pos);
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
        lily_raise(emit->raiser, lily_SyntaxError,
                "Cannot nest an assignment within an expression.\n");
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
    lily_type *right_type;
    if (right->result->item_kind == ITEM_TYPE_TYPE)
        right_type = right->padded_variant_type;
    else
        right_type = right->result->type;

    if (want_type == right_type ||
        lily_ts_type_greater_eq(emit->ts, want_type, right_type))
        ret = 1;
    else
        ret = 0;

    return ret;
}

/* 'ast' is some tree that isn't complete (it's a variant). Attempt to finish it
   off. 'infer_type' may provide some extra inference information, or be NULL.
   Generics that don't have a type get the type Dynamic as a fallback. */
static void rebox_variant_to_enum(lily_emit_state *emit, lily_ast *ast,
        lily_type *infer_type)
{
    lily_type *storage_type = ast->padded_variant_type;

    if (storage_type->flags & TYPE_IS_INCOMPLETE) {
        lily_type *unify_type = lily_ts_unify(emit->ts, storage_type,
                infer_type);
        if (unify_type)
            storage_type = unify_type;
    }

    if (storage_type->flags & TYPE_IS_INCOMPLETE)
        storage_type = lily_tm_make_dynamicd_copy(emit->tm, storage_type);

    lily_storage *s = get_storage(emit, storage_type);

    lily_u16_insert(emit->code, ast->maybe_result_pos, s->reg_spot);
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
        lily_type *expect, lily_type *got, const char *context)
{
    lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
            "%s do not have a consistent type.\n"
            "Expected Type: ^T\n"
            "Received Type: ^T\n",
            context, expect, got);
}

static void bad_assign_error(lily_emit_state *emit, int line_num,
        lily_type *left_type, lily_type *right_type)
{
    lily_raise_adjusted(emit->raiser, line_num, lily_SyntaxError,
            "Cannot assign type '^T' to type '^T'.\n",
            right_type, left_type);
}

static void get_error_name(lily_emit_state *emit, lily_ast *ast,
        const char **class_name, const char **separator, const char **name)
{
    *class_name = "";
    *separator = "";

    if (ast->tree_type == tree_binary)
        ast = ast->right;
    else if (ast->tree_type != tree_variant)
        ast = ast->arg_start;

    int item_kind = ast->item->item_kind;

    /* Unfortunately, each of these kinds of things stores the name it holds at
       a different offset. Maybe this will change one day. */
    if (item_kind == ITEM_TYPE_VAR) {
        lily_var *v = ((lily_var *)ast->item);
        if (v->parent) {
            *class_name = v->parent->name;
            *separator = ".";
        }
        *name = v->name;
    }
    else if (item_kind == ITEM_TYPE_VARIANT)
        *name = ((lily_class *)ast->item)->name;
    else if (item_kind == ITEM_TYPE_PROPERTY) {
        lily_prop_entry *p = ((lily_prop_entry *)ast->item);
        *class_name = p->cls->name;
        *separator = ".";
        *name = p->name;
    }
    else
        *name = "(anonymous)";
}

/* This is called when the call state (more on that later) has an argument that
   does not work. This will raise a SyntaxError explaining the issue. */
static void bad_arg_error(lily_emit_state *emit, lily_emit_call_state *cs,
        lily_type *expected, lily_type *got)
{
    const char *class_name, *separator, *name;
    get_error_name(emit, cs->ast, &class_name, &separator, &name);

    lily_msgbuf *msgbuf = emit->raiser->aux_msgbuf;

    emit->raiser->line_adjust = cs->ast->line_num;

    /* Ensure that generics that did not get a valid value are replaced with the
       ? type (instead of NULL, which will cause a problem). */
    lily_ts_resolve_as_question(emit->ts);
    lily_type *question = emit->ts->question_class_type;

    /* These names are intentionally the same length and on separate lines so
       that slight naming issues become more apparent. */
    lily_msgbuf_add_fmt(msgbuf,
            "Argument #%d to %s%s%s is invalid:\n"
            "Expected Type: ^T\n"
            "Received Type: ^T\n",
            cs->arg_count + 1,
            class_name, separator, name,
            lily_ts_resolve_with(emit->ts, expected, question), got);

    lily_raise(emit->raiser, lily_SyntaxError, msgbuf->message);
}

/***
 *      __  __                _
 *     |  \/  | ___ _ __ ___ | |__   ___ _ __ ___
 *     | |\/| |/ _ \ '_ ` _ \| '_ \ / _ \ '__/ __|
 *     | |  | |  __/ | | | | | |_) |  __/ |  \__ \
 *     |_|  |_|\___|_| |_| |_|_.__/ \___|_|  |___/
 *
 */

static void eval_tree(lily_emit_state *, lily_ast *, lily_type *);
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
        maybe_close_over_class_self(emit);

    if (ast->arg_start->tree_type != tree_local_var)
        eval_tree(emit, ast->arg_start, NULL);


    lily_class *lookup_class = ast->arg_start->result->type->cls;
    /* This allows variant values to use enum methods. */
    if (lookup_class->flags & CLS_IS_VARIANT)
        lookup_class = lookup_class->parent;

    char *oo_name = lily_membuf_get(emit->ast_membuf, ast->membuf_pos);
    lily_item *item = lily_find_or_dl_member(emit->parser, lookup_class,
            oo_name);

    if (item == NULL)
        lily_raise(emit->raiser, lily_SyntaxError,
                "Class %s has no method or property named %s.\n",
                lookup_class->name, oo_name);
    else if (item->item_kind == ITEM_TYPE_PROPERTY &&
             ast->arg_start->tree_type == tree_self)
        lily_raise(emit->raiser, lily_SyntaxError,
                "Use @<name> to get/set properties, not self.<name>.\n");
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
    lily_u16_write_5(emit->code, o_get_property, ast->line_num,
            ast->arg_start->result->reg_spot, prop->id, result->reg_spot);

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
        lily_u16_write_4(emit->code, o_get_readonly, ast->line_num,
                ast->sym->reg_spot, result->reg_spot);
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
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Left side of %s is not assignable.\n", opname(ast->op));

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

    lily_u16_write_5(emit->code, o_set_property, ast->line_num,
            ast->left->arg_start->result->reg_spot, ast->left->property->id,
            rhs->reg_spot);

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
    int opcode;
    lily_class *lhs_class, *rhs_class;
    lily_storage *s;

    lhs_class = ast->left->result->type->cls;
    rhs_class = ast->right->result->type->cls;

    if (lhs_class->id <= SYM_CLASS_STRING &&
        rhs_class->id <= SYM_CLASS_STRING)
        opcode = generic_binop_table[ast->op][lhs_class->id][rhs_class->id];
    else if (ast->left->result->type == ast->right->result->type)
        if (ast->op == expr_eq_eq)
            opcode = o_is_equal;
        else if (ast->op == expr_not_eq)
            opcode = o_not_eq;
        else
            opcode = -1;
    else
        opcode = -1;

    if (opcode == -1)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                   "Invalid operation: ^T %s ^T.\n", ast->left->result->type,
                   opname(ast->op), ast->right->result->type);

    lily_class *storage_class;
    switch (ast->op) {
        case expr_plus:
        case expr_minus:
        case expr_multiply:
        case expr_divide:
            if (lhs_class->id >= rhs_class->id)
                storage_class = lhs_class;
            else
                storage_class = rhs_class;
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

    s = get_storage(emit, storage_class->type);
    s->flags |= SYM_NOT_ASSIGNABLE;

    lily_u16_write_5(emit->code, opcode, ast->line_num,
            ast->left->result->reg_spot, ast->right->result->reg_spot,
            s->reg_spot);

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
    else {
        lily_raise(emit->raiser, lily_SyntaxError, "Invalid compound op: %s.\n",
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
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Left side of %s is not assignable.\n", opname(ast->op));
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
        if (left_cls_id == SYM_CLASS_INTEGER ||
            left_cls_id == SYM_CLASS_DOUBLE)
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
        int pos;
        /* Most trees dump their result at the end, so that patching is easy.
           Those that don't will write down where it should go. */
        if (ast->right->maybe_result_pos == 0)
            pos = lily_u16_pos(emit->code) - 1;
        else
            pos = ast->right->maybe_result_pos;

        lily_u16_insert(emit->code, pos, left_sym->reg_spot);
    }
    else {
        lily_u16_write_4(emit->code, opcode, ast->line_num, right_sym->reg_spot,
                left_sym->reg_spot);
    }
    ast->result = right_sym;
}

/* This handles ```@<name>```. Properties, unlike member access, are validated
   at parse-time. */
static void eval_property(lily_emit_state *emit, lily_ast *ast)
{
    ensure_valid_scope(emit, ast->sym);
    if (emit->function_block->block_type == block_lambda)
        maybe_close_over_class_self(emit);

    if (ast->property->type == NULL)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Invalid use of uninitialized property '@%s'.\n",
                ast->property->name);

    lily_storage *result = get_storage(emit, ast->property->type);

    lily_u16_write_5(emit->code, o_get_property, ast->line_num,
            emit->block->self->reg_spot, ast->property->id, result->reg_spot);

    ast->result = (lily_sym *)result;
}

/* This handles assignments to a property. It's similar in spirit to oo assign,
   but not as complicated. */
static void eval_property_assign(lily_emit_state *emit, lily_ast *ast)
{
    if (emit->function_block->block_type == block_lambda)
        maybe_close_over_class_self(emit);

    ensure_valid_scope(emit, ast->left->sym);
    lily_type *left_type = ast->left->property->type;
    lily_sym *rhs;

    eval_tree(emit, ast->right, left_type);

    lily_type *right_type = ast->right->result->type;
    /* For 'var @<name> = ...', fix the type of the property. */
    if (left_type == NULL) {
        ast->left->property->type = right_type;
        ast->left->property->flags &= ~SYM_NOT_INITIALIZED;
        left_type = right_type;
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

    lily_u16_write_5(emit->code, o_set_property, ast->line_num,
            emit->block->self->reg_spot,
            ast->left->property->id, rhs->reg_spot);

    ast->result = rhs;
}

void eval_upvalue(lily_emit_state *emit, lily_ast *ast)
{
    lily_sym *sym = ast->sym;

    int i;
    for (i = 0;i < emit->closed_pos;i++)
        if (emit->closed_syms[i] == sym)
            break;

    if (i == emit->closed_pos)
        checked_close_over_var(emit, (lily_var *)ast->sym);

    emit->function_block->make_closure = 1;

    lily_storage *s = get_storage(emit, sym->type);
    lily_u16_write_4(emit->code, o_get_upvalue, ast->line_num, i, s->reg_spot);
    ast->result = (lily_sym *)s;
}

static void emit_literal(lily_emit_state *, lily_ast *);

/* This evaluates an interpolation block `$"..."`. The children of this tree are
   divided into either tree_literal or tree_interp_block. The former does not
   need to be evaluated. The latter */
static void eval_interpolation(lily_emit_state *emit, lily_ast *ast)
{
    lily_ast *tree_iter = ast->arg_start;
    while (tree_iter) {
        if (tree_iter->tree_type == tree_interp_block) {
            char *interp_body = lily_membuf_get(emit->ast_membuf,
                    tree_iter->membuf_pos);
            lily_sym *result = lily_parser_interp_eval(emit->parser,
                    ast->line_num, interp_body);
            if (result == NULL)
                lily_raise_adjusted(emit->raiser, tree_iter->line_num,
                        lily_SyntaxError,
                        "Interpolation expression does not yield a value.\n",
                        "");

            tree_iter->result = result;
        }
        else
            emit_literal(emit, tree_iter);

        tree_iter = tree_iter->next_arg;
    }

    lily_u16_write_3(emit->code, o_interpolation, ast->line_num,
            ast->args_collected);
    lily_u16_write_prep(emit->code, ast->args_collected + 1);
    int i;
    lily_ast *arg = ast->arg_start;
    for (i = 0, arg = ast->arg_start; arg != NULL; arg = arg->next_arg, i++)
        lily_u16_write_1(emit->code, arg->result->reg_spot);

    lily_storage *s = get_storage(emit, emit->symtab->string_class->type);
    lily_u16_write_1(emit->code, s->reg_spot);

    ast->result = (lily_sym *)s;
}

/* This evaluates a lambda. The parser sent the lambda over as a blob of text
   since it didn't know what the types were. Now that the types are known, pass
   it back to the parser to, umm, parse. */
static void eval_lambda(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    int save_expr_num = emit->expr_num;
    char *lambda_body = lily_membuf_get(emit->ast_membuf, ast->membuf_pos);

    if (expect && expect->cls->id != SYM_CLASS_FUNCTION)
        expect = NULL;

    lily_sym *lambda_result = (lily_sym *)lily_parser_lambda_eval(emit->parser,
            ast->line_num, lambda_body, expect);

    /* Lambdas may run 1+ expressions. Restoring the expression count to what it
       was prevents grabbing expressions that are currently in use. */
    emit->expr_num = save_expr_num;

    lily_storage *s = get_storage(emit, lambda_result->type);

    if (emit->function_block->make_closure == 0)
        lily_u16_write_4(emit->code, o_get_readonly, ast->line_num,
                lambda_result->reg_spot, s->reg_spot);
    else
        emit_create_function(emit, lambda_result, s);

    ast->result = (lily_sym *)s;
}

/* This handles assignments to things that are marked as upvalues. */
static void eval_upvalue_assign(lily_emit_state *emit, lily_ast *ast)
{
    eval_tree(emit, ast->right, NULL);

    lily_sym *left_sym = ast->left->sym;
    int spot = find_closed_sym_spot(emit, left_sym);
    if (spot == -1) {
        checked_close_over_var(emit, (lily_var *)left_sym);
        spot = emit->closed_pos - 1;
    }

    lily_sym *rhs = ast->right->result;

    if (ast->op > expr_assign) {
        lily_storage *s = get_storage(emit, ast->left->sym->type);
        lily_u16_write_4(emit->code, o_get_upvalue, ast->line_num, spot,
                s->reg_spot);
        ast->left->result = (lily_sym *)s;
        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    lily_u16_write_4(emit->code, o_set_upvalue, ast->line_num, spot,
            rhs->reg_spot);

    ast->result = ast->right->result;
}

/* This takes care of binary || and &&. */
static void eval_logical_op(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *result;
    int is_top, jump_on;

    jump_on = (ast->op == expr_logical_or);

    /* The top-most and/or creates an ANDOR block so that all of the jumps that
       get written can be properly folded. */
    if (ast->parent == NULL ||
        (ast->parent->tree_type != tree_binary || ast->parent->op != ast->op)) {
        is_top = 1;
        lily_emit_enter_block(emit, block_andor);
    }
    else
        is_top = 0;

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

    if (is_top == 1) {
        int save_pos;
        lily_symtab *symtab = emit->symtab;

        result = get_storage(emit, symtab->integer_class->type);

        int truthy = (ast->op == expr_logical_and);

        lily_u16_write_4(emit->code, o_get_integer, ast->line_num, truthy,
                result->reg_spot);

        lily_u16_write_2(emit->code, o_jump, 0);
        save_pos = lily_u16_pos(emit->code) - 1;

        lily_emit_leave_block(emit);
        lily_u16_write_4(emit->code, o_get_integer, ast->line_num, !truthy,
                result->reg_spot);

        lily_u16_insert(emit->code, save_pos, lily_u16_pos(emit->code)
                - emit->block->jump_offset);
        ast->result = (lily_sym *)result;
    }
    else
        /* If is_top is false, then this tree has a parent that's binary and
           has the same op. The parent won't write a jump_if for this tree,
           because that would be a double-test.
           Setting this to NULL anyway as a precaution. */
        ast->result = NULL;
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
    type_for_result = get_subscript_result(var_ast->result->type, index_ast);

    lily_storage *result = get_storage(emit, type_for_result);

    lily_u16_write_5(emit->code, o_get_item, ast->line_num,
            var_ast->result->reg_spot, index_ast->result->reg_spot,
            result->reg_spot);

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
                    lily_SyntaxError,
                    "Left side of %s is not assignable.\n", opname(ast->op));
        }
    }

    if (index_ast->tree_type != tree_local_var)
        eval_tree(emit, index_ast, NULL);

    check_valid_subscript(emit, var_ast, index_ast);
    if (var_ast->result->type->cls->id == SYM_CLASS_STRING)
        lily_raise(emit->raiser, lily_SyntaxError,
                "Subscript assign not allowed on type string.\n");

    elem_type = get_subscript_result(var_ast->result->type, index_ast);

    if (type_matchup(emit, elem_type, ast->right) == 0) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, elem_type, rhs->type);
    }

    rhs = ast->right->result;

    if (ast->op > expr_assign) {
        /* For a compound assignment to work, the left side must be subscripted
           to get the value held. */

        lily_storage *subs_storage = get_storage(emit, elem_type);

        lily_u16_write_5(emit->code, o_get_item, ast->line_num,
                var_ast->result->reg_spot, index_ast->result->reg_spot,
                subs_storage->reg_spot);

        ast->left->result = (lily_sym *)subs_storage;

        /* Run the compound op now that ->left is set properly. */
        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    lily_u16_write_5(emit->code, o_set_item, ast->line_num,
            var_ast->result->reg_spot, index_ast->result->reg_spot,
            rhs->reg_spot);

    ast->result = rhs;
}

static void eval_typecast(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *boxed_type = ast->arg_start->next_arg->type;
    lily_ast *right_tree = ast->arg_start;
    lily_type *cast_type = boxed_type->subtypes[0];

    eval_tree(emit, right_tree, cast_type);

    lily_type *var_type = right_tree->result->type;

    if (var_type->cls->id == SYM_CLASS_DYNAMIC) {
        /* Lily's emitter is designed so that it has full type information.
           However, the vm only knows about classes. Because of that, casts that
           use subtyping need to be forbidden. */
        if (cast_type->cls->generic_count != 0)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Casts from Dynamic cannot include subtypes.\n");

        lily_storage *result = get_storage(emit, boxed_type);

        lily_u16_write_5(emit->code, o_dynamic_cast, ast->line_num,
                cast_type->cls->id, right_tree->result->reg_spot,
                result->reg_spot);
        ast->result = (lily_sym *)result;
    }
    else
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Cannot cast type '^T' to type '^T'.\n", var_type, cast_type);
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
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Invalid operation: %s%s.\n",
                opname(ast->op), lhs_class->name);

    storage = get_storage(emit, lhs_class->type);
    storage->flags |= SYM_NOT_ASSIGNABLE;

    lily_u16_write_4(emit->code, opcode, ast->line_num,
            ast->left->result->reg_spot, storage->reg_spot);

    ast->result = (lily_sym *)storage;
}

/* This handles building tuples ```<[1, "2", 3.3]>```. Tuples are structures
   that allow varying types, but with a fixed size. */
static void eval_build_tuple(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    if (ast->args_collected == 0) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "Cannot create an empty tuple.\n");
    }

    if (expect != NULL &&
        (expect->cls->id != SYM_CLASS_TUPLE ||
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
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

static void emit_literal(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *s = get_storage(emit, ast->type);

    lily_u16_write_4(emit->code, o_get_readonly, ast->line_num,
            ast->literal_reg_spot, s->reg_spot);

    ast->result = (lily_sym *)s;
}

/* This handles globals and static functions. */
static void emit_nonlocal_var(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *ret;
    int opcode;

    switch (ast->tree_type) {
        case tree_global_var:
            opcode = o_get_global;
            break;
        case tree_static_func:
            ensure_valid_scope(emit, ast->sym);
        default:
            opcode = o_get_readonly;
            break;
    }

    ret = get_storage(emit, ast->sym->type);

    if (opcode != o_get_global)
        ret->flags |= SYM_NOT_ASSIGNABLE;

    if ((ast->sym->flags & VAR_NEEDS_CLOSURE) == 0)
        lily_u16_write_4(emit->code, opcode, ast->line_num, ast->sym->reg_spot,
                ret->reg_spot);
    else
        emit_create_function(emit, ast->sym, ret);

    ast->result = (lily_sym *)ret;
}

static void emit_integer(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *s = get_storage(emit, emit->symtab->integer_class->type);

    lily_u16_write_4(emit->code, o_get_integer, ast->line_num,
            ast->backing_value, s->reg_spot);

    ast->result = (lily_sym *)s;
}

static void emit_boolean(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *s = get_storage(emit, emit->symtab->boolean_class->type);

    lily_u16_write_4(emit->code, o_get_boolean, ast->line_num,
            ast->backing_value, s->reg_spot);

    ast->result = (lily_sym *)s;
}

void eval_self(lily_emit_state *emit, lily_ast *ast)
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

static lily_type *partial_eval(lily_emit_state *, lily_ast *, lily_type *,
        uint16_t *);

/** The eval for these two is broken away from the others because it is fairly
    difficult. The bulk of the problem comes with trying to find a 'bottom type'
    of all the values that were entered. This process is termed 'unification',
    and it isn't easy here.

    Unification is currently done after lists and hashes have evaluated all of
    their members. This is unfortunate, because it means that certain cases will
    not work as well as they could. Another problem with the current unification
    is that it only works for enums and variants. Anything else gets sent
    straight to type Dynamic. It's unfortunate. **/

/* This is called when a static list or hash has one or more variant values. It
   walks over each value and determines if the value is a variant. Now that the
   type that each variant will take is known, the variants can be finalized. */
static void rebox_enum_variant_values(lily_emit_state *emit, lily_ast *ast,
        lily_type *rebox_type, int is_hash)
{
    lily_ast *tree_iter = ast->arg_start;
    if (is_hash)
        tree_iter = tree_iter->next_arg;

    while (tree_iter != NULL) {
        if (tree_iter->result->item_kind == ITEM_TYPE_TYPE)
            rebox_variant_to_enum(emit, tree_iter, rebox_type);

        tree_iter = tree_iter->next_arg;
        if (is_hash && tree_iter)
            tree_iter = tree_iter->next_arg;
    }
}

/* Make sure that 'key_type' is a valid key. It may be NULL or ? depending on
   inference. If 'key_type' is not suitable to be a hash key, then raise a
   syntax error. */
static void ensure_valid_key_type(lily_emit_state *emit, lily_ast *ast,
        lily_type *key_type)
{
    if (key_type == NULL || key_type->cls->id == SYM_CLASS_QUESTION)
        key_type = emit->symtab->dynamic_class->type;

    if (key_type == NULL || (key_type->cls->flags & CLS_VALID_HASH_KEY) == 0)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Type '^T' is not a valid hash key.\n", key_type);
}

/* Build an empty something. It's an empty hash only if the caller wanted a
   hash. In any other case, it becomes an empty list. Use Dynamic as a default
   where it's needed. The purpose of this function is to make it so list and
   hash build do not need to worry about missing information. */
static void make_empty_list_or_hash(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_type *dynamic_type = emit->symtab->dynamic_class->type;
    lily_class *cls;
    int num, op;

    if (expect && expect->cls->id == SYM_CLASS_HASH) {
        lily_type *key_type = expect->subtypes[0];
        lily_type *value_type = expect->subtypes[1];
        ensure_valid_key_type(emit, ast, key_type);

        if (value_type == NULL || value_type->cls->id == SYM_CLASS_QUESTION)
            value_type = dynamic_type;

        lily_tm_add(emit->tm, key_type);
        lily_tm_add(emit->tm, value_type);

        cls = emit->symtab->hash_class;
        op = o_build_hash;
        num = 2;
    }
    else {
        lily_type *elem_type;
        if (expect && expect->cls->id == SYM_CLASS_LIST &&
            expect->subtypes[0]->cls->id != SYM_CLASS_QUESTION) {
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
    write_build_op(emit, op, ast->arg_start, ast->line_num, 0, s->reg_spot);
    ast->result = (lily_sym *)s;
}

static void eval_build_hash(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_ast *tree_iter;

    lily_type *key_type, *question_type, *value_type;
    question_type = emit->symtab->question_class->type;
    uint16_t found_variant_or_enum = 0;

    if (expect && expect->cls->id == SYM_CLASS_HASH) {
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

        lily_type *partial_type, *unify_type;

        partial_type = partial_eval(emit, key_tree, key_type,
                &found_variant_or_enum);

        unify_type = lily_ts_unify(emit->ts, key_type, partial_type);
        if (unify_type == NULL)
            inconsistent_type_error(emit, key_tree, key_type, partial_type,
                    "Hash keys");
        else {
            ensure_valid_key_type(emit, ast, unify_type);
            key_type = unify_type;
        }

        partial_type = partial_eval(emit, value_tree, value_type,
                &found_variant_or_enum);
        unify_type = lily_ts_unify(emit->ts, value_type, partial_type);
        if (unify_type == NULL)
            inconsistent_type_error(emit, value_tree, value_type, partial_type,
                    "Hash values");
        else
            value_type = unify_type;
    }

    if (found_variant_or_enum) {
        if (value_type->flags & TYPE_IS_INCOMPLETE)
            value_type = lily_tm_make_dynamicd_copy(emit->tm, value_type);

        rebox_enum_variant_values(emit, ast, value_type, 1);
    }

    lily_class *hash_cls = emit->symtab->hash_class;
    lily_tm_add(emit->tm, key_type);
    lily_tm_add(emit->tm, value_type);
    lily_type *new_type = lily_tm_make(emit->tm, 0, hash_cls, 2);

    lily_storage *s = get_storage(emit, new_type);

    write_build_op(emit, o_build_hash, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
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
    uint16_t found_variant = 0;

    if (expect && expect->cls->id == SYM_CLASS_LIST)
        elem_type = expect->subtypes[0];

    if (elem_type == NULL)
        elem_type = emit->ts->question_class_type;

    for (arg = ast->arg_start;arg != NULL;arg = arg->next_arg) {
        lily_type *unify_type = partial_eval(emit, arg, elem_type, &found_variant);

        lily_type *new_elem_type = lily_ts_unify(emit->ts, elem_type, unify_type);
        if (new_elem_type == NULL)
            inconsistent_type_error(emit, arg, elem_type, unify_type, "List elements");

        elem_type = new_elem_type;
    }

    if (found_variant) {
        if (elem_type->flags & TYPE_IS_INCOMPLETE)
            elem_type = lily_tm_make_dynamicd_copy(emit->tm, elem_type);

        rebox_enum_variant_values(emit, ast, elem_type, 0);
    }

    lily_tm_add(emit->tm, elem_type);
    lily_type *new_type = lily_tm_make(emit->tm, 0, emit->symtab->list_class,
            1);

    lily_storage *s = get_storage(emit, new_type);

    write_build_op(emit, o_build_list, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
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

static void eval_variant(lily_emit_state *emit, lily_ast *, lily_type *);

/** Lily allows the following kinds of calls:

    * x()
    * x.y()
    * @y
    * x[0]()
    * x()()
    * Some(10)
    * {|| 10} ()
    * [1, 2, 3].y()

    Most of the above are allowed to have default arguments. All of them support
    varargs. This flexibility is great to have, but it's tough to implement
    right. Below are a handful of functions that do the backbone of calls. A big
    part of this is type_matchup, which is listed far above.

    To handle the different needs of calls, the emitter provides call states. A
    call state holds everything that one might need to know about a call: Where
    to get error information, the current arg number, if there are variants to
    be promoted, and more. Calls begin by using, naturally, begin_call.

    A big problem right now with calls is checking arguments. Calls put the
    target as the first tree and count it as an argument. The target may or may
    not be part of the value (ex: Yes for x.y(), No for x()). For calls like
    x.y(), the 'x' should always be first. Plain calls within a class should get
    self instead first.

    There's also call piping which adds a value that isn't associated with a
    child tree. If there's no value to claim the first spot, then the pipe
    claims it. Otherwise, the pipe gets the second spot. But the pipe doesn't go
    to a call (ex: x |> y is y(x), but x |> y() is y()(x)).
    **/

static void add_call_state(lily_emit_state *emit)
{
    lily_emit_call_state *new_state = lily_malloc(sizeof(lily_emit_call_state));

    if (emit->call_state != NULL)
        emit->call_state->next = new_state;

    new_state->prev = emit->call_state;
    new_state->next = NULL;
    new_state->item = NULL;
    new_state->call_type = NULL;

    emit->call_state = new_state;
}

static void grow_call_values(lily_emit_state *emit)
{
    emit->call_values_size *= 2;
    emit->call_values = lily_realloc(emit->call_values,
            sizeof(lily_sym *) * emit->call_values_size);
}

static void add_value(lily_emit_state *emit, lily_emit_call_state *cs,
        lily_sym *sym)
{
    if (emit->call_values_pos == emit->call_values_size)
        grow_call_values(emit);

    emit->call_values[emit->call_values_pos] = sym;
    emit->call_values_pos++;
    cs->arg_count++;
}

static lily_type *get_expected_type(lily_emit_call_state *cs, int pos)
{
    lily_type *result;
    if (cs->vararg_start > (pos + 1)) {
        /* The + 1 is because the return type of a function is the first subtype
           inside of it. */
        result = cs->call_type->subtypes[pos + 1];
        if (result->cls->id == SYM_CLASS_OPTARG)
            result = result->subtypes[0];
    }
    else {
        /* There's no check for optarg here because there's no such thing as
           varargs with optional values. */
        result = cs->vararg_elem_type;
    }

    return result;
}

static void write_call_values(lily_emit_state *emit, lily_emit_call_state *cs,
        uint16_t from)
{
    int offset = (emit->call_values_pos - cs->arg_count) + from;
    int count = cs->arg_count - from;
    int i;

    for (i = 0;i < count;i++)
        lily_u16_write_1(emit->code, emit->call_values[offset + i]->reg_spot);
}

static void write_varargs(lily_emit_state *emit, lily_emit_call_state *cs,
        lily_type *type, uint16_t from)
{
    lily_storage *s = get_storage(emit, type);
    int count = cs->arg_count - from;

    lily_u16_write_3(emit->code, o_build_list, cs->ast->line_num, count);
    write_call_values(emit, cs, from);
    lily_u16_write_1(emit->code, s->reg_spot);

    /* The individual extra values are gone now... */
    emit->call_values_pos -= count;
    cs->arg_count -= count;

    add_value(emit, cs, (lily_sym *)s);
}

static void write_build_enum(lily_emit_state *emit, lily_emit_call_state *cs,
        lily_variant_class *variant_cls)
{
    lily_u16_write_5(emit->code, o_build_enum, cs->ast->line_num,
            variant_cls->parent->id, variant_cls->variant_id, cs->arg_count);
    write_call_values(emit, cs, 0);
    lily_u16_write_1(emit->code, 0);
}

/* This evaluates a call argument and checks that the type is what is wanted or
   equivalent to what's expected. */
static void eval_call_arg(lily_emit_state *emit, lily_emit_call_state *cs,
        lily_ast *arg)
{
    lily_type *want_type = get_expected_type(cs, cs->arg_count);

    if (want_type->cls->id == SYM_CLASS_OPTARG)
        want_type = want_type->subtypes[0];

    lily_type *eval_type = want_type;
    if (eval_type->flags & TYPE_IS_UNRESOLVED) {
        eval_type = lily_ts_resolve_with(emit->ts, want_type,
                emit->ts->question_class_type);
    }

    lily_type *result_type = partial_eval(emit, arg, eval_type,
            &cs->have_bare_variants);

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
        lily_ts_check(emit->ts, result_type, solved_want);
        lily_type *solved_result = lily_ts_resolve_with(emit->ts, result_type,
                question_type);
        lily_ts_scope_restore(emit->ts, &p);
        /* Don't assume it succeeded, because it worsens the error message in
           the case that it didn't. */
        if (solved_want == solved_result)
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
         type_matchup(emit, want_type, arg)))
        add_value(emit, cs, arg->result);
    else
        bad_arg_error(emit, cs, want_type, result_type);
}

static void box_variant_at(lily_emit_state *emit, lily_emit_call_state *cs,
        lily_ast *ast, int where)
{
    int offset = emit->call_values_pos - cs->arg_count;
    lily_type *enum_type = lily_ts_resolve(emit->ts,
            get_expected_type(cs, where));
    rebox_variant_to_enum(emit, ast, enum_type);
    emit->call_values[offset + where] = ast->result;
}

/* This is called after call arguments have been evaluated. It reboxes any
   variant into an enum using the generic information known. */
static void box_call_variants(lily_emit_state *emit, lily_emit_call_state *cs)
{
    int arg_num = 0;
    lily_ast *tree_iter = cs->ast->arg_start;

    if (tree_iter->tree_type == tree_oo_access) {
        /* The first tree will yield a value unless it's a variant. */
        if (tree_iter->arg_start->result->item_kind == ITEM_TYPE_TYPE)
            box_variant_at(emit, cs, tree_iter->arg_start, 0);

        arg_num++;
    }
    else if (tree_iter->tree_type == tree_method)
        /* This causes self to be injected, and self is never a variant. */
        arg_num++;

    /* The first tree has been handled or is uninteresting. Skip it. */
    tree_iter = tree_iter->next_arg;

    for (;tree_iter;tree_iter = tree_iter->next_arg, arg_num++)
        if (tree_iter->result->item_kind == ITEM_TYPE_TYPE)
            box_variant_at(emit, cs, tree_iter, arg_num);
}

static void get_func_min_max(lily_type *call_type, unsigned int *min,
        unsigned int *max)
{
    *min = call_type->subtype_count - 1;
    *max = *min;

    /* For now, it's currently not possible to have a function that has optional
       arguments and variable arguments too. */
    if (call_type->flags & TYPE_HAS_OPTARGS) {
        int i;
        for (i = 1;i < call_type->subtype_count;i++) {
            if (call_type->subtypes[i]->cls->id == SYM_CLASS_OPTARG)
                break;
        }
        *min = i - 1;
    }
    else if (call_type->flags & TYPE_IS_VARARGS) {
        *max = (unsigned int)-1;
        *min = *min - 1;
    }
}

/* Make sure that the function being called has the right number of
   arguments. SyntaxError is raised if the count is wrong. */
static void verify_argument_count(lily_emit_state *emit, lily_ast *target,
        lily_type *call_type, int num_args)
{
    /* unsignedness is intentional: It causes -1 to be whatever the signed max
       is without using limits.h. */
    unsigned int min, max;
    get_func_min_max(call_type, &min, &max);

    if (num_args == -1 || num_args < min || num_args > max) {
        /* I'd like the error message to be done all at once, instead of one
           piece at a time. Here are the possibilites:
           (# for n)
           (# for n+)
           (# for n..m) */
        const char *class_name, *separator, *name, *div_str = "";
        char arg_str[8], min_str[8] = "", max_str[8] = "";

        if (num_args == -1)
            strncpy(arg_str, "none", sizeof(arg_str));
        else
            snprintf(arg_str, sizeof(arg_str), "%d", num_args);

        snprintf(min_str, sizeof(min_str), "%d", min);

        if (min == max)
            div_str = "";
        else if (max == -1)
            div_str = "+";
        else {
            div_str = "..";
            snprintf(max_str, sizeof(max_str), "%d", max);
        }

        get_error_name(emit, target, &class_name, &separator, &name);

        lily_raise_adjusted(emit->raiser, target->line_num, lily_SyntaxError,
                "Wrong number of arguments to %s%s%s (%s for %s%s%s).\n",
                class_name, separator, name, arg_str,
                min_str, div_str, max_str);
    }
}

/* This is called when the first tree of a call implies a starting value. This
   pushes the appropriate value. */
static void push_first_tree_value(lily_emit_state *emit,
        lily_emit_call_state *cs)
{
    lily_ast *ast = cs->ast;
    lily_tree_type call_tt = ast->arg_start->tree_type;
    lily_type *push_type;
    lily_sym *push_value;

    if (call_tt == tree_method) {
        push_type = emit->block->self->type;
        push_value = (lily_sym *)emit->block->self;
    }
    else {
        lily_ast *arg = ast->arg_start->arg_start;
        push_value = arg->result;
        push_type = arg->result->type;
        if (push_type->cls->flags & CLS_IS_VARIANT)
            cs->have_bare_variants = 1;
    }

    lily_type *expect = get_expected_type(cs, 0);
    /* This will almost always succeed. But occasionally (like with File.open),
       the first argument is not self. So make sure to check it. */
    if (lily_ts_check(emit->ts, expect, push_type) == 1)
        add_value(emit, cs, push_value);
    else
        bad_arg_error(emit, cs, expect, push_type);
}

/* This will make sure the call is sound, and add some starting type
   information. It is then possible to run the call. */
static void validate_and_prep_call(lily_emit_state *emit,
        lily_emit_call_state *cs, lily_type *expect, int num_args)
{
    /* NOTE: This works for both calls and func pipe because the arg_start and
       right fields of lily_ast are in a union together. */
    lily_tree_type first_tt = cs->ast->arg_start->tree_type;
    /* The first tree is counted as an argument. However, most trees don't
       actually add the first argument. In fact, only two will:
       tree_method will inject self as a first argument.
       tree_oo_access will inject the left of the dot (a.x() adds 'a'). */
    int count_first = (first_tt == tree_oo_access || first_tt == tree_method);

    verify_argument_count(emit, cs->ast, cs->call_type, num_args + count_first);

    if (count_first)
        push_first_tree_value(emit, cs);

    if (cs->call_type->flags & TYPE_IS_UNRESOLVED) {
        if (first_tt == tree_local_var || first_tt == tree_upvalue ||
            first_tt == tree_inherited_new)
            /* This forces generic types to be solved as themselves.
               For the first two cases, this is about correctness. When inside
               of a generic function, the generics are quantified but as some
               unknown type. So allowing them to be solved allows wrong code. To
               do otherwise requires (at least) rank 2 polymorphism, which does
               not exist in Lily (YET).

               For the last case, solving generics as themselves forces the A
               of a class to be in the same position regardless of how much it's
               inherited and extended. That makes solving for types easier,
               because A is always strictly A. */
            lily_ts_check(emit->ts, cs->call_type, cs->call_type);
        else {
            lily_type *call_result = cs->call_type->subtypes[0];
            if (call_result && expect) {
                /* If the caller wants something and the result is that same
                   sort of thing, then fill in info based on what the caller
                   wants. */
                if (expect->cls->id == call_result->cls->id) {
                    /* The return isn't checked because there will be a more
                       accurate problem that is likely to manifest later. */
                    lily_ts_check(emit->ts, call_result, expect);
                }
            }
        }
    }
}

/* This actually does the evaluating for calls. */
static void eval_verify_call_args(lily_emit_state *emit, lily_emit_call_state *cs,
        lily_type *expect)
{
    lily_ast *ast = cs->ast;

    validate_and_prep_call(emit, cs, expect, ast->args_collected - 1);

    lily_ast *arg;
    for (arg = ast->arg_start->next_arg;arg != NULL;arg = arg->next_arg)
        eval_call_arg(emit, cs, arg);

    /* All arguments have been collected and run. If there are any incomplete
       solutions to a generic (ex: Option[?]), then default those incomplete
       inner types to Dynamic. Incomplete toplevel types (just ?) are left
       alone. */
    lily_ts_default_incomplete_solves(emit->ts);

    if (cs->have_bare_variants)
        box_call_variants(emit, cs);

    if (cs->call_type->flags & TYPE_IS_VARARGS) {
        int va_pos = cs->call_type->subtype_count - 1;
        lily_type *vararg_type = cs->call_type->subtypes[va_pos];
        if (vararg_type->flags & TYPE_IS_UNRESOLVED)
            vararg_type = lily_ts_resolve(emit->ts, vararg_type);

        write_varargs(emit, cs, vararg_type, cs->call_type->subtype_count - 2);
    }
}

/* This grabs and prepares a new call state. Part of this involves figuring out
   the type of the first tree (possibly evaluating it too). */
static lily_emit_call_state *begin_call(lily_emit_state *emit,
        lily_ast *ast)
{
    lily_emit_call_state *result = emit->call_state;
    if (result->next == NULL)
        add_call_state(emit);

    emit->call_state = result->next;
    result->ast = ast;
    result->arg_count = 0;
    result->have_bare_variants = 0;

    lily_ast *first_tree = ast->arg_start;
    lily_tree_type first_tt = first_tree->tree_type;
    lily_item *call_item = NULL;
    lily_type *call_type = NULL;

    if (first_tt == tree_defined_func ||
        first_tt == tree_inherited_new ||
        first_tt == tree_method) {
        call_item = ast->arg_start->item;
        if (call_item->flags & VAR_NEEDS_CLOSURE) {
            lily_storage *s = get_storage(emit, ast->arg_start->sym->type);
            emit_create_function(emit, ast->arg_start->sym, s);
            call_item = (lily_item *)s;
        }
    }
    else if (first_tt == tree_static_func) {
        ensure_valid_scope(emit, ast->arg_start->sym);
        call_item = ast->arg_start->item;
    }
    else if (first_tt == tree_oo_access) {
        eval_oo_access_for_item(emit, ast->arg_start);
        if (first_tree->item->item_kind == ITEM_TYPE_PROPERTY) {
            oo_property_read(emit, first_tree);
            call_item = (lily_item *)first_tree->result;
        }
        else
            call_item = first_tree->item;
    }
    else if (first_tt != tree_variant) {
        eval_tree(emit, ast->arg_start, NULL);
        call_item = (lily_item *)ast->arg_start->result;
    }
    else {
        call_item = (lily_item *)ast->arg_start->variant;
        call_type = ast->arg_start->variant->build_type;
    }

    if (call_type == NULL)
        call_type = ((lily_sym *)call_item)->type;

    if (call_type->cls->id != SYM_CLASS_FUNCTION &&
        first_tt != tree_variant)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Cannot anonymously call resulting type '^T'.\n",
                call_type);

    result->item = call_item;
    result->call_type = call_type;

    if (call_type->flags & TYPE_IS_VARARGS) {
        /* The vararg type is always the last type in the function. It is
           represented as a list. The first type of that list is the type that
           each vararg entry will need to be. */
        int va_pos = call_type->subtype_count - 1;
        result->vararg_elem_type =
                call_type->subtypes[va_pos]->subtypes[0];
        result->vararg_start = va_pos;
    }
    else {
        result->vararg_elem_type = NULL;
        result->vararg_start = (uint16_t)-1;
    }

    return result;
}

/* The call's subtrees have been evaluated now. Write the instruction to do the
   call and make a storage to put the result in (if needed). */
static void write_call(lily_emit_state *emit, lily_emit_call_state *cs)
{
    lily_sym *call_sym = cs->sym;
    lily_ast *ast = cs->ast;

    if (call_sym->flags & VAR_IS_READONLY) {
        uint16_t opcode;
        if (call_sym->flags & VAR_IS_FOREIGN_FUNC)
            opcode = o_foreign_call;
        else
            opcode = o_native_call;

        lily_u16_write_4(emit->code, opcode, ast->line_num,
                call_sym->reg_spot, cs->arg_count);
    }
    else
        lily_u16_write_4(emit->code, o_function_call, ast->line_num,
                call_sym->reg_spot, cs->arg_count);

    lily_u16_write_1(emit->code, 0);
    /* Calls are unique, because the return is NOT the very last instruction
       written. This is necessary for the vm to be able to easily call foreign
       functions. */

    if (cs->call_type->subtypes[0] != NULL) {
        lily_type *return_type = cs->call_type->subtypes[0];

        if (return_type->flags & (TYPE_IS_UNRESOLVED | TYPE_HAS_SCOOP))
            return_type = lily_ts_resolve(emit->ts, return_type);

        lily_storage *storage = get_storage(emit, return_type);
        storage->flags |= SYM_NOT_ASSIGNABLE;

        ast->result = (lily_sym *)storage;
        lily_u16_insert(emit->code, lily_u16_pos(emit->code) - 1,
                ast->result->reg_spot);
    }
    else {
        /* It's okay to not push a return value, unless something needs it.
           Assume that if the tree has a parent, something needs a value. */
        if (ast->parent == NULL)
            ast->result = NULL;
        else {
            lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                    "Function needed to return a value, but did not.\n", "");
        }
        lily_u16_insert(emit->code, lily_u16_pos(emit->code) - 1, 0);
    }

    ast->maybe_result_pos = lily_u16_pos(emit->code) - 1;

    write_call_values(emit, cs, 0);
}

/* Finishes a call: The state is relinquished, and the ts ceiling associated
   with it is lowered back down. */
static void end_call(lily_emit_state *emit, lily_emit_call_state *cs)
{
    emit->call_values_pos -= cs->arg_count;
    emit->call_state = cs;
}

/* This is the gateway to handling all kinds of calls. Most work has been farmed
   out to begin_call and related helpers though. */
static void eval_call(lily_emit_state *emit, lily_ast *ast, lily_type *expect)
{
    lily_tree_type first_t = ast->arg_start->tree_type;
    /* Variants are created by calling them in a function-like manner, so the
       parser adds them as if they were functions. They're not. */
    if (first_t == tree_variant) {
        eval_variant(emit, ast, expect);
        return;
    }

    lily_emit_call_state *cs = begin_call(emit, ast);

    lily_ts_save_point p;
    lily_ts_scope_save(emit->ts, &p);
    eval_verify_call_args(emit, cs, expect);
    write_call(emit, cs);
    end_call(emit, cs);
    lily_ts_scope_restore(emit->ts, &p);
}

/* This evaluates a variant type. Variant types are interesting because some of
   them take arguments (and thus look like calls). However, for the sake of
   simplicity, they're actually tuples with a different name and in a box (the
   enum). */
static void eval_variant(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect)
{
    lily_type *padded_type;

    /* tree_binary is only if the caller is really |>. */
    if (ast->tree_type == tree_call || ast->tree_type == tree_binary) {
        ast->result = NULL;

        /* The first arg is actually the variant. */
        lily_ast *variant_tree = ast->arg_start;
        lily_variant_class *variant = variant_tree->variant;
        lily_type *build_type = variant->build_type;

        if (build_type->subtype_count == 1)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Variant %s should not get args.\n",
                    variant->name);

        lily_emit_call_state *cs;
        lily_ts_save_point p;

        cs = begin_call(emit, ast);
        lily_ts_scope_save(emit->ts, &p);
        eval_verify_call_args(emit, cs, expect);

        /* A variant is responsible for creating a padded type. Said padded type
           describes an enum wherein the types are either filled or have ? in
           their place. This makes working with variants much saner. */
        padded_type = lily_ts_resolve_with(emit->ts,
                variant->parent->self_type, emit->ts->question_class_type);

        /* This causes all arguments to be written down into an o_build_enum op
           and be drained from the call. */
        write_build_enum(emit, cs, variant);

        end_call(emit, cs);
        lily_ts_scope_restore(emit->ts, &p);
    }
    else {
        lily_variant_class *variant = ast->variant;
        /* Did this need arguments? It was used incorrectly if so. */
        if ((variant->flags & CLS_EMPTY_VARIANT) == 0)
            verify_argument_count(emit, ast, variant->build_type, -1);

        lily_u16_write_4(emit->code, o_get_readonly, ast->line_num,
                variant->default_value->reg_spot, 0);

        if (variant->parent->generic_count) {
            lily_ts_save_point p;
            lily_ts_scope_save(emit->ts, &p);
            lily_type *self_type = variant->parent->self_type;

            /* Since the variant has no opinion on generics, try to pull any
               inference possible before defaulting to ?. */
            if (expect && expect->cls == variant->parent)
                lily_ts_check(emit->ts, self_type, expect);

            padded_type = lily_ts_resolve_with(emit->ts, self_type,
                    emit->ts->question_class_type);

            lily_ts_scope_restore(emit->ts, &p);
        }
        else
            padded_type = ast->variant->parent->self_type;
    }

    ast->maybe_result_pos = lily_u16_pos(emit->code) - 1;
    ast->padded_variant_type = padded_type;
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
static void eval_raw(lily_emit_state *emit, lily_ast *ast, lily_type *expect)
{
    if (ast->tree_type == tree_global_var ||
        ast->tree_type == tree_defined_func ||
        ast->tree_type == tree_static_func ||
        ast->tree_type == tree_method ||
        ast->tree_type == tree_inherited_new)
        emit_nonlocal_var(emit, ast);
    else if (ast->tree_type == tree_literal)
        emit_literal(emit, ast);
    else if (ast->tree_type == tree_integer)
        emit_integer(emit, ast);
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

        eval_raw(emit, start, expect);
        ast->maybe_result_pos = start->maybe_result_pos;
        ast->result = start->result;
   }
    else if (ast->tree_type == tree_unary)
        eval_unary_op(emit, ast);
    else if (ast->tree_type == tree_interp_top)
        eval_interpolation(emit, ast);
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
    else if (ast->tree_type == tree_upvalue)
        eval_upvalue(emit, ast);
}

/* Evaluate 'ast', using 'expect' for inference. If 'ast' produces an incomplete
   variant, then that variant is finalized using the currently available
   inference information.
   Use this function if the caller needs complete values and has nothing more to
   offer in the way of inference. */
static void eval_tree(lily_emit_state *emit, lily_ast *ast, lily_type *expect)
{
    eval_raw(emit, ast, expect);

    /* Variants are the only thing that won't return a value (aside from
       toplevel expressions). */
    if (ast->result && ast->result->item_kind == ITEM_TYPE_TYPE)
        rebox_variant_to_enum(emit, ast, expect);
}

/* This is used for doing an eval but with an interest in not defaulting any
   variant types (unless expect is Dynamic). The result of this function is
   either the padded enum type, or the type of any result. Additionally, if a
   variant is seen, then *found_variant is set to 1. */
static lily_type *partial_eval(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect, uint16_t *found_variant)
{
    eval_raw(emit, ast, expect);

    lily_type *eval_type;
    if (ast->tree_type == tree_variant ||
        (ast->tree_type == tree_binary &&
         ast->op == expr_func_pipe &&
         ast->right->tree_type == tree_variant) ||
        (ast->tree_type == tree_call &&
         ast->arg_start->tree_type == tree_variant)) {
        eval_type = ast->padded_variant_type;
        *found_variant = 1;
    }
    else
        eval_type = ast->result->type;

    return eval_type;
}

/* Evaluate a tree with 'expect' sent for inference. If the tree does not return
   a value, then SyntaxError is raised with 'message'. */
static void eval_enforce_value(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect, const char *message)
{
    eval_tree(emit, ast, expect);
    emit->expr_num++;

    if (ast->result == NULL)
        lily_raise(emit->raiser, lily_SyntaxError, message);
}

/* This evaluates an expression at the root of the given pool, then resets the
   pool for the next expression. */
void lily_emit_eval_expr(lily_emit_state *emit, lily_ast_pool *ap)
{
    eval_tree(emit, ap->root, NULL);
    emit->expr_num++;

    RESET_POOL(ap)
}

lily_sym *lily_emit_eval_interp_expr(lily_emit_state *emit, lily_ast_pool *ap)
{
    eval_tree(emit, ap->root, NULL);
    return ap->root->result;
}

/* This is used by 'for...in'. It evaluates an expression, then writes an
   assignment that targets 'var'.
   Since this is used by 'for...in', it checks to make sure that the expression
   returns a value of type integer. If it does not, then SyntaxError is raised.
   The pool given will be cleared. */
void lily_emit_eval_expr_to_var(lily_emit_state *emit, lily_ast_pool *ap,
        lily_var *var)
{
    lily_ast *ast = ap->root;

    eval_tree(emit, ast, NULL);
    emit->expr_num++;

    if (ast->result->type->cls->id != SYM_CLASS_INTEGER) {
        lily_raise(emit->raiser, lily_SyntaxError,
                   "Expected type 'integer', but got type '^T'.\n",
                   ast->result->type);
    }

    /* Note: This works because the only time this is called is to handle
             for..in range expressions, which are always integers. */
    lily_u16_write_4(emit->code, o_fast_assign, ast->line_num,
            ast->result->reg_spot, var->reg_spot);

    RESET_POOL(ap)
}

/* Evaluate the root of the given pool, making sure that the result is something
   that can be truthy/falsey. SyntaxError is raised if the result isn't.
   Since this is called to evaluate conditions, this also writes any needed jump
   or patch necessary. */
void lily_emit_eval_condition(lily_emit_state *emit, lily_ast_pool *ap)
{
    lily_ast *ast = ap->root;
    lily_block_type current_type = emit->block->block_type;

    if (((ast->tree_type == tree_boolean ||
          ast->tree_type == tree_integer) &&
          ast->backing_value != 0) == 0) {
        eval_enforce_value(emit, ast, NULL,
                "Conditional expression has no value.\n");
        ensure_valid_condition_type(emit, ast->result->type);

        if (current_type != block_do_while)
            /* If this doesn't work, add a jump which will get fixed to the next
               branch start or the end of the block. */
            emit_jump_if(emit, ast, 0);
        else {
            /* In a 'do...while' block, the condition is at the end, so the jump is
               reversed: If successful, go back to the top, otherwise fall out of
               the loop. */
            lily_u16_write_4(emit->code, o_jump_if, 1, ast->result->reg_spot,
                    emit->block->loop_start);
        }
    }
    else {
        if (current_type != block_do_while) {
            /* Code that handles if/elif/else transitions expects each branch to
               write a jump. There's no easy way to tell it that none was made...
               so give it a fake jump. */
            lily_u16_write_1(emit->patches, (uint16_t)-1);
        }
        else
            lily_u16_write_2(emit->code, o_jump, emit->block->loop_start);
    }

    RESET_POOL(ap)
}

/* This is called from parser to evaluate the last expression that is within a
   lambda. This is rather tricky, because 'full_type' is supposed to describe
   the full type of the lambda, but may be NULL. If it isn't NULL, then use that
   to infer what the result of the lambda should be. */
void lily_emit_eval_lambda_body(lily_emit_state *emit, lily_ast_pool *ap,
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

    eval_tree(emit, ap->root, wanted_type);
    lily_sym *root_result = ap->root->result;

    if (return_wanted && root_result != NULL) {
        /* If the caller doesn't want a return, then don't give one...regardless
           of if there is one available. */
        lily_u16_write_3(emit->code, o_return_val, ap->root->line_num,
                ap->root->result->reg_spot);
    }
    else if (return_wanted == 0)
        ap->root->result = NULL;
}

/* This handles the 'return' keyword. If parser has the pool filled with some
   expression, then run that expression (checking the result). The pool will be
   cleared out if there was an expression. */
void lily_emit_eval_return(lily_emit_state *emit, lily_ast_pool *ap)
{
    lily_ast *ast = ap->root;

    if (ast) {
        lily_type *ret_type = emit->top_function_ret;

        eval_enforce_value(emit, ast, ret_type,
                "'return' expression has no value.\n");

        if (ast->result->type != ret_type &&
            type_matchup(emit, ret_type, ast) == 0) {
            lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                    "return expected type '^T' but got type '^T'.\n", ret_type,
                    ast->result->type);
        }

        write_pop_try_blocks_up_to(emit, emit->function_block);
        lily_u16_write_3(emit->code, o_return_val, ast->line_num,
                ast->result->reg_spot);
        emit->block->last_exit = lily_u16_pos(emit->code);
        RESET_POOL(ap)
    }
    else {
        write_pop_try_blocks_up_to(emit, emit->function_block);
        lily_u16_write_2(emit->code, o_return_noval, *emit->lex_linenum);
    }
}

/* This is called after parsing the header of a define or a class. Since blocks
   are entered early, this does adjustments to block data. */
void lily_emit_update_function_block(lily_emit_state *emit,
        lily_type *self_type, lily_type *ret_type)
{
    emit->top_function_ret = ret_type;

    if (self_type) {
        /* If there's a type for 'self', then this must be a class constructor.
           Create the storage that will represent 'self' and write the
           instruction to actually make the class. */
        lily_storage *self = get_storage(emit, self_type);
        emit->block->self = self;

        /* If this ends up not being a basic instance, then it will be patched
           when the constructor closes. */
        lily_u16_write_4(emit->code, o_new_instance_basic, *emit->lex_linenum,
                self_type->cls->id, self->reg_spot);
    }
}

/* Evaluate the given tree, then try to write instructions that will raise the
   result of the tree.
   SyntaxError happens if the tree's result is not raise-able. */
void lily_emit_raise(lily_emit_state *emit, lily_ast_pool *ap)
{
    lily_ast *ast = ap->root;
    eval_enforce_value(emit, ast, NULL, "'raise' expression has no value.\n");

    lily_class *result_cls = ast->result->type->cls;
    lily_class *except_cls = lily_find_class(emit->symtab, NULL, "Exception");
    if (lily_class_greater_eq(except_cls, result_cls) == 0) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "Invalid class '%s' given to raise.\n", result_cls->name);
    }

    lily_u16_write_3(emit->code, o_raise, ast->line_num, ast->result->reg_spot);
    emit->block->last_exit = lily_u16_pos(emit->code);
    RESET_POOL(ap)
}

/* This resets __main__'s code position for the next pass. Only tagged mode
   needs this. */
void lily_reset_main(lily_emit_state *emit)
{
    emit->code->pos = 0;
}


/* This function is to be called before lily_vm_prep. This will ensure that the
   register info for __main__ is up-to-date. If any text is parsed, then this
   has to be called before running the vm. */
void lily_prepare_main(lily_emit_state *emit)
{
    lily_function_val *f = emit->symtab->main_function;
    int register_count = emit->main_block->next_reg_spot;

    /* Hack: This exists because of a two decisions.
       * One: __main__'s code is a shallow copy of emit->code.
       * Two: Parser's exception dynaload causes an expression to run, and then
         slices away code (like anything else that dynaloads). Except that this
         may happen at vm-time.
         If there is not enough space, then emit->code may be realloc'd, and
         thus invalidate __main__'s code...during vm exec. */
    lily_u16_write_prep(emit->code, 32);

    lily_u16_write_1(emit->code, o_return_from_vm);

    f->code = emit->code->data;
    f->reg_count = register_count;
}

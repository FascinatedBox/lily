#include <string.h>
#include <inttypes.h>

#include "lily_alloc.h"
#include "lily_ast.h"
#include "lily_value.h"
#include "lily_emitter.h"
#include "lily_opcode.h"
#include "lily_emit_table.h"
#include "lily_parser.h"
#include "lily_opcode_table.h"

#include "lily_cls_function.h"

/** Emitter is responsible for:
    * Taking in a given tree and writing out code that represents it. In the
      case of lambdas, it will dispatch into parser to process the lambdas
      block.
    * Verifying that call argument counts are correct, and that types are valid.
    * Doing block handling + validation (if/elif/else, for.., etc.)
    * Preparing functions to be called by the vm when functions exit. **/

# define IS_LOOP_BLOCK(b) (b == BLOCK_WHILE || \
                           b == BLOCK_DO_WHILE || \
                           b == BLOCK_FOR_IN)

# define lily_raise_adjusted(r, adjust, error_code, message, ...) \
{ \
    r->line_adjust = adjust; \
    lily_raise(r, error_code, message, __VA_ARGS__); \
}

static int type_matchup(lily_emit_state *, lily_type *, lily_ast *);
static void eval_tree(lily_emit_state *, lily_ast *, lily_type *, int);
static void eval_variant(lily_emit_state *, lily_ast *, lily_type *, int);
static void add_call_state(lily_emit_state *);

/*****************************************************************************/
/* Emitter setup and teardown                                                */
/*****************************************************************************/

lily_emit_state *lily_new_emit_state(lily_options *options,
        lily_symtab *symtab, lily_raiser *raiser)
{
    lily_emit_state *emit = lily_malloc(sizeof(lily_emit_state));

    emit->patches = lily_malloc(sizeof(int) * 4);
    emit->match_cases = lily_malloc(sizeof(int) * 4);
    emit->ts = lily_new_type_system(options, symtab, raiser);
    emit->code = lily_malloc(sizeof(uint16_t) * 32);
    emit->closed_syms = lily_malloc(sizeof(lily_sym *) * 4);
    emit->transform_table = NULL;
    emit->transform_size = 0;

    emit->call_values = lily_malloc(sizeof(lily_sym *) * 8);
    emit->call_state = NULL;
    emit->code_pos = 0;
    emit->code_size = 32;

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

    emit->patch_pos = 0;
    emit->patch_size = 4;
    emit->lambda_depth = 0;
    emit->function_depth = 0;

    emit->current_class = NULL;
    emit->raiser = raiser;
    emit->expr_num = 1;
    emit->loop_start = -1;

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

    lily_free(emit->transform_table);
    lily_free(emit->closed_syms);
    lily_free(emit->call_values);
    lily_free_type_system(emit->ts);
    lily_free(emit->match_cases);
    lily_free(emit->patches);
    lily_free(emit->code);
    lily_free(emit);
}

/*****************************************************************************/
/* Writing functions                                                         */
/*****************************************************************************/

/*  small_grow
    Grow the size of emitter's current code block once. This should be called
    if a grow must be done, and the code to be written is not a variable size.

    This is intended to be called by individual write_* functions which will
    only need one grow at the most. */
static void small_grow(lily_emit_state *emit)
{
    emit->code_size *= 2;
    emit->code = lily_realloc(emit->code, sizeof(uint16_t) * emit->code_size);
}

/*  write_prep
    This ensures that the current function can take 'size' more blocks of code.
    This will grow emitter's code until it's the right size, if necessary. */
static void write_prep(lily_emit_state *emit, int size)
{
    if ((emit->code_pos + size) > emit->code_size) {
        while ((emit->code_pos + size) > emit->code_size)
            emit->code_size *= 2;

        emit->code = lily_realloc(emit->code,
                sizeof(uint16_t) * emit->code_size);
    }
}

/* These next five functions write a particular number of instructions to code
   and increment the code's position.
   Some tips on writing instructions to code:
   * write_5 and write_2 is better than inlining a "write_7", except for
     certain circumstances.
   * If implementing new instructions, 'show' is helpful when debugging. */
static void write_1(lily_emit_state *emit, uint16_t one)
{
    if ((emit->code_pos + 1) > emit->code_size)
        small_grow(emit);

    emit->code[emit->code_pos] = one;
    emit->code_pos += 1;
}

static void write_2(lily_emit_state *emit, uint16_t one, uint16_t two)
{
    if ((emit->code_pos + 2) > emit->code_size)
        small_grow(emit);

    emit->code[emit->code_pos] = one;
    emit->code[emit->code_pos + 1] = two;
    emit->code_pos += 2;
}

static void write_3(lily_emit_state *emit, uint16_t one, uint16_t two,
        uint16_t three)
{
    if ((emit->code_pos + 3) > emit->code_size)
        small_grow(emit);

    emit->code[emit->code_pos] = one;
    emit->code[emit->code_pos + 1] = two;
    emit->code[emit->code_pos + 2] = three;
    emit->code_pos += 3;
}

static void write_4(lily_emit_state *emit, uint16_t one, uint16_t two,
        uint16_t three, uint16_t four)
{
    if ((emit->code_pos + 4) > emit->code_size)
        small_grow(emit);

    emit->code[emit->code_pos] = one;
    emit->code[emit->code_pos + 1] = two;
    emit->code[emit->code_pos + 2] = three;
    emit->code[emit->code_pos + 3] = four;
    emit->code_pos += 4;
}

static void write_5(lily_emit_state *emit, uint16_t one, uint16_t two,
        uint16_t three, uint16_t four, uint16_t five)
{
    if ((emit->code_pos + 5) > emit->code_size)
        small_grow(emit);

    emit->code[emit->code_pos] = one;
    emit->code[emit->code_pos + 1] = two;
    emit->code[emit->code_pos + 2] = three;
    emit->code[emit->code_pos + 3] = four;
    emit->code[emit->code_pos + 4] = five;
    emit->code_pos += 5;
}

/*****************************************************************************/
/* Internal helper functions                                                 */
/*****************************************************************************/

/*  opname
    Return a string that represents the given lily_expr_op. */
static char *opname(lily_expr_op op)
{
    static char *opnames[] =
    {"+", "-", "==", "<", "<=", ">", ">=", "!=", "%", "*", "/", "<<", ">>", "&",
     "|", "^", "!", "-", "&&", "||", "=", "+=", "-=", "%=", "*=", "/=", "<<=",
     ">>="};

    return opnames[op];
}

/*  condition_optimize_check
    This is called when lily_emit_eval_condition is called with a tree that has
    type 'tree_literal'. If the given tree is always true, then the emitter
    can optimize the load out.
    Without this, a 'while 1: { ... }' will load "1" and check it at the top of
    every loop...which is rather silly. */
static int condition_optimize_check(lily_ast *ast)
{
    int can_optimize = 1;

    /* This may not be a literal. It could be a user-defined/built-in function
       which would always automatically be true. */
    if (ast->result->flags & ITEM_TYPE_TIE) {
        lily_tie *lit = (lily_tie *)ast->result;

        /* Keep this synced with vm's o_jump_if calculation. */
        int lit_cls_id = lit->type->cls->id;
        if (lit_cls_id == SYM_CLASS_INTEGER && lit->value.integer == 0)
            can_optimize = 0;
        else if (lit_cls_id == SYM_CLASS_DOUBLE && lit->value.doubleval == 0.0)
            can_optimize = 0;
        else if (lit_cls_id == SYM_CLASS_STRING && lit->value.string->size == 0)
            can_optimize = 0;
        else if (lit->type->cls->flags & CLS_VARIANT_CLASS)
            can_optimize = 0;
    }

    return can_optimize;
}

/*  count_inner_try_blocks
    Count the number of 'try' blocks entered where an 'except' has not yet been
    seen. This counts up to the deepest loop block, or the current function,
    whichever comes first. */
static int count_inner_try_blocks(lily_emit_state *emit)
{
    lily_block *block_iter = emit->block;
    int ret = 0;
    while (IS_LOOP_BLOCK(block_iter->block_type) == 0 &&
           (block_iter->block_type & BLOCK_FUNCTION) == 0) {
        if (block_iter->block_type == BLOCK_TRY)
            ret++;

        block_iter = block_iter->prev;
    }
    return ret;
}

/*  write_pop_inner_try_blocks
    This must be called before any 'continue', 'break', or 'return' code is
    emitted. It ensures that the vm will pop the proper number of 'try' blocks
    registered to offset the movement being done. */
static void write_pop_inner_try_blocks(lily_emit_state *emit)
{
    int try_count = count_inner_try_blocks(emit);
    if (try_count) {
        write_prep(emit, try_count);
        int i;
        for (i = 0;i <= try_count;i++)
            emit->code[emit->code_pos+i] = o_pop_try;

        emit->code_pos += try_count;
    }
}

/*  find_deepest_loop
    Look backward from the current block to find the inner-most block that is a
    loop. This block is the one that should receive any 'continue' and 'break'
    jumps.

    Success: A block is returned that is a loop block.
    Failure: NULL is returned. */
static lily_block *find_deepest_loop(lily_emit_state *emit)
{
    lily_block *block, *ret;
    ret = NULL;

    for (block = emit->block; block; block = block->prev) {
        if (IS_LOOP_BLOCK(block->block_type)) {
            ret = block;
            break;
        }
        else if (block->block_type & BLOCK_FUNCTION) {
            ret = NULL;
            break;
        }
    }

    return ret;
}

static lily_block *find_deepest_func(lily_emit_state *emit)
{
    lily_block *block;
    lily_block *result = NULL;

    for (block = emit->block; block; block = block->prev) {
        if ((block->block_type & BLOCK_FUNCTION) &&
             block->block_type != BLOCK_FILE) {
            result = block;
            break;
        }
    }

    return result;
}


/*  grow_patches
    Make emitter's patches bigger. */
static void grow_patches(lily_emit_state *emit)
{
    emit->patch_size *= 2;
    emit->patches = lily_realloc(emit->patches,
        sizeof(int) * emit->patch_size);
}

void inject_patch_into_block(lily_emit_state *emit, lily_block *block,
        int target)
{
    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    /* This is the most recent block, so add the patch to the top. */
    if (emit->block == block) {
        emit->patches[emit->patch_pos] = target;
        emit->patch_pos++;
    }
    else {
        /* The block is not on top, so this will be fairly annoying... */
        int move_by, move_start;

        move_start = block->next->patch_start;
        move_by = emit->patch_pos - move_start;

        /* Move everything after this patch start over one, so that there's a
           hole after the last while patch to write in a new one. */
        memmove(emit->patches+move_start+1, emit->patches+move_start,
                move_by * sizeof(int));
        emit->patch_pos++;
        emit->patches[move_start] = target;

        for (block = block->next;
             block;
             block = block->next)
            block->patch_start++;
    }
}

void write_block_patches(lily_emit_state *emit, int pos)
{
    int from = emit->patch_pos-1;
    int to = emit->block->patch_start;

    for (;from >= to;from--) {
        /* Skip -1's, which are fake patches from conditions that were
            optimized out. */
        if (emit->patches[from] != -1)
            emit->code[emit->patches[from]] = pos;
    }

    /* Use the space for new patches now. */
    emit->patch_pos = to;
}

static void grow_closed_syms(lily_emit_state *emit)
{
    emit->closed_size *= 2;
    emit->closed_syms = lily_realloc(emit->closed_syms,
        sizeof(lily_sym *) * emit->closed_size);
}

static void grow_match_cases(lily_emit_state *emit)
{
    emit->match_case_size *= 2;
    emit->match_cases = lily_realloc(emit->match_cases,
        sizeof(int) * emit->match_case_size);
}

/*  emit_jump_if
    Write a conditional jump and add it to emitter's current patches.
    0 == o_jump_if_false
        The jump happens if the ast's result is 0/false.
    1 == o_jump_if_true
        The jump happens if the ast's result is non-zero. */
static void emit_jump_if(lily_emit_state *emit, lily_ast *ast, int jump_on)
{
    write_4(emit, o_jump_if, jump_on, ast->result->reg_spot, 0);
    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    emit->patches[emit->patch_pos] = emit->code_pos - 1;
    emit->patch_pos++;
}

/*  ensure_valid_condition_type
    This ensures that the resulting value for a condition is one that the vm
    can determine is true or false.
    If these are changed, then the vm's o_jump_if should be updated. */
static void ensure_valid_condition_type(lily_emit_state *emit, lily_type *type)
{
    int cls_id = type->cls->id;

    if (cls_id != SYM_CLASS_INTEGER &&
        cls_id != SYM_CLASS_DOUBLE &&
        cls_id != SYM_CLASS_STRING &&
        cls_id != SYM_CLASS_LIST)
        lily_raise(emit->raiser, lily_SyntaxError,
                "^T is not a valid condition type.\n", type);
}

/*  check_valid_subscript
    Determine if the given var is subscriptable by the type of the given index.
    Additionally, an 'index literal' is given as a special-case for tuples.
    This raises an error for unsubscriptable types. */
static void check_valid_subscript(lily_emit_state *emit, lily_ast *var_ast,
        lily_ast *index_ast)
{
    int var_cls_id = var_ast->result->type->cls->id;
    if (var_cls_id == SYM_CLASS_LIST) {
        if (index_ast->result->type->cls->id != SYM_CLASS_INTEGER)
            lily_raise_adjusted(emit->raiser, var_ast->line_num,
                    lily_SyntaxError, "list index is not an integer.\n", "");
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
        if (index_ast->result->type->cls->id != SYM_CLASS_INTEGER ||
            index_ast->tree_type != tree_literal) {
            lily_raise_adjusted(emit->raiser, var_ast->line_num, lily_SyntaxError,
                    "tuple subscripts must be integer literals.\n", "");
        }

        int index_value = index_ast->literal->value.integer;
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

/*  get_subscript_result
    Get the type that would result from doing a subscript. tuple_index_lit is
    a special case for tuples. */
static lily_type *get_subscript_result(lily_type *type, lily_ast *index_ast)
{
    lily_type *result;
    if (type->cls->id == SYM_CLASS_LIST)
        result = type->subtypes[0];
    else if (type->cls->id == SYM_CLASS_HASH)
        result = type->subtypes[1];
    else if (type->cls->id == SYM_CLASS_TUPLE) {
        /* check_valid_subscript ensures that this is safe. */
        int literal_index = index_ast->literal->value.integer;
        result = type->subtypes[literal_index];
    }
    else
        /* Won't happen, but keeps the compiler from complaining. */
        result = NULL;

    return result;
}

/*  Add a new storage to emitter's list of storages. */
static void add_storage(lily_emit_state *emit)
{
    lily_storage *storage = lily_malloc(sizeof(lily_storage));

    storage->type = NULL;
    storage->next = NULL;
    storage->expr_num = 0;
    storage->flags = 0;

    if (emit->all_storage_start == NULL)
        emit->all_storage_start = storage;
    else
        emit->all_storage_top->next = storage;

    emit->all_storage_top = storage;
    emit->unused_storage_start = storage;
}

/*  get_storage
    Attempt to get an unused storage of the type given. Additionally, a
    line number is required to fix up the line number in case there is an
    out-of-memory situation.
    Additionally, this function ensures that emit->unused_storage_start is both
    updated appropriately and will never become NULL.

    This returns a valid storage. */
static lily_storage *get_storage(lily_emit_state *emit, lily_type *type)
{
    lily_storage *storage_iter = emit->block->storage_start;
    int expr_num = emit->expr_num;

    /* Emitter's linked list of storages is done such that there is always one
       unused storage at the end. Therefore, this loop will never end with
       storage_iter == NULL. */
    while (storage_iter) {
        /* If the type is NULL, then nothing is using this storage and it
           can be repurposed for the current function. */
        if (storage_iter->type == NULL) {
            storage_iter->type = type;

            storage_iter->reg_spot = emit->function_block->next_reg_spot;
            emit->function_block->next_reg_spot++;

            /* This ensures that lambdas don't clobber on current storages. */
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
    /* This ensures that emit->unused_storage_start is always valid and always
       something unused. */
    if (storage_iter->next == NULL)
        add_storage(emit);

    storage_iter->flags &= ~SYM_NOT_ASSIGNABLE;
    return storage_iter;
}

/*  This attempts to locate a storage, but makes sure that it has never been
    used before. This is necessary for closures, which dump the source of
    upvalue into a register (this prevents it from being destroyed early). */
lily_storage *get_unique_storage(lily_emit_state *emit, lily_type *type)
{
    int next_spot = emit->function_block->next_reg_spot;
    lily_storage *s = NULL;

    /* As long as the next register spot doesn't change, the resulting storage
       may be one that is used somewhere else. There's probably a faster, more
       direct approach, but this is really likely to succeed the first time. */
    do {
        s = get_storage(emit, type);
    } while (emit->function_block->next_reg_spot == next_spot);

    return s;
}

static void close_over_sym(lily_emit_state *emit, lily_sym *sym)
{
    if (emit->closed_pos == emit->closed_size)
        grow_closed_syms(emit);

    emit->closed_syms[emit->closed_pos] = sym;
    emit->closed_pos++;
    sym->flags |= SYM_CLOSED_OVER;
}

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

static int find_closed_self_spot(lily_emit_state *emit)
{
    int i, result = -1;
    for (i = 0;i < emit->closed_pos;i++) {
        lily_sym *s = emit->closed_syms[i];
        if ((s->flags & ITEM_TYPE_VAR) == 0) {
            result = i;
            break;
        }
    }

    return result;
}

static void maybe_close_over_class_self(lily_emit_state *emit)
{
    lily_block *block = emit->block;
    while ((block->block_type & BLOCK_CLASS) == 0)
        block = block->prev;

    lily_sym *self = (lily_sym *)block->self;
    if (find_closed_sym_spot(emit, self) == -1)
        close_over_sym(emit, self);

    if (emit->block->self == NULL)
        emit->block->self = get_storage(emit, self->type);
}

/*  write_build_op

    This is responsible for writing the actual o_build_list_tuple or
    o_build_hash code, depending on the opcode given. The list will be put into
    a register at reg_spot, which is assumed to have the correct type to hold
    the given result.

    emit:       The emitter holding the function to write to.
    opcode:     The opcode to write: o_build_list_tuple for a list/tuple, or
                o_build_hash for a hash.
    first_arg:  The first argument to start iterating over.
    line_num:   A line number for the o_build_* opcode.
    num_values: The number of values that will be written. This is typically
                the parent's args_collected.
    reg_spot:   The id of a register where the opcode's result will go. The
                caller is expected to ensure that the register has the proper
                type to hold the resulting thing.
*/
static void write_build_op(lily_emit_state *emit, int opcode,
        lily_ast *first_arg, int line_num, int num_values, int reg_spot)
{
    int i;
    lily_ast *arg;

    write_prep(emit, num_values + 4);
    emit->code[emit->code_pos] = opcode;
    emit->code[emit->code_pos+1] = line_num;
    emit->code[emit->code_pos+2] = num_values;

    for (i = 3, arg = first_arg; arg != NULL; arg = arg->next_arg, i++)
        emit->code[emit->code_pos + i] = arg->result->reg_spot;

    emit->code[emit->code_pos+i] = reg_spot;
    emit->code_pos += 4 + num_values;
}

static void emit_rebox_value(lily_emit_state *, lily_type *, lily_ast *);

/*  rebox_variant_to_enum
    This is a convenience function that will convert the variant value within
    the given ast to an enum value.
    Note: If the variant does not supply full type information, then missing
          types are given the type of 'any'. */
static void rebox_variant_to_enum(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *rebox_type = lily_ts_build_enum_by_variant(emit->ts,
            ast->result->type);

    emit_rebox_value(emit, rebox_type, ast);
}

static lily_storage *emit_rebox_sym(lily_emit_state *emit,
        lily_type *new_type, lily_sym *sym, uint32_t line_num)
{
    lily_storage *storage = get_storage(emit, new_type);

    /* Don't allow a bare variant to be thrown into an any until it's thrown
       into an enum box first. */
    if (new_type->cls->id == SYM_CLASS_ANY &&
        sym->type->cls->flags & CLS_VARIANT_CLASS) {
        lily_type *rebox_type = lily_ts_build_enum_by_variant(emit->ts,
                sym->type);

        sym = (lily_sym *)emit_rebox_sym(emit, rebox_type, sym, line_num);
    }

    write_4(emit, o_assign, line_num, sym->reg_spot,
            storage->reg_spot);

    return storage;
}

/*  emit_rebox_value
    Make a storage of type 'new_type' and assign ast's result to it. The tree's
    result is written over. */
static void emit_rebox_value(lily_emit_state *emit, lily_type *new_type,
        lily_ast *ast)
{
    lily_storage *storage = get_storage(emit, new_type);

    /* Don't allow a bare variant to be thrown into an any until it's thrown
       into an enum box first. */
    if (new_type->cls->id == SYM_CLASS_ANY &&
        ast->result->type->cls->flags & CLS_VARIANT_CLASS) {
        rebox_variant_to_enum(emit, ast);
    }

    write_4(emit, o_assign, ast->line_num, ast->result->reg_spot,
            storage->reg_spot);

    ast->result = (lily_sym *)storage;
}

/*  emit_rebox_to_any
    This is a helper function that calls emit_rebox_value on the given tree
    with a type of class any. */
static void emit_rebox_to_any(lily_emit_state *emit, lily_ast *ast)
{
    emit_rebox_value(emit, emit->symtab->any_class->type, ast);
}

/*  setup_types_for_build
    This is called before building a static list or hash.

    expect_type is checked for being a generic that unwraps to a type with
    a class id of 'wanted_id', or having that actual id.

    If expect_type's class is correct, then the types inside of it are laid
    down into emitter's ts. They can be retrieved using lily_ts_get_ceiling_type.

    This processing is done because it's necessary for type inference.

    Returns 1 on success, 0 on failure. */
static int setup_types_for_build(lily_emit_state *emit,
        lily_type *expect_type, int wanted_id, int did_resolve)
{
    int ret = 1;

    if (expect_type && did_resolve == 0 &&
        expect_type->cls->id == SYM_CLASS_GENERIC) {
        expect_type = lily_ts_easy_resolve(emit->ts, expect_type);
        did_resolve = 1;
    }

    if (expect_type && expect_type->cls->id == wanted_id) {
        int i;
        for (i = 0;i < expect_type->subtype_count;i++) {
            lily_type *inner_type = expect_type->subtypes[i];
            if (did_resolve == 0 &&
                inner_type->cls->id == SYM_CLASS_GENERIC) {
                inner_type = lily_ts_easy_resolve(emit->ts, inner_type);
            }
            lily_ts_set_ceiling_type(emit->ts, inner_type, i);
        }
    }
    else
        ret = 0;

    return ret;
}

/*  add_var_chain_to_info
    Add info for a linked-list of vars to the given register info. Functions do
    not get a register (VAR_IS_READONLY), so don't add them. */
static void add_var_chain_to_info(lily_emit_state *emit,
        lily_register_info *info, lily_var *from_var, lily_var *to_var)
{
    while (from_var != to_var) {
        if ((from_var->flags & VAR_IS_READONLY) == 0) {
            info[from_var->reg_spot].type = from_var->type;
            info[from_var->reg_spot].name = from_var->name;
            info[from_var->reg_spot].line_num = from_var->line_num;
        }

        from_var = from_var->next;
    }
}

/*  add_storage_chain_to_info
    Add info for a linked-list of storages to the given register info. Only
    used by finalize_function_val. */
static void add_storage_chain_to_info(lily_register_info *info,
        lily_storage *storage)
{
    while (storage && storage->type) {
        info[storage->reg_spot].type = storage->type;
        info[storage->reg_spot].name = NULL;
        info[storage->reg_spot].line_num = -1;
        storage = storage->next;
    }
}

/* This traverses within emit->code from the initially given start and end
   positions.
   This function is concerned with values that are local to this function which
   have been closed over.
   * Any read of a local that has been closed over will have a o_get_upvalue
     written before the opcode.
   * Any write of a local will have o_set_upvalue written after the opcode.
   * Jumps encounted are adjusted to account for any get/set_upvalue's written.
   In most cases, this isn't necessary. However, consider this:
    define f() {
        var i = 10
        var g = {|| i += 1 }
        g()
        i += 1
        show(i)
    }
   In this case, 'i' should have o_get/set_upvalue written appropriately so that
   'i' will come out as 12 (the lambda update being factored in) instead of 11
   (using local assigns only). */
static void transform_code(lily_emit_state *emit, lily_function_val *f,
        int pos, int end, int starting_adjust)
{
    uint16_t *transform_table = emit->transform_table;
    int jump_adjust = starting_adjust;
    int jump_pos = -1, jump_end;
    int output_pos = -1, output_end;
    /* Do not create a local copy of emit->code here, because the write_4's
       may cause it to be realloc'd. */

    while (pos < end) {
        int j = 0, op = emit->code[pos];
        int c, count, call_type, i, line_num;
        const int *opcode_data = opcode_table[op];

        for (i = 1;i <= opcode_data[1];i++) {
            c = opcode_data[i + 1];
            if (c == C_LINENO)
                line_num = emit->code[pos + i + j];
            else if ((c == C_INPUT || c == C_MATCH_INPUT ||
                      (c == C_CALL_INPUT && call_type == 0)) &&
                     op != o_create_function) {
                int spot = emit->code[pos + i + j];
                if (transform_table[spot] != (uint16_t)-1) {
                    write_4(emit, o_get_upvalue, line_num,
                            transform_table[spot], spot);
                    jump_adjust += 4;
                }
            }
            else if (c == C_OUTPUT) {
                int spot = emit->code[pos + i + j];
                if (spot != (uint16_t)-1 && transform_table[spot] != -1) {
                    output_pos = i + j;
                    output_end = output_pos + 1;
                }
            }
            else if (c == C_COUNT)
                count = emit->code[pos + i + j];
            else if (c == C_NOP)
                break;
            else if (c == C_CALL_TYPE)
                call_type = emit->code[pos + i + j];
            else if (c == C_COUNT_OUTPUTS) {
                output_pos = i + j;
                output_end = output_pos + count;
                j += count - 1;
            }
            else if (c == C_JUMP) {
                /* All of the o_except cases of a single try block are linked
                   together. The last one has a jump position of 0 to mean that
                   it's at the end. Make sure that is preserved. */
                if (op != o_except && emit->code[pos + i + j] != 0) {
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
                    int spot = emit->code[pos + i + j];
                    if (transform_table[spot] != (uint16_t)-1) {
                        write_4(emit, o_get_upvalue, line_num,
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
                count = emit->code[pos + i + j];
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

        write_prep(emit, move);
        memcpy(emit->code + emit->code_pos, emit->code + pos,
               move * sizeof(uint16_t));

        if (jump_pos != -1) {
            for (;jump_pos < jump_end;jump_pos++)
                emit->code[emit->code_pos + jump_pos] += jump_adjust;

            jump_pos = -1;
        }

        emit->code_pos += move;

        if (output_pos != -1) {
            for (;output_pos < output_end;output_pos++) {
                int spot = emit->code[pos + output_pos];
                if (spot != (uint16_t)-1 &&
                    transform_table[spot] != (uint16_t)-1) {
                    write_4(emit, o_set_upvalue, line_num,
                            transform_table[spot], spot);
                    jump_adjust += 4;
                }
            }
            output_pos = -1;
        }

        pos += move;
    }
}

/*  Consider this:
    define f(a: integer => function( => integer)) {
        return {|| a}
    }

    The parameter 'a' is used as an upvalue, is never used within the function
    it is declared in. As a result, there are no writes to transform as a means
    of putting 'a' into the closure.

    This solves that by writing an explicit o_set_upvalue where it is needed,
    before the transform. However, if o_setup_optargs is present, then nothing
    is written for the parameter (o_setup_optargs will come later, and it will
    not have a value). */
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

    lily_var *var_iter = emit->symtab->active_import->var_chain;
    while (var_iter != function_var) {
        if (var_iter->flags & SYM_CLOSED_OVER &&
            var_iter->reg_spot < local_count) {
            lily_type *real_type = real_param_types[var_iter->reg_spot + 1];
            if (real_type->cls != optarg_class)
                write_4(emit, o_set_upvalue, function_var->line_num,
                        find_closed_sym_spot(emit, (lily_sym *)var_iter),
                        var_iter->reg_spot);
        }

        var_iter = var_iter->next;
    }
}

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
        if (s && s->flags & ITEM_TYPE_VAR) {
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
    int end = emit->code_pos;
    *new_start = emit->code_pos;
    int save_code_pos = emit->code_pos;

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
        write_4(emit, o_create_closure, f->line_num, emit->closed_pos,
                s->reg_spot);

        if (emit->block->block_type & BLOCK_CLASS) {
            /* Classes are slightly tricky. There are (up to) three different
               things that really want to be at the top of the code:
               o_new_instance, o_setup_optargs, and o_function_call (in the
               event that there is an inherited new).
               Inject o_new_instance, then patch that out of the header so that
               transform doesn't write it in again. */

            uint16_t linenum = emit->code[start + 1];
            uint16_t self_reg_spot = emit->code[start + 2];
            write_3(emit, o_new_instance, linenum, self_reg_spot);

            transform_start += 3;

            /* The closure only needs to hold self if there was a lambda that
               used self (because the lambda doesn't automatically get self). */
            if (closed_self_spot != -1) {
                write_4(emit, o_set_upvalue, linenum, closed_self_spot,
                        self_reg_spot);
                /* This class is going out of scope, so the 'self' it contians
                   is going away as well. */
                emit->closed_syms[closed_self_spot] = NULL;
            }

            lily_class *cls = emit->block->class_entry;
            /* This is only set if a class method needed to access some part of
               the closure through the class. This is likely to be the case, but
               may not always be (ex: the class only contains lambdas). */
            lily_prop_entry *closure_prop;
            closure_prop = lily_find_property(emit->symtab, cls, "*closure");

            if (closure_prop) {
                write_5(emit, o_set_property, linenum, self_reg_spot,
                        closure_prop->id, s->reg_spot);
            }
        }
    }
    else if (emit->block->prev &&
             emit->block->prev->block_type & BLOCK_CLASS) {
        if ((emit->block->block_type & BLOCK_LAMBDA) == 0) {
            lily_class *cls = emit->block->prev->class_entry;
            lily_prop_entry *closure_prop = lily_find_property(emit->symtab,
                    cls, "*closure");
            lily_class *parent = cls->parent;
            if (closure_prop == NULL ||
                /* This should yield a closure stored in THIS class, not one
                   that may be in a parent class. */
                (parent && closure_prop->id <= parent->prop_count))
                closure_prop = lily_add_class_property(emit->symtab, cls,
                    s->type, "*closure", 0);

            write_5(emit, o_load_class_closure, f->line_num,
                    emit->block->self->reg_spot, closure_prop->id, s->reg_spot);
        }
        else {
            /* Lambdas inside of a class are weird because they don't
               necessarily have self as their first argument. They will,
               however, have a closure to draw from. */
            write_3(emit, o_load_closure, f->line_num, s->reg_spot);

            lily_storage *lambda_self = emit->block->self;
            if (lambda_self) {
                write_4(emit, o_get_upvalue, *emit->lex_linenum,
                        closed_self_spot, lambda_self->reg_spot);
            }
        }
    }
    else
        write_3(emit, o_load_closure, (uint16_t)f->line_num, s->reg_spot);

    ensure_params_in_closure(emit);
    setup_transform_table(emit);

    if (emit->function_depth == 2)
        emit->closed_pos = 0;

    /* Closures create patches when they write o_create_function. Fix those
       patches with the spot of the closure (since they need to draw closure
       info but won't have it just yet). */
    if (emit->block->patch_start != emit->patch_pos)
        write_block_patches(emit, s->reg_spot);

    /* Since jumps reference absolute locations, they need to be adjusted
       for however much bytecode is written as a header. The
       transform - code_start is so that class closures are accounted for as
       well (since the o_new_instance is rewritten). */
    int starting_adjust = (emit->code_pos - save_code_pos) +
            (transform_start - emit->block->code_start);
    transform_code(emit, f, transform_start, end, starting_adjust);
    *new_size = emit->code_pos - *new_start;
}

static void create_code_block_for(lily_emit_state *emit,
        lily_function_val *f)
{
    int code_start, code_size;

    if (emit->closed_pos == 0) {
        code_start = emit->block->code_start;
        code_size = emit->code_pos - emit->block->code_start;
    }
    else
        closure_code_transform(emit, f, &code_start, &code_size);

    uint16_t *code = lily_malloc((code_size + 1) * sizeof(uint16_t));
    memcpy(code, emit->code + code_start, sizeof(uint16_t) * code_size);

    f->code = code;
    f->len = code_size - 1;
}

/*  finalize_function_val
    This is a helper called when a function block is being exited, OR __main__
    needs to run.

    In both cases, the register info that the vm needs to init the registers
    for this function is created.

    For non-__main__ functions, inner functions are hidden in symtab's
    old_function_chain, and the vars go out of scope. */
static void finalize_function_val(lily_emit_state *emit,
        lily_block *function_block)
{
    lily_function_val *f = emit->top_function;
    /* This must run before the rest, because if 'f' needs to be a
       closure, it will require a unique storage. */
    create_code_block_for(emit, f);

    int register_count = emit->function_block->next_reg_spot;
    lily_storage *storage_iter = function_block->storage_start;
    lily_register_info *info = lily_malloc(
            register_count * sizeof(lily_register_info));
    lily_var *var_stop = function_block->function_var;
    lily_type *function_type = var_stop->type;

    /* Don't include functions inside of themselves... */
    if (emit->function_depth == 1)
        var_stop = var_stop->next;

    if (emit->function_depth != 1)
        add_var_chain_to_info(emit, info,
                emit->symtab->active_import->var_chain, var_stop);

    if (function_type->flags & TYPE_IS_UNRESOLVED)
        f->has_generics = 1;

    add_storage_chain_to_info(info, function_block->storage_start);

    if (emit->function_depth > 1) {
        /* todo: Reuse the var shells instead of destroying. Seems petty, but
                 malloc isn't cheap if there are a lot of vars. */
        lily_var *var_iter = emit->symtab->active_import->var_chain;
        lily_var *var_temp;
        while (var_iter != var_stop) {
            var_temp = var_iter->next;
            if ((var_iter->flags & VAR_IS_READONLY) == 0)
                lily_free(var_iter);
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

    /* Blank the types of the storages that were used. This lets other functions
       know that the types are not in use. */
    storage_iter = function_block->storage_start;
    while (storage_iter) {
        storage_iter->type = NULL;
        storage_iter = storage_iter->next;
    }

    /* Unused storages now begin where the function starting zapping them. */
    emit->unused_storage_start = function_block->storage_start;

    f->reg_info = info;
    f->reg_count = register_count;
}

static void leave_function(lily_emit_state *emit, lily_block *block)
{
    /* A lambda block never has to update the return type because the return is
       whatever the expression in the body returns. */
    if (block->block_type & BLOCK_LAMBDA)
        emit->top_function_ret = emit->top_var->type->subtypes[0];
    if (emit->block->class_entry == NULL) {
        if (emit->top_function_ret == NULL)
            /* Write an implicit 'return' at the end of a function claiming to not
               return a value. This saves the user from having to write an explicit
               'return'. */
            write_2(emit, o_return_noval, *emit->lex_linenum);
        else if (block->block_type == BLOCK_FUNCTION &&
                 block->last_return != emit->code_pos) {
            /* If this is a function created with 'define', then determine if
               the last return was the last instruction written. This is the
               simple way of ensuring that a function always returns a value
               that stops potential issues at emit-time. */
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Missing return statement at end of function.\n");
        }
    }
    else {
        /* Constructors always return self. */
        write_3(emit,
                o_return_val,
                *emit->lex_linenum,
                emit->block->self->reg_spot);
    }

    finalize_function_val(emit, block);

    /* Information must be pulled from and saved to the last function-like
       block. This loop is because of lambdas. */
    lily_block *last_func_block = block->prev;
    while ((last_func_block->block_type & BLOCK_FUNCTION) == 0)
        last_func_block = last_func_block->prev;

    lily_var *v = last_func_block->function_var;

    emit->current_class = block->prev->class_entry;

    /* If this function was the ::new for a class, move it over into that class
       since the class is about to close. */
    if (emit->block->class_entry) {
        lily_class *cls = emit->block->class_entry;

        emit->symtab->active_import->var_chain = block->function_var;
        lily_add_class_method(emit->symtab, cls, block->function_var);
    }
    else if (emit->block->block_type != BLOCK_FILE)
        emit->symtab->active_import->var_chain = block->function_var;
    /* For file 'blocks', don't fix the var_chain or all of the toplevel
       functions in that block will vanish! */

    if (block->prev->generic_count != block->generic_count &&
        (block->block_type & BLOCK_LAMBDA) == 0) {
        lily_update_symtab_generics(emit->symtab, NULL,
                last_func_block->generic_count);
    }

    emit->top_function = last_func_block->function_value;
    emit->top_var = v;
    emit->top_function_ret = v->type->subtypes[0];
    emit->code_pos = block->code_start;
    emit->function_block = last_func_block;

    /* File 'blocks' do not bump up the depth because that's used to determine
       if something is a global or not. */
    if (block->block_type != BLOCK_FILE) {
        if (block->block_type & BLOCK_LAMBDA)
            emit->lambda_depth--;

        emit->function_depth--;
    }
}

/*  eval_enforce_value
    Evaluate a given ast and make sure it returns a value. */
static void eval_enforce_value(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type, char *message)
{
    eval_tree(emit, ast, expect_type, 1);
    emit->expr_num++;

    if (ast->result == NULL)
        lily_raise(emit->raiser, lily_SyntaxError, message);
}

/*  ensure_proper_match_block
    This function checks if the current block (verified to be a match block by
    the caller) has all cases satisfied. Raise SyntaxError if there are missing
    cases. */
static void ensure_proper_match_block(lily_emit_state *emit)
{
    lily_block *block = emit->block;
    int error = 0;
    lily_msgbuf *msgbuf = emit->raiser->msgbuf;
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
        lily_raise_prebuilt(emit->raiser, lily_SyntaxError);
}

static void push_info_to_error(lily_emit_state *emit, lily_emit_call_state *cs)
{
    char *class_name = "", *separator = "", *kind = "Function";
    char *call_name;
    lily_msgbuf *msgbuf = emit->raiser->msgbuf;

    int item_flags = cs->error_item->flags;

    if (item_flags & ITEM_TYPE_VAR) {
        lily_var *var = (lily_var *)cs->error_item;
        if (var->parent) {
            class_name = var->parent->name;
            separator = "::";
        }

        call_name = var->name;
    }
    else if (item_flags & ITEM_TYPE_VARIANT_CLASS) {
        lily_class *variant_cls = (lily_class *)cs->error_item;
        call_name = variant_cls->name;

        if (variant_cls->parent->flags & CLS_ENUM_IS_SCOPED) {
            class_name = variant_cls->parent->name;
            separator = "::";
        }

        kind = "Variant";
    }
    else if (item_flags & ITEM_TYPE_PROPERTY) {
        lily_prop_entry *prop = (lily_prop_entry *)cs->error_item;

        class_name = prop->cls->name;
        call_name = prop->name;
        separator = ".";
        kind = "Property";
    }
    else {
        /* This occurs when there's a call of a call, a call of a subscript
           result, or something else weird. */
        call_name = "(anonymous)";
    }

    lily_msgbuf_add_fmt(msgbuf, "%s %s%s%s", kind, class_name, separator,
            call_name);
}

/*  assign_post_check
    This function is called after any assignment is evaluated. This allows
    assignment chains (because those are nice), but disables assignments from
    being nested within other expressions.

    Without this function, things like 'integer a = (b = 10)' are possible.

    The tree passed is the assignment tree itself. */
static void assign_post_check(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->parent &&
         (ast->parent->tree_type != tree_binary ||
          ast->parent->op < expr_assign)) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "Cannot nest an assignment within an expression.\n");
    }
    else if (ast->parent == NULL)
        /* This prevents conditions from using the result of an assignment. */
        ast->result = NULL;
}

/*****************************************************************************/
/* Error raising functions                                                   */
/*****************************************************************************/

static void bad_assign_error(lily_emit_state *emit, int line_num,
                          lily_type *left_type, lily_type *right_type)
{
    /* Remember that right is being assigned to left, so right should
       get printed first. */
    lily_raise_adjusted(emit->raiser, line_num, lily_SyntaxError,
            "Cannot assign type '^T' to type '^T'.\n",
            right_type, left_type);
}

static void bad_arg_error(lily_emit_state *emit, lily_emit_call_state *cs,
        lily_type *got, lily_type *expected)
{
    push_info_to_error(emit, cs);
    lily_msgbuf *msgbuf = emit->raiser->msgbuf;

    emit->raiser->line_adjust = cs->ast->line_num;

    /* If this call has unresolved generics, resolve those generics as
       themselves so the error message prints out correctly. */
    lily_ts_resolve_as_self(emit->ts);

    /* These names are intentionally the same length and on separate lines so
       that slight naming issues become more apparent. */
    lily_msgbuf_add_fmt(msgbuf,
            ", argument #%d is invalid:\n"
            "Expected Type: ^T\n"
            "Received Type: ^T\n",
            cs->arg_count + 1, lily_ts_resolve(emit->ts, expected), got);
    lily_raise_prebuilt(emit->raiser, lily_SyntaxError);
}

/*  determine_left_type
    This function is called on the left side of an assignment to determine
    what the result of that assignment will be. However, this function does
    NOT do any evaluation.

    This function exists because assignments run from right to left, but at
    the same time the right side should infer the resulting type based off of
    the left side. */
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
                if (index_tree->tree_type != tree_literal ||
                    index_tree->sym->type->cls->id != SYM_CLASS_INTEGER)
                    result_type = NULL;
                else {
                    int literal_index = index_tree->literal->value.integer;
                    if (literal_index < 0 ||
                        literal_index > result_type->subtype_count)
                        result_type = NULL;
                    else
                        result_type = result_type->subtypes[literal_index];
                }
            }
            else if (result_type->cls->id == SYM_CLASS_LIST)
                result_type = result_type->subtypes[0];
        }
    }
    else if (ast->tree_type == tree_oo_access) {
        result_type = determine_left_type(emit, ast->arg_start);
        if (result_type != NULL) {
            char *oo_name = lily_membuf_get(emit->ast_membuf, ast->membuf_pos);
            lily_class *lookup_class = result_type->cls;
            lily_type *lookup_type = result_type;

            lily_prop_entry *prop = lily_find_property(emit->symtab,
                    lookup_class, oo_name);

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

/*****************************************************************************/
/* Tree evaluation functions (and tree-related helpers).                     */
/*****************************************************************************/

/*  emit_binary_op
    This is called to handle simple binary ops (except for assign). Compound
    ops will route through here via emit_op_for_compound, and depend on this
    function NOT doing any evaluation. */
static void emit_binary_op(lily_emit_state *emit, lily_ast *ast)
{
    int opcode;
    lily_class *lhs_class, *rhs_class, *storage_class;
    lily_storage *s;

    lhs_class = ast->left->result->type->cls;
    rhs_class = ast->right->result->type->cls;

    if (lhs_class->id <= SYM_CLASS_STRING &&
        rhs_class->id <= SYM_CLASS_STRING)
        opcode = generic_binop_table[ast->op][lhs_class->id][rhs_class->id];
    else {
        /* Calling type_matchup here to do the test allows 'any' to compare to
           base values, as well as enum classes to compare to instances of
           their inner subtypes.
           Call it twice for each side so that this works:
               any a = 10
               a == 10
               10 == a */
        if (ast->left->result->type == ast->right->result->type ||
            type_matchup(emit, ast->left->result->type, ast->right) ||
            type_matchup(emit, ast->right->result->type, ast->left)) {
            if (ast->op == expr_eq_eq)
                opcode = o_is_equal;
            else if (ast->op == expr_not_eq)
                opcode = o_not_eq;
            else
                opcode = -1;
        }
        else
            opcode = -1;
    }

    if (opcode == -1)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                   "Invalid operation: ^T %s ^T.\n", ast->left->result->type,
                   opname(ast->op), ast->right->result->type);

    if (ast->op == expr_plus || ast->op == expr_minus ||
        ast->op == expr_multiply || ast->op == expr_divide)
        if (lhs_class->id >= rhs_class->id)
            storage_class = lhs_class;
        else
            storage_class = rhs_class;
    else
        /* assign is handled elsewhere, so these are just comparison ops. These
           always return 0 or 1, regardless of the classes put in. There's no
           bool class (yet), so an integer class is used instead. */
        storage_class = emit->symtab->integer_class;

    s = get_storage(emit, storage_class->type);
    s->flags |= SYM_NOT_ASSIGNABLE;

    write_5(emit,
            opcode,
            ast->line_num,
            ast->left->result->reg_spot,
            ast->right->result->reg_spot,
            s->reg_spot);

    ast->result = (lily_sym *)s;
}

/*  emit_op_for_compound
    Examples: +=, -=, *=, /=, etc.

    X Y= Z
    can be folded into
    X = X Y Z

    This allows the vm to not have compound expression opcodes. This assumes
    that the left and the right have already been walked. */
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

/*  assign_optimize_check
    ALL opcodes that return a result always have the result as the last value
    written. This is no accident: There are many cases where the emitter makes
    a storage that isn't needed.

    This function determines if an assignment can be optimized out by rewriting
    the last emitted opcode to return to what would have been assigned to. */
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

        /* If the parent is binary, then it is an assignment or compound op.
           Those eval from right-to-left, so leave them alone. */
        if (ast->parent != NULL && ast->parent->tree_type == tree_binary) {
            can_optimize = 0;
            break;
        }

        /* Also check if the right side is an assignment or compound op. */
        if (right_tree->tree_type == tree_binary &&
            right_tree->op >= expr_assign) {
            can_optimize = 0;
            break;
        }

        /* If the left is an any and the right is not, then don't reduce.
           Any assignment is written so that it puts the right side into a
           container. */
        if (ast->left->result->type->cls->id == SYM_CLASS_ANY &&
            right_tree->result->type->cls->id != SYM_CLASS_ANY) {
            can_optimize = 0;
            break;
        }
    } while (0);

    return can_optimize;
}

/*  calculate_var_type
    This is called when the left side of an assignment doesn't have a type
    because it was declared using 'var ...'. This will return the proper
    type for the left side of the expression. */
static lily_type *calculate_var_type(lily_emit_state *emit, lily_type *input_type)
{
    lily_type *result;
    if (input_type->cls->flags & CLS_VARIANT_CLASS)
        result = lily_ts_build_enum_by_variant(emit->ts, input_type);
    else
        result = input_type;

    return result;
}

/*  eval_assign
    This handles assignments where the left is not a subscript or dot access. */
static void eval_assign(lily_emit_state *emit, lily_ast *ast)
{
    int left_cls_id, opcode;
    lily_sym *left_sym, *right_sym;
    opcode = -1;

    if (ast->left->tree_type != tree_global_var &&
        ast->left->tree_type != tree_local_var) {
        /* If the left is complex and valid, it would have been sent off to a
           different assign. Ergo, it must be invalid. */
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Left side of %s is not assignable.\n", opname(ast->op));
    }

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, ast->left->result->type, 1);

    /* For 'var <name> = ...', fix the type. */
    if (ast->left->result->type == NULL)
        ast->left->result->type = calculate_var_type(emit,
                ast->right->result->type);

    ast->left->result->flags &= ~SYM_NOT_INITIALIZED;

    left_sym = ast->left->result;
    right_sym = ast->right->result;
    left_cls_id = left_sym->type->cls->id;

    if (left_sym->type != right_sym->type) {
        if (left_sym->type->cls->id == SYM_CLASS_ANY) {
            /* Bare variants are not allowed, and type_matchup is a bad idea
               here. Rebox the variant into an enum, then let assign do the
               rest of the magic.
               The reason that type_matchup is a bad idea is that it will box
               the variant into an enum, then an any, which will be the target
               of the assign. This results in a junk storage. */
            if (right_sym->type->cls->flags & CLS_VARIANT_CLASS) {
                rebox_variant_to_enum(emit, ast->right);
                right_sym = ast->right->result;
            }

            opcode = o_assign;
        }
        else if (type_matchup(emit, ast->left->result->type, ast->right)) {
            /* type_matchup may update the result, so update the cache. */
            right_sym = ast->right->result;
        }
        else
            bad_assign_error(emit, ast->line_num, left_sym->type,
                    right_sym->type);
    }

    if (opcode == -1) {
        if (left_cls_id == SYM_CLASS_INTEGER ||
            left_cls_id == SYM_CLASS_DOUBLE)
            opcode = o_fast_assign;
        else
            opcode = o_assign;
    }

    if (ast->op > expr_assign) {
        if (ast->left->tree_type == tree_global_var)
            eval_tree(emit, ast->left, NULL, 1);

        emit_op_for_compound(emit, ast);
        right_sym = ast->result;
    }

    if (ast->left->tree_type == tree_global_var)
        opcode = o_set_global;

    /* If assign can be optimized out, then rewrite the last result to point to
       the left side. */
    if (assign_optimize_check(ast))
        emit->code[emit->code_pos-1] = left_sym->reg_spot;
    else {
        write_4(emit,
                opcode,
                ast->line_num,
                right_sym->reg_spot,
                left_sym->reg_spot);
    }
    ast->result = right_sym;
}

/*  This is an access like 'abc.xyz'. There are two fairly different cases for
    this:
    1: The given class has a method named xyz. This is checked first.
       Examples: 'string.concat' and 'integer.to_string'.
    2: The given class has a property named xyz. In this case, the value is a
       class which is subscripted for the right property.

    This stores either the method var or the property within the ast's item. */
static void eval_oo_access_for_item(lily_emit_state *emit, lily_ast *ast)
{
    if (emit->lambda_depth && ast->arg_start->tree_type == tree_self)
        maybe_close_over_class_self(emit);

    if (ast->arg_start->tree_type != tree_local_var)
        eval_tree(emit, ast->arg_start, NULL, 1);

    lily_class *lookup_class = ast->arg_start->result->type->cls;
    char *oo_name = lily_membuf_get(emit->ast_membuf, ast->membuf_pos);
    lily_var *var = lily_find_class_callable(emit->symtab,
            lookup_class, oo_name);

    /* Is this an attempt to access a method that hasn't been loaded yet? */
    if (var == NULL)
        var = lily_parser_dynamic_load(emit->parser, lookup_class, oo_name);

    if (var == NULL) {
        lily_prop_entry *prop = lily_find_property(emit->symtab,
                lookup_class, oo_name);

        if (prop == NULL) {
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Class %s has no method or property named %s.\n",
                    lookup_class->name, oo_name);
        }

        if (ast->arg_start->tree_type == tree_self)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Use @<name> to get/set properties, not self.<name>.\n");

        ast->item = (lily_item *)prop;
    }
    else
        ast->item = (lily_item *)var;
}

/* This is called on trees of type tree_oo_access which have their ast->item
   as a property. This will solve the property with the type that lead up to
   it.

   class example[A](value: A) { var contents = value }

   var v = example::new(10).contents

   'contents' has type 'A' relative to whatever type that 'example' contains.
   Since example has type 'integer', then 'v' is solved to be of type integer.

   This is a convenience function to avoid creating a storage for the property
   when that may not be wanted. */
static lily_type *get_solved_property_type(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *property_type = ast->property->type;
    if (property_type->flags & TYPE_IS_UNRESOLVED) {
        property_type = lily_ts_resolve_by_second(emit->ts,
                ast->arg_start->result->type, property_type);
    }

    return property_type;
}

/* This is called after the caller has run eval_oo_access_for_item and has
   determined that they want the property in a storage. This does that. */
static void oo_property_read(lily_emit_state *emit, lily_ast *ast)
{
    lily_prop_entry *prop = ast->property;
    lily_type *type = get_solved_property_type(emit, ast);
    lily_storage *result = get_storage(emit, type);

    /* This function is only called on trees of type tree_oo_access which have
       a property into the ast's item. */
    write_5(emit, o_get_property, ast->line_num,
            ast->arg_start->result->reg_spot, prop->id, result->reg_spot);

    ast->result = (lily_sym *)result;
}

/* This handles tree_oo_access for eval_tree. The contents are always dumped
   into a storage, unlike with some users of tree_oo_access. */
static void eval_oo_access(lily_emit_state *emit, lily_ast *ast)
{
    eval_oo_access_for_item(emit, ast);
    /* An 'x.y' access will either yield a property or a class method. */
    if (ast->item->flags & ITEM_TYPE_PROPERTY)
        oo_property_read(emit, ast);
    else {
        lily_storage *result = get_storage(emit, ast->sym->type);
        write_4(emit, o_get_readonly, ast->line_num, ast->sym->reg_spot,
                result->reg_spot);
        ast->result = (lily_sym *)result;
    }
}

/*  eval_property
    This handles evaluating '@<x>' within a either a class constructor or a
    function/method defined within a class. */
static void eval_property(lily_emit_state *emit, lily_ast *ast)
{
    if (emit->lambda_depth && ast->left->tree_type == tree_self);
        maybe_close_over_class_self(emit);

    if (ast->property->type == NULL)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Invalid use of uninitialized property '@%s'.\n",
                ast->property->name);

    lily_storage *result = get_storage(emit, ast->property->type);

    write_5(emit,
            o_get_property,
            ast->line_num,
            emit->block->self->reg_spot,
            ast->property->id,
            result->reg_spot);

    ast->result = (lily_sym *)result;
}

/*  This is called to handle assignments when the left side is of type
    tree_oo_access (ex: x[0].y = z, a.b.c = d). */
static void eval_oo_assign(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *left_type;

    eval_oo_access_for_item(emit, ast->left);
    left_type = ast->left->property->type;
    if ((ast->left->item->flags & ITEM_TYPE_PROPERTY) == 0)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Left side of %s is not assignable.\n", opname(ast->op));

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, left_type, 1);

    left_type = get_solved_property_type(emit, ast->left);
    lily_sym *rhs = ast->right->result;
    lily_type *right_type = rhs->type;

    if (left_type != right_type && left_type->cls->id != SYM_CLASS_ANY) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, left_type,
                         right_type);
    }

    if (ast->op > expr_assign) {
        oo_property_read(emit, ast->left);
        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    write_5(emit,
            o_set_property,
            ast->line_num,
            ast->left->arg_start->result->reg_spot,
            ast->left->property->id,
            rhs->reg_spot);

    ast->result = rhs;
}

/* This handles assignments like '@x = y'. These are always simple, because if
   it was something like '@x[y] = z', then subscript assign would get it.
   The left side is always just a property. */
static void eval_property_assign(lily_emit_state *emit, lily_ast *ast)
{
    if (emit->lambda_depth)
        maybe_close_over_class_self(emit);

    lily_type *left_type = ast->left->property->type;
    lily_sym *rhs;

    if (ast->right->tree_type != tree_local_var)
        /* Important! Expecting the lhs will auto-fix the rhs if needed. */
        eval_tree(emit, ast->right, left_type, 1);

    rhs = ast->right->result;
    lily_type *right_type = ast->right->result->type;
    /* For 'var @<name> = ...', fix the type of the property. */
    if (left_type == NULL) {
        ast->left->property->type = right_type;
        ast->left->property->flags &= ~SYM_NOT_INITIALIZED;
        left_type = right_type;
    }

    if (left_type != right_type && left_type->cls->id != SYM_CLASS_ANY) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, left_type,
                         right_type);
    }

    if (ast->op > expr_assign) {
        eval_tree(emit, ast->left, NULL, 1);
        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    write_5(emit,
            o_set_property,
            ast->line_num,
            emit->block->self->reg_spot,
            ast->left->property->id,
            rhs->reg_spot);

    ast->result = rhs;
}

static void eval_upvalue_assign(lily_emit_state *emit, lily_ast *ast)
{
    eval_tree(emit, ast->right, NULL, 1);

    int spot;
    lily_sym *left_sym = ast->left->sym;
    if (ast->left->tree_type == tree_open_upvalue) {
        close_over_sym(emit, left_sym);
        spot = emit->closed_pos - 1;
    }
    else
        spot = find_closed_sym_spot(emit, ast->left->sym);

    lily_sym *rhs = ast->right->result;

    if (ast->op > expr_assign) {
        /* Don't call eval_tree again, because if the left is tree_open_upvalue,
           the left will be closed over again. That will result in the compound
           op using the wrong upvalue spot, which is bad. */
        lily_storage *s = get_storage(emit, ast->left->sym->type);
        write_4(emit, o_get_upvalue, ast->line_num, spot, s->reg_spot);
        ast->left->result = (lily_sym *)s;
        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    write_4(emit, o_set_upvalue, ast->line_num, spot, rhs->reg_spot);

    ast->result = ast->right->result;
}

/*  eval_logical_op
    This handles || (or) as well as && (and). */
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
        lily_emit_enter_block(emit, BLOCK_ANDOR);
    }
    else
        is_top = 0;

    if (ast->left->tree_type != tree_local_var)
        eval_tree(emit, ast->left, NULL, 1);

    /* If the left is the same as this tree, then it's already checked itself
       and doesn't need a retest. However, and/or are opposites, so they have
       to check each other (so the op has to be exactly the same). */
    if ((ast->left->tree_type == tree_binary && ast->left->op == ast->op) == 0)
        emit_jump_if(emit, ast->left, jump_on);

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, NULL, 1);

    emit_jump_if(emit, ast->right, jump_on);

    if (is_top == 1) {
        int save_pos;
        lily_tie *success_lit, *failure_lit;
        lily_symtab *symtab = emit->symtab;

        result = get_storage(emit, symtab->integer_class->type);

        success_lit = lily_get_integer_literal(symtab,
                (ast->op == expr_logical_and));
        failure_lit = lily_get_integer_literal(symtab,
                (ast->op == expr_logical_or));

        write_4(emit,
                o_get_readonly,
                ast->line_num,
                success_lit->reg_spot,
                result->reg_spot);

        write_2(emit, o_jump, 0);
        save_pos = emit->code_pos - 1;

        lily_emit_leave_block(emit);
        write_4(emit,
                o_get_readonly,
                ast->line_num,
                failure_lit->reg_spot,
                result->reg_spot);
        emit->code[save_pos] = emit->code_pos - emit->block->jump_offset;
        ast->result = (lily_sym *)result;
    }
    else
        /* If is_top is false, then this tree has a parent that's binary and
           has the same op. The parent won't write a jump_if for this tree,
           because that would be a double-test.
           Setting this to NULL anyway as a precaution. */
        ast->result = NULL;
}

/*  emit_sub_assign
    This handles an assignment where the left side has a subscript involved
    (ex: x[0] = 10). This handles compound ops as well as all subscript
    assigning types (list, hash, and tuple)

    There are three parts: The var, the index, and the new value (right).

    Var:   ast->left->arg_start
    Index: ast->left->arg_start->next
    Right: ast->right */
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
        eval_tree(emit, ast->right, left_type, 1);

    rhs = ast->right->result;

    if (var_ast->tree_type != tree_local_var) {
        eval_tree(emit, var_ast, NULL, 1);
        if (var_ast->result->flags & SYM_NOT_ASSIGNABLE) {
            lily_raise_adjusted(emit->raiser, ast->line_num,
                    lily_SyntaxError,
                    "Left side of %s is not assignable.\n", opname(ast->op));
        }
    }

    if (index_ast->tree_type != tree_local_var)
        eval_tree(emit, index_ast, NULL, 1);

    check_valid_subscript(emit, var_ast, index_ast);

    elem_type = get_subscript_result(var_ast->result->type, index_ast);

    if (elem_type != rhs->type && elem_type->cls->id != SYM_CLASS_ANY) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, elem_type,
                         rhs->type);
    }

    if (ast->op > expr_assign) {
        /* For a compound assignment to work, the left side must be subscripted
           to get the value held. */

        lily_storage *subs_storage = get_storage(emit, elem_type);

        write_5(emit,
                o_get_item,
                ast->line_num,
                var_ast->result->reg_spot,
                index_ast->result->reg_spot,
                subs_storage->reg_spot);

        ast->left->result = (lily_sym *)subs_storage;

        /* Run the compound op now that ->left is set properly. */
        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    write_5(emit,
            o_set_item,
            ast->line_num,
            var_ast->result->reg_spot,
            index_ast->result->reg_spot,
            rhs->reg_spot);

    ast->result = rhs;
}

/*  eval_typecast
    This handles writing a typecast. A typecast has two parts:
    Value:     ast->arg_start
    type: ast->arg_start->next_arg->type */
static void eval_typecast(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *cast_type = ast->arg_start->next_arg->typecast_type;
    lily_ast *right_tree = ast->arg_start;
    if (right_tree->tree_type != tree_local_var)
        eval_tree(emit, right_tree, NULL, 1);

    lily_type *var_type = right_tree->result->type;

    if (cast_type == var_type)
        ast->result = (lily_sym *)right_tree->result;
    else if (cast_type->cls->id == SYM_CLASS_ANY) {
        /* This function automatically fixes right_tree's result to the
           new any value. */
        emit_rebox_to_any(emit, right_tree);
        ast->result = right_tree->result;
    }
    else if (var_type->cls->id == SYM_CLASS_ANY) {
        lily_storage *result = get_storage(emit, cast_type);

        write_4(emit, o_any_typecast, ast->line_num,
                right_tree->result->reg_spot, result->reg_spot);
        ast->result = (lily_sym *)result;
    }
    else {
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Cannot cast type '^T' to type '^T'.\n", var_type, cast_type);
    }
}

/*  eval_unary_op
    This handles unary ops. Unary ops currently only work on integers. */
static void eval_unary_op(lily_emit_state *emit, lily_ast *ast)
{
    uint16_t opcode;
    lily_class *lhs_class;
    lily_storage *storage;
    lhs_class = ast->left->result->type->cls;
    lily_class *integer_class = emit->symtab->integer_class;

    if (lhs_class != integer_class)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Invalid operation: %s%s.\n",
                opname(ast->op), lhs_class->name);

    storage = get_storage(emit, integer_class->type);
    storage->flags |= SYM_NOT_ASSIGNABLE;

    if (ast->op == expr_unary_minus)
        opcode = o_unary_minus;
    else if (ast->op == expr_unary_not)
        opcode = o_unary_not;
    else
        opcode = -1;

    write_4(emit,
            opcode,
            ast->line_num,
            ast->left->result->reg_spot,
            storage->reg_spot);

    ast->result = (lily_sym *)storage;
}

/*  rebox_enum_variant_values
    This function is called when building a list or a hash and the values
    contain at least one variant or enum value.
    In the event that there is not a common type, the function attempts
    to find one by looking at the common parts between each value.

    If all values are of a given enum class or variants of that class, then
    the function ensures that the variants are put into an enum class value of
    the common type.

    If the common type is incomplete (some of the generics of the enum class
    are not specified), then missing parts are given the class 'any', and the
    values are put into an enum class value of some type.

    If there is no common type, then each variant is put into an enum class
    value based upon information known to only it, and all values are put into
    an 'any' value (except those that are already 'any'). This is unlikely. */
static void rebox_enum_variant_values(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type, int is_hash)
{
    lily_ast *tree_iter = ast->arg_start;
    lily_type *rebox_type = NULL;
    lily_class *any_class = emit->symtab->any_class;

    /* If ast is tree_hash (is_hash == 1), then the values are key, value, key
       value, and so on. This is about the values, not the keys. */
    if (is_hash)
        tree_iter = tree_iter->next_arg;

    /* Raise the ceiling so that lily_ts_match doesn't damage the current
       generic information. */
    int adjust = lily_ts_raise_ceiling(emit->ts);
    lily_class *first_cls = tree_iter->result->type->cls;
    lily_type *matching_type = NULL;
    int ok = 1;

    /* The first order of business is to find the type that parser created which
       has a class of the enum class, and all generics.
       ex: enum class Option[A] { Some(A), None }
       For the above, there's a Option[A] made by parser. Get that. If that
       isn't possible, then everything gets to be smacked to any. */
    if (first_cls->flags & CLS_VARIANT_CLASS)
        first_cls = first_cls->parent;
    if (first_cls->flags & CLS_ENUM_CLASS &&
        first_cls != any_class) {
        matching_type = first_cls->variant_type;
    }
    else
        ok = 0;

    if (matching_type != NULL) {
        /* lily_ts_check is awesome. It makes sure that stuff matches while also
           solving stuff. Begin by throwing in what the caller wants (if the
           caller knows what they want). This is important, because the caller
           may want Option[integer] but have [None, None, None]. The three None
           values should upgrade to Option[integer], not Option[any] as they
           would do otherwise. */
        if (expect_type)
            lily_ts_check(emit->ts, matching_type, expect_type);

        while (tree_iter != NULL) {
            lily_type *type = tree_iter->result->type;
            /* If there's some disagreement, give up and let everything default
               to any. */
            if (lily_ts_check(emit->ts, matching_type, type) == 0) {
                ok = 0;
                break;
            }

            tree_iter = tree_iter->next_arg;
            if (is_hash && tree_iter)
                tree_iter = tree_iter->next_arg;
        }
    }

    /* If there are some generics unresolved (ex: [None, None, None] where
       there ISN'T a caller value to infer from), then lily_ts_resolve helps out
       by defaulting the unsolved generics to type any. */
    if (ok)
        rebox_type = lily_ts_resolve(emit->ts, matching_type);
    else
        rebox_type = any_class->type;

    tree_iter = ast->arg_start;
    if (is_hash)
        tree_iter = tree_iter->next_arg;

    /* Bash everything into the appropriate type. emit_rebox_value will have the
       variant types first boxed into an enum based off of their individual info
       before shoving them into an any. */
    while (tree_iter) {
        if (tree_iter->result->type != rebox_type)
            emit_rebox_value(emit, rebox_type, tree_iter);

        tree_iter = tree_iter->next_arg;
        if (is_hash && tree_iter)
            tree_iter = tree_iter->next_arg;
    }

    lily_ts_lower_ceiling(emit->ts, adjust);
}

/*  hash_values_to_anys

    This converts all of the values of the given ast into anys using
    o_assign. The result of each value is rewritten to be the any,
    instead of the old value.

    emit:     The emitter holding the function to write code to.
    hash_ast: An ast of type tree_hash which has already been evaluated.

    Caveats:
    * Caller must do this before writing the o_build_hash instruction out.
    * Caller must evaluate the hash before calling this. */
static void emit_hash_values_to_anys(lily_emit_state *emit,
        lily_ast *hash_ast)
{
    /* The keys and values are in hash_ast as args. Since they're in pairs and
       this only modifies the values, this is how many values there are. */
    int value_count = hash_ast->args_collected / 2;

    /* Make a single large prep that will cover everything needed. This ensures
       that any growing will be done all at once, instead of in smaller
       blocks. */
    write_prep(emit, value_count * 4);

    lily_ast *iter_ast;
    for (iter_ast = hash_ast->arg_start;
         iter_ast != NULL;
         iter_ast = iter_ast->next_arg->next_arg) {

        emit_rebox_to_any(emit, iter_ast->next_arg);
    }
}

/*  emit_list_values_to_anys

    This converts all of the values of the given ast into anys using
    o_assign. The result of each value is rewritten to be the any, instead of
    the old value.

    emit:     The emitter holding the function to write code to.
    list_ast: An ast of type tree_list which has already been evaluated.

    Caveats:
    * Caller must do this before writing the o_build_list_tuple instruction.
    * Caller must evaluate the list before calling this. */
static void emit_list_values_to_anys(lily_emit_state *emit,
        lily_ast *list_ast)
{
    int value_count = list_ast->args_collected;

    write_prep(emit, value_count * 4);

    lily_ast *iter_ast;
    for (iter_ast = list_ast->arg_start;
         iter_ast != NULL;
         iter_ast = iter_ast->next_arg) {
        emit_rebox_to_any(emit, iter_ast);
    }
}

/*  eval_build_hash
    This handles evaluating trees that are of type tree_hash. This tree is
    created from a static hash (ex: ["a" => 1, "b" => 2, ...]). Parser has
    chained the keys and values in a tree_hash as arguments. The arguments are
    key, value, key, value, key, value. Thus, ->args_collected is the number of
    items, not the number of pairs collected.

    Caveats:
    * Keys can't default to "any", because "any" is not immutable.

    emit: The emit state containing a function to write the resulting code to.
    ast:  An ast of type tree_hash. */
static void eval_build_hash(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type, int did_resolve)
{
    lily_ast *tree_iter;

    lily_type *last_key_type = NULL, *last_value_type = NULL,
             *expect_key_type = NULL, *expect_value_type = NULL;
    int make_anys = 0, found_variant_or_enum = 0;

    if (expect_type) {
        int ok = setup_types_for_build(emit, expect_type, SYM_CLASS_HASH,
                did_resolve);

        if (ok) {
            expect_key_type = lily_ts_get_ceiling_type(emit->ts, 0);
            expect_value_type = lily_ts_get_ceiling_type(emit->ts, 1);
        }
    }

    for (tree_iter = ast->arg_start;
         tree_iter != NULL;
         tree_iter = tree_iter->next_arg->next_arg) {

        lily_ast *key_tree, *value_tree;
        key_tree = tree_iter;
        value_tree = tree_iter->next_arg;

        if (key_tree->tree_type != tree_local_var)
            eval_tree(emit, key_tree, expect_key_type, 1);

        /* Keys -must- all be the same type. They cannot be converted to any
           later on because any are not valid keys (not immutable). */
        if (key_tree->result->type != last_key_type) {
            if (last_key_type == NULL) {
                if ((key_tree->result->type->cls->flags & CLS_VALID_HASH_KEY) == 0) {
                    lily_raise_adjusted(emit->raiser, key_tree->line_num,
                            lily_SyntaxError,
                            "Resulting type '^T' is not a valid hash key.\n",
                            key_tree->result->type);
                }

                last_key_type = key_tree->result->type;
            }
            else {
                lily_raise_adjusted(emit->raiser, key_tree->line_num,
                        lily_SyntaxError,
                        "Expected a key of type '^T', but key is of type '^T'.\n",
                        last_key_type, key_tree->result->type);
            }
        }

        if (value_tree->tree_type != tree_local_var)
            eval_tree(emit, value_tree, expect_value_type, 1);

        /* Only mark user-defined enum classes/variants, because those are the
           ones that can default. */
        if (value_tree->result->type->cls->flags &
            (CLS_VARIANT_CLASS | CLS_ENUM_CLASS) &&
            value_tree->result->type->cls->id != SYM_CLASS_ANY)
            found_variant_or_enum = 1;

        /* Values being promoted to any is okay though. :) */
        if (value_tree->result->type != last_value_type) {
            if (last_value_type == NULL)
                last_value_type = value_tree->result->type;
            else
                make_anys = 1;
        }
    }

    if (ast->args_collected == 0) {
        last_key_type = expect_key_type;
        last_value_type = expect_value_type;
    }
    else {
        if (found_variant_or_enum)
            rebox_enum_variant_values(emit, ast, expect_value_type, 1);
        else if (make_anys ||
                 (expect_value_type &&
                  expect_value_type->cls->id == SYM_CLASS_ANY))
            emit_hash_values_to_anys(emit, ast);

        last_value_type = ast->arg_start->next_arg->result->type;
    }

    lily_class *hash_cls = emit->symtab->hash_class;
    lily_ts_set_ceiling_type(emit->ts, last_key_type, 0);
    lily_ts_set_ceiling_type(emit->ts, last_value_type, 1);
    lily_type *new_type = lily_ts_build_by_ceiling(emit->ts, hash_cls, 2, 0);

    lily_storage *s = get_storage(emit, new_type);

    write_build_op(emit, o_build_hash, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

/*  check_proper_variant
    Make sure that the variant has the proper inner type to satisfy the
    type wanted by the enum. */
static int check_proper_variant(lily_emit_state *emit, lily_type *enum_type,
        lily_type *given_type, lily_class *variant_cls)
{
    lily_type *variant_type = variant_cls->variant_type;
    int i, result = 1;

    if (variant_type->subtype_count != 0) {
        lily_type *variant_result = variant_type->subtypes[0];
        for (i = 0;i < variant_result->subtype_count;i++) {
            /* The variant may not have all the generics that the parent does.
               Consider the variant to be proper if the generics that it has
               match up to the enum type.
               Ex: For SomeVariant[B] and SomeEnum[A, B], consider it right if
                   the B's match. */
            int pos = variant_result->subtypes[i]->generic_pos;
            if (given_type->subtypes[i] != enum_type->subtypes[pos]) {
                result = 0;
                break;
            }
        }
    }
    /* else the variant takes no generics, and nothing can be wrong. */

    return result;
}

/*  enum_membership_check
    Given a type which is for some enum class, determine if 'right'
    is a member of the enum class.

    Returns 1 if yes, 0 if no. */
static int enum_membership_check(lily_emit_state *emit, lily_type *enum_type,
        lily_type *right)
{
    lily_class *variant_class = right->cls;
    lily_class *enum_class = enum_type->cls;

    int ok = 1;

    /* A variant's parent is always the enum class that it belongs to. */
    if (variant_class->parent == enum_class) {
        /* If the variant does not take arguments, then there's nothing that
           could have been called wrong. Therefore, the use of the variant MUST
           be correct. */
        if (right->subtype_count != 0)
            ok = check_proper_variant(emit, enum_type, right, variant_class);
    }
    else
        ok = 0;

    return ok;
}

/*  type_matchup
    This is called when 'right' doesn't have quite the right type.
    If the wanted type is 'any', the value of 'right' is made into an any.

    On success: right is fixed, 1 is returned.
    On failure: right isn't fixed, 0 is returned. */
static int type_matchup(lily_emit_state *emit, lily_type *want_type,
        lily_ast *right)
{
    int ret = 1;
    if (want_type->cls->id == SYM_CLASS_ANY)
        emit_rebox_to_any(emit, right);
    else if (want_type->cls->flags & CLS_ENUM_CLASS) {
        ret = enum_membership_check(emit, want_type, right->result->type);
        if (ret)
            emit_rebox_value(emit, want_type, right);
    }
    else
        ret = 0;

    return ret;
}

/*  eval_build_list
    This writes an instruction to build a list from a set of values given.

    If all list elements have the same type, the resulting list shall be of the
    common type (Ex: [1, 2, 3] is a list[integer]).

    If they do not, the resulting type shall be list[any]. */
static void eval_build_list(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type, int did_resolve)
{
    lily_type *elem_type = NULL;
    lily_ast *arg;
    int found_variant_or_enum = 0, make_anys = 0;

    if (expect_type) {
        if (ast->args_collected == 0) {
            lily_type *check_type;
            if (expect_type->cls->id == SYM_CLASS_GENERIC &&
                did_resolve == 0) {
                check_type = lily_ts_easy_resolve(emit->ts, expect_type);
            }
            else
                check_type = expect_type;

            if (check_type && check_type->cls->id == SYM_CLASS_HASH) {
                eval_build_hash(emit, ast, expect_type, 1);
                return;
            }
        }

        int ok = setup_types_for_build(emit, expect_type, SYM_CLASS_LIST,
                did_resolve);
        if (ok) {
            elem_type = lily_ts_get_ceiling_type(emit->ts, 0);
            expect_type = elem_type;
        }
    }

    lily_type *last_type = NULL;

    for (arg = ast->arg_start;arg != NULL;arg = arg->next_arg) {
        if (arg->tree_type != tree_local_var)
            eval_tree(emit, arg, elem_type, 1);

        /* 'any' is marked as an enum class, but this is only interested in
           user-defined enum classes (which have special defaulting). */
        if ((arg->result->type->cls->flags & (CLS_ENUM_CLASS | CLS_VARIANT_CLASS)) &&
            arg->result->type->cls->id != SYM_CLASS_ANY)
            found_variant_or_enum = 1;

        if (arg->result->type != last_type) {
            if (last_type == NULL)
                last_type = arg->result->type;
            else
                make_anys = 1;
        }
    }

    if (elem_type == NULL && last_type == NULL) {
        /* This happens when there's an empty list and a list is probably not
           expected. Default to list[any] and hope that's right. */
        lily_class *cls = emit->symtab->any_class;
        elem_type = cls->type;
    }
    else if (last_type != NULL) {
        if (found_variant_or_enum)
            rebox_enum_variant_values(emit, ast, expect_type, 0);
        else if (make_anys ||
                 (elem_type && elem_type->cls->id == SYM_CLASS_ANY))
            emit_list_values_to_anys(emit, ast);
        /* else all types already match, so nothing to do. */

        /* At this point, all list values are guaranteed to have the same
           type, so this works. */
        elem_type = ast->arg_start->result->type;
    }

    lily_ts_set_ceiling_type(emit->ts, elem_type, 0);
    lily_type *new_type = lily_ts_build_by_ceiling(emit->ts,
            emit->symtab->list_class, 1, 0);

    lily_storage *s = get_storage(emit, new_type);

    write_build_op(emit, o_build_list_tuple, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

/*  eval_build_tuple
    This handles creation of a tuple from a series of values. The resulting
    tuple will have a type that matches what it obtained.

    <[1, "2", 3.3]> # tuple[integer, string, double]

    This attempts to do the same sort of defaulting that eval_build_list and
    eval_build_hash do:

    tuple[any] t = <[1]> # Becomes tuple[any]. */
static void eval_build_tuple(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type, int did_unwrap)
{
    if (ast->args_collected == 0) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "Cannot create an empty tuple.\n");
    }

    /* It is not possible to use setup_types_for_build here, because tuple takes
       N types and subtrees may damage those types. Those types
       also cannot be hidden, because the expected type may contain
       generics that the callee may attempt to check the resolution of.
       Just...don't unwrap things more than once here. */

    if (expect_type && expect_type->cls->id == SYM_CLASS_GENERIC &&
        did_unwrap == 0) {
        expect_type = lily_ts_easy_resolve(emit->ts, expect_type);
        did_unwrap = 1;
    }

    if (expect_type &&
         (expect_type->cls->id != SYM_CLASS_TUPLE ||
          expect_type->subtype_count != ast->args_collected))
        expect_type = NULL;

    int i;
    lily_ast *arg;

    for (i = 0, arg = ast->arg_start;
         arg != NULL;
         i++, arg = arg->next_arg) {
        lily_type *elem_type = NULL;

        /* It's important to do this for each pass because it allows the inner
           trees to infer types that this tree's parent may want. */
        if (expect_type) {
            elem_type = expect_type->subtypes[i];
            if (did_unwrap == 0 && elem_type &&
                elem_type->cls->id == SYM_CLASS_GENERIC) {
                elem_type = lily_ts_easy_resolve(emit->ts, elem_type);
            }
        }

        if (arg->tree_type != tree_local_var)
            eval_tree(emit, arg, elem_type, 1);

        if (elem_type && elem_type != arg->result->type)
            /* Attempt to fix the type to what's wanted. If it fails, the parent
               tree will note a type mismatch. Can't do anything else here
               though. */
            type_matchup(emit, elem_type, arg);
    }

    for (i = 0, arg = ast->arg_start;
         i < ast->args_collected;
         i++, arg = arg->next_arg) {
        lily_ts_set_ceiling_type(emit->ts, arg->result->type, i);
    }

    lily_type *new_type = lily_ts_build_by_ceiling(emit->ts,
            emit->symtab->tuple_class, i, 0);
    lily_storage *s = get_storage(emit, new_type);

    write_build_op(emit, o_build_list_tuple, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

/*  eval_subscript
    Evaluate a subscript, returning the resulting value. This handles
    subscripts of list, hash, and tuple. */
static void eval_subscript(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type)
{
    lily_ast *var_ast = ast->arg_start;
    lily_ast *index_ast = var_ast->next_arg;
    if (var_ast->tree_type != tree_local_var)
        eval_tree(emit, var_ast, NULL, 1);

    if (index_ast->tree_type != tree_local_var)
        eval_tree(emit, index_ast, NULL, 1);

    check_valid_subscript(emit, var_ast, index_ast);

    lily_type *type_for_result;
    type_for_result = get_subscript_result(var_ast->result->type, index_ast);

    lily_storage *result = get_storage(emit, type_for_result);

    write_5(emit,
            o_get_item,
            ast->line_num,
            var_ast->result->reg_spot,
            index_ast->result->reg_spot,
            result->reg_spot);

    if (var_ast->result->flags & SYM_NOT_ASSIGNABLE)
        result->flags |= SYM_NOT_ASSIGNABLE;

    ast->result = (lily_sym *)result;
}

/******************************************************************************/
/* Call handling                                                              */
/******************************************************************************/

/* Function calls are rather complex things, and so they have everything under
   this area.
   To begin with, Lily supports the following kinds of calls:
   * x()          # This is a simple call.
   * x.y()        # x is an instance of some class. y is either a property of the
                  # class, or a class method. 'x' is added as the first argument
                  # of the call.
   * {|| 10} ()   # A call of a lambda. Weird, but okay.
   * x[0]()       # A call of a subscript result.
   * x()()        # A call of a call result.
   enum class Option[A] { Some(A), None }
   * x = Some(10) # A 'call' of a variant type. eval_variant handles writing out
                  # the code, but the arguments given pass through the same type
                  # checking that a regular call's arguments pass through.

   Calls are also the backbone of type inference. The arguments they expect are
   passed down to evaluated trees. List, hash, and tuple eval all use the types
   that are given to infer what they can. However, calls also need to infer a
   a little bit. One example is that if a call wants type any, but a non-any is
   supplied, it will 'rebox' the non-any into an any.

   Finally, calls must keep track of their arguments. This is kept track of
   using a min and a max. Most functions take a set number of arguments, and min
   will be the same as max. However, min is less than max for functions that
   have optional arguments. Max is set to -1 for functions that take varargs. It
   is currently not possible for a function to be both optargs and varargs. */
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

static void condense_args(lily_emit_state *emit, lily_emit_call_state *cs,
        lily_type *type, uint16_t from, uint16_t to)
{
    int i;
    int offset = (emit->call_values_pos - cs->arg_count) + from;
    int count = to - from;
    lily_storage *s = get_storage(emit, type);

    write_prep(emit, 4 + count);

    emit->code[emit->code_pos] = o_build_list_tuple;
    emit->code[emit->code_pos + 1] = cs->ast->line_num;
    emit->code[emit->code_pos + 2] = count;
    for (i = 0;i < count;i++)
        emit->code[emit->code_pos + 3 + i] =
                emit->call_values[offset + i]->reg_spot;

    /* The individual extra values are gone now... */
    emit->call_values_pos -= count;
    cs->arg_count -= count;

    /* With the list of them added in place of it. */
    add_value(emit, cs, (lily_sym *)s);

    emit->code[emit->code_pos + 3 + i] = s->reg_spot;
    emit->code_pos += 4 + i;
}

/*  eval_call_arg
    Evaluate the argument of a function call and do some type matching up on
    the result. This is different than type_matchup, because it's a fair chance
    that the arguments may hold information about generics. */
static void eval_call_arg(lily_emit_state *emit, lily_emit_call_state *cs,
        lily_ast *arg)
{
    lily_type *want_type = get_expected_type(cs, cs->arg_count);

    if (want_type->cls->id == SYM_CLASS_OPTARG)
        want_type = want_type->subtypes[0];

    if (arg->tree_type != tree_local_var)
        /* Calls fill in their type info as they go along, courteousy of their
           arguments. So the types are NEVER resolved. */
        eval_tree(emit, arg, want_type, 0);

    /* This is used so that type_matchup gets the resolved type (if there
       is one) because the resolved type might be 'any'. */
    lily_type *matchup_type = want_type;

    /* Don't allow bare variants to solve for a type. Always force them to be in
       something to prevent bare variant values. */
    if (arg->result->type->cls->flags & CLS_VARIANT_CLASS) {
        cs->have_bare_variants = 1;
        if (want_type->cls->id == SYM_CLASS_GENERIC) {
            matchup_type = lily_ts_easy_resolve(emit->ts, want_type);
            if (matchup_type == NULL)
                rebox_variant_to_enum(emit, arg);
        }
    }

    lily_type *match_type = want_type;
    if (want_type->cls->id == SYM_CLASS_GENERIC)
        match_type = lily_ts_easy_resolve(emit->ts, want_type);

    /* ok == 0 protects from potentially attempting to resolve the same generic
       twice, which breaks things. */
    if (lily_ts_check(emit->ts, want_type, arg->result->type) ||
        type_matchup(emit, match_type, arg)) {
        add_value(emit, cs, arg->result);
    }
    else
        bad_arg_error(emit, cs, arg->result->type, want_type);
}

/*  box_call_variants
    This function is called when check_call_args is done processing arguments
    AND the call has been tagged by the symtab as having enum values.

    This function exists because it's possible for a Lily function to not know
    what the resulting enum class should be. In such a case, call argument
    processing calls this to make sure any variants are put into a proper enum
    class value. */
static void box_call_variants(lily_emit_state *emit, lily_emit_call_state *cs)
{
    int num_args = cs->call_type->subtype_count - 1;
    int i;
    lily_sym *sym;
    int offset = emit->call_values_pos - cs->arg_count;
    uint32_t line_num = cs->ast->line_num;

    if (cs->vararg_start != (uint16_t)-1)
        num_args--;

    for (i = 0;i != num_args;i++) {
        sym = emit->call_values[offset + i];
        if (sym->type->cls->flags & CLS_VARIANT_CLASS) {
            lily_type *enum_type = lily_ts_resolve(emit->ts,
                    get_expected_type(cs, i));
            sym = (lily_sym *)emit_rebox_sym(emit, enum_type, sym, line_num);
            emit->call_values[offset + i] = sym;
        }
    }

    if (i != cs->arg_count &&
        cs->vararg_elem_type->cls->flags & CLS_ENUM_CLASS &&
        cs->vararg_elem_type->cls != emit->symtab->any_class) {
        lily_type *solved_elem_type = lily_ts_resolve(emit->ts,
                get_expected_type(cs, i));
        for (;i != cs->arg_count;i++) {
            sym = emit->call_values[offset + i];
            /* This is called before the varargs are shoved into a list, so
               looping over the args is fine.
               Varargs is represented as a list of some type, so this next line
               grabs the list, then what the list holds. */
            if (sym->type->cls->flags & CLS_VARIANT_CLASS) {
                sym = (lily_sym *)emit_rebox_sym(emit, solved_elem_type, sym,
                        line_num);
                emit->call_values[offset + i] = sym;
            }
        }
    }
}

/*  verify_argument_count
    This makes sure that the function being called (specified by 'ast') is being
    called with the right number of arguments. This is slightly tricky, because
    of optional arguments and variable arguments. */
static void verify_argument_count(lily_emit_state *emit,
        lily_emit_call_state *cs, int num_args)
{
    lily_type *call_type = cs->call_type;
    /* The -1 is because the return type of a function is the first type. */
    int args_needed = cs->call_type->subtype_count - 1;
    unsigned int min = args_needed;
    unsigned int max = args_needed;

    /* A function can be either varargs or optargs. They cannot coexist because
       parser does not allow a default value for varargs, and varargs must
       always be last. */
    if (call_type->flags & TYPE_HAS_OPTARGS) {
        int i;
        for (i = 1;i < call_type->subtype_count;i++) {
            if (call_type->subtypes[i]->cls->id == SYM_CLASS_OPTARG)
                break;
        }
        min = i - 1;
    }
    else if (call_type->flags & TYPE_IS_VARARGS) {
        max = (unsigned int)-1;
        min = args_needed - 1;
    }

    if (num_args < min || num_args > max) {
        push_info_to_error(emit, cs);
        lily_msgbuf *msgbuf = emit->raiser->msgbuf;

        lily_msgbuf_add(msgbuf, " expects ");

        if (max == (unsigned int)-1)
            lily_msgbuf_add_fmt(msgbuf, "at least %d args", min);
        else if (max > min)
            lily_msgbuf_add_fmt(msgbuf, "%d to %d args", min, max);
        else
            lily_msgbuf_add_fmt(msgbuf, "%d args", min);

        lily_msgbuf_add_fmt(msgbuf, ", but got %d.\n", num_args);

        emit->raiser->line_adjust = cs->ast->line_num;
        lily_raise_prebuilt(emit->raiser, lily_SyntaxError);
    }
}

/*  check_call_args
    eval_call uses this to make sure the types of all the arguments are right.

    If the function takes varargs, the extra arguments are packed into a list
    of the vararg type. */
static void eval_verify_call_args(lily_emit_state *emit, lily_emit_call_state *cs,
        lily_type *expect_type, int did_resolve)
{
    lily_ast *ast = cs->ast;
    int num_args = ast->args_collected;
    lily_tree_type call_tt = ast->arg_start->tree_type;

    if (call_tt == tree_defined_func) {
        /* Do a self insert if the thing being called belongs to this class. */
        lily_var *first_result = ((lily_var *)ast->arg_start->sym);
        if (emit->current_class != NULL &&
            emit->current_class == first_result->parent) {
            add_value(emit, cs, (lily_sym *)emit->block->self);
        }
        else
            num_args--;
    }
    else if (call_tt != tree_oo_access)
        num_args--;

    /* ast->args_collected includes the first tree in that count. If the first
       tree doesn't provide that value, then the argument count must be
       adjusted. */

    verify_argument_count(emit, cs, num_args);

    /* Since this assumes there is at least one argument needed, it has to come
       after verifying the argument count. */
    if (call_tt == tree_oo_access) {
        lily_ts_check(emit->ts, get_expected_type(cs, 0),
                ast->arg_start->arg_start->result->type);
        /* For x.y kinds of accesses, add the (evaluated) 'x' as the first
           value. */
        add_value(emit, cs, ast->arg_start->arg_start->result);
    }

    if (cs->call_type->flags & TYPE_IS_UNRESOLVED) {
        if (call_tt == tree_local_var || call_tt == tree_inherited_new) {
            /* This forces each generic to be resolved as itself. (A = A, B = B,
                etc.). This is really important.
                tree_local_var:
                    define f[A](a: function (A => A), b: A)
                If g is called, it can't resolve what A is. It gets that
                information from f. I call this being 'quasi-solved'.

                tree_inherited_new:
                    class one[A, B](a: A, b: B) { ... }
                    class two[A, B, C](a: A, b: B, c: C) < one(b, a) # INVALID

                By forcing 'two' to have the same generic ordering as 'one', Lily
                greatly simplifies generics handling. The A of one is the A of
                two. */
            lily_ts_resolve_as_self(emit->ts);
        }
        else {
            lily_type *call_result = cs->call_type->subtypes[0];
            if (call_result && expect_type && did_resolve) {
                /* If the caller wants something and the result is that same
                   sort of thing, then fill in info based on what the caller
                   wants. */
                if (expect_type->cls->id == call_result->cls->id) {
                    /* The return isn't checked because there will be a more
                       accurate problem that is likely to manifest later. */
                    lily_ts_check(emit->ts, call_result, expect_type);
                }
                else if (expect_type->cls->flags & CLS_ENUM_CLASS &&
                            call_result->cls->parent == expect_type->cls) {
                    lily_ts_resolve_as_variant_by_enum(emit->ts,
                            call_result, expect_type);
                }
            }
        }
    }

    lily_ast *arg;
    for (arg = ast->arg_start->next_arg;arg != NULL;arg = arg->next_arg)
        eval_call_arg(emit, cs, arg);

    if (cs->have_bare_variants)
        box_call_variants(emit, cs);

    if (cs->call_type->flags & TYPE_IS_VARARGS) {
        int va_pos = cs->call_type->subtype_count - 1;
        lily_type *vararg_type = cs->call_type->subtypes[va_pos];
        if (vararg_type->flags & TYPE_IS_UNRESOLVED)
            vararg_type = lily_ts_resolve(emit->ts, vararg_type);

        condense_args(emit, cs, vararg_type,
                cs->call_type->subtype_count - 2, cs->arg_count);
    }
}

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
    lily_item *debug_item = NULL;
    lily_type *call_type = NULL;

    if (first_tt == tree_defined_func || first_tt == tree_inherited_new)
        call_item = ast->arg_start->item;
    else if (first_tt == tree_oo_access) {
        eval_oo_access_for_item(emit, ast->arg_start);
        if (first_tree->item->flags & ITEM_TYPE_PROPERTY) {
            debug_item = (lily_item *)first_tree->property;
            oo_property_read(emit, first_tree);
            call_item = (lily_item *)first_tree->result;
        }
        else
            call_item = first_tree->item;
    }
    else if (first_tt != tree_variant) {
        eval_tree(emit, ast->arg_start, NULL, 1);
        call_item = (lily_item *)ast->arg_start->result;
        if (first_tt == tree_upvalue || first_tt == tree_open_upvalue)
            debug_item = ast->arg_start->item;
    }
    else {
        call_item = (lily_item *)ast->arg_start->variant_class;
        call_type = ast->arg_start->variant_class->variant_type;
    }

    if (debug_item == NULL)
        debug_item = call_item;

    if (call_type == NULL)
        call_type = ((lily_sym *)call_item)->type;

    if (call_type->cls->id != SYM_CLASS_FUNCTION &&
        first_tt != tree_variant)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Cannot anonymously call resulting type '^T'.\n",
                call_type);

    result->item = call_item;
    result->call_type = call_type;
    result->error_item = debug_item;

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

static void write_call(lily_emit_state *emit, lily_emit_call_state *cs)
{
    int offset = emit->call_values_pos - cs->arg_count;
    lily_sym *call_sym = cs->sym;
    lily_ast *ast = cs->ast;

    write_prep(emit, 6 + cs->arg_count);

    emit->code[emit->code_pos] = o_function_call;
    emit->code[emit->code_pos+1] = ast->line_num;
    emit->code[emit->code_pos+2] = !!(call_sym->flags & VAR_IS_READONLY);
    emit->code[emit->code_pos+3] = call_sym->reg_spot;
    emit->code[emit->code_pos+4] = cs->arg_count;
    int i, j;
    for (i = 5, j = 0;j < cs->arg_count;i++, j++) {
        emit->code[emit->code_pos + i] =
                emit->call_values[offset + j]->reg_spot;
    }

    if (cs->call_type->subtypes[0] != NULL) {
        lily_type *return_type = cs->call_type->subtypes[0];

        /* If it's just a generic, grab the appropriate thing from the type
           stack (which is okay until the next eval_call). Otherwise, just
           give up and build the right thing. */
        if (return_type->cls->id == SYM_CLASS_GENERIC)
            return_type = lily_ts_easy_resolve(emit->ts, return_type);
        else if (return_type->flags & TYPE_IS_UNRESOLVED)
            return_type = lily_ts_resolve(emit->ts, return_type);

        lily_storage *storage = get_storage(emit, return_type);
        storage->flags |= SYM_NOT_ASSIGNABLE;

        ast->result = (lily_sym *)storage;
        emit->code[emit->code_pos+i] = ast->result->reg_spot;
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
        emit->code[emit->code_pos+i] = -1;
    }

    emit->code_pos += 6 + cs->arg_count;
}

static void end_call(lily_emit_state *emit, lily_emit_call_state *cs)
{
    emit->call_values_pos -= cs->arg_count;
    emit->call_state = cs;
}

/*  eval_call
    This handles doing calls to what should be a function. */
static void eval_call(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type, int did_resolve)
{
    lily_tree_type first_t = ast->arg_start->tree_type;
    /* Variants are created by calling them in a function-like manner, so the
       parser adds them as if they were functions. They're not. */
    if (first_t == tree_variant) {
        eval_variant(emit, ast, expect_type, did_resolve);
        return;
    }

    int saved_ts_adjust = lily_ts_raise_ceiling(emit->ts);

    lily_emit_call_state *cs;
    cs = begin_call(emit, ast);
    eval_verify_call_args(emit, cs, expect_type, did_resolve);
    write_call(emit, cs);
    end_call(emit, cs);

    lily_ts_lower_ceiling(emit->ts, saved_ts_adjust);
}

/* emit_nonlocal_var
   This handles vars that are not local and are on the right hand side of an
   expression. This handles loading both literals and globals into a local
   register. */
static void emit_nonlocal_var(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *ret;
    int opcode;

    if (ast->tree_type == tree_global_var)
        opcode = o_get_global;
    else
        opcode = o_get_readonly;

    ret = get_storage(emit, ast->sym->type);

    if (opcode != o_get_global)
        ret->flags |= SYM_NOT_ASSIGNABLE;

    write_4(emit,
            opcode,
            ast->line_num,
            ast->sym->reg_spot,
            ret->reg_spot);

    ast->result = (lily_sym *)ret;
}

static void eval_variant(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type, int did_resolve)
{
    lily_storage *result = NULL;

    if (ast->tree_type == tree_call) {
        ast->result = NULL;

        /* The first arg is actually the variant. */
        lily_ast *variant_tree = ast->arg_start;
        lily_class *variant_class = variant_tree->variant_class;
        lily_type *variant_type = variant_class->variant_type;

        /* This is necessary because ast->item is used for retrieving info if
           there is an error. */
        ast->item = (lily_item *)variant_class;

        if (variant_type->subtype_count == 1)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Variant class %s should not get args.\n",
                    variant_class->name);

        int save_ceiling = lily_ts_raise_ceiling(emit->ts);
        lily_emit_call_state *cs;
        cs = begin_call(emit, ast);
        eval_verify_call_args(emit, cs, expect_type, did_resolve);

        lily_type *result_type = variant_class->variant_type->subtypes[0];
        if (result_type->flags & TYPE_IS_UNRESOLVED)
            result_type = lily_ts_resolve(emit->ts, result_type);

        /* This will cause all of the args to be put together in a tuple. The
           tuple will be put into emit->call_values as the most recent value. */
        condense_args(emit, cs, result_type, 0, cs->arg_count);

        result = (lily_storage *)emit->call_values[emit->call_values_pos - 1];

        end_call(emit, cs);

        lily_ts_lower_ceiling(emit->ts, save_ceiling);
    }
    else {
        /* Did this need arguments? It was used incorrectly if so. */
        lily_type *variant_init_type = ast->variant_class->variant_type;
        if (variant_init_type->subtype_count != 0)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Variant class %s needs %d arg(s).\n",
                    ast->variant_class->name,
                    variant_init_type->subtype_count - 1);

        /* If a variant type takes no arguments, then it's essentially an empty
           container. It would be rather silly to have a bunch of UNIQUE empty
           containers (which will always be empty).
           So the interpreter creates a literal and hands that off. */
        lily_type *variant_type = ast->variant_class->variant_type;
        lily_tie *variant_lit = lily_get_variant_literal(emit->symtab,
                variant_type);

        result = get_storage(emit, variant_type);
        write_4(emit, o_get_readonly, ast->line_num, variant_lit->reg_spot,
                result->reg_spot);
    }

    ast->result = (lily_sym *)result;
}

static void eval_lambda(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type, int did_resolve)
{
    char *lambda_body = lily_membuf_get(emit->ast_membuf, ast->membuf_pos);

    if (expect_type && expect_type->cls->id == SYM_CLASS_GENERIC &&
        did_resolve == 0) {
        expect_type = lily_ts_easy_resolve(emit->ts, expect_type);
        did_resolve = 1;
    }

    if (expect_type && expect_type->cls->id != SYM_CLASS_FUNCTION)
        expect_type = NULL;

    lily_sym *lambda_result = (lily_sym *)lily_parser_lambda_eval(emit->parser,
            ast->line_num, lambda_body, expect_type, did_resolve);
    lily_storage *s = get_storage(emit, lambda_result->type);

    if (emit->closed_pos) {
        /* Assume that the lambda uses either upvalues or is inside of a class
           and uses self. */
        close_over_sym(emit, lambda_result);

        /* This will create a copy of the function (the copy will get a shallow
           copy of upvalues to use). The code transformer will later rewrite the
           upcoming lambda access to use the upvalue copy. */
        write_4(emit, o_create_function, lambda_result->reg_spot, 0,
                emit->closed_pos - 1);

        inject_patch_into_block(emit, find_deepest_func(emit),
                emit->code_pos - 2);

        write_4(emit, o_get_upvalue, ast->line_num, emit->closed_pos - 1,
                s->reg_spot);
    }
    else {
        write_4(emit,
                o_get_readonly,
                ast->line_num,
                lambda_result->reg_spot,
                s->reg_spot);
    }

    ast->result = (lily_sym *)s;
}

void eval_self(lily_emit_state *emit, lily_ast *ast)
{
    ast->result = (lily_sym *)emit->block->self;
}

void eval_upvalue(lily_emit_state *emit, lily_ast *ast)
{
    lily_sym *sym = ast->sym;

    int i;
    for (i = 0;i < emit->closed_pos;i++)
        if (emit->closed_syms[i] == sym)
            break;

    lily_storage *s = get_storage(emit, sym->type);
    write_4(emit, o_get_upvalue, ast->line_num, i, s->reg_spot);
    ast->result = (lily_sym *)s;
}

void eval_open_upvalue(lily_emit_state *emit, lily_ast *ast)
{
    lily_sym *sym = ast->sym;

    close_over_sym(emit, ast->sym);
    lily_storage *result = get_storage(emit, sym->type);
    write_4(emit, o_get_upvalue, ast->line_num, emit->closed_pos - 1,
            result->reg_spot);

    ast->result = (lily_sym *)result;
}

/*  eval_tree
    Magically determine what function actually handles the given ast. */
static void eval_tree(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type, int did_resolve)
{
    if (ast->tree_type == tree_global_var ||
        ast->tree_type == tree_literal ||
        ast->tree_type == tree_defined_func ||
        ast->tree_type == tree_inherited_new)
        emit_nonlocal_var(emit, ast);
    else if (ast->tree_type == tree_call)
        eval_call(emit, ast, expect_type, did_resolve);
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
            else if (left_tt == tree_upvalue ||
                     left_tt == tree_open_upvalue)
                eval_upvalue_assign(emit, ast);
            else
                /* Let eval_assign say that it's wrong. */
                eval_assign(emit, ast);

            assign_post_check(emit, ast);
        }
        else if (ast->op == expr_logical_or || ast->op == expr_logical_and)
            eval_logical_op(emit, ast);
        else {
            if (ast->left->tree_type != tree_local_var)
                eval_tree(emit, ast->left, NULL, 1);

            if (ast->right->tree_type != tree_local_var)
                eval_tree(emit, ast->right, ast->left->result->type, 1);

            emit_binary_op(emit, ast);
        }
    }
    else if (ast->tree_type == tree_parenth) {
        if (ast->arg_start->tree_type != tree_local_var)
            eval_tree(emit, ast->arg_start, expect_type, 1);

        ast->result = ast->arg_start->result;
    }
    else if (ast->tree_type == tree_unary) {
        if (ast->left->tree_type != tree_local_var)
            eval_tree(emit, ast->left, expect_type, 1);

        eval_unary_op(emit, ast);
    }
    else if (ast->tree_type == tree_list)
        eval_build_list(emit, ast, expect_type, did_resolve);
    else if (ast->tree_type == tree_hash)
        eval_build_hash(emit, ast, expect_type, did_resolve);
    else if (ast->tree_type == tree_tuple)
        eval_build_tuple(emit, ast, expect_type, did_resolve);
    else if (ast->tree_type == tree_subscript)
        eval_subscript(emit, ast, expect_type);
    else if (ast->tree_type == tree_typecast)
        eval_typecast(emit, ast);
    else if (ast->tree_type == tree_oo_access)
        eval_oo_access(emit, ast);
    else if (ast->tree_type == tree_property)
        eval_property(emit, ast);
    else if (ast->tree_type == tree_variant)
        eval_variant(emit, ast, expect_type, did_resolve);
    else if (ast->tree_type == tree_lambda)
        eval_lambda(emit, ast, expect_type, did_resolve);
    else if (ast->tree_type == tree_self)
        eval_self(emit, ast);
    else if (ast->tree_type == tree_upvalue)
        eval_upvalue(emit, ast);
    else if (ast->tree_type == tree_open_upvalue)
        eval_open_upvalue(emit, ast);
}

/*****************************************************************************/
/* Exported functions                                                        */
/*****************************************************************************/

/*  lily_emit_change_block_to
    This is called when the parser would like to change the current block into
    another block type.

    One example is when the parser sees 'elif'. In that case, it wants to
    change the current block into 'BLOCK_IF_ELIF'. */
void lily_emit_change_block_to(lily_emit_state *emit, int new_type)
{
    int current_type = emit->block->block_type;
    int save_jump;

    if (new_type == BLOCK_IF_ELIF || new_type == BLOCK_IF_ELSE) {
        char *block_name;
        if (new_type == BLOCK_IF_ELIF)
            block_name = "elif";
        else
            block_name = "else";

        if (current_type != BLOCK_IF && current_type != BLOCK_IF_ELIF)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "'%s' without 'if'.\n", block_name);

        if (current_type == BLOCK_IF_ELSE)
            lily_raise(emit->raiser, lily_SyntaxError, "'%s' after 'else'.\n",
                    block_name);

        lily_var *v = emit->block->var_start;
        if (v != emit->symtab->active_import->var_chain)
            lily_hide_block_vars(emit->symtab, v);
    }
    else if (new_type == BLOCK_TRY_EXCEPT) {
        if (current_type != BLOCK_TRY && current_type != BLOCK_TRY_EXCEPT)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "'except' outside 'try'.\n");

        /* If nothing in the 'try' block raises an error, the vm needs to be
           told to unregister the 'try' block since will become unreachable
           when the jump below occurs. */
        if (current_type == BLOCK_TRY)
            write_1(emit, o_pop_try);
    }

    /* Transitioning between blocks is simple: First write a jump at the end of
       the current branch. This will get patched to the if/try's exit. */
    write_2(emit, o_jump, 0);
    save_jump = emit->code_pos - 1;

    /* The last jump of the previous branch wants to know where the check for
       the next branch starts. It's right now. */
    if (emit->patches[emit->patch_pos - 1] != -1)
        emit->code[emit->patches[emit->patch_pos-1]] =
                emit->code_pos - emit->block->jump_offset;
    /* else it's a fake branch from a condition that was optimized out. */

    emit->patches[emit->patch_pos-1] = save_jump;
    emit->block->block_type = new_type;
}

/*  lily_emit_expr
    This evaluates the root of the ast pool given (the expression), then clears
    the pool for the next expression. */
void lily_emit_eval_expr(lily_emit_state *emit, lily_ast_pool *ap)
{
    eval_tree(emit, ap->root, NULL, 1);
    emit->expr_num++;

    lily_ast_reset_pool(ap);
}

/*  lily_emit_eval_expr_to_var
    This evaluates the root of the current ast pool, then assigns the result
    to the given var.

    This is used for expressions within 'for..in', and thus the var is expected
    to always be an integer.

    This clears the ast pool for the next pass. */
void lily_emit_eval_expr_to_var(lily_emit_state *emit, lily_ast_pool *ap,
        lily_var *var)
{
    lily_ast *ast = ap->root;

    eval_tree(emit, ast, NULL, 1);
    emit->expr_num++;

    if (ast->result->type->cls->id != SYM_CLASS_INTEGER) {
        lily_raise(emit->raiser, lily_SyntaxError,
                   "Expected type 'integer', but got type '^T'.\n",
                   ast->result->type);
    }

    /* Note: This works because the only time this is called is to handle
             for..in range expressions, which are always integers. */
    write_4(emit,
            o_fast_assign,
            ast->line_num,
            ast->result->reg_spot,
            var->reg_spot);

    lily_ast_reset_pool(ap);
}

/*  lily_emit_eval_condition
    This function evaluates an ast that will decide if a block should be
    entered. This will write o_jump_if_false which will jump to the next
    branch or outside the block if the ast's result is false.

    This is suitable for 'if', 'elif', 'while', and 'do...while'.

    This clears the ast pool for the next pass. */
void lily_emit_eval_condition(lily_emit_state *emit, lily_ast_pool *ap)
{
    lily_ast *ast = ap->root;
    int current_type = emit->block->block_type;

    if ((ast->tree_type == tree_literal &&
         condition_optimize_check(ast)) == 0) {
        eval_enforce_value(emit, ast, NULL,
                "Conditional expression has no value.\n");
        ensure_valid_condition_type(emit, ast->result->type);

        if (current_type != BLOCK_DO_WHILE)
            /* If this doesn't work, add a jump which will get fixed to the next
               branch start or the end of the block. */
            emit_jump_if(emit, ast, 0);
        else {
            /* In a 'do...while' block, the condition is at the end, so the jump is
               reversed: If successful, go back to the top, otherwise fall out of
               the loop. */
            write_4(emit,
                    o_jump_if,
                    1,
                    ast->result->reg_spot,
                    emit->loop_start);
        }
    }
    else {
        if (current_type != BLOCK_DO_WHILE) {
            /* Code that handles if/elif/else transitions expects each branch to
               write a jump. There's no easy way to tell it that none was made...
               so give it a fake jump. */
            if (emit->patch_pos == emit->patch_size)
                grow_patches(emit);

            emit->patches[emit->patch_pos] = -1;
            emit->patch_pos++;
        }
        else
            write_2(emit, o_jump, emit->loop_start);
    }

    lily_ast_reset_pool(ap);
}

/*  lily_emit_variant_decompose
    This function writes out an o_variant_decompose instruction based upon the
    type given. The target(s) of the decompose are however many vars that
    the variant calls for, and pulled from the top of the symtab's vars.

    Assumptions:
    * The most recent vars that have been added to the symtab are the ones that
      are to get the values.
    * The given variant type actually has inner values (empty variants
      should never be sent here). */
void lily_emit_variant_decompose(lily_emit_state *emit, lily_type *variant_type)
{
    int value_count = variant_type->subtype_count - 1;
    int i;

    write_prep(emit, 4 + value_count);

    emit->code[emit->code_pos  ] = o_variant_decompose;
    emit->code[emit->code_pos+1] = *(emit->lex_linenum);
    emit->code[emit->code_pos+2] = emit->block->match_sym->reg_spot;
    emit->code[emit->code_pos+3] = value_count;

    /* Since this function is called immediately after declaring the last var
       that will receive the decompose, it's safe to pull the vars directly
       from symtab's var chain. */
    lily_var *var_iter = emit->symtab->active_import->var_chain;

    /* Go down because the vars are linked from newest -> oldest. If this isn't
       done, then the first var will get the last value in the variant, the
       second will get the next-to-last value, etc. */
    for (i = value_count - 1;i >= 0;i--) {
        emit->code[emit->code_pos+4+i] = var_iter->reg_spot;
        var_iter = var_iter->next;
    }

    emit->code_pos += 4 + value_count;
}

/*  lily_emit_add_match_case
    This function is called by parser with a valid index of some variant class
    within the current match enum class. This is responsible for ensuring that
    a class does not have two cases for it.

    Additionally, this function also writes a jump at the end of every case
    that will be patched to the match block's end.

    Any vars from previous match cases are also wiped out here, as they're no
    longer valid now. */
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

    if (emit->match_cases[block_offset + pos] == 0) {
        emit->match_cases[block_offset + pos] = 1;

        /* Every case added after the first needs to write an exit jump before
           any code. This makes it so the previous branch jumps outside the
           match instead of falling through (very bad, in this case). */
        if (is_first_case == 0) {
            write_2(emit, o_jump, 0);

            if (emit->patch_pos == emit->patch_size)
                grow_patches(emit);

            emit->patches[emit->patch_pos] = emit->code_pos - 1;
            emit->patch_pos++;
        }

        /* Patch the o_match_dispatch spot the corresponds with this class
           so that it will jump to the current location.
           Oh, and make sure to do it AFTER writing the jump, or the dispatch
           will go to the exit jump. */
        emit->code[emit->block->match_code_start + pos] =
                emit->code_pos - emit->block->jump_offset;

        /* This is necessary to keep vars created from the decomposition of one
           class from showing up in subsequent cases. */
        lily_var *v = emit->block->var_start;
        if (v != emit->symtab->active_import->var_chain)
            lily_hide_block_vars(emit->symtab, v);
    }
    else
        ret = 0;

    return ret;
}

/*  lily_emit_eval_match_expr
    This function is called by parser with an expression to switch on for
    'match'. This evaluates the given expression, checks it, and then sets
    up the current block with the appropriate information for the match. */
void lily_emit_eval_match_expr(lily_emit_state *emit, lily_ast_pool *ap)
{
    lily_ast *ast = ap->root;
    lily_block *block = emit->block;
    eval_enforce_value(emit, ast, NULL, "Match expression has no value.\n");

    if ((ast->result->type->cls->flags & CLS_ENUM_CLASS) == 0 ||
        ast->result->type->cls->id == SYM_CLASS_ANY) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "Match expression is not an enum class value.\n");
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

    block->match_code_start = emit->code_pos + 4;
    block->match_sym = (lily_sym *)ast->result;

    write_prep(emit, 4 + match_cases_needed);

    emit->code[emit->code_pos  ] = o_match_dispatch;
    emit->code[emit->code_pos+1] = *(emit->lex_linenum);
    emit->code[emit->code_pos+2] = ast->result->reg_spot;
    emit->code[emit->code_pos+3] = match_cases_needed;
    for (i = 0;i < match_cases_needed;i++)
        emit->code[emit->code_pos + 4 + i] = 0;

    emit->code_pos += 4 + i;

    lily_ast_reset_pool(ap);
}

/*  lily_emit_finalize_for_in
    This function takes the symbols used in a for..in loop and writes out the
    appropriate code to start off a for loop. This should be done at the very
    end of a for..in loop, after the 'by' expression has been collected.
    * user_loop_var: This is the user var that will have the range value
                     written to it.
    * for_start:     The var holding the start of the range.
    * for_end:       The var holding the end of the range.
    * for_step:      The var holding the step of the range. This is NULL if the
                     user did not specify a step.
    * line_num:      A line number for writing code to be run before the actual
                     for code. */
void lily_emit_finalize_for_in(lily_emit_state *emit, lily_var *user_loop_var,
        lily_var *for_start, lily_var *for_end, lily_var *for_step,
        int line_num)
{
    lily_class *cls = emit->symtab->integer_class;

    int have_step = (for_step != NULL);
    if (have_step == 0)
        for_step = lily_emit_new_scoped_var(emit, cls->type, "(for step)");

    lily_sym *target;
    /* Global vars cannot be used directly, because o_for_setup and
       o_integer_for expect local registers. */
    if (user_loop_var->function_depth == 1)
        target = (lily_sym *)get_storage(emit, user_loop_var->type);
    else
        target = (lily_sym *)user_loop_var;

    write_prep(emit, 16 + ((target != (lily_sym *)user_loop_var) * 8));
    emit->code[emit->code_pos  ] = o_for_setup;
    emit->code[emit->code_pos+1] = line_num;
    emit->code[emit->code_pos+2] = target->reg_spot;
    emit->code[emit->code_pos+3] = for_start->reg_spot;
    emit->code[emit->code_pos+4] = for_end->reg_spot;
    emit->code[emit->code_pos+5] = for_step->reg_spot;
    /* This value is used to determine if the step needs to be calculated. */
    emit->code[emit->code_pos+6] = !have_step;

    if (target != (lily_sym *)user_loop_var) {
        emit->code[emit->code_pos+7] = o_set_global;
        emit->code[emit->code_pos+8] = line_num;
        emit->code[emit->code_pos+9] = target->reg_spot;
        emit->code[emit->code_pos+10] = user_loop_var->reg_spot;
        emit->code_pos += 4;
    }
    /* for..in is entered right after 'for' is seen. However, range values can
       be expressions. This needs to be fixed, or the loop will jump back up to
       re-eval those expressions. */
    emit->loop_start = emit->code_pos+9;

    /* Write a jump to the inside of the loop. This prevents the value from
       being incremented before being seen by the inside of the loop. */
    emit->code[emit->code_pos+7] = o_jump;
    emit->code[emit->code_pos+8] =
            (emit->code_pos - emit->block->jump_offset) + 16;

    emit->code[emit->code_pos+9] = o_integer_for;
    emit->code[emit->code_pos+10] = line_num;
    emit->code[emit->code_pos+11] = target->reg_spot;
    emit->code[emit->code_pos+12] = for_start->reg_spot;
    emit->code[emit->code_pos+13] = for_end->reg_spot;
    emit->code[emit->code_pos+14] = for_step->reg_spot;
    emit->code[emit->code_pos+15] = 0;
    if (target != (lily_sym *)user_loop_var) {
        emit->code[emit->code_pos+16] = o_set_global;
        emit->code[emit->code_pos+17] = line_num;
        emit->code[emit->code_pos+18] = target->reg_spot;
        emit->code[emit->code_pos+19] = user_loop_var->reg_spot;
        emit->code_pos += 4;
    }

    emit->code_pos += 16;

    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    int offset;
    if (target == (lily_sym *)user_loop_var)
        offset = 1;
    else
        offset = 5;

    emit->patches[emit->patch_pos] = emit->code_pos - offset;

    emit->patch_pos++;
}

void lily_emit_eval_lambda_body(lily_emit_state *emit, lily_ast_pool *ap,
        lily_type *wanted_type, int did_resolve)
{
    if (wanted_type && wanted_type->cls->id == SYM_CLASS_GENERIC &&
        did_resolve == 0) {
        wanted_type = lily_ts_easy_resolve(emit->ts, wanted_type);
        did_resolve = 1;
    }

    eval_tree(emit, ap->root, wanted_type, did_resolve);

    if (ap->root->result != NULL) {
        /* Type inference has to be done here, because the callers won't know
           to do it. This is similar to how return has to do this too.
           But don't error for the wrong type: Instead, let the info bubble
           upward to something that will know the full types in play. */
        if (wanted_type != NULL && ap->root->result->type != wanted_type)
            type_matchup(emit, wanted_type, ap->root);

        write_3(emit, o_return_val, ap->root->line_num,
                ap->root->result->reg_spot);
    }
    /* It's important to NOT increase the count of expressions here. If it were
       to be increased, then the expression holding the lambda would think it
       isn't using any storages (and start writing over the ones that it is
       actually using). */
}

/* lily_emit_break
   This writes a break (jump to the end of a loop) for the parser. Since it
   is called by parser, it needs to verify that it is called from within a
   loop. */
void lily_emit_break(lily_emit_state *emit)
{
    if (emit->loop_start == -1) {
        /* This is called by parser on the source line, so do not adjust the
           raiser. */
        lily_raise(emit->raiser, lily_SyntaxError,
                "'break' used outside of a loop.\n");
    }

    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    write_pop_inner_try_blocks(emit);

    /* Write the jump, then figure out where to put it. */
    write_2(emit, o_jump, 0);

    inject_patch_into_block(emit, find_deepest_loop(emit), emit->code_pos - 1);
}

/*  lily_emit_continue
    The parser wants to write a jump to the top of the current loop (continue
    keyword). */
void lily_emit_continue(lily_emit_state *emit)
{
    /* This is called by parser on the source line, so do not adjust the
       raiser. */
    if (emit->loop_start == -1) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "'continue' used outside of a loop.\n");
    }

    write_pop_inner_try_blocks(emit);

    write_2(emit, o_jump, emit->loop_start);
}

/*  lily_emit_return
    This handles the 'return' keyword for the parser.

    If the current function DOES return a value, then ast should NOT be NULL.
    The ast given will be evaluated and the type checked.

    If it does not, then ast should be NULL. */
void lily_emit_return(lily_emit_state *emit, lily_ast *ast)
{
    if (emit->function_depth == 1)
        lily_raise(emit->raiser, lily_SyntaxError,
                "'return' used outside of a function.\n");

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
    }

    write_pop_inner_try_blocks(emit);

    if (ast) {
        write_3(emit, o_return_val, ast->line_num, ast->result->reg_spot);
        emit->block->last_return = emit->code_pos;
    }
    else
        write_2(emit, o_return_noval, *emit->lex_linenum);
}

/*  lily_emit_update_function_block
    This is called at the opening of a new class, before any user code. This
    writes an initialization for the hidden self variable. */
void lily_emit_update_function_block(lily_emit_state *emit,
        lily_class *decl_class, int generic_count, lily_type *ret_type)
{
    emit->top_function_ret = ret_type;
    emit->block->generic_count = generic_count;

    if (decl_class) {
        /* The most recent function is the constructor for this class, which will
           always return a class instance. Since it's also the function var (and
           the return of a function is always [0], this works. */
        lily_type *self_type = emit->block->function_var->type->subtypes[0];

        lily_storage *self = get_storage(emit, self_type);
        emit->block->self = self;

        write_3(emit,
                o_new_instance,
                *emit->lex_linenum,
                self->reg_spot);
    }
}

/*  lily_emit_try
    This should be called after adding a TRY block. This registers a try and
    mentions the line in which it starts (for debug).

    At the end of a 'try' block, there is an o_pop_try that gets written to
    unregister this try from the vm. Similarly, write_pop_try_blocks is called
    for each current 'try' when a continue/break/return is called to exit any
    current 'try' entries. */
void lily_emit_try(lily_emit_state *emit, int line_num)
{
    write_3(emit,
            o_push_try,
            line_num,
            0);

    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    emit->patches[emit->patch_pos] = emit->code_pos - 1;
    emit->patch_pos++;
}

/*  lily_emit_raise
    Process the given ast and write an instruction that will attempt to raise
    the resulting value. The ast is checked to ensure it can be raised. */
void lily_emit_raise(lily_emit_state *emit, lily_ast *ast)
{
    eval_enforce_value(emit, ast, NULL, "'raise' expression has no value.\n");

    lily_class *result_cls = ast->result->type->cls;
    lily_class *except_cls = lily_find_class(emit->symtab, NULL, "Exception");
    if (lily_check_right_inherits_or_is(except_cls, result_cls) == 0) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "Invalid class '%s' given to raise.\n", result_cls->name);
    }

    write_3(emit,
            o_raise,
            ast->line_num,
            ast->result->reg_spot);
}

/*  lily_emit_except
    This handles writing an 'except' block. It should be called after calling
    to change the current block to a TRY_EXCEPT block.

    cls:        The class that this 'except' will catch.
    except_var: If an 'as x' clause is specified, this is the var that will be
                given the exception value. If there is no clause, then the
                parser will send NULL.
    line_num:   The line on which the 'except' starts. */
void lily_emit_except(lily_emit_state *emit, lily_class *cls,
        lily_var *except_var, int line_num)
{
    lily_type *except_type = cls->type;
    lily_sym *except_sym = (lily_sym *)except_var;
    if (except_sym == NULL)
        except_sym = (lily_sym *)get_storage(emit, except_type);

    write_5(emit,
            o_except,
            line_num,
            0,
            (except_var != NULL),
            except_sym->reg_spot);

    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    emit->patches[emit->patch_pos] = emit->code_pos - 3;
    emit->patch_pos++;
}

/*  lily_prepare_main
    This is called before __main__ is about to be executed (which happens at eof
    for normal files, or for each ?> when doing tags. This will prepare type
    information for __main__'s global registers, and write o_return_from_vm at
    the end of __main__'s code. */
void lily_prepare_main(lily_emit_state *emit, lily_import_entry *import_iter)
{
    /* Whenever any packages are loaded, the vars are created as globals,
       instead of trying to create some 'backing' value. Because of that, this
       must work through every import loaded to discover all the globals.
       Additionally, the current var list must also be loaded. */
    lily_function_val *f = emit->top_function;
    int register_count = emit->main_block->next_reg_spot;
    lily_register_info *info = lily_realloc(emit->top_function->reg_info,
            register_count * sizeof(lily_register_info));

    while (import_iter) {
        add_var_chain_to_info(emit, info, import_iter->var_chain, NULL);
        import_iter = import_iter->root_next;
    }

    add_var_chain_to_info(emit, info, emit->symtab->active_import->var_chain,
            NULL);
    add_storage_chain_to_info(info, emit->block->storage_start);

    /* Ensure that there are at least 16 code slots after __main__'s code. It is
       possible for an exception to dynaload at vm time, and that will want to
       write initializing code into __main__ past where __main__'s code is. This
       may, in turn, cause code to be resized. Since __main__'s code is a
       shallow reference to emitter->code, that's really, really bad.
       This prevents that, by setting up enough space that any dynaloaded code
       will not be large enough to resize emitter->code. */
    write_prep(emit, 16);

    write_1(emit, o_return_from_vm);

    /* To simplify things, __main__'s code IS emitter->code. There's no reason
       to give it a private block of code, since __main__'s code is wiped on the
       next pass anyway. */
    f->code = emit->code;
    f->len = emit->code_pos;
    f->reg_info = info;
    f->reg_count = register_count;
}

/*  lily_reset_main
    (tagged mode) This is called after __main__ is executed to prepare __main__
    for new code. */
void lily_reset_main(lily_emit_state *emit)
{
    emit->code_pos = 0;
}

/*  lily_emit_enter_block
    Enter a block of a given type. This only handles block states, not
    multi/single line information. */
void lily_emit_enter_block(lily_emit_state *emit, int block_type)
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
    new_block->var_start = emit->symtab->active_import->var_chain;
    new_block->class_entry = NULL;
    new_block->self = NULL;
    new_block->generic_count = 0;
    new_block->patch_start = emit->patch_pos;

    if ((block_type & BLOCK_FUNCTION) == 0) {
        /* Non-functions will continue using the storages that the parent uses.
           Additionally, the same technique is used to allow loop starts to
           bubble upward until a function gets in the way. */
        new_block->storage_start = emit->block->storage_start;
        new_block->jump_offset = emit->block->jump_offset;

        if (IS_LOOP_BLOCK(block_type))
            emit->loop_start = emit->code_pos;
    }
    else {
        lily_var *v = emit->symtab->active_import->var_chain;
        if (block_type & BLOCK_CLASS) {
            emit->current_class = emit->symtab->active_import->class_chain;
            new_block->class_entry = emit->symtab->active_import->class_chain;
        }

        char *class_name;
        v->parent = emit->current_class;
        if (v->parent)
            class_name = v->parent->name;
        else
            class_name = NULL;

        lily_function_val *fval = lily_new_native_function_val(
                class_name, v->name);
        lily_tie_function(emit->symtab, v, fval);

        if (emit->function_depth >= 2 &&
            (emit->block->block_type & BLOCK_CLASS) == 0 &&
            (block_type == BLOCK_FUNCTION)) {
            /* This isn't a class method and it isn't a lambda. Close over the
               function now, on the assumption that it may use upvalues. */
            close_over_sym(emit, (lily_sym *)v);
            write_4(emit, o_create_function, v->reg_spot, 0, emit->closed_pos - 1);
            if (emit->patch_pos == emit->patch_size)
                grow_patches(emit);

            emit->patches[emit->patch_pos] = emit->code_pos - 2;
            emit->patch_pos++;

            /* THIS IS EXTREMELY IMPORTANT. This patch belongs to the current
               block, not the block that was just entered. If this is not done,
               then the block entered will write down the patch. That's bad. */
            new_block->patch_start = emit->patch_pos;
        }

        new_block->next_reg_spot = 0;

        /* This causes vars within this imported file to be seen as global
           vars, instead of locals. Without this, the interpreter gets confused
           and thinks the imported file's globals are really upvalues. */
        if (block_type != BLOCK_FILE)
            emit->function_depth++;

        emit->function_block = new_block;

        if (block_type & BLOCK_LAMBDA)
            emit->lambda_depth++;

        /* This function's storages start where the unused ones start, or NULL if
           all are currently taken. */
        new_block->storage_start = emit->unused_storage_start;
        new_block->function_var = v;
        new_block->function_value = fval;
        new_block->last_return = -1;
        new_block->code_start = emit->code_pos;
        new_block->jump_offset = emit->code_pos;

        emit->top_function = fval;
        emit->top_var = v;
    }

    new_block->closed_start = emit->closed_pos;

    emit->block = new_block;
}

lily_block *find_block_of_type(lily_emit_state *emit, int type)
{
    lily_block *block_iter = emit->block->prev;
    while (block_iter) {
        if (block_iter->block_type & type)
            break;

        block_iter = block_iter->prev;
    }

    return block_iter;
}

/*  lily_emit_leave_block
    Leave a block. This includes a check for trying to leave from __main__.
    This hides vars that are no longer in scope, as well as finializing
    functions. */
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
    if (block_type == BLOCK_WHILE || block_type == BLOCK_FOR_IN)
        write_2(emit, o_jump, emit->loop_start - block->jump_offset);
    else if (block_type == BLOCK_MATCH) {
        ensure_proper_match_block(emit);
        emit->match_case_pos = emit->block->match_case_start;
    }

    v = block->var_start;

    if ((block_type & BLOCK_FUNCTION) == 0) {
        write_block_patches(emit, emit->code_pos - block->jump_offset);

        lily_hide_block_vars(emit->symtab, v);
    }
    else
        leave_function(emit, block);

    emit->block = emit->block->prev;

    if (IS_LOOP_BLOCK(block_type)) {
        lily_block *prev_loop_block = find_deepest_loop(emit);
        emit->loop_start = (prev_loop_block) ? prev_loop_block->code_start : -1;
    }
}

/*  lily_emit_write_import_call
    This is called by the parser after an imported file has left. The var is
    always the special __import__ function holding the contents of the imported
    file (which takes 0 args and returns nothing). */
void lily_emit_write_import_call(lily_emit_state *emit, lily_var *var)
{
    write_prep(emit, 6);
    emit->code[emit->code_pos] = o_function_call;
    emit->code[emit->code_pos+1] = *emit->lex_linenum;
    /* 1 means that +3 is a readonly var's spot. */
    emit->code[emit->code_pos+2] = 1;
    emit->code[emit->code_pos+3] = var->reg_spot;
    /* 0 arguments collected. */
    emit->code[emit->code_pos+4] = 0;
    /* This does not return a value. */
    emit->code[emit->code_pos+5] = -1;

    emit->code_pos += 6;
}

/*  lily_emit_write_optargs
    This function writes o_setup_optargs for the parser. It's currently called
    near the beginning of any function that uses optional arguments.

    reg_spots: A series of pairs to write out. Each pair is a literal's register
               spot, then a var's register spot.
    count:     The total number of spots to write (not the number of pairs).

    Parser writes optargs in pairs so the it doesn't have to potentially resize
    and shift things over for large optarg blocks. However, debug and vm would
    like optargs to be written with the literals first in a block, then the vars
    next in a block. */
void lily_emit_write_optargs(lily_emit_state *emit, uint16_t *reg_spots,
        uint16_t count)
{
    write_prep(emit, count + 2);

    emit->code[emit->code_pos] = o_setup_optargs;
    emit->code[emit->code_pos+1] = count;

    emit->code_pos += 2;

    int i, j;
    for (j = 0;j < 2;j++) {
        for (i = j;i < count;i += 2) {
            emit->code[emit->code_pos] = reg_spots[i];
            emit->code_pos++;
        }
    }
}

lily_var *lily_emit_new_scoped_var(lily_emit_state *emit, lily_type *type,
        char *name)
{
    lily_var *new_var = lily_new_raw_var(emit->symtab, type, name);

    if (emit->function_depth == 1) {
        new_var->reg_spot = emit->main_block->next_reg_spot;
        emit->main_block->next_reg_spot++;
    }
    else {
        new_var->reg_spot = emit->function_block->next_reg_spot;
        emit->function_block->next_reg_spot++;
    }

    new_var->function_depth = emit->function_depth;

    return new_var;
}

lily_var *lily_emit_new_define_var(lily_emit_state *emit, lily_type *type,
        char *name)
{
    lily_var *new_var = lily_new_raw_var(emit->symtab, type, name);

    new_var->reg_spot = emit->symtab->next_readonly_spot;
    emit->symtab->next_readonly_spot++;
    new_var->function_depth = 1;
    new_var->flags |= VAR_IS_READONLY;

    return new_var;
}

lily_var *lily_emit_new_dyna_define_var(lily_emit_state *emit,
        lily_foreign_func func, lily_import_entry *import, lily_type *type,
        char *name)
{
    lily_var *new_var = lily_new_raw_unlinked_var(emit->symtab, type, name);

    new_var->next = import->var_chain;
    import->var_chain = new_var;

    new_var->reg_spot = emit->symtab->next_readonly_spot;
    emit->symtab->next_readonly_spot++;
    new_var->function_depth = 1;
    new_var->flags |= VAR_IS_READONLY;

    /* Make sure to use new_var->name, because the name parameter is a shallow
       copy of lex->label (and will be mutated). */
    lily_function_val *func_val = lily_new_foreign_function_val(func, NULL,
            new_var->name);
    lily_tie_builtin(emit->symtab, new_var, func_val);
    return new_var;
}

lily_var *lily_emit_new_dyna_method_var(lily_emit_state *emit,
        lily_foreign_func func, lily_class *cls, lily_type *type, char *name)
{
    lily_var *new_var = lily_new_raw_unlinked_var(emit->symtab, type, name);
    new_var->parent = cls;
    new_var->function_depth = 1;
    new_var->flags |= VAR_IS_READONLY;

    new_var->reg_spot = emit->symtab->next_readonly_spot;
    emit->symtab->next_readonly_spot++;

    new_var->next = cls->call_chain;
    cls->call_chain = new_var;

    lily_function_val *func_val = lily_new_foreign_function_val(func, cls->name,
            new_var->name);
    lily_tie_function(emit->symtab, new_var, func_val);
    return new_var;
}

lily_var *lily_emit_new_dyna_var(lily_emit_state *emit,
        lily_import_entry *import, lily_type *type, char *name)
{
    lily_var *new_var = lily_new_raw_unlinked_var(emit->symtab, type, name);

    new_var->reg_spot = emit->main_block->next_reg_spot;
    emit->main_block->next_reg_spot++;
    new_var->function_depth = 1;

    new_var->next = import->var_chain;
    import->var_chain = new_var;

    return new_var;
}

/*  Create the first block that will represent __main__, as well as __main__
    itself. This first block will never be exited from. */
void lily_emit_enter_main(lily_emit_state *emit)
{
    /* This adds the first storage and makes sure that the emitter can always
       know that emit->unused_storage_start is never NULL. */
    add_storage(emit);

    lily_type *main_type = lily_new_type(emit->symtab,
            emit->symtab->function_class);
    /* This next part is only ok because __main__ is the first function and it
       is not possible for this type to be a duplicate of anything else. */
    main_type->subtypes = lily_malloc(2 * sizeof(lily_type));
    main_type->subtypes[0] = NULL;
    main_type->subtype_count = 1;
    main_type->generic_pos = 0;
    main_type->flags = 0;

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
    /* __main__ is given two refs so that it must go through a custom deref to
       be destroyed. This is because the names in the function info it has are
       shared with vars that are still around. */
    main_function->refcount++;
    lily_tie_function(emit->symtab, main_var, main_function);

    main_block->prev = NULL;
    main_block->next = NULL;
    main_block->block_type = BLOCK_FUNCTION;
    main_block->function_var = main_var;
    main_block->storage_start = emit->all_storage_start;
    main_block->class_entry = NULL;
    main_block->generic_count = 0;
    main_block->function_value = main_function;
    main_block->self = NULL;
    main_block->code_start = 0;
    main_block->jump_offset = 0;
    main_block->next_reg_spot = 0;
    emit->top_function = main_function;
    emit->top_var = main_var;
    emit->block = main_block;
    emit->function_depth++;
    emit->main_block = main_block;
    emit->function_block = main_block;
}

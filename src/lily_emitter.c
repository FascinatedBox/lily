#include <string.h>

#include "lily_impl.h"
#include "lily_ast.h"
#include "lily_value.h"
#include "lily_emitter.h"
#include "lily_opcode.h"
#include "lily_emit_table.h"
#include "lily_parser.h"

/** Emitter is responsible for:
    * Taking in an ast and generating code that the vm/debug can work with.
    * Writing the code that allows if/elif/else, for, while, try, etc. to work.
    * Determining if a given block is correct (ex: 'elif' without 'if' is
      wrong).
    * Clearing vars from a block when it is done.
    * When a function exits, creating function info (register types, names,
      etc.) that the vm will use later when entering the function.
    * Keeping track of intermediate values that are available (storages).
      An intermediate value cannot be used twice in the same expression, and
      the vm also needs to know this information in function information too.

    eval vs. emit:
    * eval: This evaluates a tree. In most cases, it's bad to eval a tree
      twice.
    * emit: This writes some code, but doesn't evaluate any tree. **/

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

/*****************************************************************************/
/* Emitter setup and teardown                                                */
/*****************************************************************************/

lily_emit_state *lily_new_emit_state(lily_symtab *symtab, lily_raiser *raiser)
{
    lily_emit_state *s = lily_malloc(sizeof(lily_emit_state));

    if (s == NULL)
        return NULL;

    s->patches = lily_malloc(sizeof(int) * 4);
    s->match_cases = lily_malloc(sizeof(int) * 4);
    s->ts = lily_new_type_stack(symtab, raiser);

    if (s->patches == NULL || s->ts == NULL || s->match_cases == NULL) {
        lily_free(s->match_cases);
        lily_free(s->patches);
        lily_free_type_stack(s->ts);
        lily_free(s);
        return NULL;
    }

    s->match_case_pos = 0;
    s->match_case_size = 4;

    s->block = NULL;
    s->unused_storage_start = NULL;
    s->all_storage_start = NULL;
    s->all_storage_top = NULL;

    s->patch_pos = 0;
    s->patch_size = 4;
    s->function_depth = 0;

    s->current_generic_adjust = 0;
    s->current_class = NULL;
    s->raiser = raiser;
    s->expr_num = 1;

    return s;
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

    lily_free_type_stack(emit->ts);
    lily_free(emit->match_cases);
    lily_free(emit->patches);
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
    lily_function_val *f = emit->top_function;

    uint16_t *save_code;
    f->len *= 2;
    save_code = lily_realloc(f->code, sizeof(uint16_t) * f->len);
    if (save_code == NULL)
        lily_raise_nomem(emit->raiser);
    f->code = save_code;
}

/*  write_prep
    This ensures that the current function can take 'size' more blocks of code.
    This will grow emitter's code until it's the right size, if necessary. */
static void write_prep(lily_emit_state *emit, int size)
{
    lily_function_val *f = emit->top_function;
    if ((f->pos + size) > f->len) {
        uint16_t *save_code;
        while ((f->pos + size) > f->len)
            f->len *= 2;
        save_code = lily_realloc(f->code, sizeof(uint16_t) * f->len);
        if (save_code == NULL)
            lily_raise_nomem(emit->raiser);
        f->code = save_code;
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
    lily_function_val *f = emit->top_function;

    if ((f->pos + 1) > f->len)
        small_grow(emit);

    f->code[f->pos] = one;
    f->pos += 1;
}

static void write_2(lily_emit_state *emit, uint16_t one, uint16_t two)
{
    lily_function_val *f = emit->top_function;

    if ((f->pos + 2) > f->len)
        small_grow(emit);

    f->code[f->pos] = one;
    f->code[f->pos + 1] = two;
    f->pos += 2;
}

static void write_3(lily_emit_state *emit, uint16_t one, uint16_t two,
        uint16_t three)
{
    lily_function_val *f = emit->top_function;

    if ((f->pos + 3) > f->len)
        small_grow(emit);

    f->code[f->pos] = one;
    f->code[f->pos + 1] = two;
    f->code[f->pos + 2] = three;
    f->pos += 3;
}

static void write_4(lily_emit_state *emit, uint16_t one, uint16_t two,
        uint16_t three, uint16_t four)
{
    lily_function_val *f = emit->top_function;

    if ((f->pos + 4) > f->len)
        small_grow(emit);

    f->code[f->pos] = one;
    f->code[f->pos + 1] = two;
    f->code[f->pos + 2] = three;
    f->code[f->pos + 3] = four;
    f->pos += 4;
}

static void write_5(lily_emit_state *emit, uint16_t one, uint16_t two,
        uint16_t three, uint16_t four, uint16_t five)
{
    lily_function_val *f = emit->top_function;

    if ((f->pos + 5) > f->len)
        small_grow(emit);

    f->code[f->pos] = one;
    f->code[f->pos + 1] = two;
    f->code[f->pos + 2] = three;
    f->code[f->pos + 3] = four;
    f->code[f->pos + 4] = five;
    f->pos += 5;
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
    if (ast->result->flags & SYM_TYPE_LITERAL) {
        lily_literal *lit = (lily_literal *)ast->result;

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
        lily_function_val *f = emit->top_function;
        for (i = 0;i <= try_count;i++)
            f->code[f->pos+i] = o_pop_try;

        f->pos += try_count;
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

    for (block = emit->block;
         block;
         block = block->prev) {
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

/*  grow_patches
    Make emitter's patches bigger. */
static void grow_patches(lily_emit_state *emit)
{
    emit->patch_size *= 2;

    int *new_patches = lily_realloc(emit->patches,
        sizeof(int) * emit->patch_size);

    if (new_patches == NULL)
        lily_raise_nomem(emit->raiser);

    emit->patches = new_patches;
}

static void grow_match_cases(lily_emit_state *emit)
{
    emit->match_case_size *= 2;

    int *new_cases = lily_realloc(emit->match_cases,
        sizeof(int) * emit->match_case_size);

    if (new_cases == NULL)
        lily_raise_nomem(emit->raiser);

    emit->match_cases = new_cases;
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

    lily_function_val *f = emit->top_function;
    emit->patches[emit->patch_pos] = f->pos-1;
    emit->patch_pos++;
}

/*  try_new_block
    Attempt to create a new block.

    Success: A new block is returned with ->next set to NULL.
    Failure: NULL is returned. */
static lily_block *try_new_block(void)
{
    lily_block *ret = lily_malloc(sizeof(lily_block));

    if (ret)
        ret->next = NULL;

    return ret;
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

        int index_value = index_ast->original_sym->value.integer;
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
        int literal_index = index_ast->original_sym->value.integer;
        result = type->subtypes[literal_index];
    }
    else
        /* Won't happen, but keeps the compiler from complaining. */
        result = NULL;

    return result;
}

/*  try_add_storage
    Attempt to add a new storage at the top of emitter's linked list of
    storages. This should be called as a means of ensuring that there is always
    one valid unused storage. Doing so makes storage handling simpler all
    around. */
static int try_add_storage(lily_emit_state *emit)
{
    lily_storage *storage = lily_malloc(sizeof(lily_storage));
    if (storage == NULL)
        return 0;

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
    return 1;
}

/*  get_storage
    Attempt to get an unused storage of the type given. Additionally, a
    line number is required to fix up the line number in case there is an
    out-of-memory situation.
    Additionally, this function ensures that emit->unused_storage_start is both
    updated appropriately and will never become NULL.

    This will either return a valid storage, or call lily_raise_nomem. */
static lily_storage *get_storage(lily_emit_state *emit,
        lily_type *type, int line_num)
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

            storage_iter->reg_spot = emit->symtab->next_register_spot;
            emit->symtab->next_register_spot++;

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
    if (storage_iter->next == NULL && try_add_storage(emit) == 0) {
        emit->raiser->line_adjust = line_num;
        lily_raise_nomem(emit->raiser);
    }

    storage_iter->flags &= ~SYM_NOT_ASSIGNABLE;
    return storage_iter;
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
    lily_function_val *f = emit->top_function;

    write_prep(emit, num_values + 4);
    f->code[f->pos] = opcode;
    f->code[f->pos+1] = line_num;
    f->code[f->pos+2] = num_values;

    for (i = 3, arg = first_arg; arg != NULL; arg = arg->next_arg, i++)
        f->code[f->pos + i] = arg->result->reg_spot;

    f->code[f->pos+i] = reg_spot;
    f->pos += 4 + num_values;
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

/*  emit_rebox_value
    Make a storage of type 'new_type' and assign ast's result to it. The tree's
    result is written over. */
static void emit_rebox_value(lily_emit_state *emit, lily_type *new_type,
        lily_ast *ast)
{
    lily_storage *storage = get_storage(emit, new_type, ast->line_num);

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
    lily_class *any_cls = lily_class_by_id(emit->symtab, SYM_CLASS_ANY);

    emit_rebox_value(emit, any_cls->type, ast);
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
        expect_type->cls->id == SYM_CLASS_TEMPLATE) {
        expect_type = lily_ts_easy_resolve(emit->ts, expect_type);
        did_resolve = 1;
    }

    if (expect_type && expect_type->cls->id == wanted_id) {
        int i;
        for (i = 0;i < expect_type->subtype_count;i++) {
            lily_type *inner_type = expect_type->subtypes[i];
            if (did_resolve == 0 &&
                inner_type->cls->id == SYM_CLASS_TEMPLATE) {
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

/*  count_generics
    Return a count of how many unique generic types are used in a
    function. */
static int count_generics(lily_type *function_type, lily_register_info *info,
        int info_size)
{
    int count = 0, i, j;
    for (i = 0;i < info_size;i++) {
        int match = 0;
        lily_type *temp = info[i].type;
        for (j = 0;j < i;j++) {
            if (info[j].type == temp) {
                match = 1;
                break;
            }
        }

        count += !match;
    }

    /* The return is always used when figuring out what result a generic
       function should have. It needs to be counted too. */
    if (function_type->subtypes[0] &&
        function_type->subtypes[0]->template_pos) {
        count++;
        lily_type *return_type = function_type->subtypes[0];
        for (i = 0;i < info_size;i++) {
            if (info[i].type == return_type) {
                count--;
                break;
            }
        }
    }

    return count;
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
    int register_count = emit->symtab->next_register_spot;
    lily_storage *storage_iter = function_block->storage_start;

    lily_register_info *info;
    lily_function_val *f = emit->top_function;
    if (f->reg_info == NULL)
        info = lily_malloc(register_count * sizeof(lily_register_info));
    else
        info = lily_realloc(f->reg_info,
                register_count * sizeof(lily_register_info));

    if (info == NULL)
        /* This is called directly from parser, so don't set an adjust. */
        lily_raise_nomem(emit->raiser);

    lily_var *var_stop = function_block->function_var;
    lily_type *function_type = var_stop->type;

    /* Don't include functions inside of themselves... */
    if (emit->function_depth == 1)
        var_stop = var_stop->next;
    /* else we're in __main__, which does include itself as an arg so it can be
       passed to show and other neat stuff. */

    add_var_chain_to_info(emit, info, emit->symtab->var_chain, var_stop);
    add_storage_chain_to_info(info, function_block->storage_start);

    if (function_type->template_pos)
        f->generic_count = count_generics(function_type, info, register_count);

    if (emit->function_depth > 1) {
        /* todo: Reuse the var shells instead of destroying. Seems petty, but
                 malloc isn't cheap if there are a lot of vars. */
        lily_var *var_iter = emit->symtab->var_chain;
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

        /* Blank the types of the storages that were used. This lets other
           functions know that the types are not in use. */
        storage_iter = function_block->storage_start;
        while (storage_iter) {
            storage_iter->type = NULL;
            storage_iter = storage_iter->next;
        }

        /* Unused storages now begin where the function starting zapping them. */
        emit->unused_storage_start = function_block->storage_start;
    }

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
        else
            /* Ensure that if the function does not raise a value that an error is
               raised at vm-time. */
            write_2(emit, o_return_expected, *emit->lex_linenum);
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

    /* If this function has no storages, then it can use the ones from the function
       that just exited. This reuse cuts down on a lot of memory. */
    if (last_func_block->storage_start == NULL)
        last_func_block->storage_start = emit->unused_storage_start;

    emit->current_class = block->prev->class_entry;

    /* If this function was the ::new for a class, move it over into that class
       since the class is about to close. */
    if (emit->block->class_entry) {
        lily_class *cls = emit->block->class_entry;

        /* The symtab will see that the method to add is also symtab->var_chain
           and advance the chain to the next spot (which is right). */
        emit->symtab->var_chain = block->function_var;
        lily_add_class_method(emit->symtab, cls, block->function_var);
    }
    else
        emit->symtab->var_chain = block->function_var;

    if (block->prev->generic_count != block->generic_count &&
        (block->block_type & BLOCK_LAMBDA) == 0) {
        lily_update_symtab_generics(emit->symtab, NULL,
                last_func_block->generic_count);
    }

    emit->symtab->next_register_spot = block->save_register_spot;
    emit->top_function = v->value.function;
    emit->top_var = v;
    emit->top_function_ret = v->type->subtypes[0];

    emit->symtab->function_depth--;
    emit->function_depth--;
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
            /* Assume that the message buffer has at least enough space to dump
               an error message to. If may not, if there are a lot of cases and
               aft is being used. In such a case, well, sorry. */
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

static void push_info_to_error(lily_emit_state *emit, lily_ast *ast)
{
    char *class_name = "", *separator = "", *kind = "Function";
    char *call_name;
    lily_msgbuf *msgbuf = emit->raiser->msgbuf;

    if (ast->result) {
        lily_var *var = (lily_var *)ast->result;
        if (var->parent) {
            class_name = var->parent->name;
            separator = "::";
        }

        call_name = var->name;
    }
    else if (ast->arg_start->tree_type == tree_variant) {
        lily_class *variant_cls = ast->arg_start->variant_class;
        call_name = variant_cls->name;

        if (variant_cls->parent->flags & CLS_ENUM_IS_SCOPED) {
            class_name = variant_cls->parent->name;
            separator = "::";
        }

        kind = "Variant";
    }
    else if (ast->arg_start->tree_type == tree_oo_access) {
        lily_ast *start = ast->arg_start;
        class_name = start->result->type->cls->name;
        call_name = lily_membuf_get(emit->ast_membuf, start->membuf_pos);

        if (ast->arg_start->oo_property_index == -1)
            separator = "::";
        else {
            separator = ".";
            kind = "Property";
        }
    }
    else if (ast->arg_start->tree_type == tree_local_var)
        call_name = ((lily_var *)ast->arg_start->result)->name;
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

static void bad_arg_error(lily_emit_state *emit, lily_ast *ast,
    lily_type *got, lily_type *expected, int arg_num)
{
    push_info_to_error(emit, ast);
    lily_msgbuf *msgbuf = emit->raiser->msgbuf;

    emit->raiser->line_adjust = ast->line_num;

    /* If this call has unresolved generics, resolve those generics as
       themselves so the error message prints out correctly. */
    lily_ts_resolve_as_self(emit->ts);

    /* These names are intentionally the same length and on separate lines so
       that slight naming issues become more apparent. */
    lily_msgbuf_add_fmt(msgbuf,
            ", argument #%d is invalid:\n"
            "Expected Type: ^T\n"
            "Received Type: ^T\n",
            arg_num + 1, lily_ts_resolve(emit->ts, expected), got);
    lily_raise_prebuilt(emit->raiser, lily_SyntaxError);
}

/* bad_num_args
   Reports that the ast didn't get as many args as it should have. Takes
   anonymous calls and var args into account. */
static void bad_num_args(lily_emit_state *emit, lily_ast *ast,
        lily_type *call_type)
{
    push_info_to_error(emit, ast);
    lily_msgbuf *msgbuf = emit->raiser->msgbuf;

    char *va_text;

    if (call_type->flags & TYPE_IS_VARARGS)
        va_text = "at least ";
    else
        va_text = "";

    emit->raiser->line_adjust = ast->line_num;

    lily_msgbuf_add_fmt(msgbuf, " expects %s%d args, but got %d.\n",
            va_text, call_type->subtype_count - 1, ast->args_collected);

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
        result_type = ast->original_sym->type;
    else if (ast->tree_type == tree_subscript) {
        lily_ast *var_tree = ast->arg_start;
        lily_ast *index_tree = var_tree->next_arg;

        result_type = determine_left_type(emit, var_tree);

        if (result_type != NULL) {
            if (result_type->cls->id == SYM_CLASS_HASH)
                result_type = result_type->subtypes[1];
            else if (result_type->cls->id == SYM_CLASS_TUPLE) {
                if (index_tree->tree_type != tree_literal ||
                    index_tree->original_sym->type->cls->id != SYM_CLASS_INTEGER)
                    result_type = NULL;
                else {
                    int literal_index = index_tree->original_sym->value.integer;
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
    /* All other trees are either invalid for the left side of an assignment,
       or tree_package which I don't care about. */
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
        storage_class = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

    s = get_storage(emit, storage_class->type, ast->line_num);
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

        /* Parenths don't write anything, so dive to the bottom of them. */
        while (right_tree->tree_type == tree_parenth)
            right_tree = right_tree->arg_start;

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
    This handles assignments where the left is not a subscript or package
    access. */
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
    if (assign_optimize_check(ast)) {
        lily_function_val *f = emit->top_function;
        f->code[f->pos-1] = left_sym->reg_spot;
    }
    else {
        write_4(emit,
                opcode,
                ast->line_num,
                right_sym->reg_spot,
                left_sym->reg_spot);
    }
    ast->result = right_sym;
}

/*  eval_oo_and_prop_assign
    This is called to handle assignments when the left side is of type
    tree_oo_access. Example:
        ValueError v = ValueError::new("test")
        v.message = "test\n"

    This also handles property assignments, such as '@x = 10' and '@y = 11'.
    For these, the left will have type 'tree_property'. */
static void eval_oo_and_prop_assign(lily_emit_state *emit, lily_ast *ast)
{
    lily_type *left_type;
    lily_sym *rhs;

    if (ast->left->tree_type != tree_property) {
        eval_tree(emit, ast->left, NULL, 1);

        /* Make sure that it was a property access, and not a class member
           access. The latter is not reassignable. */
        if (ast->left->result->flags & SYM_TYPE_VAR)
            lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                    "Left side of %s is not assignable.\n", opname(ast->op));

        left_type = ast->left->result->type;
    }
    else
        /* Don't bother evaluating the left, because the property's id and type
           are already available. Evaluating it would just dump the contents
           into a var, which isn't useful. */
        left_type = ast->left->property->type;

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

    lily_literal *lit;

    if (ast->left->tree_type == tree_oo_access)
        lit = lily_get_integer_literal(emit->symtab,
            ast->left->oo_property_index);
    else
        lit = lily_get_integer_literal(emit->symtab,
            ast->left->property->id);

    lily_storage *lit_result = get_storage(emit, lit->type,
            ast->line_num);

    if (ast->op > expr_assign) {
        if (ast->left->tree_type == tree_property)
            eval_tree(emit, ast->left, NULL, 1);

        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    write_4(emit,
            o_get_const,
            ast->line_num,
            lit->reg_spot,
            lit_result->reg_spot);

    if (ast->left->tree_type == tree_oo_access)
        write_5(emit,
                o_set_item,
                ast->line_num,
                ast->left->arg_start->result->reg_spot,
                lit_result->reg_spot,
                rhs->reg_spot);
    else
        write_5(emit,
                o_set_item,
                ast->line_num,
                emit->block->self->reg_spot,
                lit_result->reg_spot,
                rhs->reg_spot);

    ast->result = rhs;
}

static int get_package_index(lily_emit_state *emit, lily_ast *ast)
{
    lily_package_val *pval = ast->arg_start->result->value.package;
    lily_var *want_var = (lily_var *)(ast->arg_start->next_arg->result);

    int i;
    for (i = 0;i < pval->var_count;i++) {
        if (pval->vars[i] == want_var)
            break;
    }

    return i;
}

/*  eval_package_tree_for_op
    This function will scan the given package tree (a::b sort of access).
    * ast->arg_start
      This is either a global var, or another package tree. If it's a global
      var, then it's something like 'a::b'. If it's a package, it's 'a::b::c'
      or deeper.
    * ast->arg_start->next_arg
      This is a var within the current package. The name of this var will be
      looked up to get its index in the package.

    emit:      The emit state to write to.
    ast:       A tree of type tree_package.
    is_set_op: If 1: Use get operations to yield a value.
               If 0: Use set operations to set a value.
    set_value: If is_set_op is 1, this is the value to assign. */
static void eval_package_tree_for_op(lily_emit_state *emit, lily_ast *ast,
        int is_set_op, lily_sym *set_value)
{
    lily_sym *s = set_value;
    int opcode;

    int index = get_package_index(emit, ast);
    if (is_set_op)
        opcode = o_package_set;
    else {
        s = (lily_sym *)get_storage(emit,
            ast->arg_start->next_arg->result->type, ast->line_num);
        opcode = o_package_get;
    }

    write_5(emit, opcode, ast->line_num, ast->arg_start->result->reg_spot,
            index, s->reg_spot);

    ast->result = s;
}

/*  eval_package_assign
    This is like eval_package, except the var is going to be assigned to
    instead of read from.
    * ast->left is the package tree.
    * ast->right is the value to assign. */
static void eval_package_assign(lily_emit_state *emit, lily_ast *ast)
{
    lily_ast *rhs_tree = ast->right;

    /* The left may contain packages in it. However, the resulting value will
       always be the var at the very top. */
    lily_ast *package_right = ast->left->arg_start->next_arg;
    lily_type *wanted_type = package_right->result->type;
    lily_sym *rhs;

    if (ast->right->tree_type != tree_global_var)
        eval_tree(emit, ast->right, wanted_type, 1);

    /* Don't evaluate the package tree. Like subscript assign, this has to
       write directly to the var at the given part of the package. Since parser
       passes the var to be assigned, just grab that from result for checking
       the type. No need to do a symtab lookup of a name. */

    rhs = ast->right->result;

    /* Before doing an eval, make sure that the two types actually match up. */
    if (wanted_type != rhs_tree->result->type &&
        type_matchup(emit, wanted_type, rhs_tree) == 0) {
        bad_assign_error(emit, ast->line_num, wanted_type,
                rhs_tree->result->type);
    }

    if (ast->op > expr_assign) {
        eval_tree(emit, ast->left, NULL, 1);
        emit_op_for_compound(emit, ast);
        rhs = ast->result;
    }

    /* Evaluate the tree grab on the left side. Use set opcodes instead of get
       opcodes. The result given is what will be assigned. */
    eval_package_tree_for_op(emit, ast->left, 1, rhs);
}

/*  eval_logical_op
    This handles || (or) as well as && (and). */
static void eval_logical_op(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *result;
    int is_top, jump_on;
    lily_function_val *f = emit->top_function;

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
        lily_literal *success_lit, *failure_lit;
        lily_symtab *symtab = emit->symtab;
        lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

        result = get_storage(emit, cls->type, ast->line_num);

        success_lit = lily_get_integer_literal(symtab,
                (ast->op == expr_logical_and));
        failure_lit = lily_get_integer_literal(symtab,
                (ast->op == expr_logical_or));

        write_4(emit,
                o_get_const,
                ast->line_num,
                success_lit->reg_spot,
                result->reg_spot);

        write_2(emit, o_jump, 0);
        save_pos = f->pos - 1;

        lily_emit_leave_block(emit);
        write_4(emit,
                o_get_const,
                ast->line_num,
                failure_lit->reg_spot,
                result->reg_spot);
        f->code[save_pos] = f->pos;
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

        lily_storage *subs_storage = get_storage(emit, elem_type, ast->line_num);

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
        lily_storage *result = get_storage(emit, cast_type, ast->line_num);

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

    if (lhs_class->id != SYM_CLASS_INTEGER)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Invalid operation: %s%s.\n",
                opname(ast->op), lhs_class->name);

    lily_class *integer_cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

    storage = get_storage(emit, integer_cls->type, ast->line_num);
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
    int enum_count = 0, variant_count = 0;
    lily_type *rebox_type = NULL;
    lily_class *any_class = lily_class_by_id(emit->symtab, SYM_CLASS_ANY);

    /* If ast is tree_hash (is_hash == 1), then the values are key, value, key
       value, and so on. This is about the values, not the keys. */
    if (is_hash)
        tree_iter = tree_iter->next_arg;

    while (tree_iter != NULL) {
        if (tree_iter->result->type->cls->flags & CLS_ENUM_CLASS)
            enum_count++;
        else if (tree_iter->result->type->cls->flags & CLS_VARIANT_CLASS)
            variant_count++;
        else {
            rebox_type = any_class->type;
            break;
        }

        tree_iter = tree_iter->next_arg;
        if (is_hash && tree_iter)
            tree_iter = tree_iter->next_arg;
    }

    lily_class *variant_parent = NULL;
    tree_iter = ast->arg_start;
    if (is_hash)
        tree_iter = tree_iter->next_arg;

    if (rebox_type == NULL) {
        variant_parent = tree_iter->result->type->cls;
        if (variant_parent->flags & CLS_VARIANT_CLASS)
            variant_parent = variant_parent->parent;

        int generic_count = variant_parent->variant_type->template_pos;
        int i;
        /* If this isn't done, then old type info from...who knows where will
           improperly alter any defaulting. */
        lily_ts_zap_ceiling_types(emit->ts, generic_count);

        while (tree_iter != NULL) {
            lily_type *tree_result_type = tree_iter->result->type;
            lily_type *matcher_type;

            if (tree_result_type->cls->flags & CLS_ENUM_CLASS) {
                if (tree_result_type->cls != variant_parent)
                    rebox_type = any_class->type;
            }
            else if (tree_result_type->cls->flags & CLS_VARIANT_CLASS) {
                if (tree_result_type->cls->parent != variant_parent)
                    rebox_type = any_class->type;
            }
            else
                rebox_type = any_class->type;

            if (rebox_type != NULL)
                break;

            matcher_type = tree_result_type->cls->variant_type;
            /* If the variant takes arguments, then the variant_type it has is a
               function returning a type of the class at [0]. Otherwise, it's
               just a type of the class. */
            if (matcher_type->subtype_count != 0)
                matcher_type = matcher_type->subtypes[0];

            /* Make sure that there are no disagreements about what type(s) the
               generics (if any) are for the resulting enum class value. */
            for (i = 0;i < matcher_type->subtype_count;i++) {
                int pos = matcher_type->subtypes[i]->template_pos;
                lily_type *ceil_type = lily_ts_get_ceiling_type(emit->ts, pos);
                if (ceil_type == NULL)
                    lily_ts_set_ceiling_type(emit->ts,
                            tree_result_type->subtypes[i], pos);
                else if (ceil_type != tree_result_type->subtypes[i]) {
                    rebox_type = any_class->type;
                    break;
                }
            }

            if (rebox_type != NULL)
                break;

            tree_iter = tree_iter->next_arg;
            if (is_hash && tree_iter)
                tree_iter = tree_iter->next_arg;
        }

        if (rebox_type == NULL) {
            /* It may be that the enum class specifies generics that were not
               satisfied by any variant member. In such a case, default to
               class 'any'.
               Example: enum class Option[A] { Some(A), None }

               [None, None, None].

               FIRST try to see if the type wanted can give whatever info
               may be missing. If it can't, then default to any.

               Otherwise, 'list[Option[integer]] k = [None, None]' fails. */
            lily_type *ceil_type;
            if (expect_type && expect_type->cls == variant_parent) {
                for (i = 0;i < generic_count;i++) {
                    ceil_type = lily_ts_get_ceiling_type(emit->ts, i);
                    if (ceil_type == NULL)
                        lily_ts_set_ceiling_type(emit->ts,
                                expect_type->subtypes[i], i);
                }
            }
            else {
                for (i = 0;i < generic_count;i++) {
                    ceil_type = lily_ts_get_ceiling_type(emit->ts, i);
                    if (ceil_type == NULL)
                        lily_ts_set_ceiling_type(emit->ts, any_class->type, i);
                }
            }

            rebox_type = lily_ts_build_by_ceiling(emit->ts, variant_parent,
                    generic_count, 0);
        }
    }

    tree_iter = ast->arg_start;
    if (is_hash)
        tree_iter = tree_iter->next_arg;

    while (tree_iter) {
        if (tree_iter->result->type != rebox_type)
            emit_rebox_value(emit, rebox_type, tree_iter);

        tree_iter = tree_iter->next_arg;
        if (is_hash && tree_iter)
            tree_iter = tree_iter->next_arg;
    }
}

/*  hash_values_to_anys

    This converts all of the values of the given ast into anys using
    o_assign. The result of each value is rewritten to be the any,
    instead of the old value.

    emit:     The emitter holding the function to write code to.
    hash_ast: An ast of type tree_hash which has already been evaluated.

    Caveats:
    * Caller must do this before writing the o_build_hash instruction out.
    * Caller must evaluate the hash before calling this.
    * This will call lily_raise_nomem in the event of being unable to allocate
      an any value. */
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
    * Caller must evaluate the list before calling this.
    * This will call lily_raise_nomem in the event of being unable to allocate
      an any value. */
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

    lily_class *hash_cls = lily_class_by_id(emit->symtab, SYM_CLASS_HASH);
    lily_ts_set_ceiling_type(emit->ts, last_key_type, 0);
    lily_ts_set_ceiling_type(emit->ts, last_value_type, 1);
    lily_type *new_type = lily_ts_build_by_ceiling(emit->ts, hash_cls, 2, 0);

    lily_storage *s = get_storage(emit, new_type, ast->line_num);

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
            int pos = variant_result->subtypes[i]->template_pos;
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
    /* First, make sure that right is a member... */
    lily_class *variant_class = right->cls;
    lily_class *enum_class = enum_type->cls;
    int i, ok = 0;
    for (i = 0;i < enum_class->variant_size;i++) {
        if (enum_class->variant_members[i] == variant_class) {
            ok = 1;
            break;
        }
    }

    if (ok == 1) {
        /* If the variant does not take arguments, then there's nothing that
           could have been called wrong. Therefore, the use of the variant MUST
           be correct. */
        if (right->subtype_count != 0)
            ok = check_proper_variant(emit, enum_type, right, variant_class);
    }

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
            if (expect_type->cls->id == SYM_CLASS_TEMPLATE &&
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
        lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_ANY);
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

    lily_class *list_cls = lily_class_by_id(emit->symtab, SYM_CLASS_LIST);
    lily_ts_set_ceiling_type(emit->ts, elem_type, 0);
    lily_type *new_type = lily_ts_build_by_ceiling(emit->ts, list_cls, 1, 0);

    lily_storage *s = get_storage(emit, new_type, ast->line_num);

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

    if (expect_type && expect_type->cls->id == SYM_CLASS_TEMPLATE &&
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
                elem_type->cls->id == SYM_CLASS_TEMPLATE) {
                elem_type = lily_ts_easy_resolve(emit->ts, elem_type);
            }
        }

        if (arg->tree_type != tree_local_var)
            eval_tree(emit, arg, elem_type, 1);

        if ((elem_type && elem_type != arg->result->type) ||
            (arg->result->type->cls->flags & CLS_VARIANT_CLASS)) {
            if (elem_type && elem_type != arg->result->type)
                /* Attempt to fix the type to what's wanted. If it fails, the
                   caller will likely note a type mismatch. Can't do anything
                   else though. */
                type_matchup(emit, elem_type, arg);
            else {
                /* Not sure what the caller wants, so make an enum type based
                   of what's known and use that. */
                lily_type *enum_type = lily_ts_build_enum_by_variant(emit->ts,
                        arg->result->type);

                emit_rebox_value(emit, enum_type, arg);
            }
        }
    }

    for (i = 0, arg = ast->arg_start;
         i < ast->args_collected;
         i++, arg = arg->next_arg) {
        lily_ts_set_ceiling_type(emit->ts, arg->result->type, i);
    }

    lily_class *tuple_cls = lily_class_by_id(emit->symtab, SYM_CLASS_TUPLE);
    lily_type *new_type = lily_ts_build_by_ceiling(emit->ts, tuple_cls, i, 0);
    lily_storage *s = get_storage(emit, new_type, ast->line_num);

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

    lily_storage *result = get_storage(emit, type_for_result, ast->line_num);

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

/*  eval_call_arg
    Evaluate the argument of a function call and do some type matching up on
    the result. This is different than type_matchup, because it's a fair chance
    that the arguments may hold information about generics. */
static void eval_call_arg(lily_emit_state *emit, lily_ast *call_ast,
        int template_adjust, lily_type *want_type, lily_ast *arg, int arg_num)
{
    if (arg->tree_type != tree_local_var)
        /* Calls fill in their type info as they go along, courteousy of their
           arguments. So the types are NEVER resolved. */
        eval_tree(emit, arg, want_type, 0);

    /* It may seem tempting to do type == type in here. Don't.
       If one generic function calls another, then the caller needs to know
       that the generic types of the callee match to the generic types of the
       caller. */
    int ok = 0;

    /* This is used so that type_matchup gets the resolved type (if there
       is one) because the resolved type might be 'any'. */
    lily_type *matchup_type = want_type;

    /* A simple generic is easy to resolve. */
    if (want_type->cls->id == SYM_CLASS_TEMPLATE) {
        matchup_type = lily_ts_easy_resolve(emit->ts, want_type);
        if (matchup_type != NULL) {
            if (matchup_type == arg->result->type)
                ok = 1;
        }
        else if (arg->result->type->cls->flags & CLS_VARIANT_CLASS &&
                matchup_type == NULL) {
            /* If a bare variant wants to resolve a generic, it first has to go
               into the proper enum type. */
            lily_type *enum_type = lily_ts_build_enum_by_variant(emit->ts,
                    arg->result->type);
            emit_rebox_value(emit, enum_type, arg);
        }
    }

    /* ok == 0 protects from potentially attempting to resolve the same generic
       twice, which breaks things. */
    if (ok == 0 &&
        (lily_ts_check(emit->ts, want_type, arg->result->type) ||
         type_matchup(emit, matchup_type, arg)))
        ok = 1;

    if (ok == 0)
        bad_arg_error(emit, call_ast, arg->result->type, want_type,
                      arg_num);
}

/*  box_call_variants
    This function is called when check_call_args is done processing arguments
    AND the call has been tagged by the symtab as having enum values.

    This function exists because it's possible for a Lily function to not know
    what the resulting enum class should be. In such a case, call argument
    processing calls this to make sure any variants are put into a proper enum
    class value. */
static void box_call_variants(lily_emit_state *emit, lily_type *call_type,
        int num_args, lily_ast *arg)
{
    int i;
    for (i = 0;
         i != num_args;
         i++, arg = arg->next_arg) {
        if (arg->result->type->cls->flags & CLS_VARIANT_CLASS) {
            lily_type *arg_type = call_type->subtypes[i + 1];
            lily_type *enum_type = lily_ts_resolve(emit->ts, arg_type);
            emit_rebox_value(emit, enum_type, arg);
        }
    }

    if (call_type->flags & TYPE_IS_VARARGS) {
        /* This is called before the varargs are shoved into a list, so looping
           over the args is fine.
           Varargs is represented as a list of some type, so this next line grabs
           the list, then what the list holds. */
        lily_type *va_comp_type = call_type->subtypes[i + 1]->subtypes[0];
        if (va_comp_type->cls->flags & CLS_ENUM_CLASS) {
            lily_type *enum_type = lily_ts_resolve(emit->ts, va_comp_type);
            for (;arg != NULL;
                  i++, arg = arg->next_arg) {
                if (arg->result->type->cls->flags & CLS_VARIANT_CLASS)
                    emit_rebox_value(emit, enum_type, arg);
            }
        }
    }
}

/*  check_call_args
    eval_call uses this to make sure the types of all the arguments are right.

    If the function takes varargs, the extra arguments are packed into a list
    of the vararg type. */
static void check_call_args(lily_emit_state *emit, lily_ast *ast,
        lily_type *call_type, lily_type *expect_type, int did_resolve)
{
    lily_ast *arg = ast->arg_start;
    lily_ast *true_start = arg;
    int have_args, i, is_varargs, num_args;
    int auto_resolve = 0;

    /* oo_access is the only tree where the first argument is actually supposed
       to be passed in as a value. */
    if (arg->tree_type != tree_oo_access) {
        if (arg->tree_type == tree_local_var ||
            arg->tree_type == tree_inherited_new)
            auto_resolve = 1;
        arg = arg->next_arg;
        true_start = arg;
        ast->args_collected--;
    }

    /* Ast doesn't check the call args. It can't check types, so why do only
       half of the validation? */
    have_args = ast->args_collected;
    is_varargs = call_type->flags & TYPE_IS_VARARGS;
    /* Take the last arg off of the arg count. This will be verified using the
       var arg type. */
    num_args = (call_type->subtype_count - 1) - is_varargs;

    if ((is_varargs && (have_args < num_args)) ||
        (is_varargs == 0 && (have_args != num_args))) {
        bad_num_args(emit, ast, call_type);
    }

    /* Templates are rather simple: The first time they're seen, the type they
       see is written into emitter's type stack. Subsequent passes check that
       the type seen is the same one (so multiple uses of A have the same
       type). */
    int template_adjust = call_type->template_pos;

    emit->current_generic_adjust = template_adjust;

    if (template_adjust) {
        if (auto_resolve == 0) {
            lily_type *call_result = call_type->subtypes[0];
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
        else {
            /* This block of code makes it so that each generic is resolved as
               itself, thereby preventing generics from being resolved as
               something else.
               There are two cases for this:
               * This function is a parameter to a generic function. It's bad
                 to attempt to solve types IN a generic function.
               * This function is being executed to process one function
                 inheriting another one. This one is a choice: By making sure
                 that A does not change between classes, determining the type
                 of a property is MUCH easier. */
            lily_ts_resolve_as_self(emit->ts);
        }
    }

    emit->current_generic_adjust = template_adjust;

    for (i = 0;i != num_args;arg = arg->next_arg, i++)
        eval_call_arg(emit, ast, template_adjust, call_type->subtypes[i + 1],
                arg, i);

    if (is_varargs == 0 && call_type->flags & TYPE_CALL_HAS_ENUM_ARG)
        box_call_variants(emit, call_type, num_args, true_start);
    else if (is_varargs) {
        lily_type *va_comp_type = call_type->subtypes[i + 1];
        lily_ast *save_arg = arg;
        lily_type *save_type;

        /* varargs is handled by shoving the excess into a list. The elements
           need to be the type of what the list holds. */
        save_type = va_comp_type;
        va_comp_type = va_comp_type->subtypes[0];

        /* The difference is that this time the type wanted is always
           va_comp_type. */
        for (;arg != NULL;arg = arg->next_arg)
            eval_call_arg(emit, ast, template_adjust, va_comp_type, arg, i);

        if (call_type->flags & TYPE_CALL_HAS_ENUM_ARG)
            box_call_variants(emit, call_type, num_args, true_start);

        i = (have_args - i);
        lily_storage *s;

        /* Make sure the generic type is resolved. This is important if, for
           example, the varargs sig is list[F]... and the caller has F as some
           lower generic type. */
        if (save_type->flags & TYPE_IS_UNRESOLVED)
            save_type = lily_ts_resolve(emit->ts, save_type);

        s = get_storage(emit, save_type, ast->line_num);

        if (have_args > num_args)
            write_build_op(emit, o_build_list_tuple, save_arg,
                save_arg->line_num, i, s->reg_spot);
        else {
            /* This happens when the user doesn't pass anything for the vararg
               part. Solve this by creating a blank value (with the right type)
               and adding a tree to hold said value. */
            write_4(emit, o_build_list_tuple, ast->line_num, 0, s->reg_spot);
            save_arg = ast->stashed_tree;
            ast->stashed_tree->tree_type = tree_list;

            lily_ast *tree_iter = true_start;
            /* If there are other arguments, put this new tree at the end. */
            if (true_start) {
                while (tree_iter->next_arg != NULL)
                    tree_iter = tree_iter->next_arg;

                tree_iter->next_arg = save_arg;
            }
            else
                /* Otherwise, the new tree becomes the argument chain. */
                true_start = save_arg;
        }

        /* Fix the ast so that it thinks the vararg list is the last value. */
        save_arg->result = (lily_sym *)s;
        save_arg->next_arg = NULL;
        ast->args_collected = num_args + 1;
    }

    /* Now that there can be no errors in calling, fix up the arg start to the
       real thing for the caller. */
    ast->arg_start = true_start;
}

/*  maybe_self_insert
    This is called when eval_call finds a readonly tre (a non-anonymous
    function call) and there is a current class.
    The ast given is the tree holding the function at result. Determine if this
    function belongs to the current class. If so, replace the ast with 'self'.
    Since this checks for tree_defined_func, only explicit calls to functions
    defined in the class get an implicit self. */
static int maybe_self_insert(lily_emit_state *emit, lily_ast *ast)
{
    /* Global functions defined outside of this class do not automatically get
       self as the first argument. */
    if (emit->current_class != ((lily_var *)ast->result)->parent)
        return 0;

    ast->arg_start->result = (lily_sym *)emit->block->self;
    ast->arg_start->tree_type = tree_local_var;
    return 1;
}

/*  eval_call
    This handles doing calls to what should be a function. It handles doing oo
    calls by farming out the oo lookup elsewhere. */
static void eval_call(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type, int did_resolve)
{
    int expect_size, i;
    lily_ast *arg;
    lily_type *call_type;
    lily_sym *call_sym;

    lily_tree_type first_tt = ast->arg_start->tree_type;
    /* Variants are created by calling them in a function-like manner, so the
       parser adds them as if they were functions. They're not. */
    if (first_tt == tree_variant) {
        eval_variant(emit, ast, expect_type, did_resolve);
        return;
    }
    /* DON'T walk either of these, because doing so puts them in a storage and
       the vm can call them by their spot without doing so. */
    else if (first_tt != tree_defined_func &&
             first_tt != tree_inherited_new)
        eval_tree(emit, ast->arg_start, NULL, 1);

    int cls_id;

    /* This is important because wrong type/wrong number of args looks to
       either ast->result or the first tree to get the function name. */
    ast->result = ast->arg_start->result;
    call_sym = ast->result;

    /* Make sure the result is callable (ex: NOT @(integer: 10) ()). */
    cls_id = ast->result->type->cls->id;
    if (cls_id != SYM_CLASS_FUNCTION)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Cannot anonymously call resulting type '^T'.\n",
                ast->result->type);

    if (first_tt == tree_oo_access) {
        /* Fix the oo access to return the first arg it had, since that's
           the call's first value. It's really important that
           check_call_args get all the args, because the first is the most
           likely to have a template parameter. */
        ast->arg_start->result = ast->arg_start->arg_start->result;
    }
    else if (first_tt == tree_defined_func) {
        /* If inside a class, then consider inserting replacing the
           unnecessary readonly tree with 'self'.
           If this isn't possible, drop the readonly tree from args since
           it isn't truly an argument. */
        if (emit->current_class != NULL)
            maybe_self_insert(emit, ast);
    }
    else if (first_tt != tree_inherited_new) {
        /* For anonymously called functions, unset ast->result so that arg
           error functions will report that it's anonymously called. */
        ast->result = NULL;
    }

    call_type = call_sym->type;
    arg = ast->arg_start;
    expect_size = 6 + ast->args_collected;

    int saved_ts_adjust = lily_ts_raise_ceiling(emit->ts,
            call_type->template_pos);
    check_call_args(emit, ast, call_type, expect_type, did_resolve);

    write_prep(emit, expect_size);

    lily_function_val *f = emit->top_function;
    f->code[f->pos] = o_function_call;
    f->code[f->pos+1] = ast->line_num;
    f->code[f->pos+2] = !!(call_sym->flags & VAR_IS_READONLY);
    f->code[f->pos+3] = call_sym->reg_spot;
    f->code[f->pos+4] = ast->args_collected;

    for (i = 5, arg = ast->arg_start;
        arg != NULL;
        arg = arg->next_arg, i++) {
        f->code[f->pos + i] = arg->result->reg_spot;
    }

    if (call_type->subtypes[0] != NULL) {
        lily_type *return_type = call_type->subtypes[0];

        /* If it's just a template, grab the appropriate thing from the type
           stack (which is okay until the next eval_call). Otherwise, just
           give up and build the right thing. */
        if (return_type->cls->id == SYM_CLASS_TEMPLATE)
            return_type = lily_ts_easy_resolve(emit->ts, return_type);
        else if (return_type->flags & TYPE_IS_UNRESOLVED)
            return_type = lily_ts_resolve(emit->ts, return_type);

        lily_storage *storage = get_storage(emit, return_type, ast->line_num);
        storage->flags |= SYM_NOT_ASSIGNABLE;

        ast->result = (lily_sym *)storage;
        f->code[f->pos+i] = ast->result->reg_spot;
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
        f->code[f->pos+i] = -1;
    }

    f->pos += 6 + ast->args_collected;

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

    if (ast->tree_type == tree_literal)
        opcode = o_get_const;
    else if (ast->tree_type == tree_defined_func ||
             ast->tree_type == tree_inherited_new)
        opcode = o_get_function;
    else if (ast->tree_type == tree_global_var)
        opcode = o_get_global;
    else
        opcode = -1;

    ret = get_storage(emit, ast->original_sym->type, ast->line_num);

    if (opcode != o_get_global)
        ret->flags |= SYM_NOT_ASSIGNABLE;

    write_4(emit,
            opcode,
            ast->line_num,
            ast->original_sym->reg_spot,
            ret->reg_spot);

    ast->result = (lily_sym *)ret;
}

/*  eval_package
    This is a wrapper for eval_package_tree_for_op. */
static void eval_package(lily_emit_state *emit, lily_ast *ast)
{
    /* Evaluate this tree using a generic function that can write get/set ops
       as needed. 0 = use get opcodes, NULL, because a value is only needed if
       using a set opcode. */
    eval_package_tree_for_op(emit, ast, 0, NULL);
}

/*  eval_oo_access
    This is an access like 'abc.xyz'. There are two fairly different cases for
    this:
    1: The given class has a method named xyz. This is checked first.
       Examples: 'string.concat' and 'integer.to_string'.
    2: The given class has a property named xyz. In this case, the value is a
       class which is subscripted for the right property. */
static void eval_oo_access(lily_emit_state *emit, lily_ast *ast)
{
    /* If this tree is to be called, it will be evaluated twice: First by
       eval_call to figure out what to call, and then by check_call_args since
       the result is the first argument. */
    if (ast->result)
        return;

    if (ast->arg_start->tree_type != tree_local_var)
        eval_tree(emit, ast->arg_start, NULL, 1);

    lily_class *lookup_class = ast->arg_start->result->type->cls;
    char *oo_name = lily_membuf_get(emit->ast_membuf, ast->membuf_pos);
    lily_var *var = lily_find_class_callable(emit->symtab,
            lookup_class, oo_name);

    /* Is this an attempt to access a property that hasn't been loaded yet? */
    if (var == NULL)
        var = lily_parser_dynamic_load(emit->parser, lookup_class, oo_name);

    if (var)
        ast->result = (lily_sym *)var;
    else {
        lily_prop_entry *prop = lily_find_property(emit->symtab,
                lookup_class, oo_name);

        if (prop == NULL) {
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Class %s has no callable or property named %s.\n",
                    lookup_class->name, oo_name);
        }

        /* oo_assign also needs this. Might as well set it for all. */
        ast->oo_property_index = prop->id;

        lily_type *property_type = prop->type;
        if (property_type->cls->id == SYM_CLASS_TEMPLATE ||
            property_type->template_pos) {
            property_type = lily_ts_resolve_by_second(emit->ts,
                    ast->arg_start->result->type,
                    property_type);
        }

        lily_storage *result = get_storage(emit, property_type,
                ast->line_num);

        /* Hack: If the parent is really oo_assign, then don't load the result
                 into a register. The parent tree just wants to know the
                 resulting type and the property index. */
        if (ast->parent == NULL ||
            ast->parent->tree_type != tree_binary ||
            ast->parent->op != expr_assign) {
            lily_literal *lit = lily_get_integer_literal(emit->symtab, prop->id);
            lily_storage *lit_result = get_storage(emit, lit->type,
                    ast->line_num);
            /* Don't use lookup_class->type, in case the class doesn't have a
               default type. */

            ast->result = (lily_sym *)result;

            write_4(emit,
                    o_get_const,
                    ast->line_num,
                    lit->reg_spot,
                    lit_result->reg_spot);

            write_5(emit,
                    o_get_item,
                    ast->line_num,
                    ast->arg_start->result->reg_spot,
                    lit_result->reg_spot,
                    result->reg_spot);
        }

        ast->result = (lily_sym *)result;
    }
}

/*  eval_property
    This handles evaluating '@<x>' within a class constructor. */
static void eval_property(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->property->type == NULL)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Invalid use of uninitialized property '@%s'.\n",
                ast->property->name);

    lily_storage *result = get_storage(emit, ast->property->type,
            ast->line_num);

    lily_literal *lit = lily_get_integer_literal(emit->symtab,
            ast->property->id);
    lily_storage *index_storage = get_storage(emit, lit->type, ast->line_num);

    write_4(emit,
            o_get_const,
            ast->line_num,
            lit->reg_spot,
            index_storage->reg_spot);

    write_5(emit,
            o_get_item,
            ast->line_num,
            emit->block->self->reg_spot,
            index_storage->reg_spot,
            result->reg_spot);

    ast->result = (lily_sym *)result;
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

        if (variant_type->subtype_count == 1)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Variant class %s should not get args.\n",
                    variant_class->name);

        int save_ceiling = lily_ts_raise_ceiling(emit->ts,
                variant_type->template_pos);
        check_call_args(emit, ast, variant_type, expect_type,
                did_resolve);

        lily_type *result_type = variant_class->variant_type->subtypes[0];
        if (result_type->template_pos != 0)
            result_type = lily_ts_resolve(emit->ts, result_type);

        result = get_storage(emit, result_type, ast->line_num);

        /* It's pretty darn close to a tuple, so...let's use that. :) */
        write_build_op(emit, o_build_list_tuple, ast->arg_start,
                ast->line_num, ast->args_collected, result->reg_spot);

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
        lily_literal *variant_lit = lily_get_variant_literal(emit->symtab,
                variant_type);

        result = get_storage(emit, variant_type, ast->line_num);
        write_4(emit, o_get_const, ast->line_num, variant_lit->reg_spot,
                result->reg_spot);
    }

    ast->result = (lily_sym *)result;
}

static void eval_lambda(lily_emit_state *emit, lily_ast *ast,
        lily_type *expect_type, int did_resolve)
{
    char *lambda_body = lily_membuf_get(emit->ast_membuf, ast->membuf_pos);

    if (expect_type && expect_type->cls->id == SYM_CLASS_TEMPLATE &&
        did_resolve == 0) {
        expect_type = lily_ts_easy_resolve(emit->ts, expect_type);
        did_resolve = 1;
    }

    if (expect_type && expect_type->cls->id != SYM_CLASS_FUNCTION)
        expect_type = NULL;

    lily_var *lambda_result = lily_parser_lambda_eval(emit->parser,
            ast->line_num, lambda_body, expect_type, did_resolve);

    lily_storage *s = get_storage(emit, lambda_result->type, ast->line_num);
    write_4(emit,
            o_get_function,
            ast->line_num,
            lambda_result->reg_spot,
            s->reg_spot);

    ast->result = (lily_sym *)s;
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
            if (ast->left->tree_type != tree_subscript &&
                ast->left->tree_type != tree_package &&
                ast->left->tree_type != tree_oo_access &&
                ast->left->tree_type != tree_property)
                eval_assign(emit, ast);
            else if (ast->left->tree_type == tree_package)
                eval_package_assign(emit, ast);
            else if (ast->left->tree_type == tree_subscript)
                eval_sub_assign(emit, ast);
            else
                eval_oo_and_prop_assign(emit, ast);

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
    else if (ast->tree_type == tree_package)
        eval_package(emit, ast);
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
        if (v != emit->symtab->var_chain)
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

    lily_function_val *f = emit->top_function;

    /* Transitioning between blocks is simple: First write a jump at the end of
       the current branch. This will get patched to the if/try's exit. */
    write_2(emit, o_jump, 0);
    save_jump = f->pos - 1;

    /* The last jump of the previous branch wants to know where the check for
       the next branch starts. It's right now. */
    if (emit->patches[emit->patch_pos - 1] != -1)
        f->code[emit->patches[emit->patch_pos-1]] = f->pos;
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
                    emit->block->loop_start);
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
            write_2(emit, o_jump, emit->block->loop_start);
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
    lily_function_val *f = emit->top_function;
    int value_count = variant_type->subtype_count - 1;
    int i;

    write_prep(emit, 4 + value_count);

    f->code[f->pos  ] = o_variant_decompose;
    f->code[f->pos+1] = *(emit->lex_linenum);
    f->code[f->pos+2] = emit->block->match_sym->reg_spot;
    f->code[f->pos+3] = value_count;

    /* Since this function is called immediately after declaring the last var
       that will receive the decompose, it's safe to pull the vars directly
       from symtab's var chain. */
    lily_var *var_iter = emit->symtab->var_chain;

    /* Go down because the vars are linked from newest -> oldest. If this isn't
       done, then the first var will get the last value in the variant, the
       second will get the next-to-last value, etc. */
    for (i = value_count - 1;i >= 0;i--) {
        f->code[f->pos+4+i] = var_iter->reg_spot;
        var_iter = var_iter->next;
    }

    f->pos += 4 + value_count;
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
    lily_function_val *f = emit->top_function;
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

            emit->patches[emit->patch_pos] = f->pos-1;
            emit->patch_pos++;
        }

        /* Patch the o_match_dispatch spot the corresponds with this class
           so that it will jump to the current location.
           Oh, and make sure to do it AFTER writing the jump, or the dispatch
           will go to the exit jump. */
        f->code[emit->block->match_code_start + pos] = f->pos;

        /* This is necessary to keep vars created from the decomposition of one
           class from showing up in subsequent cases. */
        lily_var *v = emit->block->var_start;
        if (v != emit->symtab->var_chain)
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

    block->match_code_start = emit->top_function->pos + 4;
    block->match_sym = (lily_sym *)ast->result;

    write_prep(emit, 4 + match_cases_needed);

    lily_function_val *f = emit->top_function;

    f->code[f->pos  ] = o_match_dispatch;
    f->code[f->pos+1] = *(emit->lex_linenum);
    f->code[f->pos+2] = ast->result->reg_spot;
    f->code[f->pos+3] = match_cases_needed;
    for (i = 0;i < match_cases_needed;i++)
        f->code[f->pos + 4 + i] = 0;

    f->pos += 4 + i;

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
    lily_block *loop_block = emit->block;
    lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

    int have_step = (for_step != NULL);
    if (have_step == 0) {
        /* This var isn't visible, so don't bother with a valid shorthash. */
        for_step = lily_try_new_var(emit->symtab, cls->type, "(for step)", 0);
        if (for_step == NULL)
            lily_raise_nomem(emit->raiser);
    }

    lily_sym *target;
    /* Global vars cannot be used directly, because o_for_setup and
       o_integer_for expect local registers. */
    if (user_loop_var->function_depth == 1)
        target = (lily_sym *)get_storage(emit, user_loop_var->type, line_num);
    else
        target = (lily_sym *)user_loop_var;

    write_prep(emit, 16 + ((target != (lily_sym *)user_loop_var) * 8));
    lily_function_val *f = emit->top_function;
    f->code[f->pos  ] = o_for_setup;
    f->code[f->pos+1] = line_num;
    f->code[f->pos+2] = target->reg_spot;
    f->code[f->pos+3] = for_start->reg_spot;
    f->code[f->pos+4] = for_end->reg_spot;
    f->code[f->pos+5] = for_step->reg_spot;
    /* This value is used to determine if the step needs to be calculated. */
    f->code[f->pos+6] = !have_step;

    if (target != (lily_sym *)user_loop_var) {
        f->code[f->pos+7] = o_set_global;
        f->code[f->pos+8] = line_num;
        f->code[f->pos+9] = target->reg_spot;
        f->code[f->pos+10] = user_loop_var->reg_spot;
        f->pos += 4;
    }
    /* for..in is entered right after 'for' is seen. However, range values can
       be expressions. This needs to be fixed, or the loop will jump back up to
       re-eval those expressions. */
    loop_block->loop_start = f->pos+9;

    /* Write a jump to the inside of the loop. This prevents the value from
       being incremented before being seen by the inside of the loop. */
    f->code[f->pos+7] = o_jump;
    f->code[f->pos+8] = f->pos + 16;

    f->code[f->pos+9] = o_integer_for;
    f->code[f->pos+10] = line_num;
    f->code[f->pos+11] = target->reg_spot;
    f->code[f->pos+12] = for_start->reg_spot;
    f->code[f->pos+13] = for_end->reg_spot;
    f->code[f->pos+14] = for_step->reg_spot;
    f->code[f->pos+15] = 0;
    if (target != (lily_sym *)user_loop_var) {
        f->code[f->pos+16] = o_set_global;
        f->code[f->pos+17] = line_num;
        f->code[f->pos+18] = target->reg_spot;
        f->code[f->pos+19] = user_loop_var->reg_spot;
        f->pos += 4;
    }

    f->pos += 16;

    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    if (target == (lily_sym *)user_loop_var)
        emit->patches[emit->patch_pos] = f->pos - 1;
    else
        emit->patches[emit->patch_pos] = f->pos - 5;

    emit->patch_pos++;
}

void lily_emit_eval_lambda_body(lily_emit_state *emit, lily_ast_pool *ap,
        lily_type *wanted_type, int did_resolve)
{
    if (wanted_type && wanted_type->cls->id == SYM_CLASS_TEMPLATE &&
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
    if (emit->block->loop_start == -1) {
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
    lily_function_val *f = emit->top_function;

    /* If the while is the most current, then add it to the end. */
    if (emit->block->block_type == BLOCK_WHILE) {
        emit->patches[emit->patch_pos] = f->pos-1;
        emit->patch_pos++;
    }
    else {
        lily_block *block = find_deepest_loop(emit);
        /* The while is not on top, so this will be fairly annoying... */
        int move_by, move_start;

        move_start = block->next->patch_start;
        move_by = emit->patch_pos - move_start;

        /* Move everything after this patch start over one, so that there's a
           hole after the last while patch to write in a new one. */
        memmove(emit->patches+move_start+1, emit->patches+move_start,
                move_by * sizeof(int));
        emit->patch_pos++;
        emit->patches[move_start] = f->pos-1;

        for (block = block->next;
             block;
             block = block->next)
            block->patch_start++;
    }
}

/*  lily_emit_continue
    The parser wants to write a jump to the top of the current loop (continue
    keyword). */
void lily_emit_continue(lily_emit_state *emit)
{
    /* This is called by parser on the source line, so do not adjust the
       raiser. */
    if (emit->block->loop_start == -1) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "'continue' used outside of a loop.\n");
    }

    write_pop_inner_try_blocks(emit);

    write_2(emit, o_jump, emit->block->loop_start);
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

    if (ast)
        write_3(emit, o_return_val, ast->line_num, ast->result->reg_spot);
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

        lily_storage *self = get_storage(emit, self_type, *emit->lex_linenum);
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

    lily_function_val *f = emit->top_function;
    emit->patches[emit->patch_pos] = f->pos - 1;
    emit->patch_pos++;
}

/*  lily_emit_raise
    Process the given ast and write an instruction that will attempt to raise
    the resulting value. The ast is checked to ensure it can be raised. */
void lily_emit_raise(lily_emit_state *emit, lily_ast *ast)
{
    eval_enforce_value(emit, ast, NULL, "'raise' expression has no value.\n");

    lily_class *result_cls = ast->result->type->cls;
    lily_class *except_cls = lily_class_by_name(emit->symtab, "Exception");
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
        except_sym = (lily_sym *)get_storage(emit, except_type, line_num);

    write_5(emit,
            o_except,
            line_num,
            0,
            (except_var != NULL),
            except_sym->reg_spot);

    lily_function_val *f = emit->top_function;

    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    emit->patches[emit->patch_pos] = f->pos - 3;
    emit->patch_pos++;
}

/*  lily_emit_vm_return
    This is called just before __main__ is to be executed. It 'finalizes'
    __main__ so the vm knows what types to use for different registers,
    and writes the o_return_from_vm instruction that will leave the vm. */
void lily_emit_vm_return(lily_emit_state *emit)
{
    finalize_function_val(emit, emit->block);
    write_1(emit, o_return_from_vm);
}

/*  lily_reset_main
    (tagged mode) This is called after __main__ is executed to prepare __main__
    for new code. */
void lily_reset_main(lily_emit_state *emit)
{
    ((lily_function_val *)emit->top_function)->pos = 0;
}

/*  lily_emit_enter_block
    Enter a block of a given type. If unable to get a block, this will call
    lily_raise_nomem. This only handles block states, not multi/single line
    information. */
void lily_emit_enter_block(lily_emit_state *emit, int block_type)
{
    lily_block *new_block;
    if (emit->block->next == NULL) {
        new_block = try_new_block();
        if (new_block == NULL)
            lily_raise_nomem(emit->raiser);

        emit->block->next = new_block;
        new_block->prev = emit->block;
    }
    else
        new_block = emit->block->next;

    new_block->block_type = block_type;
    new_block->var_start = emit->symtab->var_chain;
    new_block->class_entry = NULL;
    new_block->self = emit->block->self;
    new_block->generic_count = 0;

    if ((block_type & BLOCK_FUNCTION) == 0) {
        new_block->patch_start = emit->patch_pos;
        /* Non-functions will continue using the storages that the parent uses.
           Additionally, the same technique is used to allow loop starts to
           bubble upward until a function gets in the way. */
        new_block->storage_start = emit->block->storage_start;
        if (IS_LOOP_BLOCK(block_type))
            new_block->loop_start = emit->top_function->pos;
        else
            new_block->loop_start = emit->block->loop_start;
    }
    else {
        lily_var *v = emit->symtab->var_chain;
        if (block_type & BLOCK_CLASS) {
            emit->current_class = emit->symtab->class_chain;
            new_block->class_entry = emit->symtab->class_chain;
        }

        char *class_name;
        v->parent = emit->current_class;
        if (v->parent)
            class_name = v->parent->name;
        else
            class_name = NULL;

        v->value.function = lily_try_new_native_function_val(class_name,
                v->name);
        if (v->value.function == NULL)
            lily_raise_nomem(emit->raiser);

        v->flags &= ~(VAL_IS_NIL);

        new_block->save_register_spot = emit->symtab->next_register_spot;

        emit->symtab->function_depth++;
        /* Make sure registers start at 0 again. This will be restored when this
           function leaves. */
        emit->symtab->next_register_spot = 0;
        /* This function's storages start where the unused ones start, or NULL if
           all are currently taken. */
        new_block->storage_start = emit->unused_storage_start;
        new_block->function_var = v;
        /* -1 to indicate that there is no current loop. */
        new_block->loop_start = -1;

        emit->top_function = v->value.function;
        emit->top_var = v;
        emit->function_depth++;
    }

    emit->block = new_block;
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
        write_2(emit, o_jump, emit->block->loop_start);
    else if (block_type == BLOCK_MATCH) {
        ensure_proper_match_block(emit);
        emit->match_case_pos = emit->block->match_case_start;
    }

    v = block->var_start;

    if ((block_type & BLOCK_FUNCTION) == 0) {
        int from, to, pos;
        from = emit->patch_pos-1;
        to = block->patch_start;
        pos = emit->top_function->pos;

        for (;from >= to;from--) {
            /* Skip -1's, which are fake patches from conditions that were
               optimized out. */
            if (emit->patches[from] != -1)
                emit->top_function->code[emit->patches[from]] = pos;
        }

        /* Use the space for new patches now. */
        emit->patch_pos = to;

        lily_hide_block_vars(emit->symtab, v);
    }
    else
        leave_function(emit, block);

    emit->block = emit->block->prev;
}

/*  lily_emit_try_enter_main
    Make a block representing __main__ and go inside of it. Returns 1 on
    success, 0 on failure. This should only be called once. */
int lily_emit_try_enter_main(lily_emit_state *emit, lily_var *main_var)
{
    /* This adds the first storage and makes sure that the emitter can always
       know that emit->unused_storage_start is never NULL. */
    if (try_add_storage(emit) == 0)
        return 0;

    lily_block *main_block = try_new_block();
    if (main_block == NULL)
        return 0;

    main_var->value.function = lily_try_new_native_function_val(NULL,
            main_var->name);
    if (main_var->value.function == NULL) {
        lily_free(main_block);
        return 0;
    }
    /* __main__ is given two refs so that it must go through a custom deref to
       be destroyed. This is because the names in the function info it has are
       shared with vars that are still around. */
    main_var->value.function->refcount++;
    main_var->flags &= ~VAL_IS_NIL;

    main_block->prev = NULL;
    main_block->block_type = BLOCK_FUNCTION;
    main_block->function_var = main_var;
    main_block->storage_start = emit->all_storage_start;
    /* This is necessary for trapping break/continue inside of __main__. */
    main_block->loop_start = -1;
    main_block->class_entry = NULL;
    main_block->generic_count = 0;
    main_block->self = NULL;
    emit->top_function = main_var->value.function;
    emit->top_var = main_var;
    emit->block = main_block;
    emit->function_depth++;
    return 1;
}

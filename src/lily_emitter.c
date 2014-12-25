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

static int type_matchup(lily_emit_state *, lily_sig *, lily_ast *);
static void eval_tree(lily_emit_state *, lily_ast *, lily_sig *);
static void eval_variant(lily_emit_state *, lily_ast *, lily_sig *);

/*****************************************************************************/
/* Emitter setup and teardown                                                */
/*****************************************************************************/

lily_emit_state *lily_new_emit_state(lily_raiser *raiser)
{
    lily_emit_state *s = lily_malloc(sizeof(lily_emit_state));

    if (s == NULL)
        return NULL;

    s->patches = lily_malloc(sizeof(int) * 4);
    s->sig_stack = lily_malloc(sizeof(lily_sig *) * 4);
    s->match_cases = lily_malloc(sizeof(int) * 4);

    if (s->patches == NULL || s->sig_stack == NULL ||
        s->match_cases == NULL) {
        lily_free(s->match_cases);
        lily_free(s->patches);
        lily_free(s->sig_stack);
        lily_free(s);
        return NULL;
    }

    s->match_case_pos = 0;
    s->match_case_size = 4;

    s->current_block = NULL;
    s->unused_storage_start = NULL;
    s->all_storage_start = NULL;
    s->all_storage_top = NULL;

    s->sig_stack_pos = 0;
    s->sig_stack_size = 4;

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
    current = emit->current_block;
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

    lily_free(emit->match_cases);
    lily_free(emit->sig_stack);
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
    type 'tree_readonly'. If the given tree is always true, then the emitter
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
        int lit_cls_id = lit->sig->cls->id;
        if (lit_cls_id == SYM_CLASS_INTEGER && lit->value.integer == 0)
            can_optimize = 0;
        else if (lit_cls_id == SYM_CLASS_DOUBLE && lit->value.doubleval == 0.0)
            can_optimize = 0;
        else if (lit_cls_id == SYM_CLASS_STRING && lit->value.string->size == 0)
            can_optimize = 0;
        else if (lit->sig->cls->flags & CLS_VARIANT_CLASS)
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
    lily_block *block_iter = emit->current_block;
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

    for (block = emit->current_block;
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

/*  grow_sig_stack
    Make emitter's sig_stack bigger. */
static void grow_sig_stack(lily_emit_state *emit)
{
    emit->sig_stack_size *= 2;

    lily_sig **new_sig_stack = lily_realloc(emit->sig_stack,
        sizeof(lily_sig *) * emit->sig_stack_size);

    if (new_sig_stack == NULL)
        lily_raise_nomem(emit->raiser);

    emit->sig_stack = new_sig_stack;
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
static void ensure_valid_condition_type(lily_emit_state *emit, lily_sig *sig)
{
    int cls_id = sig->cls->id;

    if (cls_id != SYM_CLASS_INTEGER &&
        cls_id != SYM_CLASS_DOUBLE &&
        cls_id != SYM_CLASS_STRING &&
        cls_id != SYM_CLASS_LIST)
        lily_raise(emit->raiser, lily_SyntaxError,
                "%T is not a valid condition type.\n", sig);
}

/*  check_valid_subscript
    Determine if the given var is subscriptable by the type of the given index.
    Additionally, an 'index literal' is given as a special-case for tuples.
    This raises an error for unsubscriptable types. */
static void check_valid_subscript(lily_emit_state *emit, lily_ast *var_ast,
        lily_ast *index_ast, lily_literal *index_literal)
{
    int var_cls_id = var_ast->result->sig->cls->id;
    if (var_cls_id == SYM_CLASS_LIST) {
        if (index_ast->result->sig->cls->id != SYM_CLASS_INTEGER)
            lily_raise_adjusted(emit->raiser, var_ast->line_num,
                    lily_SyntaxError, "list index is not an integer.\n", "");
    }
    else if (var_cls_id == SYM_CLASS_HASH) {
        lily_sig *want_key = var_ast->result->sig->siglist[0];
        lily_sig *have_key = index_ast->result->sig;

        if (want_key != have_key) {
            lily_raise_adjusted(emit->raiser, var_ast->line_num, lily_SyntaxError,
                    "hash index should be type '%T', not type '%T'.\n",
                    want_key, have_key);
        }
    }
    else if (var_cls_id == SYM_CLASS_TUPLE) {
        if (index_ast->result->sig->cls->id != SYM_CLASS_INTEGER ||
            index_ast->tree_type != tree_readonly) {
            lily_raise_adjusted(emit->raiser, var_ast->line_num, lily_SyntaxError,
                    "tuple subscripts must be integer literals.\n", "");
        }
        lily_sig *var_sig = var_ast->result->sig;
        if (index_literal->value.integer < 0 ||
            index_literal->value.integer >= var_sig->siglist_size) {

            lily_raise_adjusted(emit->raiser, var_ast->line_num,
                    lily_SyntaxError, "Index %d is out of range for %T.\n",
                    index_literal->value.integer, var_sig);
        }
    }
    else {
        lily_raise_adjusted(emit->raiser, var_ast->line_num, lily_SyntaxError,
                "Cannot subscript type '%T'.\n",
                var_ast->result->sig);
    }
}

/*  get_subscript_result
    Get the type that would result from doing a subscript. tuple_index_lit is
    a special case for tuples. */
static lily_sig *get_subscript_result(lily_sig *sig, lily_ast *index_ast,
        lily_literal *tuple_index_lit)
{
    lily_sig *result;
    if (sig->cls->id == SYM_CLASS_LIST)
        result = sig->siglist[0];
    else if (sig->cls->id == SYM_CLASS_HASH)
        result = sig->siglist[1];
    else if (sig->cls->id == SYM_CLASS_TUPLE)
        /* check_valid_subscript verifies that the literal is an integer with
           a sane index. */
        result = sig->siglist[tuple_index_lit->value.integer];
    else
        /* Won't happen, but keeps the compiler from complaining. */
        result = NULL;

    return result;
}

/*  try_add_storage
    This is a helper for get_storage which attempts to add a new storage to
    emitter's chain of storages with the given signature.

    Success: A new storage is returned.
    Failure: NULL is returned. */
static lily_storage *try_add_storage(lily_emit_state *emit, lily_sig *sig)
{
    lily_storage *ret = lily_malloc(sizeof(lily_storage));
    if (ret == NULL)
        return ret;

    ret->sig = sig;
    ret->next = NULL;
    ret->expr_num = emit->expr_num;
    ret->flags = 0;

    ret->reg_spot = emit->symtab->next_register_spot;
    emit->symtab->next_register_spot++;

    if (emit->all_storage_start == NULL)
        emit->all_storage_start = ret;
    else
        emit->all_storage_top->next = ret;

    emit->all_storage_top = ret;
    return ret;
}

/*  get_storage
    Attempt to get an available storage register for the given signature. This
    will first attempt to get an available storage in the current function,
    then try to create a new one. This has the side-effect of fixing
    storage_start in the event that storage_start is NULL. This fixup is done
    up to the current function block.

    emit:     The emitter to search for a storage in.
    sig:      The signature to search for.
    line_num: If a storage cannot be obtained, lily_raise_nomem is called after
              fixing the raiser's line number to line_num. This cuts down on
              boilerplate code from checking that obtaining a storage succeded.

    This call shall either return a valid storage of the given signature, or
    raise NoMemoryError with the given line_num. */
static lily_storage *get_storage(lily_emit_state *emit,
        lily_sig *sig, int line_num)
{
    lily_storage *ret = NULL;
    lily_storage *start = emit->current_block->storage_start;
    int expr_num = emit->expr_num;

    if (start) {
        while (start) {
            /* The signature is only null if it belonged to a function that is
               now done. It can be taken, but don't forget to set a proper
               register place for it. */
            if (start->sig == NULL) {
                start->sig = sig;
                start->expr_num = expr_num;
                start->reg_spot = emit->symtab->next_register_spot;
                emit->symtab->next_register_spot++;
                /* Since this stops on the first unused storage it finds, this
                   must be unused_storage_start, which is also the first unused
                   storage. Make sure to bump the unused start ahead to the next
                   one (or NULL, if no next one). */
                emit->unused_storage_start = emit->unused_storage_start->next;
                ret = start;
                break;
            }

            /* The symtab ensures that each signature represents something
               different, so this works. */
            if (start->sig == sig && start->expr_num != expr_num) {
                start->expr_num = expr_num;
                ret = start;
                break;
            }

            start = start->next;
        }
    }

    if (ret == NULL) {
        ret = try_add_storage(emit, sig);

        if (ret != NULL) {
            ret->expr_num = expr_num;
            /* Non-function blocks inherit their storage start from the
               function block that they are in. */
            if (emit->current_block->storage_start == NULL) {
                if (emit->current_block->block_type & BLOCK_FUNCTION)
                    /* Easy mode: Just fill in for the function. */
                    emit->current_block->storage_start = ret;
                else {
                    /* Non-function block, so keep setting storage_start until
                       the function block is reached. This will allow other
                       blocks to use this storage. This is also important
                       because not doing this causes the function block to miss
                       this storage when the function is being finalized. */
                    lily_block *block = emit->current_block;
                    while ((block->block_type & BLOCK_FUNCTION) == 0) {
                        block->storage_start = ret;
                        block = block->prev;
                    }

                    block->storage_start = ret;
                }
            }
        }
        else {
            emit->raiser->line_adjust = line_num;
            lily_raise_nomem(emit->raiser);
        }
    }

    ret->flags &= ~SYM_NOT_ASSIGNABLE;
    return ret;
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

static void emit_rebox_value(lily_emit_state *, lily_sig *, lily_ast *);

/*  build_enum_sig_by_variant
    Given a complete signature of a variant class, determine what the parent
    class is and build a complete signature of that enum class.
    In the event that the variant class does not contain generics that are
    within the enum class, the class 'any' is used instead. */
static lily_sig *build_enum_sig_by_variant(lily_emit_state *emit,
        lily_sig *input_sig)
{
    /* The parent of a variant class is always the enum class it belongs to.
       The 'variant_sig' of an enum class is the signature captured when
       parsing it, and so it contains all the generics needed. */
    lily_sig *parent_variant_sig = input_sig->cls->parent->variant_sig;

    int stack_start = emit->sig_stack_pos + emit->current_generic_adjust + 1;
    if ((stack_start + parent_variant_sig->siglist_size) > emit->sig_stack_size)
        grow_sig_stack(emit);

    int saved_adjust = emit->current_generic_adjust;
    emit->sig_stack_pos += emit->current_generic_adjust;

    /* If the variant takes no values, then the variant sig is simply the
       default signature for the class.
       If it does, then it's a function with the return (at [0]) being the
       variant result. */
    lily_sig *child_result;

    if (input_sig->cls->variant_sig->siglist_size != 0)
        child_result = input_sig->cls->variant_sig->siglist[0];
    else
        child_result = NULL;

    lily_class *any_cls = lily_class_by_id(emit->symtab, SYM_CLASS_ANY);
    lily_sig *any_sig = any_cls->sig;

    /* Sometimes, a variant class does not use all of the generics provided by
       the parent enum class. In that case, the class 'any' will be used. */
    int i, j;
    for (i = 0, j = 0;i < parent_variant_sig->siglist_size;i++) {
        if (child_result &&
            child_result->siglist_size > j &&
            child_result->siglist[j]->template_pos == i) {
            emit->sig_stack[stack_start + i] = input_sig->siglist[j];
            j++;
        }
        else
            emit->sig_stack[stack_start + i] = any_sig;
    }

    lily_sig *new_sig = lily_build_ensure_sig(emit->symtab,
            input_sig->cls->parent, 0, emit->sig_stack, stack_start, i);

    emit->sig_stack_pos -= saved_adjust;
    emit->current_generic_adjust = saved_adjust;

    return new_sig;
}

/*  rebox_variant_to_enum
    This is a convenience function that will convert the variant value within
    the given ast to an enum value.
    Note: If the variant does not supply full type information, then missing
          types are given the type of 'any'. */
static void rebox_variant_to_enum(lily_emit_state *emit, lily_ast *ast)
{
    lily_sig *rebox_sig = build_enum_sig_by_variant(emit,
            ast->result->sig);

    emit_rebox_value(emit, rebox_sig, ast);
}

/*  emit_rebox_value
    Make a storage of type 'new_sig' and assign ast's result to it. The tree's
    result is written over. */
static void emit_rebox_value(lily_emit_state *emit, lily_sig *new_sig,
        lily_ast *ast)
{
    lily_storage *storage = get_storage(emit, new_sig, ast->line_num);

    /* Don't allow a bare variant to be thrown into an any until it's thrown
       into an enum box first. */
    if (new_sig->cls->id == SYM_CLASS_ANY &&
        ast->result->sig->cls->flags & CLS_VARIANT_CLASS) {
        rebox_variant_to_enum(emit, ast);
    }

    write_4(emit, o_assign, ast->line_num, ast->result->reg_spot,
            storage->reg_spot);

    ast->result = (lily_sym *)storage;
}

/*  emit_rebox_to_any
    This is a helper function that calls emit_rebox_value on the given tree
    with a signature of class any. */
static void emit_rebox_to_any(lily_emit_state *emit, lily_ast *ast)
{
    lily_class *any_cls = lily_class_by_id(emit->symtab, SYM_CLASS_ANY);

    emit_rebox_value(emit, any_cls->sig, ast);
}

/*  template_check
    This is called to check if the wanted value (lhs) is the same as the given
    value (rhs) once templates are applied.
    Templates are pretty simple: The first time they're seen, the corresponding
    lhs becomes the type. Subsequent passes need to have the same type.

    Consider: list::append(list[A] self, A newvalue)
              [1].append(10)

              lhs: function(list[integer], integer)
              (correct: both A's are integer). */
static int template_check(lily_emit_state *emit, lily_sig *lhs, lily_sig *rhs)
{
    int ret = 0;

    if (lhs == NULL || rhs == NULL)
        ret = (lhs == rhs);
    else if (lhs->cls->id == rhs->cls->id &&
             lhs->cls->id != SYM_CLASS_TEMPLATE) {
        if (lhs->siglist_size == rhs->siglist_size) {
            ret = 1;

            lily_sig **left_siglist = lhs->siglist;
            lily_sig **right_siglist = rhs->siglist;
            int i;
            /* Simple types have siglist_size as 0, so they'll skip this and
               yield 1. */
            for (i = 0;i < lhs->siglist_size;i++) {
                lily_sig *left_entry = left_siglist[i];
                lily_sig *right_entry = right_siglist[i];
                if (left_entry != right_entry &&
                    template_check(emit, left_entry, right_entry) == 0) {
                    ret = 0;
                    break;
                }
            }
        }
    }
    else if (lhs->cls->id == SYM_CLASS_TEMPLATE) {
        int template_pos = emit->sig_stack_pos + lhs->template_pos;
        ret = 1;
        if (emit->sig_stack[template_pos] == NULL)
            emit->sig_stack[template_pos] = rhs;
        else if (emit->sig_stack[template_pos] != rhs)
            ret = 0;
    }
    else {
        if (lhs->cls->flags & CLS_ENUM_CLASS &&
            rhs->cls->flags & CLS_VARIANT_CLASS &&
            rhs->cls->parent == lhs->cls) {
            /* The right is an enum class that is a member of the left.
               Consider it valid if the right's types (if any) match to all the
               types collected -so far- for the left. */

            ret = 1;

            if (rhs->cls->variant_sig->siglist_size != 0) {
                /* I think this is best explained as an example:
                   'enum class Option[A, B] { Some(A), None }'
                   In this case, the variant sig of Some is defined as:
                   'function (A => Some[A])'
                   This pulls the 'Some[A]'. */
                lily_sig *variant_output = rhs->cls->variant_sig->siglist[0];
                int i;
                /* The result is an Option[A, B], but Some only has A. Match up
                   generics that are available, to proper positions in the
                   parent. If any fail, then stop. */
                for (i = 0;i < variant_output->siglist_size;i++) {
                    int pos = variant_output->siglist[i]->template_pos;
                    ret = template_check(emit, lhs->siglist[pos],
                            rhs->siglist[i]);
                    if (ret == 0)
                        break;
                }
            }
            /* else it takes no arguments and is automatically correct because
               is nothing that could go wrong. */
        }
    }

    return ret;
}

/*  recursively_build_sig
    This is a helper function for build_untemplated_sig. This function takes a
    signature which has template information in it, and builds a signature with
    the template information replaced out.

    Example:
        Input:  list[A]        (where A is known to be 'integer')
        Output: list[integer]

    template_index is the offset where the current function started storing the
    template information that it obtained. */
static lily_sig *recursively_build_sig(lily_emit_state *emit, int template_index,
        lily_sig *sig)
{
    lily_sig *ret = sig;

    if (sig->siglist != NULL) {
        int i, save_start;
        lily_sig **siglist = sig->siglist;
        if (emit->sig_stack_pos + sig->siglist_size > emit->sig_stack_size)
            grow_sig_stack(emit);

        save_start = emit->sig_stack_pos;

        for (i = 0;i < sig->siglist_size;i++) {
            lily_sig *inner_sig = recursively_build_sig(emit, template_index,
                    siglist[i]);
            emit->sig_stack[emit->sig_stack_pos] = inner_sig;
            emit->sig_stack_pos++;
        }

        ret = lily_build_ensure_sig(emit->symtab, sig->cls, sig->flags,
                emit->sig_stack, save_start, i);

        emit->sig_stack_pos -= i;
    }
    else if (sig->cls->id == SYM_CLASS_TEMPLATE) {
        ret = emit->sig_stack[template_index + sig->template_pos];
        /* Sometimes, a generic is wanted that was never filled in. In such a
           case, use 'any' because it is the most accepting of values. */
        if (ret == NULL) {
            lily_class *any_class = lily_class_by_id(emit->symtab,
                    SYM_CLASS_ANY);
            ret = any_class->sig;
        }
    }
    return ret;
}

/*  build_untemplated_sig
    This takes a given sig that has templates inside and returns a sig that has
    those templates replaced. */
static lily_sig *build_untemplated_sig(lily_emit_state *emit, lily_sig *sig)
{
    int save_template_index = emit->sig_stack_pos;

    emit->sig_stack_pos += sig->template_pos;
    lily_sig *ret = recursively_build_sig(emit, save_template_index, sig);
    emit->sig_stack_pos -= sig->template_pos;

    return ret;
}

/*  resolve_second_sig_by_first
    This is called when one signature contains unresolved type information that
    can be resolved by another signature. However, the signature with the
    information is NOT laid out in emitter's sig stack.

    This first lays out the generics into emitter's sig stack before calling
    build_untemplated_sig and yielding the result. */
static lily_sig *resolve_second_sig_by_first(lily_emit_state *emit,
        lily_sig *first, lily_sig *second)
{
    int stack_start = emit->sig_stack_pos + emit->current_generic_adjust + 1;
    int save_ssp = emit->sig_stack_pos;

    if (stack_start + first->siglist_size > emit->sig_stack_size)
        grow_sig_stack(emit);

    int i;
    for (i = 0;i < first->siglist_size;i++)
        emit->sig_stack[stack_start + i] = first->siglist[i];

    emit->sig_stack_pos = stack_start;
    lily_sig *result_sig = build_untemplated_sig(emit, second);
    emit->sig_stack_pos = save_ssp;

    return result_sig;
}

/*  setup_sigs_for_build
    This is called before building a static list or hash.

    expect_sig is checked for being a generic that unwraps to a signature with
    a class id of 'wanted_id', or having that actual id.

    If expect_sig's class is correct, then the signatures inside of it are laid
    down into emitter's sig stack starting at
    'emit->sig_stack_pos + emit->current_generic_adjust + 1' to prevent damaging
    any caller's generics. The signatures within are unwrapped if expect_sig
    wasn't initially unwrapped. (so that generics are not processed twice).

    This processing is done because it's necessary for type inference.

    Returns 1 on success, 0 on failure.

    Notes: This does not adjust the emitter's sig_stack_pos or the generic
           adjust. The caller is expected to either pull the signatures it
           needs. */
static int setup_sigs_for_build(lily_emit_state *emit,
        lily_sig *expect_sig, int wanted_id)
{
    int did_unwrap = 0, ret = 1;

    if (expect_sig && expect_sig->cls->id == SYM_CLASS_TEMPLATE) {
        expect_sig = emit->sig_stack[emit->sig_stack_pos +
                expect_sig->template_pos];
        did_unwrap = 1;
    }

    if (expect_sig && expect_sig->cls->id == wanted_id) {
        int stack_start = emit->sig_stack_pos + emit->current_generic_adjust + 1;
        if (stack_start + expect_sig->siglist_size > emit->sig_stack_size)
            grow_sig_stack(emit);

        int i;
        for (i = 0;i < expect_sig->siglist_size;i++) {
            lily_sig *inner_sig = expect_sig->siglist[i];
            if (did_unwrap == 0 &&
                inner_sig->cls->id == SYM_CLASS_TEMPLATE) {
                inner_sig = emit->sig_stack[emit->sig_stack_pos +
                        inner_sig->template_pos];
            }
            emit->sig_stack[stack_start + i] = inner_sig;
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
            info[from_var->reg_spot].sig = from_var->sig;
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
    while (storage && storage->sig) {
        info[storage->reg_spot].sig = storage->sig;
        info[storage->reg_spot].name = NULL;
        info[storage->reg_spot].line_num = -1;
        storage = storage->next;
    }
}

/*  count_generics
    Return a count of how many unique generic signatures are used in a
    function. */
static int count_generics(lily_sig *function_sig, lily_register_info *info,
        int info_size)
{
    int count = 0, i, j;
    for (i = 0;i < info_size;i++) {
        int match = 0;
        lily_sig *temp = info[i].sig;
        for (j = 0;j < i;j++) {
            if (info[j].sig == temp) {
                match = 1;
                break;
            }
        }

        count += !match;
    }

    /* The return is always used when figuring out what result a generic
       function should have. It needs to be counted too. */
    if (function_sig->siglist[0] &&
        function_sig->siglist[0]->template_pos) {
        count++;
        lily_sig *return_sig = function_sig->siglist[0];
        for (i = 0;i < info_size;i++) {
            if (info[i].sig == return_sig) {
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
    lily_sig *function_sig = var_stop->sig;

    /* Don't include functions inside of themselves... */
    if (emit->function_depth == 1)
        var_stop = var_stop->next;
    /* else we're in __main__, which does include itself as an arg so it can be
       passed to show and other neat stuff. */

    add_var_chain_to_info(emit, info, emit->symtab->var_chain, var_stop);
    add_storage_chain_to_info(info, function_block->storage_start);

    if (function_sig->template_pos)
        f->generic_count = count_generics(function_sig, info, register_count);

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

        /* Blank the signatures of the storages that were used. This lets other
           functions know that the signatures are not in use. */
        storage_iter = function_block->storage_start;
        while (storage_iter) {
            storage_iter->sig = NULL;
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
    if (emit->current_block->class_entry == NULL) {
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
                emit->self_storage->reg_spot);
    }

    lily_var *old_function_cache = emit->symtab->old_function_chain;

    finalize_function_val(emit, block);

    /* Warning: This assumes that only functions can contain other functions. */
    lily_var *v = block->prev->function_var;

    /* If this function has no storages, then it can use the ones from the function
       that just exited. This reuse cuts down on a lot of memory. */
    if (block->prev->storage_start == NULL)
        block->prev->storage_start = emit->unused_storage_start;

    /* If this function was the ::new for a class, move it over into that class. */
    if (emit->current_block->class_entry) {
        lily_class *cls = emit->current_class;
        lily_var *save_next = block->function_var->next;

        /* Function finalize stuck all of the old functions into
           old_function_chain. Put them into the class, where they really go. */
        block->function_var->next = emit->symtab->old_function_chain;
        cls->call_start = block->function_var;
        cls->call_top = block->function_var;

        if (old_function_cache == NULL)
            emit->symtab->old_function_chain = NULL;
        else {
            /* The last entry needs to be unlinked from old_function_chain. */
            lily_var *old_iter = block->function_var;
            while (old_iter->next != old_function_cache)
                old_iter = old_iter->next;

            old_iter->next = NULL;
            emit->symtab->old_function_chain = old_function_cache;
        }

        emit->symtab->var_chain = save_next;
        emit->current_class = block->prev->class_entry;
    }
    else
        emit->symtab->var_chain = block->function_var;

    if (block->prev->generic_count != block->generic_count) {
        lily_update_symtab_generics(emit->symtab, NULL, block->prev->generic_count);
        if (block->prev->generic_count == 0)
            emit->current_generic_adjust = 0;
        else
            emit->current_generic_adjust = block->prev->generic_count;
    }

    emit->self_storage = emit->current_block->prev->self;
    emit->symtab->next_register_spot = block->save_register_spot;
    emit->top_function = v->value.function;
    emit->top_var = v;
    emit->top_function_ret = v->sig->siglist[0];

    emit->symtab->function_depth--;
    emit->function_depth--;
}

/*  eval_enforce_value
    Evaluate a given ast and make sure it returns a value. */
static void eval_enforce_value(lily_emit_state *emit, lily_ast *ast,
        char *message)
{
    eval_tree(emit, ast, NULL);
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
    lily_block *block = emit->current_block;
    int error = 0;
    lily_msgbuf *msgbuf = emit->raiser->msgbuf;
    int i;
    lily_class *match_class = block->match_input_sig->cls;

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
        call_name = ast->arg_start->variant_class->name;
        kind = "Variant";
    }
    else if (ast->arg_start->tree_type == tree_oo_access) {
        class_name = ast->arg_start->result->sig->cls->name;
        call_name = emit->oo_name_pool->str + ast->arg_start->oo_pool_index;
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

    lily_msgbuf_add_fmt(msgbuf, "%s %s%s%s ", kind, class_name, separator,
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
                          lily_sig *left_sig, lily_sig *right_sig)
{
    /* Remember that right is being assigned to left, so right should
       get printed first. */
    lily_raise_adjusted(emit->raiser, line_num, lily_SyntaxError,
            "Cannot assign type '%T' to type '%T'.\n",
            right_sig, left_sig);
}

static void bad_arg_error(lily_emit_state *emit, lily_ast *ast,
    lily_sig *got, lily_sig *expected, int arg_num)
{
    push_info_to_error(emit, ast);
    lily_msgbuf *msgbuf = emit->raiser->msgbuf;

    emit->raiser->line_adjust = ast->line_num;

    lily_msgbuf_add_fmt(msgbuf,
            "arg #%d expects type '^T' but got type '^T'.\n", arg_num,
            expected, got);
    lily_raise_prebuilt(emit->raiser, lily_SyntaxError);
}

/* bad_num_args
   Reports that the ast didn't get as many args as it should have. Takes
   anonymous calls and var args into account. */
static void bad_num_args(lily_emit_state *emit, lily_ast *ast,
        lily_sig *call_sig)
{
    push_info_to_error(emit, ast);
    lily_msgbuf *msgbuf = emit->raiser->msgbuf;

    char *va_text;

    if (call_sig->flags & SIG_IS_VARARGS)
        va_text = "at least ";
    else
        va_text = "";

    emit->raiser->line_adjust = ast->line_num;

    lily_msgbuf_add_fmt(msgbuf, "expects %s%d args, but got %d.\n",
            va_text, call_sig->siglist_size - 1, ast->args_collected);

    lily_raise_prebuilt(emit->raiser, lily_SyntaxError);
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

    lhs_class = ast->left->result->sig->cls;
    rhs_class = ast->right->result->sig->cls;

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
        if (ast->left->result->sig == ast->right->result->sig ||
            type_matchup(emit, ast->left->result->sig, ast->right) ||
            type_matchup(emit, ast->right->result->sig, ast->left)) {
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
                   "Invalid operation: %T %s %T.\n", ast->left->result->sig,
                   opname(ast->op), ast->right->result->sig);

    if (ast->op == expr_plus || ast->op == expr_minus ||
        ast->op == expr_multiply || ast->op == expr_divide)
        if (lhs_class->id >= rhs_class->id)
            storage_class = lhs_class;
        else
            storage_class = rhs_class;
    else
        /* Assign is handled elsewhere, so these are just comparison ops. These
           always return 0 or 1, regardless of the classes put in. There's no
           bool class (yet), so an integer class is used instead. */
        storage_class = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

    s = get_storage(emit, storage_class->sig, ast->line_num);
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
        /* Assigning to a global is done differently than with a local, so it
           can't be optimized. */
        if (ast->left->tree_type == tree_var) {
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
        if (ast->left->result->sig->cls->id == SYM_CLASS_ANY &&
            right_tree->result->sig->cls->id != SYM_CLASS_ANY) {
            can_optimize = 0;
            break;
        }
    } while (0);

    return can_optimize;
}

/*  calculate_var_sig
    This is called when the left side of an assignment doesn't have a signature
    because it was declared using 'var ...'. This will return the proper
    signature for the left side of the expression. */
static lily_sig *calculate_var_sig(lily_emit_state *emit, lily_sig *input_sig)
{
    lily_sig *result;
    if (input_sig->cls->flags & CLS_VARIANT_CLASS)
        result = build_enum_sig_by_variant(emit, input_sig);
    else
        result = input_sig;

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

    if (ast->left->tree_type != tree_var &&
        ast->left->tree_type != tree_local_var) {
        eval_tree(emit, ast->left, NULL);
        if (ast->left->result->flags & SYM_NOT_ASSIGNABLE)
            lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                    "Left side of %s is not assignable.\n", opname(ast->op));
    }

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, ast->left->result->sig);

    /* For 'var <name> = ...', fix the type. */
    if (ast->left->result->sig == NULL)
        ast->left->result->sig = calculate_var_sig(emit,
                ast->right->result->sig);

    ast->left->result->flags &= ~SYM_NOT_INITIALIZED;

    left_sym = ast->left->result;
    right_sym = ast->right->result;
    left_cls_id = left_sym->sig->cls->id;

    if (left_sym->sig != right_sym->sig) {
        if (left_sym->sig->cls->id == SYM_CLASS_ANY) {
            /* Bare variants are not allowed, and type_matchup is a bad idea
               here. Rebox the variant into an enum, then let assign do the
               rest of the magic.
               The reason that type_matchup is a bad idea is that it will box
               the variant into an enum, then an any, which will be the target
               of the assign. This results in a junk storage. */
            if (right_sym->sig->cls->flags & CLS_VARIANT_CLASS) {
                rebox_variant_to_enum(emit, ast->right);
                right_sym = ast->right->result;
            }

            opcode = o_assign;
        }
        else {
            if (type_matchup(emit, ast->left->result->sig, ast->right) == 0) {
                bad_assign_error(emit, ast->line_num, left_sym->sig,
                                 right_sym->sig);
            }
            else {
                /* type_matchup may update the result, so update the cache. */
                right_sym = ast->right->result;
            }
        }
    }

    if (opcode == -1) {
        if (left_cls_id == SYM_CLASS_INTEGER ||
            left_cls_id == SYM_CLASS_DOUBLE)
            opcode = o_fast_assign;
        else
            opcode = o_assign;
    }

    if (ast->op > expr_assign) {
        if (ast->left->tree_type == tree_var)
            eval_tree(emit, ast->left, NULL);

        emit_op_for_compound(emit, ast);
        right_sym = ast->result;
    }

    if (ast->left->tree_type == tree_var)
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
    lily_sig *left_sig;
    lily_sym *rhs;

    if (ast->left->tree_type != tree_property) {
        eval_tree(emit, ast->left, NULL);

        /* Make sure that it was a property access, and not a class member
           access. The latter is not reassignable. */
        if (ast->left->result->flags & SYM_TYPE_VAR)
            lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                    "Left side of %s is not assignable.\n", opname(ast->op));

        left_sig = ast->left->result->sig;
    }
    else
        /* Don't bother evaluating the left, because the property's id and sig
           are already available. Evaluating it would just dump the contents
           into a var, which isn't useful. */
        left_sig = ast->left->property->sig;

    if (ast->right->tree_type != tree_local_var)
        /* Important! Expecting the lhs will auto-fix the rhs if needed. */
        eval_tree(emit, ast->right, left_sig);

    rhs = ast->right->result;
    lily_sig *right_sig = ast->right->result->sig;
    /* For 'var @<name> = ...', fix the type of the property. */
    if (left_sig == NULL) {
        ast->left->property->sig = right_sig;
        ast->left->property->flags &= ~SYM_NOT_INITIALIZED;
        left_sig = right_sig;
    }

    if (left_sig != right_sig && left_sig->cls->id != SYM_CLASS_ANY) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, left_sig,
                         right_sig);
    }

    lily_literal *lit;

    if (ast->left->tree_type == tree_oo_access)
        lit = lily_get_integer_literal(emit->symtab,
            ast->left->oo_property_index);
    else
        lit = lily_get_integer_literal(emit->symtab,
            ast->left->property->id);

    lily_storage *lit_result = get_storage(emit, lit->sig,
            ast->line_num);

    if (ast->op > expr_assign) {
        if (ast->left->tree_type == tree_property)
            eval_tree(emit, ast->left, NULL);

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
                emit->self_storage->reg_spot,
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
            ast->arg_start->next_arg->result->sig, ast->line_num);
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
    lily_sig *wanted_sig = package_right->result->sig;
    lily_sym *rhs;

    if (ast->right->tree_type != tree_var)
        eval_tree(emit, ast->right, wanted_sig);

    /* Don't evaluate the package tree. Like subscript assign, this has to
       write directly to the var at the given part of the package. Since parser
       passes the var to be assigned, just grab that from result for checking
       the sig. No need to do a symtab lookup of a name. */

    rhs = ast->right->result;

    /* Before doing an eval, make sure that the two types actually match up. */
    if (wanted_sig != rhs_tree->result->sig &&
        type_matchup(emit, wanted_sig, rhs_tree) == 0) {
        bad_assign_error(emit, ast->line_num, wanted_sig,
                rhs_tree->result->sig);
    }

    if (ast->op > expr_assign) {
        eval_tree(emit, ast->left, NULL);
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
        lily_literal *success_lit, *failure_lit;
        lily_symtab *symtab = emit->symtab;
        lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

        result = get_storage(emit, cls->sig, ast->line_num);

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
    lily_sig *elem_sig;

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right, NULL);

    rhs = ast->right->result;

    if (var_ast->tree_type != tree_local_var &&
        var_ast->tree_type != tree_var) {
        eval_tree(emit, var_ast, NULL);
        if (var_ast->tree_type != tree_var) {
            if (var_ast->result->flags & SYM_NOT_ASSIGNABLE)
                lily_raise_adjusted(emit->raiser, ast->line_num,
                        lily_SyntaxError,
                        "Left side of %s is not assignable.\n", opname(ast->op));
        }
    }

    lily_literal *tuple_literal = NULL;

    if (index_ast->tree_type != tree_local_var) {
        if (index_ast->tree_type == tree_readonly &&
            var_ast->result->sig->cls->id == SYM_CLASS_TUPLE) {
            /* Save the literal before evaluating the tree wipes it out. */
            tuple_literal = (lily_literal *)index_ast->result;
        }
        eval_tree(emit, index_ast, NULL);
    }

    check_valid_subscript(emit, var_ast, index_ast, tuple_literal);

    elem_sig = get_subscript_result(var_ast->result->sig, index_ast,
            tuple_literal);

    if (elem_sig != rhs->sig && elem_sig->cls->id != SYM_CLASS_ANY) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, elem_sig,
                         rhs->sig);
    }

    if (ast->op > expr_assign) {
        /* For a compound assignment to work, the left side must be subscripted
           to get the value held. */

        lily_storage *subs_storage = get_storage(emit, elem_sig, ast->line_num);

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
    Signature: ast->arg_start->next_arg->sig */
static void eval_typecast(lily_emit_state *emit, lily_ast *ast)
{
    lily_sig *cast_sig = ast->arg_start->next_arg->sig;
    lily_ast *right_tree = ast->arg_start;
    if (right_tree->tree_type != tree_local_var)
        eval_tree(emit, right_tree, NULL);

    lily_sig *var_sig = right_tree->result->sig;

    if (cast_sig == var_sig)
        ast->result = (lily_sym *)right_tree->result;
    else if (cast_sig->cls->id == SYM_CLASS_ANY) {
        /* This function automatically fixes right_tree's result to the
           new any value. */
        emit_rebox_to_any(emit, right_tree);
        ast->result = right_tree->result;
    }
    else if (var_sig->cls->id == SYM_CLASS_ANY) {
        lily_storage *result = get_storage(emit, cast_sig, ast->line_num);

        write_4(emit, o_any_typecast, ast->line_num,
                right_tree->result->reg_spot, result->reg_spot);
        ast->result = (lily_sym *)result;
    }
    else {
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_BadTypecastError,
                "Cannot cast type '%T' to type '%T'.\n", var_sig, cast_sig);
    }
}

/*  eval_unary_op
    This handles unary ops. Unary ops currently only work on integers. */
static void eval_unary_op(lily_emit_state *emit, lily_ast *ast)
{
    uint16_t opcode;
    lily_class *lhs_class;
    lily_storage *storage;
    lhs_class = ast->left->result->sig->cls;

    if (lhs_class->id != SYM_CLASS_INTEGER)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Invalid operation: %s%s.\n",
                opname(ast->op), lhs_class->name);

    lily_class *integer_cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

    storage = get_storage(emit, integer_cls->sig, ast->line_num);
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
    In the event that there is not a common signature, the function attempts
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
        lily_sig *expect_sig, int is_hash)
{
    lily_ast *tree_iter = ast->arg_start;
    int enum_count = 0, variant_count = 0;
    lily_sig *rebox_sig = NULL;
    lily_class *any_class = lily_class_by_id(emit->symtab, SYM_CLASS_ANY);

    /* If ast is tree_hash (is_hash == 1), then the values are key, value, key
       value, and so on. This is about the values, not the keys. */
    if (is_hash)
        tree_iter = tree_iter->next_arg;

    while (tree_iter != NULL) {
        if (tree_iter->result->sig->cls->flags & CLS_ENUM_CLASS)
            enum_count++;
        else if (tree_iter->result->sig->cls->flags & CLS_VARIANT_CLASS)
            variant_count++;
        else {
            rebox_sig = any_class->sig;
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

    if (rebox_sig == NULL) {
        variant_parent = tree_iter->result->sig->cls;
        if (variant_parent->flags & CLS_VARIANT_CLASS)
            variant_parent = variant_parent->parent;

        int generic_count = variant_parent->variant_sig->template_pos;

        if (emit->sig_stack_pos + emit->current_generic_adjust + generic_count >=
            emit->sig_stack_size)
            grow_sig_stack(emit);

        int stack_start = emit->sig_stack_pos +
                emit->current_generic_adjust + 1;
        int i;

        for (i = 0;i < generic_count;i++)
            emit->sig_stack[stack_start + i] = NULL;

        while (tree_iter != NULL) {
            lily_sig *tree_result_sig = tree_iter->result->sig;
            lily_sig *matcher_sig;

            if (tree_result_sig->cls->flags & CLS_ENUM_CLASS) {
                if (tree_result_sig->cls != variant_parent)
                    rebox_sig = any_class->sig;
            }
            else if (tree_result_sig->cls->flags & CLS_VARIANT_CLASS) {
                if (tree_result_sig->cls->parent != variant_parent)
                    rebox_sig = any_class->sig;
            }
            else
                rebox_sig = any_class->sig;

            if (rebox_sig != NULL)
                break;

            matcher_sig = tree_result_sig->cls->variant_sig;
            /* If the variant takes arguments, then the variant_sig it has is a
               function returning a sig of the class at [0]. Otherwise, it's
               just a sig of the class. */
            if (matcher_sig->siglist_size != 0)
                matcher_sig = matcher_sig->siglist[0];

            /* Make sure that there are no disagreements about what type(s) the
               generics (if any) are for the resulting enum class value. */
            for (i = 0;i < matcher_sig->siglist_size;i++) {
                int pos = stack_start + matcher_sig->siglist[i]->template_pos;
                if (emit->sig_stack[pos] == NULL)
                    emit->sig_stack[pos] = tree_result_sig->siglist[i];
                else if (emit->sig_stack[pos] != tree_result_sig->siglist[i]) {
                    rebox_sig = any_class->sig;
                    break;
                }
            }

            if (rebox_sig != NULL)
                break;

            tree_iter = tree_iter->next_arg;
            if (is_hash && tree_iter)
                tree_iter = tree_iter->next_arg;
        }

        if (rebox_sig == NULL) {
            /* It may be that the enum class specifies generics that were not
               satisfied by any variant member. In such a case, default to
               class 'any'.
               Example: enum class Option[A] { Some(A), None }

               [None, None, None].

               FIRST try to see if the signature wanted can give whatever info
               may be missing. If it can't, then default to any.

               Otherwise, 'list[Option[integer]] k = [None, None]' fails. */
            if (expect_sig && expect_sig->cls == variant_parent) {
                for (i = 0;i < generic_count;i++) {
                    if (emit->sig_stack[stack_start + i] == NULL)
                        emit->sig_stack[stack_start + i] = expect_sig->siglist[i];
                }
            }
            else {
                for (i = 0;i < generic_count;i++) {
                    if (emit->sig_stack[stack_start + i] == NULL)
                        emit->sig_stack[stack_start + i] = any_class->sig;
                }
            }

            rebox_sig = lily_build_ensure_sig(emit->symtab, variant_parent, 0,
                    emit->sig_stack, stack_start, generic_count);
        }
    }

    tree_iter = ast->arg_start;
    if (is_hash)
        tree_iter = tree_iter->next_arg;

    while (tree_iter) {
        if (tree_iter->result->sig != rebox_sig)
            emit_rebox_value(emit, rebox_sig, tree_iter);

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
        lily_sig *expect_sig)
{
    lily_ast *tree_iter;

    lily_sig *last_key_sig = NULL, *last_value_sig = NULL,
             *expect_key_sig = NULL, *expect_value_sig = NULL;
    int make_anys = 0, found_variant_or_enum = 0;

    if (expect_sig) {
        int ok = setup_sigs_for_build(emit, expect_sig, SYM_CLASS_HASH);
        int setup_start = emit->sig_stack_pos + emit->current_generic_adjust + 1;

        if (ok) {
            expect_key_sig = emit->sig_stack[setup_start];
            expect_value_sig = emit->sig_stack[setup_start + 1];
        }
    }

    for (tree_iter = ast->arg_start;
         tree_iter != NULL;
         tree_iter = tree_iter->next_arg->next_arg) {

        lily_ast *key_tree, *value_tree;
        key_tree = tree_iter;
        value_tree = tree_iter->next_arg;

        if (key_tree->tree_type != tree_local_var)
            eval_tree(emit, key_tree, expect_key_sig);

        /* Keys -must- all be the same type. They cannot be converted to any
           later on because any are not valid keys (not immutable). */
        if (key_tree->result->sig != last_key_sig) {
            if (last_key_sig == NULL) {
                if ((key_tree->result->sig->cls->flags & CLS_VALID_HASH_KEY) == 0) {
                    lily_raise_adjusted(emit->raiser, key_tree->line_num,
                            lily_SyntaxError,
                            "Resulting type '%T' is not a valid hash key.\n",
                            key_tree->result->sig);
                }

                last_key_sig = key_tree->result->sig;
            }
            else {
                lily_raise_adjusted(emit->raiser, key_tree->line_num,
                        lily_SyntaxError,
                        "Expected a key of type '%T', but key is of type '%T'.\n",
                        last_key_sig, key_tree->result->sig);
            }
        }

        if (value_tree->tree_type != tree_local_var)
            eval_tree(emit, value_tree, expect_value_sig);

        /* Only mark user-defined enum classes/variants, because those are the
           ones that can default. */
        if (value_tree->result->sig->cls->flags &
            (CLS_VARIANT_CLASS | CLS_ENUM_CLASS) &&
            value_tree->result->sig->cls->id != SYM_CLASS_ANY)
            found_variant_or_enum = 1;

        /* Values being promoted to any is okay though. :) */
        if (value_tree->result->sig != last_value_sig) {
            if (last_value_sig == NULL)
                last_value_sig = value_tree->result->sig;
            else
                make_anys = 1;
        }
    }

    if (ast->args_collected == 0) {
        last_key_sig = expect_key_sig;
        last_value_sig = expect_value_sig;
    }
    else {
        if (found_variant_or_enum)
            rebox_enum_variant_values(emit, ast, expect_value_sig, 1);
        else if (make_anys ||
                 (expect_value_sig &&
                  expect_value_sig->cls->id == SYM_CLASS_ANY))
            emit_hash_values_to_anys(emit, ast);

        last_value_sig = ast->arg_start->next_arg->result->sig;
    }

    int stack_start = emit->sig_stack_pos + emit->current_generic_adjust + 1;
    if ((stack_start + 2) > emit->sig_stack_size)
        grow_sig_stack(emit);

    lily_class *hash_cls = lily_class_by_id(emit->symtab, SYM_CLASS_HASH);
    emit->sig_stack[stack_start] = last_key_sig;
    emit->sig_stack[stack_start+1] = last_value_sig;
    lily_sig *new_sig = lily_build_ensure_sig(emit->symtab, hash_cls, 0,
                emit->sig_stack, stack_start, 2);

    lily_storage *s = get_storage(emit, new_sig, ast->line_num);

    write_build_op(emit, o_build_hash, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

/*  check_proper_variant
    Make sure that the variant has the proper inner type to satisfy the
    signature wanted by the enum. */
static int check_proper_variant(lily_emit_state *emit, lily_sig *enum_sig,
        lily_sig *given_sig, lily_class *variant_cls)
{
    lily_sig *variant_sig = variant_cls->variant_sig;
    int i, result = 1;

    if (variant_sig->siglist_size != 0) {
        lily_sig *variant_result = variant_sig->siglist[0];
        for (i = 0;i < variant_result->siglist_size;i++) {
            /* The variant may not have all the generics that the parent does.
               Consider the variant to be proper if the generics that it has
               match up to the enum sig.
               Ex: For SomeVariant[B] and SomeEnum[A, B], consider it right if
                   the B's match. */
            int pos = variant_result->siglist[i]->template_pos;
            if (given_sig->siglist[i] != enum_sig->siglist[pos]) {
                result = 0;
                break;
            }
        }
    }
    /* else the variant takes no generics, and nothing can be wrong. */

    return result;
}

/*  enum_membership_check
    Given a signature which is for some enum class, determine if 'right'
    is a member of the enum class.

    Returns 1 if yes, 0 if no. */
static int enum_membership_check(lily_emit_state *emit, lily_sig *enum_sig,
        lily_sig *right)
{
    /* First, make sure that right is a member... */
    lily_class *variant_class = right->cls;
    lily_class *enum_class = enum_sig->cls;
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
        if (right->siglist_size != 0)
            ok = check_proper_variant(emit, enum_sig, right, variant_class);
    }

    return ok;
}

/*  type_matchup
    This is called when 'right' doesn't have quite the right signature.
    If the wanted signature is 'any', the value of 'right' is made into an any.

    On success: right is fixed, 1 is returned.
    On failure: right isn't fixed, 0 is returned. */
static int type_matchup(lily_emit_state *emit, lily_sig *want_sig,
        lily_ast *right)
{
    int ret = 1;
    if (want_sig->cls->id == SYM_CLASS_ANY)
        emit_rebox_to_any(emit, right);
    else if (want_sig->cls->flags & CLS_ENUM_CLASS) {
        ret = enum_membership_check(emit, want_sig, right->result->sig);
        if (ret)
            emit_rebox_value(emit, want_sig, right);
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
        lily_sig *expect_sig)
{
    lily_sig *elem_sig = NULL;
    lily_ast *arg;
    int found_variant_or_enum = 0, make_anys = 0;

    if (expect_sig) {
        if (ast->args_collected == 0) {
            lily_sig *check_sig;

            if (expect_sig->cls->id == SYM_CLASS_TEMPLATE)
                check_sig = emit->sig_stack[emit->sig_stack_pos +
                        expect_sig->template_pos];
            else
                check_sig = expect_sig;

            if (check_sig && check_sig->cls->id == SYM_CLASS_HASH) {
                eval_build_hash(emit, ast, expect_sig);
                return;
            }
        }

        int ok = setup_sigs_for_build(emit, expect_sig, SYM_CLASS_LIST);
        if (ok) {
            elem_sig = emit->sig_stack[
                    emit->sig_stack_pos + emit->current_generic_adjust + 1];
            expect_sig = elem_sig;
        }
    }

    lily_sig *last_sig = NULL;

    for (arg = ast->arg_start;arg != NULL;arg = arg->next_arg) {
        if (arg->tree_type != tree_local_var)
            eval_tree(emit, arg, elem_sig);

        /* 'any' is marked as an enum class, but this is only interested in
           user-defined enum classes (which have special defaulting). */
        if ((arg->result->sig->cls->flags & (CLS_ENUM_CLASS | CLS_VARIANT_CLASS)) &&
            arg->result->sig->cls->id != SYM_CLASS_ANY)
            found_variant_or_enum = 1;

        if (arg->result->sig != last_sig) {
            if (last_sig == NULL)
                last_sig = arg->result->sig;
            else
                make_anys = 1;
        }
    }

    if (elem_sig == NULL && last_sig == NULL) {
        /* This happens when there's an empty list and a list is probably not
           expected. Default to list[any] and hope that's right. */
        lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_ANY);
        elem_sig = cls->sig;
    }
    else if (last_sig != NULL) {
        if (found_variant_or_enum)
            rebox_enum_variant_values(emit, ast, expect_sig, 0);
        else if (make_anys ||
                 (elem_sig && elem_sig->cls->id == SYM_CLASS_ANY))
            emit_list_values_to_anys(emit, ast);
        /* else all types already match, so nothing to do. */

        /* At this point, all list values are guaranteed to have the same
           type, so this works. */
        elem_sig = ast->arg_start->result->sig;
    }

    int start_pos = emit->sig_stack_pos + emit->current_generic_adjust + 1;

    if (start_pos > emit->sig_stack_size)
        grow_sig_stack(emit);

    lily_class *list_cls = lily_class_by_id(emit->symtab, SYM_CLASS_LIST);
    emit->sig_stack[start_pos] = elem_sig;
    lily_sig *new_sig = lily_build_ensure_sig(emit->symtab, list_cls, 0,
                emit->sig_stack, start_pos, 1);

    lily_storage *s = get_storage(emit, new_sig, ast->line_num);

    write_build_op(emit, o_build_list_tuple, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

/*  eval_build_tuple
    This handles creation of a tuple from a series of values. The resulting
    tuple will have a signature that matches what it obtained.

    <[1, "2", 3.3]> # tuple[integer, string, double] 

    This attempts to do the same sort of defaulting that eval_build_list and
    eval_build_hash do:

    tuple[any] t = <[1]> # Becomes tuple[any]. */
static void eval_build_tuple(lily_emit_state *emit, lily_ast *ast,
        lily_sig *expect_sig)
{
    if (ast->args_collected == 0) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "Cannot create an empty tuple.\n");
    }

    /* It is not possible to use setup_sigs_for_build here, because tuple takes
       N signatures and subtrees may damage those signatures. Those signatures
       also cannot be hidden, because the expected signature may contain
       generics that the callee may attempt to check the resolution of.
       Just...don't unwrap things more than once here. */

    int did_unwrap = 0;
    if (expect_sig && expect_sig->cls->id == SYM_CLASS_TEMPLATE) {
        expect_sig = emit->sig_stack[emit->sig_stack_pos +
                expect_sig->template_pos];
        did_unwrap = 1;
    }

    if (expect_sig &&
         (expect_sig->cls->id != SYM_CLASS_TUPLE ||
          expect_sig->siglist_size != ast->args_collected))
        expect_sig = NULL;

    int i;
    lily_ast *arg;

    for (i = 0, arg = ast->arg_start;
         arg != NULL;
         i++, arg = arg->next_arg) {
        lily_sig *elem_sig = NULL;

        /* It's important to do this for each pass because it allows the inner
           trees to infer types that this tree's parent may want. */
        if (expect_sig) {
            elem_sig = expect_sig->siglist[i];
            if (did_unwrap == 0 && elem_sig &&
                elem_sig->cls->id == SYM_CLASS_TEMPLATE) {
                elem_sig = emit->sig_stack[emit->sig_stack_pos +
                        elem_sig->template_pos];
            }
        }

        if (arg->tree_type != tree_local_var)
            eval_tree(emit, arg, elem_sig);

        if ((elem_sig && elem_sig != arg->result->sig) ||
            (arg->result->sig->cls->flags & CLS_VARIANT_CLASS)) {
            if (elem_sig && elem_sig != arg->result->sig)
                /* Attempt to fix the type to what's wanted. If it fails, the
                   caller will likely note a type mismatch. Can't do anything
                   else though. */
                type_matchup(emit, elem_sig, arg);
            else {
                /* Not sure what the caller wants, so make an enum sig based
                   of what's known and use that. */
                lily_sig *enum_sig = build_enum_sig_by_variant(emit,
                        arg->result->sig);

                emit_rebox_value(emit, enum_sig, arg);
            }
        }
    }

    int stack_start = emit->sig_stack_pos + emit->current_generic_adjust + 1;
    if ((stack_start + ast->args_collected) > emit->sig_stack_size)
        grow_sig_stack(emit);

    for (i = 0, arg = ast->arg_start;
         i < ast->args_collected;
         i++, arg = arg->next_arg) {
        emit->sig_stack[stack_start + i] = arg->result->sig;
    }

    lily_class *tuple_cls = lily_class_by_id(emit->symtab, SYM_CLASS_TUPLE);
    lily_sig *new_sig = lily_build_ensure_sig(emit->symtab, tuple_cls, 0,
                emit->sig_stack, stack_start, i);
    lily_storage *s = get_storage(emit, new_sig, ast->line_num);

    write_build_op(emit, o_build_list_tuple, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

/*  eval_subscript
    Evaluate a subscript, returning the resulting value. This handles
    subscripts of list, hash, and tuple. */
static void eval_subscript(lily_emit_state *emit, lily_ast *ast,
        lily_sig *expect_sig)
{
    lily_ast *var_ast = ast->arg_start;
    lily_ast *index_ast = var_ast->next_arg;
    if (var_ast->tree_type != tree_local_var)
        eval_tree(emit, var_ast, NULL);

    lily_literal *tuple_literal = NULL;

    if (index_ast->tree_type != tree_local_var) {
        if (index_ast->tree_type == tree_readonly &&
            var_ast->result->sig->cls->id == SYM_CLASS_TUPLE) {
            /* Save the literal before evaluating the tree wipes it out. This
               will be used later to determine what the result of the subscript
               should actually be. */
            tuple_literal = (lily_literal *)index_ast->result;
        }
        eval_tree(emit, index_ast, NULL);
    }

    check_valid_subscript(emit, var_ast, index_ast, tuple_literal);

    lily_sig *sig_for_result;
    sig_for_result = get_subscript_result(var_ast->result->sig, index_ast,
            tuple_literal);

    lily_storage *result = get_storage(emit, sig_for_result, ast->line_num);

    write_5(emit,
            o_get_item,
            ast->line_num,
            var_ast->result->reg_spot,
            index_ast->result->reg_spot,
            result->reg_spot);

    ast->result = (lily_sym *)result;
}

/*  eval_call_arg
    Evaluate the argument of a function call and do some type matching up on
    the result. This is different than type_matchup, because it's a fair chance
    that the arguments may hold information about generics. */
static void eval_call_arg(lily_emit_state *emit, lily_ast *call_ast,
        int template_adjust, lily_sig *want_sig, lily_ast *arg, int arg_num)
{
    if (arg->tree_type != tree_local_var)
        eval_tree(emit, arg, want_sig);

    /* It may seem tempting to do sig == sig in here. Don't.
       If one generic function calls another, then the caller needs to know
       that the generic types of the callee match to the generic types of the
       caller. */
    int ok = 0;

    /* This is used so that type_matchup gets the resolved signature (if there
       is one) because the resolved signature might be 'any'. */
    lily_sig *matchup_sig = want_sig;

    /* A simple generic is easy to resolve. */
    if (want_sig->cls->id == SYM_CLASS_TEMPLATE) {
        int index = emit->sig_stack_pos + want_sig->template_pos;
        if (emit->sig_stack[index] != NULL) {
            matchup_sig = emit->sig_stack[index];
            if (matchup_sig == arg->result->sig)
                ok = 1;
        }
        else if (arg->result->sig->cls->flags & CLS_VARIANT_CLASS) {
            /* If a bare variant wants to resolve a generic, it first has to go
               into the proper enum type. */
            lily_sig *enum_sig = build_enum_sig_by_variant(emit,
                    arg->result->sig);
            emit_rebox_value(emit, enum_sig, arg);
        }
    }

    /* ok == 0 protects from potentially attempting to resolve the same generic
       twice, which breaks things. */
    if (ok == 0 &&
        (template_check(emit, want_sig, arg->result->sig) ||
         type_matchup(emit, matchup_sig, arg)))
        ok = 1;

    if (ok == 0)
        bad_arg_error(emit, call_ast, arg->result->sig, want_sig,
                      arg_num);
}

/*  box_call_variants
    This function is called when check_call_args is done processing arguments
    AND the call has been tagged by the symtab as having enum values.

    This function exists because it's possible for a Lily function to not know
    what the resulting enum class should be. In such a case, call argument
    processing calls this to make sure any variants are put into a proper enum
    class value. */
static void box_call_variants(lily_emit_state *emit, lily_sig *call_sig,
        int num_args, lily_ast *arg)
{
    int i;
    for (i = 0;
         i != num_args;
         i++, arg = arg->next_arg) {
        if (arg->result->sig->cls->flags & CLS_VARIANT_CLASS) {
            lily_sig *arg_sig = call_sig->siglist[i + 1];
            lily_sig *enum_sig = build_untemplated_sig(emit, arg_sig);
            emit_rebox_value(emit, enum_sig, arg);
        }
    }

    if (call_sig->flags & SIG_IS_VARARGS) {
        /* This is called before the varargs are shoved into a list, so looping
           over the args is fine.
           Varargs is represented as a list of some type, so this next line grabs
           the list, then what the list holds. */
        lily_sig *va_comp_sig = call_sig->siglist[i + 1]->siglist[0];
        if (va_comp_sig->cls->flags & CLS_ENUM_CLASS) {
            lily_sig *enum_sig = build_untemplated_sig(emit, va_comp_sig);
            for (;arg != NULL;
                  i++, arg = arg->next_arg) {
                if (arg->result->sig->cls->flags & CLS_VARIANT_CLASS)
                    emit_rebox_value(emit, enum_sig, arg);
            }
        }
    }
}

/*  check_call_args
    eval_call uses this to make sure the types of all the arguments are right.

    If the function takes varargs, the extra arguments are packed into a list
    of the vararg type. */
static void check_call_args(lily_emit_state *emit, lily_ast *ast,
        lily_sig *call_sig, lily_sig *expect_sig)
{
    lily_ast *arg = ast->arg_start;
    lily_ast *true_start = arg;
    int have_args, i, is_varargs, num_args;
    int auto_resolve = 0;

    /* oo_access is the only tree where the first argument is actually supposed
       to be passed in as a value. */
    if (arg->tree_type != tree_oo_access) {
        if (arg->tree_type == tree_local_var)
            auto_resolve = 1;
        arg = arg->next_arg;
        true_start = arg;
        ast->args_collected--;
    }

    /* Ast doesn't check the call args. It can't check types, so why do only
       half of the validation? */
    have_args = ast->args_collected;
    is_varargs = call_sig->flags & SIG_IS_VARARGS;
    /* Take the last arg off of the arg count. This will be verified using the
       var arg signature. */
    num_args = (call_sig->siglist_size - 1) - is_varargs;

    /* Templates are rather simple: The first time they're seen, the type they
       see is written into emitter's sig stack. Subsequent passes check that
       the type seen is the same one (so multiple uses of A have the same
       type). */
    int template_adjust = call_sig->template_pos;

    emit->current_generic_adjust = template_adjust;

    if (template_adjust) {
        if (emit->sig_stack_pos + template_adjust >= emit->sig_stack_size)
            grow_sig_stack(emit);

        if (auto_resolve == 0) {
            for (i = 0;i < template_adjust;i++)
                emit->sig_stack[emit->sig_stack_pos + i] = NULL;

            /* If the parent of this tree wants the same type that this one
               returns, then attempt to fill in generics based off of what
               the parent wants. This is to dodge defaulting to any where
               it is possible. */
            if (expect_sig != NULL && call_sig->siglist[0] != NULL &&
                expect_sig->cls->id == call_sig->siglist[0]->cls->id) {

                /* The return isn't checked because there will be a more
                   accurate problem that is likely to manifest later. */
                template_check(emit, call_sig->siglist[0], expect_sig);
            }
        }
        else {
            /* If the callee is within a generic function, then consider the
               callee's generics to be resolved as themselves.
               This is to prevent this:
                   function f[A, B](function g(A), B value) { g(value) }
               Such a thing is bad, because f may be called wherein A and B are
               not compatible. */
            lily_sig *sig_iter = emit->symtab->template_sig_start;
            for (i = 0;i < template_adjust;i++, sig_iter = sig_iter->next)
                emit->sig_stack[emit->sig_stack_pos + i] = sig_iter;
        }
    }

    emit->current_generic_adjust = template_adjust;

    if ((is_varargs && (have_args <= num_args)) ||
        (is_varargs == 0 && (have_args != num_args)))
        bad_num_args(emit, ast, call_sig);

    for (i = 0;i != num_args;arg = arg->next_arg, i++)
        eval_call_arg(emit, ast, template_adjust, call_sig->siglist[i + 1],
                arg, i);

    if (is_varargs == 0 && call_sig->flags & SIG_CALL_HAS_ENUM_ARG)
        box_call_variants(emit, call_sig, num_args, true_start);
    else if (is_varargs) {
        lily_sig *va_comp_sig = call_sig->siglist[i + 1];
        lily_ast *save_arg = arg;
        lily_sig *save_sig;

        /* varargs is handled by shoving the excess into a list. The elements
           need to be the type of what the list holds. */
        save_sig = va_comp_sig;
        va_comp_sig = va_comp_sig->siglist[0];

        /* The difference is that this time the sig wanted is always
           va_comp_sig. */
        for (;arg != NULL;arg = arg->next_arg)
            eval_call_arg(emit, ast, template_adjust, va_comp_sig, arg, i);

        if (call_sig->flags & SIG_CALL_HAS_ENUM_ARG)
            box_call_variants(emit, call_sig, num_args, true_start);

        i = (have_args - i);
        lily_storage *s;
        if (save_sig->template_pos == 0)
            s = get_storage(emit, save_sig, ast->line_num);
        else {
            /* The function to call wants a list of some generic type. Do not
               use that sig, because the caller could either not patch it, or
               patch it wrong.
               Instead, build a list sig from the sig of the first arg.

               The arguments may be generic. If that happens, the caller will
               be generic and will fix them up right. */
            int stack_start = emit->sig_stack_pos + emit->current_generic_adjust + 1;
            if (stack_start + 1 > emit->sig_stack_size)
                grow_sig_stack(emit);

            lily_class *list_cls = lily_class_by_id(emit->symtab,
                    SYM_CLASS_LIST);
            emit->sig_stack[stack_start] = save_arg->result->sig;
            lily_sig *new_sig = lily_build_ensure_sig(emit->symtab, list_cls,
                    0, emit->sig_stack, stack_start, 1);

            s = get_storage(emit, new_sig, ast->line_num);
        }

        write_build_op(emit, o_build_list_tuple, save_arg, save_arg->line_num,
                i, s->reg_spot);

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
    Since this checks for tree_readonly, only explicit calls to functions
    defined in the class get an implicit self. */
static int maybe_self_insert(lily_emit_state *emit, lily_ast *ast)
{
    /* Global functions defined outside of this class do not automatically get
       self as the first argument. */
    if (emit->current_class != ((lily_var *)ast->result)->parent)
        return 0;

    ast->arg_start->result = (lily_sym *)emit->self_storage;
    ast->arg_start->tree_type = tree_local_var;
    return 1;
}

/*  eval_call
    This handles doing calls to what should be a function. It handles doing oo
    calls by farming out the oo lookup elsewhere. */
static void eval_call(lily_emit_state *emit, lily_ast *ast,
        lily_sig *expect_sig)
{
    int expect_size, i;
    lily_ast *arg;
    lily_sig *call_sig;
    lily_sym *call_sym;

    int save_adjust = emit->current_generic_adjust;
    emit->sig_stack_pos += save_adjust;
    emit->current_generic_adjust = 0;

    /* Variants are created by calling them in a function-like manner, so the
       parser adds them as if they were functions. They're not. */
    if (ast->arg_start->tree_type == tree_variant) {
        emit->sig_stack_pos -= save_adjust;
        emit->current_generic_adjust = save_adjust;
        eval_variant(emit, ast, expect_sig);
        return;
    }

    int cls_id;
    /* Special case: Don't walk tree_readonly. Doing so will rewrite the
       var given to it with a storage result...which emitter cannot use
       for printing error information. */
    if (ast->arg_start->tree_type != tree_readonly)
        eval_tree(emit, ast->arg_start, NULL);

    /* This is important because wrong type/wrong number of args looks to
       either ast->result or the first tree to get the function name. */
    ast->result = ast->arg_start->result;
    call_sym = ast->result;

    /* Make sure the result is callable (ex: NOT @(integer: 10) ()). */
    cls_id = ast->result->sig->cls->id;
    if (cls_id != SYM_CLASS_FUNCTION)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Cannot anonymously call resulting type '%T'.\n",
                ast->result->sig);

    if (ast->arg_start->tree_type == tree_oo_access) {
        /* Fix the oo access to return the first arg it had, since that's
           the call's first value. It's really important that
           check_call_args get all the args, because the first is the most
           likely to have a template parameter. */
        ast->arg_start->result = ast->arg_start->arg_start->result;
    }
    else if (ast->arg_start->tree_type == tree_readonly) {
        /* If inside a class, then consider inserting replacing the
           unnecessary readonly tree with 'self'.
           If this isn't possible, drop the readonly tree from args since
           it isn't truly an argument. */
        if (emit->current_class != NULL)
            maybe_self_insert(emit, ast);
    }
    else {
        /* For anonymously called functions, unset ast->result so that arg
           error functions will report that it's anonymously called. */
        ast->result = NULL;
    }

    call_sig = call_sym->sig;
    arg = ast->arg_start;
    expect_size = 6 + ast->args_collected;

    check_call_args(emit, ast, call_sig, expect_sig);

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

    if (call_sig->siglist[0] != NULL) {
        lily_sig *return_sig = call_sig->siglist[0];

        /* If it's just a template, grab the appropriate thing from the sig
           stack (which is okay until the next eval_call). Otherwise, just
           give up and build the right thing. */
        if (return_sig->cls->id == SYM_CLASS_TEMPLATE)
            return_sig = emit->sig_stack[emit->sig_stack_pos +
                    return_sig->template_pos];
        else if (return_sig->template_pos != 0)
            return_sig = build_untemplated_sig(emit, return_sig);

        lily_storage *storage = get_storage(emit, return_sig, ast->line_num);
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

    emit->sig_stack_pos -= save_adjust;
    emit->current_generic_adjust = save_adjust;
}

/* emit_nonlocal_var
   This handles vars that are not local and are on the right hand side of an
   expression. This handles loading both literals and globals into a local
   register. */
static void emit_nonlocal_var(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *ret;
    int opcode;

    if (ast->tree_type == tree_readonly) {
        if (ast->result->flags & SYM_TYPE_LITERAL)
            opcode = o_get_const;
        else
            opcode = o_get_function;
    }
    else if (ast->result->flags & SYM_TYPE_VAR)
        opcode = o_get_global;
    else
        opcode = -1;

    ret = get_storage(emit, ast->result->sig, ast->line_num);
    ret->flags |= SYM_NOT_ASSIGNABLE;

    write_4(emit,
            opcode,
            ast->line_num,
            ast->result->reg_spot,
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
        eval_tree(emit, ast->arg_start, NULL);

    lily_class *lookup_class = ast->arg_start->result->sig->cls;
    char *oo_name = emit->oo_name_pool->str + ast->oo_pool_index;
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

        lily_sig *property_sig = prop->sig;
        if (property_sig->cls->id == SYM_CLASS_TEMPLATE ||
            property_sig->template_pos) {
            property_sig = resolve_second_sig_by_first(emit,
                    ast->arg_start->result->sig,
                    property_sig);
        }

        lily_storage *result = get_storage(emit, property_sig,
                ast->line_num);

        /* Hack: If the parent is really oo_assign, then don't load the result
                 into a register. The parent tree just wants to know the
                 resulting type and the property index. */
        if (ast->parent == NULL ||
            ast->parent->tree_type != tree_binary ||
            ast->parent->op != expr_assign) {
            lily_literal *lit = lily_get_integer_literal(emit->symtab, prop->id);
            lily_storage *lit_result = get_storage(emit, lit->sig,
                    ast->line_num);
            /* Don't use lookup_class->sig, in case the class doesn't have a
               default signature. */

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
    if (ast->property->sig == NULL)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Invalid use of uninitialized property '@%s'.\n",
                ast->property->name);

    lily_storage *result = get_storage(emit, ast->property->sig,
            ast->line_num);

    lily_literal *lit = lily_get_integer_literal(emit->symtab,
            ast->property->id);
    lily_storage *index_storage = get_storage(emit, lit->sig, ast->line_num);

    write_4(emit,
            o_get_const,
            ast->line_num,
            lit->reg_spot,
            index_storage->reg_spot);

    write_5(emit,
            o_get_item,
            ast->line_num,
            emit->self_storage->reg_spot,
            index_storage->reg_spot,
            result->reg_spot);

    ast->result = (lily_sym *)result;
}

static void eval_variant(lily_emit_state *emit, lily_ast *ast,
        lily_sig *expect_sig)
{
    lily_storage *result = NULL;

    if (ast->tree_type == tree_call) {
        /* Make space for generics, in case this variant uses them. */
        int save_adjust = emit->current_generic_adjust;
        emit->sig_stack_pos += save_adjust;
        emit->current_generic_adjust = 0;

        ast->result = NULL;

        /* The first arg is actually the variant. */
        lily_ast *variant_tree = ast->arg_start;
        lily_class *variant_class = variant_tree->variant_class;

        if (variant_class->variant_sig->siglist_size == 1)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Variant class %s should not get args.\n",
                    variant_class->name);

        check_call_args(emit, ast, variant_class->variant_sig, expect_sig);

        lily_sig *result_sig = variant_class->variant_sig->siglist[0];
        if (result_sig->template_pos != 0)
            result_sig = build_untemplated_sig(emit, result_sig);

        result = get_storage(emit, result_sig, ast->line_num);

        /* It's pretty darn close to a tuple, so...let's use that. :) */
        write_build_op(emit, o_build_list_tuple, ast->arg_start,
                ast->line_num, ast->args_collected, result->reg_spot);

        emit->sig_stack_pos -= save_adjust;
        emit->current_generic_adjust = save_adjust;
    }
    else {
        /* Did this need arguments? It was used incorrectly if so. */
        lily_sig *variant_init_sig = ast->variant_class->variant_sig;
        if (variant_init_sig->siglist_size != 0)
            lily_raise(emit->raiser, lily_SyntaxError,
                    "Variant class %s needs %d arg(s).\n",
                    ast->variant_class->name,
                    variant_init_sig->siglist_size - 1);

        /* If a variant type takes no arguments, then it's essentially an empty
           container. It would be rather silly to have a bunch of UNIQUE empty
           containers (which will always be empty).
           So the interpreter creates a literal and hands that off. */
        lily_sig *variant_sig = ast->variant_class->variant_sig;
        lily_literal *variant_lit = lily_get_variant_literal(emit->symtab,
                variant_sig);

        result = get_storage(emit, variant_sig, ast->line_num);
        write_4(emit, o_get_const, ast->line_num, variant_lit->reg_spot,
                result->reg_spot);
    }

    ast->result = (lily_sym *)result;
}

/*  eval_tree
    Magically determine what function actually handles the given ast. */
static void eval_tree(lily_emit_state *emit, lily_ast *ast,
        lily_sig *expect_sig)
{
    if (ast->tree_type == tree_var || ast->tree_type == tree_readonly)
        emit_nonlocal_var(emit, ast);
    else if (ast->tree_type == tree_call)
        eval_call(emit, ast, expect_sig);
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
                eval_tree(emit, ast->left, NULL);

            if (ast->right->tree_type != tree_local_var)
                eval_tree(emit, ast->right, ast->left->result->sig);

            emit_binary_op(emit, ast);
        }
    }
    else if (ast->tree_type == tree_parenth) {
        if (ast->arg_start->tree_type != tree_local_var)
            eval_tree(emit, ast->arg_start, expect_sig);

        ast->result = ast->arg_start->result;
    }
    else if (ast->tree_type == tree_unary) {
        if (ast->left->tree_type != tree_local_var)
            eval_tree(emit, ast->left, expect_sig);

        eval_unary_op(emit, ast);
    }
    else if (ast->tree_type == tree_list)
        eval_build_list(emit, ast, expect_sig);
    else if (ast->tree_type == tree_hash)
        eval_build_hash(emit, ast, expect_sig);
    else if (ast->tree_type == tree_tuple)
        eval_build_tuple(emit, ast, expect_sig);
    else if (ast->tree_type == tree_subscript)
        eval_subscript(emit, ast, expect_sig);
    else if (ast->tree_type == tree_package)
        eval_package(emit, ast);
    else if (ast->tree_type == tree_typecast)
        eval_typecast(emit, ast);
    else if (ast->tree_type == tree_oo_access)
        eval_oo_access(emit, ast);
    else if (ast->tree_type == tree_property)
        eval_property(emit, ast);
    else if (ast->tree_type == tree_variant)
        eval_variant(emit, ast, expect_sig);
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
    int current_type = emit->current_block->block_type;
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

        lily_var *v = emit->current_block->var_start;
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
    emit->current_block->block_type = new_type;
}

/*  lily_emit_expr
    This evaluates the root of the ast pool given (the expression), then clears
    the pool for the next expression. */
void lily_emit_eval_expr(lily_emit_state *emit, lily_ast_pool *ap)
{
    eval_tree(emit, ap->root, NULL);
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

    eval_tree(emit, ast, NULL);
    emit->expr_num++;

    if (ast->result->sig->cls->id != SYM_CLASS_INTEGER) {
        lily_raise(emit->raiser, lily_SyntaxError,
                   "Expected type 'integer', but got type '%T'.\n",
                   ast->result->sig);
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
    int current_type = emit->current_block->block_type;

    if ((ast->tree_type == tree_readonly &&
         condition_optimize_check(ast)) == 0) {
        eval_enforce_value(emit, ast, "Conditional expression has no value.\n");
        ensure_valid_condition_type(emit, ast->result->sig);

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
                    emit->current_block->loop_start);
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
            write_2(emit, o_jump, emit->current_block->loop_start);
    }

    lily_ast_reset_pool(ap);
}

/*  lily_emit_variant_decompose
    This function writes out an o_variant_decompose instruction based upon the
    signature given. The target(s) of the decompose are however many vars that
    the variant calls for, and pulled from the top of the symtab's vars.

    Assumptions:
    * The most recent vars that have been added to the symtab are the ones that
      are to get the values.
    * The given variant signature actually has inner values (empty variants
      should never be sent here). */
void lily_emit_variant_decompose(lily_emit_state *emit, lily_sig *variant_sig)
{
    lily_function_val *f = emit->top_function;
    int value_count = variant_sig->siglist_size - 1;
    int i;

    write_prep(emit, 4 + value_count);

    f->code[f->pos  ] = o_variant_decompose;
    f->code[f->pos+1] = *(emit->lex_linenum);
    f->code[f->pos+2] = emit->current_block->match_value_spot;
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
    int block_offset = emit->current_block->match_case_start;
    int is_first_case = 1, ret = 1;
    int i;

    for (i = emit->current_block->match_case_start;
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
        f->code[emit->current_block->match_code_start + pos] = f->pos;

        /* This is necessary to keep vars created from the decomposition of one
           class from showing up in subsequent cases. */
        lily_var *v = emit->current_block->var_start;
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
    lily_block *current_block = emit->current_block;
    eval_enforce_value(emit, ast, "Match expression has no value.\n");

    if ((ast->result->sig->cls->flags & CLS_ENUM_CLASS) == 0 ||
        ast->result->sig->cls->id == SYM_CLASS_ANY) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "Match expression is not an enum class value.\n");
    }

    current_block->match_input_sig = ast->result->sig;

    int match_cases_needed = ast->result->sig->cls->variant_size;
    if (emit->match_case_pos + match_cases_needed > emit->match_case_size)
        grow_match_cases(emit);

    current_block->match_case_start = emit->match_case_pos;

    /* This is how the emitter knows that no cases have been given yet. */
    int i;
    for (i = 0;i < match_cases_needed;i++)
        emit->match_cases[emit->match_case_pos + i] = 0;

    emit->match_case_pos += match_cases_needed;

    current_block->match_code_start = emit->top_function->pos + 4;
    current_block->match_value_spot = ast->result->reg_spot;

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
    lily_block *loop_block = emit->current_block;
    lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

    int have_step = (for_step != NULL);
    if (have_step == 0) {
        /* This var isn't visible, so don't bother with a valid shorthash. */
        for_step = lily_try_new_var(emit->symtab, cls->sig, "(for step)", 0);
        if (for_step == NULL)
            lily_raise_nomem(emit->raiser);
    }

    lily_sym *target;
    /* Global vars cannot be used directly, because o_for_setup and
       o_integer_for expect local registers. */
    if (user_loop_var->function_depth == 1)
        target = (lily_sym *)get_storage(emit, user_loop_var->sig, line_num);
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

/* lily_emit_break
   This writes a break (jump to the end of a loop) for the parser. Since it
   is called by parser, it needs to verify that it is called from within a
   loop. */
void lily_emit_break(lily_emit_state *emit)
{
    if (emit->current_block->loop_start == -1) {
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
    if (emit->current_block->block_type == BLOCK_WHILE) {
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
    if (emit->current_block->loop_start == -1) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "'continue' used outside of a loop.\n");
    }

    write_pop_inner_try_blocks(emit);

    write_2(emit, o_jump, emit->current_block->loop_start);
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
        eval_enforce_value(emit, ast, "'return' expression has no value.\n");

        lily_sig *ret_sig = emit->top_function_ret;

        if (ast->result->sig != ret_sig &&
            type_matchup(emit, ret_sig, ast) == 0) {
            lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                    "return expected type '%T' but got type '%T'.\n", ret_sig,
                    ast->result->sig);
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
        lily_class *decl_class, int generic_count, lily_sig *ret_sig)
{
    emit->top_function_ret = ret_sig;
    emit->current_block->generic_count = generic_count;

    /* Since functions can only be declared within other functions, it can be
       safely assumed that declared functions will always start from position 0
       of the sig stack. Each function, no matter how deep, shares generic info
       from 0. */
    if (generic_count > emit->sig_stack_pos) {
        if (generic_count > emit->sig_stack_size)
            grow_sig_stack(emit);

        /* If this isn't done, then generics within a generic function will be
           able to be assigned completely-known types because they won't be
           resolved. */
        lily_sig *sig_iter = emit->symtab->template_sig_start;
        int i;
        for (i = 0;
             i < generic_count;
             i++, sig_iter = sig_iter->next) {
            emit->sig_stack[i] = sig_iter;
        }

        /* This is so calls know how far to move the sig stack so that they
           won't damage this information. */
        emit->current_generic_adjust = i;
    }

    if (decl_class) {
        /* The most recent function is the constructor for this class, which will
           always return a class instance. Since it's also the function var (and
           the return of a function is always [0], this works. */
        lily_sig *self_sig = emit->current_block->function_var->sig->siglist[0];

        lily_storage *self = get_storage(emit, self_sig, *emit->lex_linenum);
        emit->current_block->self = self;

        write_3(emit,
                o_new_instance,
                *emit->lex_linenum,
                self->reg_spot);

        emit->self_storage = self;
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
    eval_enforce_value(emit, ast, "'raise' expression has no value.\n");

    lily_class *result_cls = ast->result->sig->cls;
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
    lily_sig *except_sig = cls->sig;
    lily_sym *except_sym = (lily_sym *)except_var;
    if (except_sym == NULL)
        except_sym = (lily_sym *)get_storage(emit, except_sig, line_num);

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
    __main__ so the vm knows what signatures to use for different registers,
    and writes the o_return_from_vm instruction that will leave the vm. */
void lily_emit_vm_return(lily_emit_state *emit)
{
    finalize_function_val(emit, emit->current_block);
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
    if (emit->current_block->next == NULL) {
        new_block = try_new_block();
        if (new_block == NULL)
            lily_raise_nomem(emit->raiser);

        emit->current_block->next = new_block;
        new_block->prev = emit->current_block;
    }
    else
        new_block = emit->current_block->next;

    new_block->block_type = block_type;
    new_block->var_start = emit->symtab->var_chain;
    new_block->class_entry = NULL;
    new_block->self = NULL;
    new_block->generic_count = 0;

    if ((block_type & BLOCK_FUNCTION) == 0) {
        new_block->patch_start = emit->patch_pos;
        /* Non-functions will continue using the storages that the parent uses.
           Additionally, the same technique is used to allow loop starts to
           bubble upward until a function gets in the way. */
        new_block->storage_start = emit->current_block->storage_start;
        if (IS_LOOP_BLOCK(block_type))
            new_block->loop_start = emit->top_function->pos;
        else
            new_block->loop_start = emit->current_block->loop_start;
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

    emit->current_block = new_block;
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

    if (emit->current_block->prev == NULL)
        lily_raise(emit->raiser, lily_SyntaxError, "'}' outside of a block.\n");

    block = emit->current_block;
    block_type = block->block_type;

    /* These blocks need to jump back up when the bottom is hit. */
    if (block_type == BLOCK_WHILE || block_type == BLOCK_FOR_IN)
        write_2(emit, o_jump, emit->current_block->loop_start);
    else if (block_type == BLOCK_MATCH) {
        ensure_proper_match_block(emit);
        emit->match_case_pos = emit->current_block->match_case_start;
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

    emit->current_block = emit->current_block->prev;
}

/*  lily_emit_try_enter_main
    Make a block representing __main__ and go inside of it. Returns 1 on
    success, 0 on failure. This should only be called once. */
int lily_emit_try_enter_main(lily_emit_state *emit, lily_var *main_var)
{
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
    main_block->storage_start = NULL;
    /* This is necessary for trapping break/continue inside of __main__. */
    main_block->loop_start = -1;
    main_block->class_entry = NULL;
    main_block->generic_count = 0;
    emit->top_function = main_var->value.function;
    emit->top_var = main_var;
    emit->current_block = main_block;
    emit->function_depth++;
    return 1;
}

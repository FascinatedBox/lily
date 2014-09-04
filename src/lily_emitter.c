#include <string.h>

#include "lily_impl.h"
#include "lily_ast.h"
#include "lily_value.h"
#include "lily_emitter.h"
#include "lily_opcode.h"
#include "lily_emit_table.h"

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

# define IS_LOOP_BLOCK(b) (b & (BLOCK_WHILE | BLOCK_DO_WHILE | BLOCK_FOR_IN))
# define GLOBAL_LOAD_CHECK(sym) \
    ((sym->flags & SYM_TYPE_VAR) && \
     ((lily_var *)sym)->function_depth == 1)

# define lily_raise_adjusted(r, adjust, error_code, message, ...) \
{ \
    r->line_adjust = adjust; \
    lily_raise(r, error_code, message, __VA_ARGS__); \
}

static int type_matchup(lily_emit_state *, lily_sig *, lily_sig *, lily_ast *);

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
    if (s->patches == NULL || s->sig_stack == NULL) {
        lily_free(s->patches);
        lily_free(s->sig_stack);
        lily_free(s);
        return NULL;
    }

    s->current_block = NULL;
    s->unused_storage_start = NULL;
    s->all_storage_start = NULL;
    s->all_storage_top = NULL;

    s->sig_stack_pos = 0;
    s->sig_stack_size = 4;

    s->patch_pos = 0;
    s->patch_size = 4;
    s->function_depth = 0;

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

    uintptr_t *save_code;
    f->len *= 2;
    save_code = lily_realloc(f->code, sizeof(uintptr_t) * f->len);
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
        uintptr_t *save_code;
        while ((f->pos + size) > f->len)
            f->len *= 2;
        save_code = lily_realloc(f->code, sizeof(uintptr_t) * f->len);
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
static void write_1(lily_emit_state *emit, int one)
{
    lily_function_val *f = emit->top_function;

    if ((f->pos + 1) > f->len)
        small_grow(emit);

    f->code[f->pos] = one;
    f->pos += 1;
}

static void write_2(lily_emit_state *emit, int one, int two)
{
    lily_function_val *f = emit->top_function;

    if ((f->pos + 2) > f->len)
        small_grow(emit);

    f->code[f->pos] = one;
    f->code[f->pos + 1] = two;
    f->pos += 2;
}

static void write_3(lily_emit_state *emit, int one, int two, int three)
{
    lily_function_val *f = emit->top_function;

    if ((f->pos + 3) > f->len)
        small_grow(emit);

    f->code[f->pos] = one;
    f->code[f->pos + 1] = two;
    f->code[f->pos + 2] = three;
    f->pos += 3;
}

static void write_4(lily_emit_state *emit, int one, int two, int three,
        int four)
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

static void write_5(lily_emit_state *emit, int one, int two, int three,
        int four, int five)
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

/*  count_inner_try_blocks
    Count the number of 'try' blocks entered where an 'except' has not yet been
    seen. This counts up to the deepest loop block, or the current function,
    whichever comes first. */
static int count_inner_try_blocks(lily_emit_state *emit)
{
    lily_block *block_iter = emit->current_block;
    int ret = 0;
    while (IS_LOOP_BLOCK(block_iter->block_type) == 0 &&
           block_iter->block_type != BLOCK_FUNCTION) {
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
        else if (block->block_type == BLOCK_FUNCTION) {
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


/* try_add_storage
   Attempt to add a new storage to emitter's chain of storages with the given
   signature. This should be seen as a helper for get_storage, and not used
   outside of it.
   On success, the newly created storage is returned.
   On failure, NULL is returned. */
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
    raise ErrNoMem with the given line_num.*/
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

            /* Signatures can be compared by ptr because lily_ensure_unique_sig
               makes so that all signatures are unique. So it isn't necessary
               to deep compare them. */
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
                if (emit->current_block->block_type == BLOCK_FUNCTION)
                    /* Easy mode: Just fill in for the function. */
                    emit->current_block->storage_start = ret;
                else {
                    /* Non-function block, so keep setting storage_start until
                       the function block is reached. This will allow other
                       blocks to use this storage. This is also important
                       because not doing this causes the function block to miss
                       this storage when the function is being finalized. */
                    lily_block *block = emit->current_block;
                    while (block->block_type != BLOCK_FUNCTION) {
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

    return ret;
}

/* emit_return_expected
   This writes o_return_expected with the current line number. This is done to
   prevent functions that should return values from not doing so. */
static void emit_return_expected(lily_emit_state *emit)
{
    write_2(emit, o_return_expected, *emit->lex_linenum);
}

/*  write_build_op

    This is responsible for writing the actual o_build_list or o_build_hash
    code, depending on the opcode given. The list will be put into a register
    at reg_spot, which is assumed to have the correct type to hold the given
    list.

    emit:       The emitter holding the function to write to.
    opcode:     The opcode to write: o_build_list for a list, o_build_hash for a
                hash, o_build_tuple for a tuple.
    first_arg:  The first argument to start iterating over.
    line_num:   A line number for the o_build_* opcode.
    num_values: The number of values that will be written. This is typically
                the parent's args_collected.
    reg_spot:   The id of a register where the opcode's result will go. The
                caller is expected to ensure that the register has the proper
                type to hold the resulting list.
*/
static void write_build_op(lily_emit_state *emit, int opcode,
        lily_ast *first_arg, int line_num, int num_values, int reg_spot)
{
    int i;
    lily_ast *arg;
    lily_function_val *f = emit->top_function;

    write_prep(emit, num_values + 4);
    f->code[f->pos] = opcode;
    f->code[f->pos+1] = first_arg->line_num;
    f->code[f->pos+2] = num_values;

    for (i = 3, arg = first_arg; arg != NULL; arg = arg->next_arg, i++)
        f->code[f->pos + i] = arg->result->reg_spot;

    f->code[f->pos+i] = reg_spot;
    f->pos += 4 + num_values;
}

/* emit_any_assign
   The given ast is a non-any, and the caller has checked that an any is
   wanted. This converts the ast's result so that it contains an any by
   using o_any_assign. */
static void emit_any_assign(lily_emit_state *emit, lily_ast *ast)
{
    lily_class *any_class = lily_class_by_id(emit->symtab, SYM_CLASS_ANY);
    lily_storage *storage = get_storage(emit, any_class->sig, ast->line_num);

    write_4(emit,
            o_any_assign,
            ast->line_num,
            ast->result->reg_spot,
            storage->reg_spot);

    ast->result = (lily_sym *)storage;
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
    else if (lhs->cls->id == rhs->cls->id) {
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
                if (left_entry == right_entry)
                    continue;

                if (template_check(emit, left_entry, right_entry) == 0) {
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

    return ret;
}

/*  recursively_build_sig
    This function is called when the return of a function uses templates.
    It will return a signature that has the current template types replaced.

    One example is the 'some' function, defined as 'function(A => maybe[A])'
    some(10) # A = integer, so the result is maybe[integer]

    This should only be called by build_templated_sig, because it needs some
    adjustments to emitter's sig_stack_pos before and after it's called.

    emit:
    template_index: For templates, the offset where templates should grab
                    signatures from. This is important because the emitter's
                    sig stack is also used to store sigs in place.
    sig:            The signature to examine. */
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
            lily_sig *inner_sig = recursively_build_sig(emit, template_index, siglist[i]);
            emit->sig_stack[emit->sig_stack_pos] = inner_sig;
            emit->sig_stack_pos++;
        }

        ret = lily_build_ensure_sig(emit->symtab, sig->cls, i,
                emit->sig_stack, save_start);

        emit->sig_stack_pos -= i;
    }
    else if (sig->cls->id == SYM_CLASS_TEMPLATE)
        ret = emit->sig_stack[template_index + sig->template_pos];

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

/** Error helpers **/
static void bad_arg_error(lily_emit_state *emit, lily_ast *ast,
    lily_sig *got, lily_sig *expected, int arg_num)
{
    lily_var *v = (lily_var *)ast->result;
    char *class_name;
    char *separator;

    if (v->parent) {
        class_name = v->parent->name;
        separator = "::";
    }
    else {
        class_name = "";
        separator = "";
    }

    /* Just in case this arg was on a different line than the call. */
    lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
            "%s%s%s arg #%d expects type '%T' but got type '%T'.\n",
            class_name, separator, v->name, arg_num, expected, got);
}

static void bad_assign_error(lily_emit_state *emit, int line_num,
                          lily_sig *left_sig, lily_sig *right_sig)
{
    /* Remember that right is being assigned to left, so right should
       get printed first. */
    lily_raise_adjusted(emit->raiser, line_num, lily_SyntaxError,
            "Cannot assign type '%T' to type '%T'.\n",
            right_sig, left_sig);
}

/* bad_num_args
   Reports that the ast didn't get as many args as it should have. Takes
   anonymous calls and var args into account. */
static void bad_num_args(lily_emit_state *emit, lily_ast *ast,
        lily_sig *call_sig)
{
    char *call_name, *class_name, *separator, *va_text;
    lily_var *var;

    if (ast->result != NULL) {
        var = (lily_var *)ast->result;
        call_name = var->name;
    }
    else {
        /* This occurs when the call is based off of a subscript, such as
           function_list[0]()
           This is generic, but it's assumed that this will be enough when
           paired with the line number. */
        call_name = "(anonymous call)";
        var = NULL;
    }

    if (call_sig->flags & SIG_IS_VARARGS)
        va_text = "at least ";
    else
        va_text = "";

    if (var->parent) {
        class_name = var->parent->name;
        separator = "::";
    }
    else {
        class_name = "";
        separator = "";
    }

    lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
               "%s%s%s expects %s%d args, but got %d.\n",
               class_name, separator, call_name, va_text,
               call_sig->siglist_size - 1, ast->args_collected);
}

/** ast walking helpers **/
static void eval_tree(lily_emit_state *, lily_ast *);

/* emit_binary_op
   This handles an ast of type tree_binary wherein the op is not an assignment
   of any type. This does not emit because eval_tree will eval the left and
   right sides before calling this. This is good, because it allows
   emit_op_for_compound to call this to handle compound assignments too. */
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
        /* Allow == and != for any class, so long as the signatures both match.
           This allows useful things like comparing functions. */
        if (ast->left->result->sig == ast->right->result->sig) {
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

    write_5(emit,
            opcode,
            ast->line_num,
            ast->left->result->reg_spot,
            ast->right->result->reg_spot,
            s->reg_spot);

    ast->result = (lily_sym *)s;
}

/*  emit_op_for_compound
    Compound assignments are assignments that also have an operation that
    includes the left side of the expression. Examples are +=, -=, *=, /=, and
    more. Rather than leave this to the vm, these compound assignments are
    broken down, then assigned to the left side of the expression.
    So if we have this:

    x *= y

    it gets broken down into

    x * y -> storage
    x = storage

    This takes in an ast that has an op that is a compound assignment, and calls
    for it to be emitted as if it were a binary operation.

    Notes:
    * This assumes ast->left and ast->right have already been walked.
    * This will raise lily_SyntaxError if the ast's op is not a compound op, or
      if the compound op is invalid (ex: list /= integer).
    * This handles the 'x * y -> storage' part. The caller is expected to handle
      the 'x = storage' part.
    * The result of the binary expression is set to ast->result.
*/
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

/* assign_optimize_check
   This takes a given ast and determines if an assignment -really- needs to be
   written for it. This is somewhat complex, but anything that can be optimized
   out at emit-time has huge gains at vm-time.
   This can be written as one very large if check with multiple or's, but for
   sanity, has been broken down into explainable chunks. */
static int assign_optimize_check(lily_ast *ast)
{
    int can_optimize = 1;

    do {
        /* Most opcodes assume that the register index is a local index for
           simplicity. This is an index of a global, so this isn't possible. */
        if (ast->left->tree_type == tree_var) {
            can_optimize = 0;
            break;
        }

        lily_ast *right_tree = ast->right;

        /* Can't optimize out basic assignments like a = b. */
        if (right_tree->tree_type == tree_var ||
            right_tree->tree_type == tree_local_var) {
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

/* eval_assign
   This handles asts with type tree_binary, wherein the left side is not a
   subscript. This will handle compound assignments, as well as basic
   assignments. */
static void eval_assign(lily_emit_state *emit, lily_ast *ast)
{
    int left_cls_id, opcode;
    lily_sym *left_sym, *right_sym;
     opcode = -1;

    if (ast->left->tree_type != tree_var)
        eval_tree(emit, ast->left);

    if ((ast->left->result->flags & SYM_TYPE_VAR) == 0)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Left side of %s is not a var.\n", opname(ast->op));

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right);

    left_sym = ast->left->result;
    right_sym = ast->right->result;
    left_cls_id = left_sym->sig->cls->id;

    if (left_sym->sig != right_sym->sig) {
        if (left_sym->sig->cls->id == SYM_CLASS_ANY)
            opcode = o_any_assign;
        else {
            if (type_matchup(emit, NULL, ast->left->result->sig,
                           ast->right) == 0) {
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
            opcode = o_assign;
        else if (left_cls_id == SYM_CLASS_ANY)
            opcode = o_any_assign;
        else
            opcode = o_ref_assign;
    }

    if (ast->op > expr_assign) {
        emit_op_for_compound(emit, ast);
        right_sym = ast->result;
    }

    if (GLOBAL_LOAD_CHECK(left_sym))
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

    /* If the argument is a package, then this contains at least two package
       accesses (a::b::c at least, maybe more). So instead of doing multiple
       single-level access, combine them into one. */
    if (ast->arg_start->tree_type == tree_package) {
        if (is_set_op)
            opcode = o_package_set_deep;
        else
            opcode = o_package_get_deep;

        int depth = 1;
        lily_ast *dive_tree = ast->arg_start;
        while (dive_tree->tree_type == tree_package) {
            dive_tree = dive_tree->arg_start;
            depth++;
        }

        dive_tree = dive_tree->parent;

        write_prep(emit, 5 + depth);

        lily_function_val *f = emit->top_function;
        f->code[f->pos] = opcode;
        f->code[f->pos+1] = ast->line_num;
        f->code[f->pos+2] = dive_tree->arg_start->result->reg_spot;
        f->code[f->pos+3] = depth;

        lily_package_val *pval;
        pval = dive_tree->arg_start->result->value.package;
        int j = 0;

        /* For each access, find out the index to use to get the package at
           that level. Write it down, then go to the next level. */
        while (1) {
            lily_var *index_var;
            index_var = (lily_var *)dive_tree->arg_start->next_arg->result;

            int i;
            for (i = 0;i < pval->var_count;i++) {
                if (pval->vars[i] == index_var)
                    break;
            }

            f->code[f->pos+4+j] = i;

            dive_tree = dive_tree->parent;
            if (dive_tree == NULL)
                break;

            pval = index_var->value.package;
            j++;
        }

        if (is_set_op == 0)
            s = (lily_sym *)get_storage(emit,
                ast->arg_start->next_arg->result->sig, ast->line_num);

        f->code[f->pos+5+j] = s->reg_spot;
        f->pos += 5 + depth;
    }
    else {
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
    }

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

    if (ast->right->tree_type != tree_var)
        eval_tree(emit, ast->right);

    /* Don't evaluate the package tree. Like subscript assign, this has to
       write directly to the var at the given part of the package. Since parser
       passes the var to be assigned, just grab that from result for checking
       the sig. No need to do a symtab lookup of a name. */
    lily_sig *result_sig = package_right->result->sig;

    /* Before doing an eval, make sure that the two types actually match up. */
    if (result_sig != rhs_tree->result->sig &&
        type_matchup(emit, NULL, result_sig, rhs_tree) == 0) {
        bad_assign_error(emit, ast->line_num, result_sig,
                rhs_tree->result->sig);
    }

    /* Evaluate the tree grab on the left side. Use set opcodes instead of get
       opcodes. The result given is what will be assigned. */
    eval_package_tree_for_op(emit, ast->left, 1, (lily_sym *)rhs_tree->result);

    /* This is necessary for assignment chains. */
    ast->result = rhs_tree->result;
}

/* Forward decls of enter/leave block for emit_logical_op. */
void lily_emit_enter_block(lily_emit_state *, int);
void lily_emit_leave_block(lily_emit_state *);

/* eval_logical_op
   This handles an ast of type tree_binary, wherein the op is either
   expr_logical_or (||) or expr_logical_and (&&). */
static void eval_logical_op(lily_emit_state *emit, lily_ast *ast)
{
    lily_symtab *symtab = emit->symtab;
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
        eval_tree(emit, ast->left);

    /* If the left is the same as this tree, then it's already checked itself
       and doesn't need a retest. However, and/or are opposites, so they have
       to check each other (so the op has to be exactly the same). */
    if ((ast->left->tree_type == tree_binary && ast->left->op == ast->op) == 0)
        lily_emit_jump_if(emit, ast->left, jump_on);

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right);

    lily_emit_jump_if(emit, ast->right, jump_on);

    if (is_top == 1) {
        int save_pos;
        lily_literal *success_lit, *failure_lit;
        lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

        /* The literals need to be pulled into storages before they can be used,
           so grab two more for them. */
        result = get_storage(emit, cls->sig, ast->line_num);

        /* The symtab adds literals 0 and 1 in that order during its init. */
        success_lit = symtab->lit_start;
        if (ast->op == expr_logical_or)
            failure_lit = success_lit->next;
        else {
            failure_lit = success_lit;
            success_lit = failure_lit->next;
        }

        write_4(emit,
                o_get_const,
                ast->line_num,
                (uintptr_t)success_lit,
                result->reg_spot);

        write_2(emit, o_jump, 0);
        save_pos = f->pos - 1;

        lily_emit_leave_block(emit);
        write_4(emit,
                o_get_const,
                ast->line_num,
                (uintptr_t)failure_lit,
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
        result = NULL;

    return result;
}

/* emit_sub_assign
   This handles an ast of type tree_binary wherein the left side has a
   subscript against it (ex: x[0] = y, x[0][0] = y, etc). This also handles
   compound assignments.
   This is necessary because subscripts place their value into a storage, so a
   typical assignment would target a storage, instead of the list value. This
   writes o_set_item to make sure the vm assigns to the list, and not a
   storage.
   Var is at ast->left->arg_start
   Index is at ast->left->arg_start->next
   Right is at ast->right */
static void eval_sub_assign(lily_emit_state *emit, lily_ast *ast)
{
    lily_ast *var_ast = ast->left->arg_start;
    lily_ast *index_ast = var_ast->next_arg;
    lily_sym *rhs;
    lily_sig *elem_sig;

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right);

    rhs = ast->right->result;

    if (var_ast->tree_type != tree_local_var)
        eval_tree(emit, var_ast);

    lily_literal *tuple_literal = NULL;

    if (index_ast->tree_type != tree_local_var) {
        if (index_ast->tree_type == tree_readonly &&
            var_ast->result->sig->cls->id == SYM_CLASS_TUPLE) {
            /* Save the literal before evaluating the tree wipes it out. */
            tuple_literal = (lily_literal *)index_ast->result;
        }
        eval_tree(emit, index_ast);
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
    This handles an ast of type tree_typecast. A typecast has two parts: A
    signature to cast to, and a value to cast.
    * The value is at ast->arg_start
    * The signature is at ast->arg_start->next_arg->sig

    This does some basic conversions:
    * Anything can be converted into 'any'.
    * 'any' can be converted into anything (with a check done at vm-time).
    * integer<->double conversion. */
static void eval_typecast(lily_emit_state *emit, lily_ast *ast)
{
    lily_sig *cast_sig = ast->arg_start->next_arg->sig;
    lily_ast *right_tree = ast->arg_start;
    if (right_tree->tree_type != tree_local_var)
        eval_tree(emit, right_tree);

    lily_sig *var_sig = right_tree->result->sig;

    if (cast_sig == var_sig) {
        ast->result = (lily_sym *)right_tree->result;
        return;
    }
    else if (cast_sig->cls->id == SYM_CLASS_ANY) {
        /* Throw it into an 'any'. */
        lily_storage *storage = get_storage(emit, cast_sig, ast->line_num);

        write_4(emit,
                o_any_assign,
                ast->line_num,
                right_tree->result->reg_spot,
                storage->reg_spot);

        ast->result = (lily_sym *)storage;
        return;
    }

    lily_storage *result;
    int cast_opcode;

    if (var_sig->cls->id == SYM_CLASS_ANY) {
        cast_opcode = o_any_typecast;
        result = get_storage(emit, cast_sig, ast->line_num);
    }
    else {
        if ((var_sig->cls->id == SYM_CLASS_INTEGER &&
             cast_sig->cls->id == SYM_CLASS_DOUBLE) ||
            (var_sig->cls->id == SYM_CLASS_DOUBLE &&
             cast_sig->cls->id == SYM_CLASS_INTEGER))
        {
            cast_opcode = o_intdbl_typecast;
            result = get_storage(emit, cast_sig, ast->line_num);
        }
        else {
            cast_opcode = -1;
            result = NULL;
            lily_raise_adjusted(emit->raiser, ast->line_num,
                    lily_BadTypecastError,
                    "Cannot cast type '%T' to type '%T'.\n",
                    var_sig, cast_sig);
        }
    }

    write_4(emit,
            cast_opcode,
            ast->line_num,
            right_tree->result->reg_spot,
            result->reg_spot);

    ast->result = (lily_sym *)result;
}

/* eval_unary_op
   This takes an ast of type tree_unary, and evaluates it. The unary op is
   stored in ast->op, and the value is at ast->left.
   This currently only handles unary ops on integers, but that may eventually
   change. */
static void eval_unary_op(lily_emit_state *emit, lily_ast *ast)
{
    uintptr_t opcode;
    lily_class *lhs_class;
    lily_storage *storage;
     lhs_class = ast->left->result->sig->cls;

    if (lhs_class->id != SYM_CLASS_INTEGER)
        lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                "Invalid operation: %s%s.\n",
                opname(ast->op), lhs_class->name);

    lily_class *integer_cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

    storage = get_storage(emit, integer_cls->sig, ast->line_num);

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

/*  hash_values_to_anys

    This converts all of the values of the given ast into anys using
    o_any_assign. The result of each value is rewritten to be the any,
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

        emit_any_assign(emit, iter_ast->next_arg);
    }
}

/*  emit_list_values_to_anys

    This converts all of the values of the given ast into anys using
    o_any_assign. The result of each value is rewritten to be the any,
    instead of the old value.

    emit:     The emitter holding the function to write code to.
    list_ast: An ast of type tree_list which has already been evaluated.

    Caveats:
    * Caller must do this before writing the o_build_list instruction out.
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
        emit_any_assign(emit, iter_ast);
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
static void eval_build_hash(lily_emit_state *emit, lily_ast *ast)
{
    lily_sig *key_sig, *value_sig;
    lily_ast *tree_iter;
    int make_anys;

    key_sig = NULL;
    value_sig = NULL;
    make_anys = 0;

    for (tree_iter = ast->arg_start;
         tree_iter != NULL;
         tree_iter = tree_iter->next_arg->next_arg) {

        lily_ast *key_tree, *value_tree;
        key_tree = tree_iter;
        value_tree = tree_iter->next_arg;

        if (key_tree->tree_type != tree_local_var)
            eval_tree(emit, key_tree);

        /* Keys -must- all be the same type. They cannot be converted to any
           later on because any are not valid keys (not immutable). */
        if (key_tree->result->sig != key_sig) {
            if (key_sig == NULL) {
                if ((key_tree->result->sig->cls->flags & CLS_VALID_HASH_KEY) == 0) {
                    lily_raise_adjusted(emit->raiser, key_tree->line_num,
                            lily_SyntaxError,
                            "Resulting type '%T' is not a valid hash key.\n",
                            key_tree->result->sig);
                }

                key_sig = key_tree->result->sig;
            }
            else {
                lily_raise_adjusted(emit->raiser, key_tree->line_num,
                        lily_SyntaxError,
                        "Expected a key of type '%T', but key is of type '%T'.\n",
                        key_sig, key_tree->result->sig);
            }
        }

        if (value_tree->tree_type != tree_local_var)
            eval_tree(emit, value_tree);

        /* Values being promoted to any is okay though. :) */
        if (value_tree->result->sig != value_sig) {
            if (value_sig == NULL)
                value_sig = value_tree->result->sig;
            else
                make_anys = 1;
        }
    }

    if (make_anys == 1) {
        lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_ANY);
        value_sig = cls->sig;
        emit_hash_values_to_anys(emit, ast);
    }

    if ((emit->sig_stack_pos + 2) > emit->sig_stack_size)
        grow_sig_stack(emit);

    lily_class *hash_cls = lily_class_by_id(emit->symtab, SYM_CLASS_HASH);
    emit->sig_stack[emit->sig_stack_pos] = key_sig;
    emit->sig_stack[emit->sig_stack_pos+1] = value_sig;
    lily_sig *new_sig = lily_build_ensure_sig(emit->symtab, hash_cls, 2,
                emit->sig_stack, emit->sig_stack_pos);

    lily_storage *s = get_storage(emit, new_sig, ast->line_num);

    write_build_op(emit, o_build_hash, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

/*  type_matchup
    This function checks if the given ast's result can be coerced into the
    wanted type. Self is passed to allow for template checking as well.
    This also handles converting lists[!any] to list[any], and
    hash[?, !any] to hash[?, any]

    emit:     The emitter, in case code needs to be written.
    self:     The self, in the event that want_sig uses templates. If it does
              not, then passing NULL is acceptable here.
    want_sig: The signature to be matched.
    right:    The ast which has a result to be converted to want_sig.

    Returns 1 if successful, 0 otherwise.
    Caveats:
    * This may rewrite right's result if it succeeds. */
static int type_matchup(lily_emit_state *emit, lily_sig *self,
        lily_sig *want_sig, lily_ast *right)
{
    int ret = 0;

    /* If it's just a template, see if the type that the template wants is
       known. If it is, do a lookup using the replaced value. This makes it
       so that defaulting to any works with templates.
       Example: [1, 1.1].append(10)
       This can't be done in template_check, because defaulting doesn't work
       inside of another type. */
    if (want_sig->cls->id == SYM_CLASS_TEMPLATE) {
        int index = emit->sig_stack_pos + want_sig->template_pos;
        if (emit->sig_stack[index] != NULL)
            want_sig = emit->sig_stack[index];
    }

    if (want_sig->cls->id == SYM_CLASS_ANY) {
        emit_any_assign(emit, right);
        ret = 1;
    }
    else if (template_check(emit, want_sig, right->result->sig))
        ret = 1;
    else if ((right->tree_type == tree_list &&
         want_sig->cls->id == SYM_CLASS_LIST &&
         want_sig->siglist[0]->cls->id == SYM_CLASS_ANY)
        ||
        (right->tree_type == tree_hash &&
         want_sig->cls->id == SYM_CLASS_HASH &&
         want_sig->siglist[0] == right->result->sig->siglist[0] &&
         want_sig->siglist[1]->cls->id == SYM_CLASS_ANY)) {
        /* tree_list: Want list[any], have list[!any]
           tree_hash: Want hash[?, any], have hash[?, !any] */
        /* In either case, convert each of the values to anys, then rewrite the
           build to pump out the right resulting type. */
        lily_function_val *f = emit->top_function;
        int element_count = right->args_collected;
        lily_storage *s = get_storage(emit, want_sig, right->line_num);

        /* WARNING: This is only safe because the tree was just evaluated and
           nothing has happened since the o_build_* was written. */
        f->pos = f->pos - (4 + element_count);

        int build_op;
        if (right->tree_type == tree_list) {
            build_op = o_build_list;
            emit_list_values_to_anys(emit, right);
        }
        else {
            build_op = o_build_hash;
            emit_hash_values_to_anys(emit, right);
        }

        write_build_op(emit, build_op, right->arg_start, right->line_num,
                right->args_collected, s->reg_spot);

        right->result = (lily_sym *)s;
        ret = 1;
    }
    else if (right->tree_type == tree_tuple &&
             want_sig->cls->id == SYM_CLASS_TUPLE &&
             right->result->sig->siglist_size == want_sig->siglist_size) {
        lily_function_val *f = emit->top_function;
        int element_count = right->args_collected;
        /* Tuple is a bit strange: Each entry can have a different type. So
           only put the values into an 'any' if an 'any' is wanted. */
        ret = 1;
        int i;
        lily_ast *arg;

        /* WARNING: This is only safe because the tree was just evaluated and
           nothing has happened since the o_build_* was written. */
        f->pos = f->pos - (4 + element_count);

        for (i = 0, arg = right->arg_start;
             i < want_sig->siglist_size;
             i++, arg = arg->next_arg) {
            lily_sig *want = want_sig->siglist[i];
            lily_sig *have = right->result->sig->siglist[i];
            if (want != have) {
                if (want->cls->id == SYM_CLASS_ANY)
                    emit_any_assign(emit, arg);
                else {
                    ret = 0;
                    break;
                }
            }
        }

        if (ret == 1) {
            lily_storage *s = get_storage(emit, want_sig, right->line_num);

            write_build_op(emit, o_build_tuple, right->arg_start, right->line_num,
                    right->args_collected, s->reg_spot);
            right->result = (lily_sym *)s;
        }
    }

    return ret;
}

/* This walks an ast of type tree_list, which indicates a static list to be
   build (ex: x = [1, 2, 3, 4...] or x = ["1", "2", "3", "4"]).
   The values start in ast->arg_start, and end when ->next_arg is NULL.
   There are a few caveats:
   * Lists where all values are the same type are created as lists of that type.
   * If any list value is different, then the list values are cast to any,
     and the list's type is set to any.
   * Empty lists have a type specified in them, like x = [string]. This can be
     a complex type, if wanted. These lists will have 0 args and ->sig set to
     the sig specified. */
static void eval_build_list(lily_emit_state *emit, lily_ast *ast)
{
    lily_sig *elem_sig = NULL;
    lily_ast *arg;
    int make_anys;

    make_anys = 0;

    /* Walk through all of the list elements, keeping a note of the class
       of the results. The class of the list elements is determined as
       follows:
       * If all results have the same class, then use that class.
       * If they do not, use any. */
    for (arg = ast->arg_start;arg != NULL;arg = arg->next_arg) {
        if (arg->tree_type != tree_local_var)
            eval_tree(emit, arg);

        if (elem_sig != NULL) {
            if (arg->result->sig != elem_sig)
                make_anys = 1;
        }
        else
            elem_sig = arg->result->sig;
    }

    /* elem_sig is only null if the list is empty, and empty lists have a sig
       specified at ast->sig. */
    if (elem_sig == NULL)
        elem_sig = ast->sig;

    if (make_anys) {
        lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_ANY);
        elem_sig = cls->sig;
        emit_list_values_to_anys(emit, ast);
    }

    if ((emit->sig_stack_pos + 1) > emit->sig_stack_size)
        grow_sig_stack(emit);

    lily_class *list_cls = lily_class_by_id(emit->symtab, SYM_CLASS_LIST);
    emit->sig_stack[emit->sig_stack_pos] = elem_sig;
    lily_sig *new_sig = lily_build_ensure_sig(emit->symtab, list_cls, 1,
                emit->sig_stack, emit->sig_stack_pos);

    lily_storage *s = get_storage(emit, new_sig, ast->line_num);

    write_build_op(emit, o_build_list, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

/*  eval_build_tuple */
static void eval_build_tuple(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->args_collected == 0) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "Cannot create an empty tuple.\n");
    }

    int i;
    lily_ast *arg;

    if ((emit->sig_stack_pos + ast->args_collected) > emit->sig_stack_size)
        grow_sig_stack(emit);

    /* Tuple lets the args be whatever they want to be. :) */
    for (i = 0, arg = ast->arg_start;
         arg != NULL;
         i++, arg = arg->next_arg) {
        if (arg->tree_type != tree_local_var)
            eval_tree(emit, arg);

        emit->sig_stack[emit->sig_stack_pos + i] = arg->result->sig;
    }

    lily_class *tuple_cls = lily_class_by_id(emit->symtab, SYM_CLASS_TUPLE);
    lily_sig *new_sig = lily_build_ensure_sig(emit->symtab, tuple_cls, i,
                emit->sig_stack, emit->sig_stack_pos);
    lily_storage *s = get_storage(emit, new_sig, ast->line_num);

    write_build_op(emit, o_build_tuple, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

/* eval_subscript
   This handles an ast of type tree_subscript. The arguments for the subscript
   start at ->arg_start.
   Arguments are: var, index, value */
static void eval_subscript(lily_emit_state *emit, lily_ast *ast)
{
     lily_ast *var_ast = ast->arg_start;
    lily_ast *index_ast = var_ast->next_arg;
    if (var_ast->tree_type != tree_var)
        eval_tree(emit, var_ast);

    lily_literal *tuple_literal = NULL;

    if (index_ast->tree_type != tree_local_var) {
        if (index_ast->tree_type == tree_readonly &&
            var_ast->result->sig->cls->id == SYM_CLASS_TUPLE) {
            /* Save the literal before evaluating the tree wipes it out. This
               will be used later to determine what the result of the subscript
               should actually be. */
            tuple_literal = (lily_literal *)index_ast->result;
        }
        eval_tree(emit, index_ast);
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

/* check_call_args
   This is used by eval_call to verify that the given arguments are all correct.
   This handles walking all of the arguments given, as well as verifying that
   the argument count is correct. For function varargs, this will pack the extra
   arguments into a list. */
static void check_call_args(lily_emit_state *emit, lily_ast *ast,
        lily_sig *call_sig)
{
    lily_ast *arg = ast->arg_start;
    int have_args, i, is_varargs, num_args;
    lily_sig *self_sig = NULL;

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
    if (template_adjust) {
        if (emit->sig_stack_pos + template_adjust >= emit->sig_stack_size)
            grow_sig_stack(emit);

        for (i = 0;i < template_adjust;i++)
            emit->sig_stack[emit->sig_stack_pos + i] = NULL;
    }

    if ((is_varargs && (have_args <= num_args)) ||
        (is_varargs == 0 && (have_args != num_args)))
        bad_num_args(emit, ast, call_sig);

    for (i = 0;i != num_args;arg = arg->next_arg, i++) {
        if (arg->tree_type != tree_local_var) {
            /* Walk the subexpressions so the result gets calculated. */
            emit->sig_stack_pos += template_adjust;
            eval_tree(emit, arg);
            emit->sig_stack_pos -= template_adjust;
        }

        if (i == 0)
            self_sig = arg->result->sig;

        if (arg->result->sig != call_sig->siglist[i + 1] &&
            type_matchup(emit, self_sig, call_sig->siglist[i+1], arg) == 0) {
            bad_arg_error(emit, ast, arg->result->sig, call_sig->siglist[i + 1],
                          i);
        }
    }

    if (is_varargs) {
        lily_sig *va_comp_sig = call_sig->siglist[i + 1];
        lily_ast *save_arg = arg;
        lily_sig *save_sig;

        /* varargs is handled by shoving the excess into a list. The elements
           need to be the type of what the list holds. */
        save_sig = va_comp_sig;
        va_comp_sig = va_comp_sig->siglist[0];

        for (;arg != NULL;arg = arg->next_arg) {
            if (arg->tree_type != tree_local_var) {
                /* Walk the subexpressions so the result gets calculated. */
                emit->sig_stack_pos += template_adjust;
                eval_tree(emit, arg);
                emit->sig_stack_pos -= template_adjust;
            }

            if (arg->result->sig != va_comp_sig &&
                type_matchup(emit, self_sig, va_comp_sig, arg) == 0) {
                bad_arg_error(emit, ast, arg->result->sig, va_comp_sig, i);
            }
        }

        i = (have_args - i);
        lily_storage *s;
        s = get_storage(emit, save_sig, ast->line_num);

        write_build_op(emit, o_build_list, save_arg, save_arg->line_num, i,
                s->reg_spot);

        /* Fix the ast so that it thinks the vararg list is the last value. */
        save_arg->result = (lily_sym *)s;
        save_arg->next_arg = NULL;
        ast->args_collected = num_args + 1;
    }
}

/* eval_call
   This walks an ast of type tree_call. The arguments start at ast->arg_start.
   If the call was to a known var at parse-time, then ast->result will be that
   var. Otherwise, the value to call is the first 'argument', which will need
   to be evaluated. */
static void eval_call(lily_emit_state *emit, lily_ast *ast)
{
     int expect_size, i;
    lily_ast *arg;
    lily_sig *call_sig;
    lily_sym *call_sym;

    if (ast->result == NULL) {
        int cls_id;
        /* This occurs when the function is obtained in some indirect way,
           such as a call from a subscript.
           Ex: function_list[0]()
           First, walk the subscript to get the storage that the call will
           go to. */
        eval_tree(emit, ast->arg_start);

        /* Set the result, because things like having a result to use.
           Ex: An empty list used as an arg may want to know what to
           default to. */
        ast->result = ast->arg_start->result;

        /* Make sure the result is callable (ex: NOT @(integer: 10) ()). */
        cls_id = ast->result->sig->cls->id;
        if (cls_id != SYM_CLASS_FUNCTION)
            lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                    "Cannot anonymously call resulting type '%T'.\n",
                    ast->result->sig);

        if (ast->arg_start->tree_type != tree_oo_call) {
            /* Then drop it from the arg list, since it's not an arg. */
            ast->arg_start = ast->arg_start->next_arg;
            ast->args_collected--;
        }
        else {
            /* Fix the oo call to return the first arg it had, since that's the
               call's first value. It's really important that check_call_args
               get all the args, because the first is the most likely to have a
               template parameter. */
            ast->arg_start->result = ast->arg_start->arg_start->result;
        }
    }

    call_sym = ast->result;
    call_sig = call_sym->sig;
    arg = ast->arg_start;
    expect_size = 6 + ast->args_collected;

    check_call_args(emit, ast, call_sig);

    if (GLOBAL_LOAD_CHECK(call_sym)) {
        lily_storage *storage = get_storage(emit, call_sym->sig, ast->line_num);

        write_4(emit,
                o_get_global,
                ast->line_num,
                call_sym->reg_spot,
                storage->reg_spot);

        call_sym = (lily_sym *)storage;
    }

    write_prep(emit, expect_size);

    lily_function_val *f = emit->top_function;
    f->code[f->pos] = o_function_call;
    f->code[f->pos+1] = ast->line_num;

    if (call_sym->flags & VAR_IS_READONLY) {
        f->code[f->pos+2] = 1;
        f->code[f->pos+3] = (uintptr_t)call_sym;
    }
    else {
        f->code[f->pos+2] = 0;
        f->code[f->pos+3] = call_sym->reg_spot;
    }

    f->code[f->pos+4] = ast->args_collected;

    for (i = 5, arg = ast->arg_start;
        arg != NULL;
        arg = arg->next_arg, i++) {
        f->code[f->pos + i] = arg->result->reg_spot;
    }

    if (call_sig->siglist[0] != NULL) {
        lily_sig *return_sig = call_sig->siglist[0];

        /* If the return is a template, use self (the first arg) to determine
           what the result should -really- be.
           Note! This won't work for more complex templated types. There are
           none of those now, so this works until then. */
        if (return_sig->cls->id == SYM_CLASS_TEMPLATE) {
            /* Grab self again, via the first argument. */
            lily_sig *first_arg_result = ast->arg_start->result->sig;
            /* Now grab out the wanted template position. */
            return_sig = first_arg_result->siglist[return_sig->template_pos];
        }
        else if (return_sig->template_pos != 0)
            return_sig = build_untemplated_sig(emit, return_sig);

        lily_storage *storage = get_storage(emit, return_sig, ast->line_num);

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
                    "Call returning nil not at end of expression.", "");
        }
        f->code[f->pos+i] = -1;
    }

    f->pos += 6 + ast->args_collected;
}

/* emit_nonlocal_var
   This handles vars that are not local and are on the right hand side of an
   expression. This handles loading both literals and globals into a local
   register. */
static void emit_nonlocal_var(lily_emit_state *emit, lily_ast *ast)
{
    lily_storage *ret;
    int opcode;
    uintptr_t value_spot;

    if (ast->tree_type == tree_readonly) {
        opcode = o_get_const;
        value_spot = (uintptr_t)ast->result;
    }
    else if (ast->result->flags & SYM_TYPE_VAR) {
        opcode = o_get_global;
        value_spot = ast->result->reg_spot;
    }
    else {
        opcode = -1;
        value_spot = 0;
    }

    ret = get_storage(emit, ast->result->sig, ast->line_num);

    write_4(emit,
            opcode,
            ast->line_num,
            value_spot,
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

/*  eval_oo_call
    This is a 'abc.xyz' sort of call. This ast's arg_start is the 'abc' part
    of the call. Eval this, then use the resulting signature and the ast's
    oo_pool_index to do a lookup (for the xyz part). This yields the found var
    as a result.
    eval_call takes the result as the var to call, then patches it later to
    have the ast arg_start's result for emitting.
    Note: This tree's arg_start is the first argument of the call, but is
          never type-checked. This isn't considered a problem, because the
          first argument of a class callable always takes self.
          Have you ever seen a class function NOT take the class as the first
          argument? Exactly. */
static void eval_oo_call(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->result)
        return;

    if (ast->arg_start->tree_type != tree_local_var)
        eval_tree(emit, ast->arg_start);

    char *oo_name = emit->oo_name_pool->str + ast->oo_pool_index;
    lily_var *var = lily_find_class_callable(emit->symtab,
            ast->arg_start->result->sig->cls, oo_name);

    if (var == NULL) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "Class %s has no callable named %s.\n",
                ast->arg_start->result->sig->cls->name,
                oo_name);
    }

    ast->result = (lily_sym *)var;
}

/*  eval_isnil
    Eval a special tree representing the 'isnil' keyword. This tree has one
    value: The inner tree swallowed. For speed, the code emitted will select
    either a global or local register at vm-time. This may or may not be
    changed in the future.
    * ast->arg_start is the one and only expression to evaluate. */
static void eval_isnil(lily_emit_state *emit, lily_ast *ast)
{
     lily_ast *inner_tree = ast->arg_start;

    if (ast->args_collected != 1)
        lily_raise(emit->raiser, lily_SyntaxError,
                "isnil expects 1 arg, but got %d args.\n", ast->args_collected);

    if (inner_tree->tree_type != tree_local_var &&
        inner_tree->tree_type != tree_var)
        eval_tree(emit, inner_tree);

    int is_global = (inner_tree->tree_type == tree_var);

    lily_class *integer_cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);
    lily_storage *s = get_storage(emit, integer_cls->sig, ast->line_num);

    write_5(emit, o_isnil, ast->line_num, is_global, inner_tree->result->reg_spot,
            s->reg_spot);
    ast->result = (lily_sym *)s;
}

/* eval_tree
   This is the main emit function. This doesn't evaluate anything itself, but
   instead determines what call to shove the work off to. */
static void eval_tree(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->tree_type == tree_var || ast->tree_type == tree_readonly)
        emit_nonlocal_var(emit, ast);
    else if (ast->tree_type == tree_call)
        eval_call(emit, ast);
    else if (ast->tree_type == tree_binary) {
        if (ast->op >= expr_assign) {
            if (ast->left->tree_type != tree_subscript &&
                ast->left->tree_type != tree_package)
                eval_assign(emit, ast);
            else if (ast->left->tree_type == tree_package)
                eval_package_assign(emit, ast);
            else if (ast->left->tree_type == tree_subscript)
                eval_sub_assign(emit, ast);
        }
        else if (ast->op == expr_logical_or || ast->op == expr_logical_and)
            eval_logical_op(emit, ast);
        else {
            if (ast->left->tree_type != tree_local_var)
                eval_tree(emit, ast->left);

            if (ast->right->tree_type != tree_local_var)
                eval_tree(emit, ast->right);

            emit_binary_op(emit, ast);
        }
    }
    else if (ast->tree_type == tree_parenth) {
        if (ast->arg_start->tree_type != tree_local_var)
            eval_tree(emit, ast->arg_start);

        ast->result = ast->arg_start->result;
    }
    else if (ast->tree_type == tree_unary) {
        if (ast->left->tree_type != tree_local_var)
            eval_tree(emit, ast->left);

        eval_unary_op(emit, ast);
    }
    else if (ast->tree_type == tree_list)
        eval_build_list(emit, ast);
    else if (ast->tree_type == tree_hash)
        eval_build_hash(emit, ast);
    else if (ast->tree_type == tree_tuple)
        eval_build_tuple(emit, ast);
    else if (ast->tree_type == tree_subscript)
        eval_subscript(emit, ast);
    else if (ast->tree_type == tree_package)
        eval_package(emit, ast);
    else if (ast->tree_type == tree_typecast)
        eval_typecast(emit, ast);
    else if (ast->tree_type == tree_oo_call)
        eval_oo_call(emit, ast);
    else if (ast->tree_type == tree_isnil)
        eval_isnil(emit, ast);
}

/** Emitter API functions **/

/*  lily_emit_expr
    This evaluates the root of the ast pool given (the expression), then clears
    the pool for the next expression. */
void lily_emit_eval_expr(lily_emit_state *emit, lily_ast_pool *ap)
{
    eval_tree(emit, ap->root);
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

    eval_tree(emit, ast);
    emit->expr_num++;

    if (ast->result->sig->cls->id != SYM_CLASS_INTEGER) {
        lily_raise(emit->raiser, lily_SyntaxError,
                   "Expected type 'integer', but got type '%T'.\n",
                   ast->result->sig);
    }

    /* Note: This works because the only time this is called is to handle
             for..in range expressions, which are always integers. */
    write_4(emit,
            o_assign,
            ast->line_num,
            ast->result->reg_spot,
            var->reg_spot);

    lily_ast_reset_pool(ap);
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

/*  lily_emit_eval_condition
    This function evaluates an ast that will decide if a block should be
    entered. This will write o_jump_if_false which will jump to the next
    branch or outside the block if the ast's result is false.

    This is suitable for 'if', 'elif', and 'while'.

    This clears the ast pool for the next pass. */
void lily_emit_eval_condition(lily_emit_state *emit, lily_ast_pool *ap)
{
    lily_ast *ast = ap->root;

    /* This does emitting for the condition of an if or elif. */
    eval_tree(emit, ast);
    emit->expr_num++;

    /* Sometimes, there won't be a result. This happens when there's a
       condition that returns nil, as one example. Make sure there is a
       result to check. */
    if (ast->result == NULL)
        lily_raise(emit->raiser, lily_SyntaxError,
                   "Conditional expression has no value.\n");

    /* If the expression is false, then jump to a future location. This will
       get patched to jump to the end of this block. For 'if'/'elif', it will
       go to the next 'elif' or the 'else'. For 'while', this will result in
       the block being skipped. */
    lily_emit_jump_if(emit, ast, 0);

    lily_ast_reset_pool(ap);
}

/*  lily_emit_eval_do_while_expr
    This handles evaluates an ast that will decide if a jump to the top of the
    current loop should be executed. This will write o_jump_if_true which will
    jump back up to the top of the loop on success.

    This is suitable for 'do...while'.

    This clears the ast pool for the next pass. */
void lily_emit_eval_do_while_expr(lily_emit_state *emit, lily_ast_pool *ap)
{
    lily_ast *ast = ap->root;

    eval_tree(emit, ast);
    emit->expr_num++;

    if (ast->result == NULL)
        lily_raise(emit->raiser, lily_SyntaxError,
                   "Conditional expression has no value.\n");

     /* If it passes, go back up to the top. Otherwise, fall out of the loop. */
    write_4(emit,
            o_jump_if,
            1,
            ast->result->reg_spot,
            emit->current_block->loop_start);

    lily_ast_reset_pool(ap);
}

/* lily_emit_continue
   This emits a jump to go back up to the top of a while. Since this is called
   by parser, it also checks that emitter is in a while. */
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

/* lily_emit_change_if_branch
   This is called when an if or elif jump has finished, and a new branch is to
   begin. have_else indicates if it's an else branch or an elif branch. */
void lily_emit_change_if_branch(lily_emit_state *emit, int have_else)
{
    int save_jump;
    lily_block *block = emit->current_block;
    lily_var *v = block->var_start;

    if (emit->current_block->block_type != BLOCK_IF &&
        emit->current_block->block_type != BLOCK_IFELSE) {
        char *name = (have_else ? "else" : "elif");
        lily_raise(emit->raiser, lily_SyntaxError,
                   "'%s' without 'if'.\n", name);
    }

    if (have_else) {
        if (block->block_type == BLOCK_IFELSE)
            lily_raise(emit->raiser, lily_SyntaxError,
                       "Only one 'else' per 'if' allowed.\n");
        else
            block->block_type = BLOCK_IFELSE;
    }
    else if (block->block_type == BLOCK_IFELSE)
        lily_raise(emit->raiser, lily_SyntaxError, "'elif' after 'else'.\n");

    if (v->next != NULL)
        lily_hide_block_vars(emit->symtab, v);

    lily_function_val *f = emit->top_function;

    /* Write an exit jump for this branch, thereby completing the branch. */
    write_2(emit, o_jump, 0);
    save_jump = f->pos - 1;

    /* The last jump actually collected wanted to know where the next branch
       would start. It's where the code is now. */
    f->code[emit->patches[emit->patch_pos-1]] = f->pos;
    /* Write the exit jump where the branch jump was. Since the branch jump got
       patched, storing the location is pointless. */
    emit->patches[emit->patch_pos-1] = save_jump;
}

/* emit_final_continue
   This writes in a 'continue'-type jump at the end of the while, so that the
   while runs multiple times. Since the while is definitely the top block, this
   is a bit simpler. */
static void emit_final_continue(lily_emit_state *emit)
{
    write_2(emit, o_jump, emit->current_block->loop_start);
}

/* lily_emit_jump_if
   This writes a conditional jump using the result of the given ast. The type
   of jump (true/false) depends on 'jump_on'.
   1: jump_if_true: The jump will be performed if the ast's result is true or
      non-zero.
   0: jump_if_false: The jump will be performed if the ast's result is zero. */
void lily_emit_jump_if(lily_emit_state *emit, lily_ast *ast, int jump_on)
{
    write_4(emit, o_jump_if, jump_on, ast->result->reg_spot, 0);
    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    lily_function_val *f = emit->top_function;
    emit->patches[emit->patch_pos] = f->pos-1;
    emit->patch_pos++;
}

/* lily_emit_return
   This writes a return statement for a function. This also checks that the
   value given matches what the function says it returns. */
void lily_emit_return(lily_emit_state *emit, lily_ast *ast, lily_sig *ret_sig)
{
    eval_tree(emit, ast);
    emit->expr_num++;

    if (ast->result->sig != ret_sig) {
        if (ret_sig->cls->id == SYM_CLASS_ANY)
            emit_any_assign(emit, ast);
        else {
            lily_raise_adjusted(emit->raiser, ast->line_num, lily_SyntaxError,
                    "return expected type '%T' but got type '%T'.\n",
                    ret_sig, ast->result->sig);
        }
    }

    write_pop_inner_try_blocks(emit);

    write_3(emit, o_return_val, ast->line_num, ast->result->reg_spot);
}

void lily_emit_raise(lily_emit_state *emit, lily_ast *ast)
{
    eval_tree(emit, ast);
    emit->expr_num++;

    if (ast->result == NULL)
        lily_raise(emit->raiser, lily_SyntaxError,
                   "raise expression has no value.\n");

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

/* lily_emit_show
   This evals the given ast, then writes o_show so the vm will show the result
   of the ast. Type-checking is intentionally NOT performed. */
void lily_emit_show(lily_emit_state *emit, lily_ast *ast)
{
    int is_global = (ast->tree_type == tree_var);

    /* Don't eval if it's a global var and nothing more. This makes it so
       globals show with their name and their proper register. Otherwise, the
       global gets loaded into a local storage, making show a bit less helpful.
       This also makes sure that global and local vars are treated consistently
       by show. */
    if (is_global == 0)
        eval_tree(emit, ast);

    if (ast->result == NULL)
        lily_raise(emit->raiser, lily_SyntaxError,
                   "show expression has no value.\n");

    emit->expr_num++;

    write_4(emit, o_show, ast->line_num, is_global, ast->result->reg_spot);
}

/* lily_emit_return_noval
   This writes the o_return_noval opcode for a function to return without sending
   a value to the caller. */
void lily_emit_return_noval(lily_emit_state *emit)
{
    /* Don't allow 'return' within __main__. */
    if (emit->function_depth == 1)
        lily_raise(emit->raiser, lily_SyntaxError,
                "'return' used outside of a function.\n");

    write_pop_inner_try_blocks(emit);

    write_2(emit, o_return_noval, *emit->lex_linenum);
}

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

void lily_emit_except(lily_emit_state *emit, lily_class *cls,
        lily_var *except_var, int line_num)
{
    if (emit->current_block->block_type != BLOCK_TRY) {
        lily_raise(emit->raiser, lily_SyntaxError,
                "'except' not within 'try' block.\n");
    }

    lily_sig *except_sig = cls->sig;
    lily_sym *except_sym = (lily_sym *)except_var;
    if (except_sym == NULL)
        except_sym = (lily_sym *)get_storage(emit, except_sig, line_num);

    /* This is done so that if the 'try' doesn't have anything that raises
       that the vm unregisters the 'try' block. */
    if (emit->current_block->block_type == BLOCK_TRY) {
        write_1(emit, o_pop_try);
        emit->current_block->block_type = BLOCK_TRY_EXCEPT;
    }

    /* Write a jump that will go to the end of the try. */
    write_2(emit, o_jump, 0);
    lily_function_val *f = emit->top_function;
    int save_jump = f->pos - 1;

    /* The last jump is always wanting to know where the next except block
       starts. That's now known.
       However, since that block is done, it now needs to know where the end
       of the try block is. */
    f->code[emit->patches[emit->patch_pos - 1]] = f->pos;
    emit->patches[emit->patch_pos - 1] = save_jump;

    write_5(emit,
            o_except,
            line_num,
            0,
            (except_var != NULL),
            except_sym->reg_spot);

    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    emit->patches[emit->patch_pos] = f->pos - 3;
    emit->patch_pos++;
}

/* add_var_chain_to_info
   This adds a chain of vars to a function's info. If not __main__, then functions
   declared within the currently-exiting function are skipped. */
static void add_var_chain_to_info(lily_emit_state *emit,
        lily_register_info *info, char *class_name, lily_var *var)
{
    while (var) {
        if ((var->flags & VAR_IS_READONLY) == 0) {
            info[var->reg_spot].sig = var->sig;
            info[var->reg_spot].name = var->name;
            info[var->reg_spot].class_name = class_name;
            info[var->reg_spot].line_num = var->line_num;
        }

        var = var->next;
    }
}

/* add_storage_chain_to_info
   This adds the storages that a function has used to the function's info. */
static void add_storage_chain_to_info(lily_register_info *info,
        lily_storage *storage)
{
    while (storage && storage->sig) {
        info[storage->reg_spot].sig = storage->sig;
        info[storage->reg_spot].name = NULL;
        info[storage->reg_spot].class_name = NULL;
        info[storage->reg_spot].line_num = -1;
        storage = storage->next;
    }
}

/* finalize_function_val
   A function is closing (or __main__ is about to be called). Since this function is
   done, prepare the reg_info part of it. This will be used to allocate the
   registers it needs at vm-time. */
static void finalize_function_val(lily_emit_state *emit, lily_block *function_block)
{
    int register_count = emit->symtab->next_register_spot;
    lily_var *var_iter;
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

    var_iter = function_block->function_var;

    /* Don't include functions inside of themselves... */
    if (emit->function_depth > 1)
        var_iter = var_iter->next;
    /* else we're in __main__, which does include itself as an arg so it can be
       passed to show and other neat stuff. */

    add_var_chain_to_info(emit, info, NULL, var_iter);
    add_storage_chain_to_info(info, function_block->storage_start);

    if (emit->function_depth > 1) {
        /* todo: Reuse the var shells instead of destroying. Seems petty, but
                 malloc isn't cheap if there are a lot of vars. */
        lily_var *var_temp;
        while (var_iter) {
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
    else {
        /* If __main__, add class functions like string::concat and all global
           vars. */
        int i;
        for (i = 0;i < emit->symtab->class_pos;i++) {
            lily_class *cls = emit->symtab->classes[i];
            if (cls->call_start)
                add_var_chain_to_info(emit, info, cls->name, cls->call_start);
        }
    }

    f->reg_info = info;
    f->reg_count = register_count;
}

/* lily_emit_vm_return
   This writes the o_vm_return opcode at the end of the __main__ function. */
void lily_emit_vm_return(lily_emit_state *emit)
{
    finalize_function_val(emit, emit->current_block);
    write_1(emit, o_return_from_vm);
}

/* lily_reset_main
   This resets the code position of __main__, so it can receive new code. */
void lily_reset_main(lily_emit_state *emit)
{
    ((lily_function_val *)emit->top_function)->pos = 0;
}

/** Block entry/exit **/

/* lily_emit_enter_block
   Enter a block of the given block_type. It will try to use an existing block
   if able, or create a new one if it cannot.
   For functions, top_function_ret is not set (because this is called when a
   function var is seen), and must be patched later.
   Note that this will call lily_raise_nomem if unable to create a block. */
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
    new_block->var_start = emit->symtab->var_top;

    if (block_type != BLOCK_FUNCTION) {
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
        lily_var *v = emit->symtab->var_top;
        v->value.function = lily_try_new_native_function_val(v->name);
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

static void leave_function(lily_emit_state *emit, lily_block *block)
{
    /* If the function returns nil, write an implicit 'return' at the end of
       it. It's easiest to just blindly write it. */
    if (emit->top_function_ret == NULL)
        lily_emit_return_noval(emit);
    else
        /* Ensure that functions that claim to return a value cannot leave without
           doing so. */
        emit_return_expected(emit);

    finalize_function_val(emit, block);

    /* Warning: This assumes that only functions can contain other functions. */
    lily_var *v = block->prev->function_var;

    /* If this function has no storages, then it can use the ones from the function
       that just exited. This reuse cuts down on a lot of memory. */
    if (block->prev->storage_start == NULL)
        block->prev->storage_start = emit->unused_storage_start;

    emit->symtab->var_top = block->function_var;
    block->function_var->next = NULL;
    emit->symtab->function_depth--;
    emit->symtab->next_register_spot = block->save_register_spot;
    emit->top_function = v->value.function;
    emit->top_var = v;
    emit->top_function_ret = v->sig->siglist[0];
    emit->function_depth--;
}

/* lily_emit_leave_block
   This closes the last block that was added to the emitter. Any vars that were
   added in the block are dropped. */
void lily_emit_leave_block(lily_emit_state *emit)
{
    lily_var *v;
    lily_block *block;
    int block_type;

    if (emit->current_block->prev == NULL)
        lily_raise(emit->raiser, lily_SyntaxError, "'}' outside of a block.\n");

    block = emit->current_block;
    block_type = block->block_type;

    /* Write in a fake continue so that the end of a while jumps back up to the
       top. */
    if (block_type == BLOCK_WHILE || block_type == BLOCK_FOR_IN)
        emit_final_continue(emit);

    v = block->var_start;

    if (block_type != BLOCK_FUNCTION) {
        int from, to, pos;
        from = emit->patch_pos-1;
        to = block->patch_start;
        pos = emit->top_function->pos;

        for (;from >= to;from--)
            emit->top_function->code[emit->patches[from]] = pos;

        /* Use the space for new patches now. */
        emit->patch_pos = to;

        lily_hide_block_vars(emit->symtab, v);
    }
    else
        leave_function(emit, block);

    emit->current_block = emit->current_block->prev;
}

/* lily_emit_try_enter_main
   Attempt to create a block representing __main__, then enter it. main_var is
   the var representing __main__. Returns 1 on success, or 0 on failure.
   Emitter hasn't had the symtab set, which is why the var must be sent. */
int lily_emit_try_enter_main(lily_emit_state *emit, lily_var *main_var)
{
    lily_block *main_block = try_new_block();
    if (main_block == NULL)
        return 0;

    main_var->value.function = lily_try_new_native_function_val(main_var->name);
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
    emit->top_function = main_var->value.function;
    emit->top_var = main_var;
    emit->current_block = main_block;
    emit->function_depth++;
    return 1;
}

/* lily_emit_finalize_for_in
   This function takes the symbols used in a for..in loop and writes out the
   appropriate code to start off a for loop. This should be done at the very end
   of a for..in loop, after the 'by' expression has been collected.
   * user_loop_var: This is the user var that will have the range value written
                    to it.
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

    write_prep(emit, 16);
    lily_function_val *f = emit->top_function;
    f->code[f->pos  ] = o_for_setup;
    f->code[f->pos+1] = line_num;
    f->code[f->pos+2] = user_loop_var->reg_spot;
    f->code[f->pos+3] = for_start->reg_spot;
    f->code[f->pos+4] = for_end->reg_spot;
    f->code[f->pos+5] = for_step->reg_spot;
    /* This value is used to determine if the step needs to be calculated. */
    f->code[f->pos+6] = (uintptr_t)!have_step;

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
    f->code[f->pos+11] = user_loop_var->reg_spot;
    f->code[f->pos+12] = for_start->reg_spot;
    f->code[f->pos+13] = for_end->reg_spot;
    f->code[f->pos+14] = for_step->reg_spot;
    f->code[f->pos+15] = 0;

    f->pos += 16;

    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    emit->patches[emit->patch_pos] = f->pos-1;
    emit->patch_pos++;
}

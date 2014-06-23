#include <string.h>

#include "lily_impl.h"
#include "lily_ast.h"
#include "lily_value.h"
#include "lily_emitter.h"
#include "lily_opcode.h"
#include "lily_emit_table.h"

/** Emitter is responsible for:
    * Taking an ast and writing instructions out to a given method value.
    * Holding decision branch (if, elif, else, etc.) information for the
      parser.
    * Holding information about what methods that parser has entered.
    * Creating 'blocks' to represent the blocks that the parser enters. These
      blocks hold information like the last variable before block entry, the
      type of block, and more.
    * Handling the deletion of vars when a block goes out of scope.
    * A block is any if, elif, etc. Blocks are created
    * Writing the proper line number of an error to raiser's line_adjust if
      emitter is walking a tree.

    How do if/elif/else work?
    * o_jump and o_jump_if are used to perform jumps to some location in the
      future. They are initially written with a location of 0.
    * The location of the 0 is added to an array of patches.
    * When the first 'if' is discovered, it saves where the patches are.
    * At the start of a branch (after the : is discovered after the statement),
      a branch jump is created. When the next branch starts, that branch jump
      will be patched with the start of the next branch. This is what makes each
      branch jump to the next when the condition is met.
    * At the end of a branch, an exit jump is written that will be patched when
      the if is done.
    * Additionally, and/or create blocks so that all of their jumps will go to
      one location on failure. This allows and/or short-circuiting (stopping on
      the first or to succeed or and to fail).

    eval vs. emit:
    * Functions with 'eval' in their name will run through a given ast and write
      the appropriate code. Each ast should only be eval'd once, which is easy
      to do with a top-down structure.
    * Functions with 'emit' in their name will write code, but assume that the
      eval has already been done. These functions are often used as helpers for
      the eval functions. **/

#define WRITE_PREP(size) \
if ((m->pos + size) > m->len) { \
    uintptr_t *save_code; \
    m->len *= 2; \
    save_code = lily_realloc(m->code, sizeof(uintptr_t) * m->len); \
    if (save_code == NULL) \
        lily_raise_nomem(emit->raiser); \
    m->code = save_code; \
}

/* Most ops need 4 or less code spaces, so only growing once is okay. However,
   calls need 5 + #args. So more than one grow might be necessary.
   Note: This macro may need to check for overflow later. */
#define WRITE_PREP_LARGE(size) \
if ((m->pos + size) > m->len) { \
    uintptr_t *save_code; \
    while ((m->pos + size) > m->len) \
        m->len *= 2; \
    save_code = lily_realloc(m->code, sizeof(uintptr_t) * m->len); \
    if (save_code == NULL) \
        lily_raise_nomem(emit->raiser); \
    m->code = save_code; \
}

#define WRITE_1(one) \
WRITE_PREP(1) \
m->code[m->pos] = one; \
m->pos += 1;

#define WRITE_2(one, two) \
WRITE_PREP(2) \
m->code[m->pos] = one; \
m->code[m->pos+1] = two; \
m->pos += 2;

#define WRITE_3(one, two, three) \
WRITE_PREP(3) \
m->code[m->pos] = one; \
m->code[m->pos+1] = two; \
m->code[m->pos+2] = three; \
m->pos += 3;

#define WRITE_4(one, two, three, four) \
WRITE_PREP(4) \
m->code[m->pos] = one; \
m->code[m->pos+1] = two; \
m->code[m->pos+2] = three; \
m->code[m->pos+3] = four; \
m->pos += 4;

#define WRITE_5(one, two, three, four, five) \
WRITE_PREP(5) \
m->code[m->pos] = one; \
m->code[m->pos+1] = two; \
m->code[m->pos+2] = three; \
m->code[m->pos+3] = four; \
m->code[m->pos+4] = five; \
m->pos += 5;

# define IS_LOOP_BLOCK(b) (b & (BLOCK_WHILE | BLOCK_DO_WHILE | BLOCK_FOR_IN))
# define GLOBAL_LOAD_CHECK(sym) \
    ((sym->flags & SYM_TYPE_VAR) && \
     ((lily_var *)sym)->method_depth == 1)

static int type_matchup(lily_emit_state *, lily_sig *, lily_sig *, lily_ast *);

/** Emitter init and deletion **/
lily_emit_state *lily_new_emit_state(lily_raiser *raiser)
{
    lily_emit_state *s = lily_malloc(sizeof(lily_emit_state));

    if (s == NULL)
        return NULL;

    s->patches = lily_malloc(sizeof(int) * 4);
    if (s->patches == NULL) {
        lily_free(s);
        return NULL;
    }

    s->first_block = NULL;
    s->current_block = NULL;
    s->unused_storage_start = NULL;
    s->all_storage_start = NULL;
    s->all_storage_top = NULL;

    s->patch_pos = 0;
    s->patch_size = 4;
    s->method_depth = 0;

    s->raiser = raiser;
    s->expr_num = 1;

    return s;
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_block *current, *temp;
    lily_storage *current_store, *temp_store;
    current = emit->first_block;
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

    lily_free(emit->patches);
    lily_free(emit);
}

/** Shared helper functions **/
static char *opname(lily_expr_op op)
{
    static char *opnames[] =
    {"+", "-", "==", "<", "<=", ">", ">=", "!=", "%", "*", "/", "<<", ">>", "&",
     "|", "^", "!", "-", "&&", "||", "=", "+=", "-=", "%=", "*=", "/=", "<<=",
     ">>="};

    return opnames[op];
}

/* grow_patches
   This is a helper function for resizing emitter's patch array. */
static void grow_patches(lily_emit_state *emit)
{
    emit->patch_size *= 2;

    int *new_patches = lily_realloc(emit->patches,
        sizeof(int) * emit->patch_size);

    if (new_patches == NULL)
        lily_raise_nomem(emit->raiser);

    emit->patches = new_patches;
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
    will first attempt to get an available storage in the current method, then
    try to create a new one. This has the side-effect of fixing storage_start in
    the event that storage_start is NULL. This fixup is done up to the current
    method block.

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
            /* The signature is only null if it belonged to a method that is
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
            /* Non-method blocks inherit their storage start from the method
               block that they are in. */
            if (emit->current_block->storage_start == NULL) {
                if (emit->current_block->block_type == BLOCK_METHOD)
                    /* Easy mode: Just fill in for the method. */
                    emit->current_block->storage_start = ret;
                else {
                    /* Non-method block, so keep setting storage_start until the
                       method block is reached. This will allow other blocks to
                       use this storage. This is also important because not
                       doing this causes the method block to miss this storage
                       when the method is being finalized. */
                    lily_block *block = emit->current_block;
                    while (block->block_type != BLOCK_METHOD) {
                        block->storage_start = ret;
                        block = block->prev;
                    }

                    block->storage_start = ret;
                }
            }
        }
    }

    if (ret == NULL) {
        emit->raiser->line_adjust = line_num;
        lily_raise_nomem(emit->raiser);
    }

    return ret;
}

/* emit_return_expected
   This writes o_return_expected with the current line number. This is done to
   prevent methods that should return values from not doing so. */
static void emit_return_expected(lily_emit_state *emit)
{
    lily_method_val *m = emit->top_method;

    WRITE_2(o_return_expected, *emit->lex_linenum)
}

/* find_deepest_loop
   This is used to find the deepest block that is a loop. Returns a block that
   represents a loop, or NULL if not in a loop. */
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
        else if (block->block_type == BLOCK_METHOD) {
            ret = NULL;
            break;
        }
    }

    return ret;
}

/* try_new_block
   Attempt to create a new block. Returns a new block, or NULL if unable to
   create a new block. If successful, the new
   block->next is set to NULL here too. */
static lily_block *try_new_block(void)
{
    lily_block *ret = lily_malloc(sizeof(lily_block));

    if (ret)
        ret->next = NULL;

    return ret;
}

/*  write_build_op

    This is responsible for writing the actual o_build_list or o_build_hash
    code, depending on the opcode given. The list will be put into a register
    at reg_spot, which is assumed to have the correct type to hold the given
    list.

    emit:       The emitter holding the method to write to.
    opcode:     The opcode to write: o_build_list for a list, o_build_hash for a
                hash.
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
    lily_method_val *m = emit->top_method;
    int i;
    lily_ast *arg;

    WRITE_PREP_LARGE(num_values + 4)
    m->code[m->pos] = opcode;
    m->code[m->pos+1] = first_arg->line_num;
    m->code[m->pos+2] = num_values;

    for (i = 3, arg = first_arg; arg != NULL; arg = arg->next_arg, i++)
        m->code[m->pos + i] = arg->result->reg_spot;

    m->code[m->pos+i] = reg_spot;
    m->pos += 4 + num_values;
}

/* emit_obj_assign
   The given ast is a non-object, and the caller has checked that an object is
   wanted. This converts the ast's result so that it contains an object by
   using o_obj_assign. */
static void emit_obj_assign(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->top_method;
    lily_class *obj_class = lily_class_by_id(emit->symtab, SYM_CLASS_OBJECT);
    lily_storage *storage = get_storage(emit, obj_class->sig, ast->line_num);

    WRITE_4(o_obj_assign,
            ast->line_num,
            ast->result->reg_spot,
            storage->reg_spot)

    ast->result = (lily_sym *)storage;
}

/* template_check
   lhs and rhs don't match, but see if the lhs has a part that's a template
   which rhs satisfies. In calls, self is the first argument given. */
static int template_check(lily_sig *self_sig, lily_sig *lhs, lily_sig *rhs)
{
    int ret = 0;

    if (lhs->cls->id == SYM_CLASS_LIST &&
        rhs->cls->id == SYM_CLASS_LIST) {
        ret = template_check(self_sig, lhs->siglist[0], rhs->siglist[0]);
    }
    else if (lhs->cls->id == SYM_CLASS_TEMPLATE) {
        lily_sig *comp_sig;
        comp_sig = self_sig->siglist[lhs->template_pos];
        if (comp_sig == rhs)
            ret = 1;
        else
            ret = 0;
    }
    else if (lhs->cls == rhs->cls)
        ret = 1;

    return ret;
}

/** Error helpers **/
static void bad_arg_error(lily_emit_state *emit, lily_ast *ast,
    lily_sig *got, lily_sig *expected, int arg_num)
{
    lily_var *v = (lily_var *)ast->result;

    /* Just in case this arg was on a different line than the call. */
    emit->raiser->line_adjust = ast->line_num;
    lily_raise(emit->raiser, lily_ErrSyntax,
            "%s arg #%d expects type '%T' but got type '%T'.\n",
            v->name, arg_num, expected, got);
}

static void bad_assign_error(lily_emit_state *emit, int line_num,
                          lily_sig *left_sig, lily_sig *right_sig)
{
    /* Remember that right is being assigned to left, so right should
       get printed first. */
    emit->raiser->line_adjust = line_num;
    lily_raise(emit->raiser, lily_ErrSyntax,
            "Cannot assign type '%T' to type '%T'.\n",
            right_sig, left_sig);
}

/* bad_subs_class
   Reports that the class of the value in var_ast cannot be subscripted. */
static void bad_subs_class(lily_emit_state *emit, lily_ast *var_ast)
{
    emit->raiser->line_adjust = var_ast->line_num;
    lily_raise(emit->raiser, lily_ErrSyntax, "Cannot subscript type '%T'.\n",
            var_ast->result->sig);
}

/* bad_num_args
   Reports that the ast didn't get as many args as it should have. Takes
   anonymous calls and var args into account. */
static void bad_num_args(lily_emit_state *emit, lily_ast *ast,
        lily_sig *call_sig)
{
    char *call_name;
    char *va_text;

    if (ast->result != NULL)
        call_name = ((lily_var *)ast->result)->name;
    else
        /* This occurs when the call is based off of a subscript, such as
           method_list[0]()
           This is generic, but it's assumed that this will be enough when
           paired with the line number. */
        call_name = "(anonymous call)";

    if (call_sig->flags & SIG_IS_VARARGS)
        va_text = "at least ";
    else
        va_text = "";

    emit->raiser->line_adjust = ast->line_num;
    lily_raise(emit->raiser, lily_ErrSyntax,
               "%s expects %s%d args, but got %d.\n", call_name, va_text,
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
    lily_method_val *m;
    lily_class *lhs_class, *rhs_class, *storage_class;
    lily_storage *s;

    m = emit->top_method;
    lhs_class = ast->left->result->sig->cls;
    rhs_class = ast->right->result->sig->cls;

    if (lhs_class->id <= SYM_CLASS_STR &&
        rhs_class->id <= SYM_CLASS_STR)
        opcode = generic_binop_table[ast->op][lhs_class->id][rhs_class->id];
    else {
        /* Allow == and != for any class, so long as the signatures both match.
           This allows useful things like comparing methods, functions, and
           more. */
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

    if (opcode == -1) {
        emit->raiser->line_adjust = ast->line_num;
        /* Print the full type, in case there's an attempt to do something like
           list[integer] == list[str]. */
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "Invalid operation: %T %s %T.\n", ast->left->result->sig,
                   opname(ast->op), ast->right->result->sig);
    }

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

    WRITE_5(opcode,
            ast->line_num,
            ast->left->result->reg_spot,
            ast->right->result->reg_spot,
            s->reg_spot)

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
    * This will raise lily_ErrSyntax if the ast's op is not a compound op, or
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
        lily_raise(emit->raiser, lily_ErrSyntax, "Invalid compound op: %s.\n",
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

        /* If the left is an object and the right is not, then don't reduce.
           Object assignment is written so that it puts the right side into a
           container. */
        if (ast->left->result->sig->cls->id == SYM_CLASS_OBJECT &&
            right_tree->result->sig->cls->id != SYM_CLASS_OBJECT) {
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
    lily_method_val *m = emit->top_method;
    opcode = -1;

    if (ast->left->tree_type != tree_var)
        eval_tree(emit, ast->left);

    if ((ast->left->result->flags & SYM_TYPE_VAR) == 0) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax,
                "Left side of %s is not a var.\n", opname(ast->op));
    }

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right);

    left_sym = ast->left->result;
    right_sym = ast->right->result;
    left_cls_id = left_sym->sig->cls->id;

    if (left_sym->sig != right_sym->sig) {
        if (left_sym->sig->cls->id == SYM_CLASS_OBJECT)
            opcode = o_obj_assign;
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
            left_cls_id == SYM_CLASS_NUMBER)
            opcode = o_assign;
        else if (left_cls_id == SYM_CLASS_OBJECT)
            opcode = o_obj_assign;
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
    if (assign_optimize_check(ast))
        m->code[m->pos-1] = left_sym->reg_spot;
    else {
        WRITE_4(opcode,
                ast->line_num,
                right_sym->reg_spot,
                left_sym->reg_spot)
    }
    ast->result = right_sym;
}

static int get_package_index(lily_emit_state *emit, lily_ast *ast)
{
    lily_package_val *pval = ast->left->result->value.package;
    lily_var *want_var = (lily_var *)(ast->right->result);

    int i;
    for (i = 0;i < pval->var_count;i++) {
        if (pval->vars[i] == want_var)
            break;
    }

    return i;
}

static void eval_package_assign(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->top_method;

    /* For now, there are no packages in packages, so ast->left will always be
       of type tree_package, with a var as the left and the right. */
    if (ast->right->tree_type != tree_var)
        eval_tree(emit, ast->right);

    /* Don't eval the lhs, because an assignment has to be done directly to the
       package var. The left's right is the var that needs to be pulled out. So
       get the signature of that and make sure it's a match. */
    lily_sig *result_sig = ast->left->right->result->sig;

    if (result_sig != ast->right->result->sig &&
        type_matchup(emit, NULL, result_sig, ast->right) == 0) {
        bad_assign_error(emit, ast->line_num, result_sig,
                ast->right->result->sig);
    }

    int index = get_package_index(emit, ast->left);

    WRITE_5(o_package_set, ast->line_num, ast->left->left->result->reg_spot,
            index, ast->right->result->reg_spot)
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
    lily_method_val *m = emit->top_method;

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

        WRITE_4(o_get_const,
                ast->line_num,
                (uintptr_t)success_lit,
                result->reg_spot)

        WRITE_2(o_jump, 0)
        save_pos = m->pos - 1;

        lily_emit_leave_block(emit);
        WRITE_4(o_get_const,
                ast->line_num,
                (uintptr_t)failure_lit,
                result->reg_spot);
        m->code[save_pos] = m->pos;
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
        lily_ast *index_ast)
{
    if (var_ast->result->sig->cls->id == SYM_CLASS_LIST &&
        index_ast->result->sig->cls->id != SYM_CLASS_INTEGER) {
        emit->raiser->line_adjust = var_ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax,
                "list index is not an integer.\n");
    }
    else if (var_ast->result->sig->cls->id == SYM_CLASS_HASH) {
        int match_check = template_check(var_ast->result->sig,
                var_ast->result->sig->siglist[0], index_ast->result->sig);

        if (match_check == 0) {
            emit->raiser->line_adjust = var_ast->line_num;
            lily_raise(emit->raiser, lily_ErrSyntax,
                    "hash expects an index of type '%T', but got type '%T'.\n",
                    var_ast->result->sig->siglist[0], index_ast->result->sig);
        }
    }
}

static lily_sig *get_subscript_result(lily_sig *sig)
{
    lily_sig *result;
    if (sig->cls->id == SYM_CLASS_LIST)
        result = sig->siglist[0];
    else if (sig->cls->id == SYM_CLASS_HASH)
        result = sig->siglist[1];
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
   writes o_sub_assign to make sure the vm assigns to the list, and not a
   storage.
   Var is at ast->left->arg_start
   Index is at ast->left->arg_start->next
   Right is at ast->right */
static void eval_sub_assign(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->top_method;

    lily_ast *var_ast = ast->left->arg_start;
    lily_ast *index_ast = var_ast->next_arg;
    lily_sym *rhs;
    lily_sig *elem_sig;

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right);

    rhs = ast->right->result;

    if (var_ast->tree_type != tree_local_var)
        eval_tree(emit, var_ast);

    if (index_ast->tree_type != tree_local_var)
        eval_tree(emit, index_ast);

    check_valid_subscript(emit, var_ast, index_ast);

    /* The subscript assign goes to the element, not the list. So... */
    elem_sig = get_subscript_result(var_ast->result->sig);

    if (elem_sig != rhs->sig && elem_sig->cls->id != SYM_CLASS_OBJECT) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, elem_sig,
                         rhs->sig);
    }

    if (ast->op > expr_assign) {
        /* For a compound assignment to work, the left side must be subscripted
           to get the value held. */

        lily_storage *subs_storage = get_storage(emit, elem_sig, ast->line_num);

        WRITE_5(o_subscript,
                ast->line_num,
                var_ast->result->reg_spot,
                index_ast->result->reg_spot,
                subs_storage->reg_spot)

        ast->left->result = (lily_sym *)subs_storage;

        /* Run the compound op now that ->left is set properly. */
        emit_op_for_compound(emit, ast);

        rhs = ast->result;
    }

    WRITE_5(o_sub_assign,
            ast->line_num,
            var_ast->result->reg_spot,
            index_ast->result->reg_spot,
            rhs->reg_spot)

    ast->result = rhs;
}

/* eval_typecast
   This walks an ast of type tree_typecast, which is used to change an object
   into a given type.
   This has two parts: a signature, and a value. The signature is ast->sig, and
   the value is ast->right. This currently only handles conversions of objects
   to other types, but could handle other conversions in the future.
   Any typecast that specifies object is transformed into an object assign. */
static void eval_typecast(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right);

    lily_sig *cast_sig = ast->sig;
    lily_sig *var_sig = ast->right->result->sig;
    lily_method_val *m = emit->top_method;

    if (cast_sig == var_sig) {
        ast->result = (lily_sym *)ast->right->result;
        return;
    }
    else if (cast_sig->cls->id == SYM_CLASS_OBJECT) {
        /* An object assign will work here. */
        lily_storage *storage = get_storage(emit, cast_sig, ast->line_num);

        WRITE_4(o_obj_assign,
                ast->line_num,
                ast->right->result->reg_spot,
                storage->reg_spot)
        ast->result = (lily_sym *)storage;
        return;
    }

    lily_storage *result;
    int cast_opcode;

    if (var_sig->cls->id == SYM_CLASS_OBJECT) {
        cast_opcode = o_obj_typecast;
        result = get_storage(emit, cast_sig, ast->line_num);
    }
    else {
        if ((var_sig->cls->id == SYM_CLASS_INTEGER &&
             cast_sig->cls->id == SYM_CLASS_NUMBER) ||
            (var_sig->cls->id == SYM_CLASS_NUMBER &&
             cast_sig->cls->id == SYM_CLASS_INTEGER))
        {
            cast_opcode = o_intnum_typecast;
            result = get_storage(emit, cast_sig, ast->line_num);
        }
        else {
            cast_opcode = -1;
            result = NULL;
            emit->raiser->line_adjust = ast->line_num;
            lily_raise(emit->raiser, lily_ErrBadCast,
                       "Cannot cast type '%T' to type '%T'.\n",
                       var_sig, cast_sig);
        }
    }

    WRITE_4(cast_opcode,
            ast->line_num,
            ast->right->result->reg_spot,
            result->reg_spot)

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
    lily_method_val *m;

    m = emit->top_method;
    lhs_class = ast->left->result->sig->cls;
    if (lhs_class->id != SYM_CLASS_INTEGER) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax, "Invalid operation: %s%s.\n",
                   opname(ast->op), lhs_class->name);
    }

    lily_class *integer_cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

    storage = get_storage(emit, integer_cls->sig, ast->line_num);

    if (ast->op == expr_unary_minus)
        opcode = o_unary_minus;
    else if (ast->op == expr_unary_not)
        opcode = o_unary_not;
    else
        opcode = -1;

    WRITE_4(opcode,
            ast->line_num,
            ast->left->result->reg_spot,
            storage->reg_spot);

    ast->result = (lily_sym *)storage;
}

/*  hash_values_to_objects

    This converts all of the values of the given ast into objects using
    o_obj_assign. The result of each value is rewritten to be the object,
    instead of the old value.

    emit:     The emitter holding the method to write code to.
    hash_ast: An ast of type tree_hash which has already been evaluated.

    Caveats:
    * Caller must do this before writing the o_build_hash instruction out.
    * Caller must evaluate the hash before calling this.
    * This will call lily_raise_nomem in the event of being unable to allocate
      an object value. */
static void emit_hash_values_to_objects(lily_emit_state *emit,
        lily_ast *hash_ast)
{
    /* The keys and values are in hash_ast as args. Since they're in pairs and
       this only modifies the values, this is how many values there are. */
    int value_count = hash_ast->args_collected / 2;
    lily_method_val *m = emit->top_method;

    /* Make a single large prep that will cover everything needed. This ensures
       that any growing will be done all at once, instead of in smaller
       blocks. */
    WRITE_PREP_LARGE(value_count * 4)

    lily_ast *iter_ast;
    for (iter_ast = hash_ast->arg_start;
         iter_ast != NULL;
         iter_ast = iter_ast->next_arg->next_arg) {

        emit_obj_assign(emit, iter_ast->next_arg);
    }
}

/*  emit_list_values_to_objects

    This converts all of the values of the given ast into objects using
    o_obj_assign. The result of each value is rewritten to be the object,
    instead of the old value.

    emit:     The emitter holding the method to write code to.
    list_ast: An ast of type tree_list which has already been evaluated.

    Caveats:
    * Caller must do this before writing the o_build_list instruction out.
    * Caller must evaluate the list before calling this.
    * This will call lily_raise_nomem in the event of being unable to allocate
      an object value. */
static void emit_list_values_to_objects(lily_emit_state *emit,
        lily_ast *list_ast)
{
    int value_count = list_ast->args_collected;
    lily_method_val *m = emit->top_method;

    WRITE_PREP_LARGE(value_count * 4)

    lily_ast *iter_ast;
    for (iter_ast = list_ast->arg_start;
         iter_ast != NULL;
         iter_ast = iter_ast->next_arg) {
        emit_obj_assign(emit, iter_ast);
    }
}

/*  eval_build_hash
    This handles evaluating trees that are of type tree_hash. This tree is
    created from a static hash (ex: ["a" => 1, "b" => 2, ...]). Parser has
    chained the keys and values in a tree_hash as arguments. The arguments are
    key, value, key, value, key, value. Thus, ->args_collected is the number of
    items, not the number of pairs collected.

    Caveats:
    * Keys can't default to object like values in a list can. This is because
      objects are not immutable.

    emit: The emit state containing a method to write the resulting code to.
    ast:  An ast of type tree_hash. */
static void eval_build_hash(lily_emit_state *emit, lily_ast *ast)
{
    lily_sig *key_sig, *value_sig;
    lily_ast *tree_iter;
    int make_objs;

    key_sig = NULL;
    value_sig = NULL;
    make_objs = 0;

    for (tree_iter = ast->arg_start;
         tree_iter != NULL;
         tree_iter = tree_iter->next_arg->next_arg) {

        lily_ast *key_tree, *value_tree;
        key_tree = tree_iter;
        value_tree = tree_iter->next_arg;

        if (key_tree->tree_type != tree_local_var)
            eval_tree(emit, key_tree);

        /* Keys -must- all be the same type. They cannot be converted to object
           later on because objects are not valid keys (not immutable). */
        if (key_tree->result->sig != key_sig) {
            if (key_sig == NULL) {
                if ((key_tree->result->sig->cls->flags & CLS_VALID_HASH_KEY) == 0) {
                    emit->raiser->line_adjust = key_tree->line_num;
                    lily_raise(emit->raiser, lily_ErrSyntax,
                            "Resulting type '%T' is not a valid hash key.\n",
                            key_tree->result->sig);
                }

                key_sig = key_tree->result->sig;
            }
            else {
                emit->raiser->line_adjust = key_tree->line_num;
                lily_raise(emit->raiser, lily_ErrSyntax,
                        "Expected a key of type '%T', but key is of type '%T'.\n",
                        key_sig, key_tree->result->sig);
            }
        }

        if (value_tree->tree_type != tree_local_var)
            eval_tree(emit, value_tree);

        /* Values being promoted to object is okay though. :) */
        if (value_tree->result->sig != value_sig) {
            if (value_sig == NULL)
                value_sig = value_tree->result->sig;
            else
                make_objs = 1;
        }
    }

    if (make_objs == 1) {
        lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_OBJECT);
        value_sig = cls->sig;
        emit_hash_values_to_objects(emit, ast);
    }

    lily_class *hash_cls = lily_class_by_id(emit->symtab, SYM_CLASS_HASH);
    lily_sig *new_sig = lily_try_sig_for_class(emit->symtab, hash_cls);
    if (new_sig == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

    new_sig->siglist = lily_malloc(2 * sizeof(lily_sig *));
    if (new_sig->siglist == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

    new_sig->siglist[0] = key_sig;
    new_sig->siglist[1] = value_sig;
    new_sig->siglist_size = 2;
    new_sig = lily_ensure_unique_sig(emit->symtab, new_sig);

    lily_storage *s = get_storage(emit, new_sig, ast->line_num);

    write_build_op(emit, o_build_hash, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

/*  type_matchup
    This function checks if the given ast's result can be coerced into the
    wanted type. Self is passed to allow for template checking as well.
    This also handles converting lists[!object] to list[object], and
    hash[?, !object] to hash[?, object]

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

    /* If the wanted value is a template, then pull from 'self' to determine
       what the template result in for this case. This allows template
       arguments to also use object copying and such.
       This is safe, because want_sig won't be a template if self is NULL. */
    if (want_sig->cls->id == SYM_CLASS_TEMPLATE)
        want_sig = self->siglist[want_sig->template_pos];

    if (self != NULL && template_check(self, want_sig, right->result->sig))
        ret = 1;
    else if (want_sig->cls->id == SYM_CLASS_OBJECT) {
        emit_obj_assign(emit, right);
        ret = 1;
    }
    else if ((right->tree_type == tree_list &&
         want_sig->cls->id == SYM_CLASS_LIST &&
         want_sig->siglist[0]->cls->id == SYM_CLASS_OBJECT)
        ||
        (right->tree_type == tree_hash &&
         want_sig->cls->id == SYM_CLASS_HASH &&
         want_sig->siglist[0] == right->result->sig->siglist[0] &&
         want_sig->siglist[1]->cls->id == SYM_CLASS_OBJECT)) {
        /* tree_list: Want list[object], have list[!object]
           tree_hash: Want hash[?, object], have hash[?, !object] */
        /* In either case, convert each of the values to objects, then rewrite
           the build to pump out the right resulting type. */
        lily_method_val *m = emit->top_method;
        int element_count = right->args_collected;
        lily_storage *s = get_storage(emit, want_sig, right->line_num);
        if (s == NULL)
            lily_raise_nomem(emit->raiser);

        /* WARNING: This is only safe because the tree was just evaluated and
           nothing has happened since the o_build_* was written. */
        m->pos = m->pos - (4 + element_count);

        int build_op;
        if (right->tree_type == tree_list) {
            build_op = o_build_list;
            emit_list_values_to_objects(emit, right);
        }
        else {
            build_op = o_build_hash;
            emit_hash_values_to_objects(emit, right);
        }

        write_build_op(emit, build_op, right->arg_start, right->line_num,
                right->args_collected, s->reg_spot);

        right->result = (lily_sym *)s;
        ret = 1;
    }

    return ret;
}

/* This walks an ast of type tree_list, which indicates a static list to be
   build (ex: x = [1, 2, 3, 4...] or x = ["1", "2", "3", "4"]).
   The values start in ast->arg_start, and end when ->next_arg is NULL.
   There are a few caveats:
   * Lists where all values are the same type are created as lists of that type.
   * If any list value is different, then the list values are cast to object,
     and the list's type is set to object.
   * Empty lists have a type specified in them, like x = [str]. This can be a
     complex type, if wanted. These lists will have 0 args and ->sig set to the
     sig specified. */
static void eval_build_list(lily_emit_state *emit, lily_ast *ast)
{
    lily_sig *elem_sig = NULL;
    lily_ast *arg;
    int make_objs;

    make_objs = 0;

    /* Walk through all of the list elements, keeping a note of the class
       of the results. The class of the list elements is determined as
       follows:
       * If all results have the same class, then use that class.
       * If they do not, use object. */
    for (arg = ast->arg_start;arg != NULL;arg = arg->next_arg) {
        if (arg->tree_type != tree_local_var)
            eval_tree(emit, arg);

        if (elem_sig != NULL) {
            if (arg->result->sig != elem_sig)
                make_objs = 1;
        }
        else
            elem_sig = arg->result->sig;
    }

    /* elem_sig is only null if the list is empty, and empty lists have a sig
       specified at ast->sig. */
    if (elem_sig == NULL)
        elem_sig = ast->sig;

    if (make_objs) {
        lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_OBJECT);
        elem_sig = cls->sig;
        emit_list_values_to_objects(emit, ast);
    }

    lily_class *list_cls = lily_class_by_id(emit->symtab, SYM_CLASS_LIST);
    lily_sig *new_sig = lily_try_sig_for_class(emit->symtab, list_cls);
    if (new_sig == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

    new_sig->siglist = lily_malloc(1 * sizeof(lily_sig *));
    if (new_sig->siglist == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

    new_sig->siglist[0] = elem_sig;
    new_sig->siglist_size = 1;
    new_sig = lily_ensure_unique_sig(emit->symtab, new_sig);

    lily_storage *s = get_storage(emit, new_sig, ast->line_num);

    write_build_op(emit, o_build_list, ast->arg_start, ast->line_num,
            ast->args_collected, s->reg_spot);
    ast->result = (lily_sym *)s;
}

/* eval_subscript
   This handles an ast of type tree_subscript. The arguments for the subscript
   start at ->arg_start.
   Arguments are: var, index, value */
static void eval_subscript(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->top_method;
    lily_ast *var_ast = ast->arg_start;
    lily_ast *index_ast = var_ast->next_arg;
    if (var_ast->tree_type != tree_var)
        eval_tree(emit, var_ast);

    lily_sig *var_sig = var_ast->result->sig;
    if (var_sig->cls->id != SYM_CLASS_LIST &&
        var_sig->cls->id != SYM_CLASS_HASH)
        bad_subs_class(emit, var_ast);

    if (index_ast->tree_type != tree_local_var)
        eval_tree(emit, index_ast);

    check_valid_subscript(emit, var_ast, index_ast);

    lily_sig *sig_for_result = var_sig->siglist[0];
    lily_storage *result;

    result = get_storage(emit, sig_for_result, ast->line_num);

    WRITE_5(o_subscript,
            ast->line_num,
            var_ast->result->reg_spot,
            index_ast->result->reg_spot,
            result->reg_spot);

    ast->result = (lily_sym *)result;
}

/* check_call_args
   This is used by eval_call to verify that the given arguments are all correct.
   This handles walking all of the arguments given, as well as verifying that
   the argument count is correct. For method varargs, this will pack the extra
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

    if ((is_varargs && (have_args <= num_args)) ||
        (is_varargs == 0 && (have_args != num_args)))
        bad_num_args(emit, ast, call_sig);

    for (i = 0;i != num_args;arg = arg->next_arg, i++) {
        if (arg->tree_type != tree_local_var)
            /* Walk the subexpressions so the result gets calculated. */
            eval_tree(emit, arg);

        if (i == 0)
            self_sig = arg->result->sig;

        if (arg->result->sig != call_sig->siglist[i + 1] &&
            type_matchup(emit, self_sig, call_sig->siglist[i+1], arg) == 0) {
            bad_arg_error(emit, ast, arg->result->sig, call_sig->siglist[i + 1],
                          i);
        }
    }

    if (is_varargs) {
        int is_method = (ast->result->sig->cls->id == SYM_CLASS_METHOD);
        lily_sig *va_comp_sig = call_sig->siglist[i + 1];
        lily_ast *save_arg = arg;
        lily_sig *save_sig;

        /* Methods handle var-args by shoving them into a list so that they can
           have a name. So the extra args need to verify against that type. */
        if (is_method) {
            save_sig = va_comp_sig;
            va_comp_sig = va_comp_sig->siglist[0];
        }

        for (;arg != NULL;arg = arg->next_arg) {
            if (arg->tree_type != tree_local_var)
                /* Walk the subexpressions so the result gets calculated. */
                eval_tree(emit, arg);

            if (arg->result->sig != va_comp_sig &&
                type_matchup(emit, self_sig, va_comp_sig, arg) == 0) {
                bad_arg_error(emit, ast, arg->result->sig, va_comp_sig, i);
            }
        }

        i = (have_args - i);
        if (is_method) {
            lily_storage *s;
            s = get_storage(emit, save_sig, ast->line_num);

            /* Put all of the extra arguments into a list, then fix the ast so
               eval_call has the right args and argument count.

               save_arg's line number is used in case the varargs is on a
               different line than the opening (. */
            write_build_op(emit, o_build_list, save_arg, save_arg->line_num, i,
                    s->reg_spot);
            save_arg->result = (lily_sym *)s;
            save_arg->next_arg = NULL;
            ast->args_collected = num_args + 1;
        }
    }
}

/* eval_call
   This walks an ast of type tree_call. The arguments start at ast->arg_start.
   If the call was to a known var at parse-time, then ast->result will be that
   var. Otherwise, the value to call is the first 'argument', which will need
   to be evaluated. */
static void eval_call(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->top_method;
    int expect_size, i, is_method;
    lily_ast *arg;
    lily_sig *call_sig;
    lily_sym *call_sym;

    if (ast->result == NULL) {
        int cls_id;
        /* This occurs when the method is obtained in some indirect way,
           such as a call from a subscript.
           Ex: method_list[0]()
           First, walk the subscript to get the storage that the call will
           go to. */
        eval_tree(emit, ast->arg_start);

        /* Set the result, because things like having a result to use.
           Ex: An empty list used as an arg may want to know what to
           default to. */
        ast->result = ast->arg_start->result;

        /* Make sure the result is callable (ex: NOT @(integer: 10) ()). */
        cls_id = ast->result->sig->cls->id;
        if (cls_id != SYM_CLASS_METHOD && cls_id != SYM_CLASS_FUNCTION) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise(emit->raiser, lily_ErrSyntax,
                    "Cannot anonymously call resulting type '%T'.\n",
                    ast->result->sig);
        }

        /* Then drop it from the arg list, since it's not an arg. */
        ast->arg_start = ast->arg_start->next_arg;
        ast->args_collected--;
    }

    call_sym = ast->result;
    call_sig = call_sym->sig;
    arg = ast->arg_start;
    is_method = (call_sym->sig->cls->id == SYM_CLASS_METHOD);
    expect_size = 6 + ast->args_collected;

    check_call_args(emit, ast, call_sig);

    if (GLOBAL_LOAD_CHECK(call_sym)) {
        lily_storage *storage = get_storage(emit, call_sym->sig, ast->line_num);

        WRITE_4(o_get_global,
                ast->line_num,
                call_sym->reg_spot,
                storage->reg_spot);

        call_sym = (lily_sym *)storage;
    }

    WRITE_PREP_LARGE(expect_size)

    if (is_method)
        m->code[m->pos] = o_method_call;
    else
        m->code[m->pos] = o_func_call;

    m->code[m->pos+1] = ast->line_num;

    if (call_sym->flags & VAR_IS_READONLY) {
        m->code[m->pos+2] = 1;
        m->code[m->pos+3] = (uintptr_t)call_sym;
    }
    else {
        m->code[m->pos+2] = 0;
        m->code[m->pos+3] = call_sym->reg_spot;
    }

    m->code[m->pos+4] = ast->args_collected;

    for (i = 5, arg = ast->arg_start;
        arg != NULL;
        arg = arg->next_arg, i++) {
        m->code[m->pos + i] = arg->result->reg_spot;
    }

    if (call_sig->siglist[0] != NULL) {
        lily_storage *storage = get_storage(emit, call_sig->siglist[0],
                ast->line_num);

        ast->result = (lily_sym *)storage;
        m->code[m->pos+i] = ast->result->reg_spot;
    }
    else {
        /* It's okay to not push a return value, unless something needs it.
           Assume that if the tree has a parent, something needs a value. */
        if (ast->parent == NULL)
            ast->result = NULL;
        else {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise(emit->raiser, lily_ErrSyntax,
                       "Call returning nil not at end of expression.");
        }
        m->code[m->pos+i] = -1;
    }

    m->pos += 6 + ast->args_collected;
}

/* emit_nonlocal_var
   This handles vars that are not local and are on the right hand side of an
   expression. This handles loading both literals and globals into a local
   register. */
static void emit_nonlocal_var(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->top_method;
    lily_storage *ret;

    if (ast->result->flags & (SYM_TYPE_LITERAL | VAR_IS_READONLY)) {
        ret = get_storage(emit, ast->result->sig, ast->line_num);

        WRITE_4(o_get_const,
                ast->line_num,
                (uintptr_t)ast->result,
                ret->reg_spot)

        ast->result = (lily_sym *)ret;
    }
    else if (ast->result->flags & SYM_TYPE_VAR) {
        ret = get_storage(emit, ast->result->sig, ast->line_num);

        /* We'll load this from an absolute position within __main__. */
        WRITE_4(o_get_global,
                ast->line_num,
                ast->result->reg_spot,
                ret->reg_spot)

        ast->result = (lily_sym *)ret;
    }
}

static void eval_package(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->top_method;

    if (ast->left->tree_type != tree_var)
        eval_tree(emit, ast->left);

    int index;
    lily_storage *s;

    index = get_package_index(emit, ast);
    s = get_storage(emit, ast->right->result->sig, ast->line_num);

    WRITE_5(o_package_get, ast->line_num, ast->left->result->reg_spot, index,
            s->reg_spot)

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
    else if (ast->tree_type == tree_subscript)
        eval_subscript(emit, ast);
    else if (ast->tree_type == tree_package)
        eval_package(emit, ast);
    else if (ast->tree_type == tree_typecast)
        eval_typecast(emit, ast);
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
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "Expected type 'integer', but got type '%T'.\n",
                   ast->result->sig);
    }

    /* Note: This works because the only time this is called is to handle
             for..in range expressions, which are always integers. */
    lily_method_val *m = emit->top_method;

    WRITE_4(o_assign,
            ast->line_num,
            ast->result->reg_spot,
            var->reg_spot)

    lily_ast_reset_pool(ap);
}

/* lily_emit_break
   This writes a break (jump to the end of a loop) for the parser. Since it
   is called by parser, it needs to verify that it is called from within a
   loop. */
void lily_emit_break(lily_emit_state *emit)
{
    lily_method_val *m = emit->top_method;

    if (emit->current_block->loop_start == -1) {
        /* This is called by parser on the source line, so do not adjust the
           raiser. */
        lily_raise(emit->raiser, lily_ErrSyntax,
                "'break' used outside of a loop.\n");
    }

    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    /* Write the jump, then figure out where to put it. */
    WRITE_2(o_jump, 0)

    /* If the while is the most current, then add it to the end. */
    if (emit->current_block->block_type == BLOCK_WHILE) {
        emit->patches[emit->patch_pos] = m->pos-1;
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
        emit->patches[move_start] = m->pos-1;

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
        lily_raise(emit->raiser, lily_ErrSyntax,
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
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "Conditional expression has no value.\n");

    lily_method_val *m = emit->top_method;
    /* If it passes, go back up to the top. Otherwise, fall out of the loop. */
    WRITE_4(o_jump_if,
            1,
            ast->result->reg_spot,
            emit->current_block->loop_start)

    lily_ast_reset_pool(ap);
}

/* lily_emit_continue
   This emits a jump to go back up to the top of a while. Since this is called
   by parser, it also checks that emitter is in a while. */
void lily_emit_continue(lily_emit_state *emit)
{
    lily_method_val *m = emit->top_method;

    /* This is called by parser on the source line, so do not adjust the
       raiser. */
    if (emit->current_block->loop_start == -1) {
        lily_raise(emit->raiser, lily_ErrSyntax,
                "'continue' used outside of a loop.\n");
    }

    WRITE_2(o_jump, emit->current_block->loop_start)
}

/* lily_emit_change_if_branch
   This is called when an if or elif jump has finished, and a new branch is to
   begin. have_else indicates if it's an else branch or an elif branch. */
void lily_emit_change_if_branch(lily_emit_state *emit, int have_else)
{
    int save_jump;
    lily_method_val *m = emit->top_method;
    lily_block *block = emit->current_block;
    lily_var *v = block->var_start;

    if (emit->current_block->block_type != BLOCK_IF &&
        emit->current_block->block_type != BLOCK_IFELSE) {
        char *name = (have_else ? "else" : "elif");
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "'%s' without 'if'.\n", name);
    }

    if (have_else) {
        if (block->block_type == BLOCK_IFELSE)
            lily_raise(emit->raiser, lily_ErrSyntax,
                       "Only one 'else' per 'if' allowed.\n");
        else
            block->block_type = BLOCK_IFELSE;
    }
    else if (block->block_type == BLOCK_IFELSE)
        lily_raise(emit->raiser, lily_ErrSyntax, "'elif' after 'else'.\n");

    if (v->next != NULL)
        lily_hide_block_vars(emit->symtab, v);

    /* Write an exit jump for this branch, thereby completing the branch. */
    WRITE_2(o_jump, 0);
    save_jump = m->pos - 1;

    /* The last jump actually collected wanted to know where the next branch
       would start. It's where the code is now. */
    m->code[emit->patches[emit->patch_pos-1]] = m->pos;
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
    lily_method_val *m = emit->top_method;

    WRITE_2(o_jump, emit->current_block->loop_start)
}

/* lily_emit_jump_if
   This writes a conditional jump using the result of the given ast. The type
   of jump (true/false) depends on 'jump_on'.
   1: jump_if_true: The jump will be performed if the ast's result is true or
      non-zero.
   0: jump_if_false: The jump will be performed if the ast's result is zero. */
void lily_emit_jump_if(lily_emit_state *emit, lily_ast *ast, int jump_on)
{
    lily_method_val *m = emit->top_method;

    WRITE_4(o_jump_if, jump_on, ast->result->reg_spot, 0);
    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    emit->patches[emit->patch_pos] = m->pos-1;
    emit->patch_pos++;
}

/* lily_emit_return
   This writes a return statement for a method. This also checks that the
   value given matches what the method says it returns. */
void lily_emit_return(lily_emit_state *emit, lily_ast *ast, lily_sig *ret_sig)
{
    eval_tree(emit, ast);
    emit->expr_num++;

    if (ast->result->sig != ret_sig) {
        if (ret_sig->cls->id == SYM_CLASS_OBJECT)
            emit_obj_assign(emit, ast);
        else {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise(emit->raiser, lily_ErrSyntax,
                    "return expected type '%T' but got type '%T'.\n",
                    ret_sig, ast->result->sig);
        }
    }

    lily_method_val *m = emit->top_method;
    WRITE_3(o_return_val, ast->line_num, ast->result->reg_spot)
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
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "show expression has no value.\n");

    emit->expr_num++;

    lily_method_val *m = emit->top_method;

    WRITE_4(o_show, ast->line_num, is_global, ast->result->reg_spot)
}

/* lily_emit_return_noval
   This writes the o_return_noval opcode for a method to return without sending
   a value to the caller. */
void lily_emit_return_noval(lily_emit_state *emit)
{
    /* Don't allow 'return' within __main__. */
    if (emit->method_depth == 1)
        lily_raise(emit->raiser, lily_ErrSyntax,
                "'return' used outside of a method.\n");

    lily_method_val *m = emit->top_method;
    WRITE_2(o_return_noval, *emit->lex_linenum)
}

/* add_var_chain_to_info
   This adds a chain of vars to a method's info. If not __main__, then methods
   declared within the currently-exiting method are skipped. */
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
   This adds the storages that a method has used to the method's info. */
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

/* finalize_method_val
   A method is closing (or __main__ is about to be called). Since this method is
   done, prepare the reg_info part of it. This will be used to allocate the
   registers it needs at vm-time. */
static void finalize_method_val(lily_emit_state *emit, lily_block *method_block)
{
    int register_count = emit->symtab->next_register_spot;
    lily_method_val *m = emit->top_method;
    lily_var *var_iter;
    lily_storage *storage_iter = method_block->storage_start;

    lily_register_info *info;
    if (m->reg_info == NULL)
        info = lily_malloc(register_count * sizeof(lily_register_info));
    else
        info = lily_realloc(m->reg_info,
                register_count * sizeof(lily_register_info));

    if (info == NULL)
        /* This is called directly from parser, so don't set an adjust. */
        lily_raise_nomem(emit->raiser);

    var_iter = method_block->method_var;

    /* Don't include methods inside of themselves... */
    if (emit->method_depth > 1)
        var_iter = var_iter->next;
    /* else we're in __main__, which does include itself as an arg so it can be
       passed to show and other neat stuff. */

    add_var_chain_to_info(emit, info, NULL, var_iter);
    add_storage_chain_to_info(info, method_block->storage_start);

    if (emit->method_depth > 1) {
        /* todo: Reuse the var shells instead of destroying. Seems petty, but
                 malloc isn't cheap if there are a lot of vars. */
        lily_var *var_temp;
        while (var_iter) {
            var_temp = var_iter->next;
            if ((var_iter->flags & VAR_IS_READONLY) == 0)
                lily_free(var_iter);
            else {
                /* This is a method declared within the current method. Hide it
                   in symtab's old methods since it's going out of scope. */
                var_iter->next = emit->symtab->old_method_chain;
                emit->symtab->old_method_chain = var_iter;
            }

            /* The method value now owns the var names, so don't free them. */
            var_iter = var_temp;
        }

        /* Blank the signatures of the storages that were used. This lets other
           methods know that the signatures are not in use. */
        storage_iter = method_block->storage_start;
        while (storage_iter) {
            storage_iter->sig = NULL;
            storage_iter = storage_iter->next;
        }

        /* Unused storages now begin where the method starting zapping them. */
        emit->unused_storage_start = method_block->storage_start;
    }
    else {
        /* If __main__, add global functions like str's concat and all global
           vars. */
        int i;
        for (i = 0;i < emit->symtab->class_pos;i++) {
            lily_class *cls = emit->symtab->classes[i];
            if (cls->call_start)
                add_var_chain_to_info(emit, info, cls->name, cls->call_start);
        }
    }

    m->reg_info = info;
    m->reg_count = register_count;
}

/* lily_emit_vm_return
   This writes the o_vm_return opcode at the end of the __main__ method. */
void lily_emit_vm_return(lily_emit_state *emit)
{
    lily_method_val *m = emit->top_method;

    finalize_method_val(emit, emit->current_block);
    WRITE_1(o_return_from_vm)
}

/* lily_reset_main
   This resets the code position of __main__, so it can receive new code. */
void lily_reset_main(lily_emit_state *emit)
{
    ((lily_method_val *)emit->top_method)->pos = 0;
}

/** Block entry/exit **/

/* lily_emit_enter_block
   Enter a block of the given block_type. It will try to use an existing block
   if able, or create a new one if it cannot.
   For methods, top_method_ret is not set (because this is called when a method
   var is seen), and must be patched later.
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

    if (block_type != BLOCK_METHOD) {
        new_block->patch_start = emit->patch_pos;
        /* Non-methods will continue using the storages that the parent uses.
           Additionally, the same technique is used to allow loop starts to
           bubble upward until a method gets in the way. */
        new_block->storage_start = emit->current_block->storage_start;
        if (IS_LOOP_BLOCK(block_type))
            new_block->loop_start = emit->top_method->pos;
        else
            new_block->loop_start = emit->current_block->loop_start;
    }
    else {
        lily_var *v = emit->symtab->var_top;
        v->value.method = lily_try_new_method_val();
        if (v->value.method == NULL)
            lily_raise_nomem(emit->raiser);

        v->value.method->trace_name = v->name;
        v->flags &= ~(VAL_IS_NIL);

        new_block->save_register_spot = emit->symtab->next_register_spot;

        emit->symtab->method_depth++;
        /* Make sure registers start at 0 again. This will be restored when this
           method leaves. */
        emit->symtab->next_register_spot = 0;
        /* This method's storages start where the unused ones start, or NULL if
           all are currently taken. */
        new_block->storage_start = emit->unused_storage_start;
        new_block->method_var = v;
        /* -1 to indicate that there is no current loop. */
        new_block->loop_start = -1;
        emit->top_method = v->value.method;
        emit->top_var = v;
        emit->method_depth++;
    }

    emit->current_block = new_block;
}

static void leave_method(lily_emit_state *emit, lily_block *block)
{
    /* If the method returns nil, write an implicit 'return' at the end of it.
       It's easiest to just blindly write it. */
    if (emit->top_method_ret == NULL)
        lily_emit_return_noval(emit);
    else
        /* Ensure that methods that claim to return a value cannot leave without
           doing so. */
        emit_return_expected(emit);

    finalize_method_val(emit, block);

    /* Warning: This assumes that only methods can contain other methods. */
    lily_var *v = block->prev->method_var;

    /* If this method has no storages, then it can use the ones from the method
       that just exited. This reuse cuts down on a lot of memory. */
    if (block->prev->storage_start == NULL)
        block->prev->storage_start = emit->unused_storage_start;

    emit->symtab->var_top = block->method_var;
    block->method_var->next = NULL;
    emit->symtab->method_depth--;
    emit->symtab->next_register_spot = block->save_register_spot;
    emit->top_method = v->value.method;
    emit->top_var = v;
    emit->top_method_ret = v->sig->siglist[0];
    emit->method_depth--;
}

/* lily_emit_leave_block
   This closes the last block that was added to the emitter. Any vars that were
   added in the block are dropped. */
void lily_emit_leave_block(lily_emit_state *emit)
{
    lily_var *v;
    lily_block *block;
    int block_type;

    if (emit->first_block == emit->current_block)
        lily_raise(emit->raiser, lily_ErrSyntax, "'}' outside of a block.\n");

    block = emit->current_block;
    block_type = block->block_type;

    /* Write in a fake continue so that the end of a while jumps back up to the
       top. */
    if (block_type == BLOCK_WHILE || block_type == BLOCK_FOR_IN)
        emit_final_continue(emit);

    v = block->var_start;

    if (block_type != BLOCK_METHOD) {
        int from, to, pos;
        from = emit->patch_pos-1;
        to = block->patch_start;
        pos = emit->top_method->pos;

        for (;from >= to;from--)
            emit->top_method->code[emit->patches[from]] = pos;

        /* Use the space for new patches now. */
        emit->patch_pos = to;

        lily_hide_block_vars(emit->symtab, v);
    }
    else
        leave_method(emit, block);

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

    main_var->value.method = lily_try_new_method_val();
    if (main_var->value.method == NULL) {
        lily_free(main_block);
        return 0;
    }

    /* __main__ is given two refs so that it must go through a custom deref to
       be destroyed. This is because the names in the method info it has are
       shared with vars that are still around. */
    main_var->value.method->refcount++;
    main_var->flags &= ~VAL_IS_NIL;
    main_var->value.method->trace_name = main_var->name;
    main_block->block_type = BLOCK_METHOD;
    main_block->method_var = main_var;
    main_block->storage_start = NULL;
    /* This is necessary for trapping break/continue inside of __main__. */
    main_block->loop_start = -1;
    emit->top_method = main_var->value.method;
    emit->top_var = main_var;
    emit->first_block = main_block;
    emit->current_block = main_block;
    emit->method_depth++;
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
    lily_method_val *m = emit->top_method;
    lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

    int have_step = (for_step != NULL);
    if (have_step == 0) {
        /* This var isn't visible, so don't bother with a valid shorthash. */
        for_step = lily_try_new_var(emit->symtab, cls->sig, "(for step)", 0);
        if (for_step == NULL)
            lily_raise_nomem(emit->raiser);
    }

    WRITE_PREP_LARGE(16)
    m->code[m->pos  ] = o_for_setup;
    m->code[m->pos+1] = line_num;
    m->code[m->pos+2] = user_loop_var->reg_spot;
    m->code[m->pos+3] = for_start->reg_spot;
    m->code[m->pos+4] = for_end->reg_spot;
    m->code[m->pos+5] = for_step->reg_spot;
    /* This value is used to determine if the step needs to be calculated. */
    m->code[m->pos+6] = (uintptr_t)!have_step;

    /* for..in is entered right after 'for' is seen. However, range values can
       be expressions. This needs to be fixed, or the loop will jump back up to
       re-eval those expressions. */
    loop_block->loop_start = m->pos+9;

    /* Write a jump to the inside of the loop. This prevents the value from
       being incremented before being seen by the inside of the loop. */
    m->code[m->pos+7] = o_jump;
    m->code[m->pos+8] = m->pos + 16;

    m->code[m->pos+9] = o_integer_for;
    m->code[m->pos+10] = line_num;
    m->code[m->pos+11] = user_loop_var->reg_spot;
    m->code[m->pos+12] = for_start->reg_spot;
    m->code[m->pos+13] = for_end->reg_spot;
    m->code[m->pos+14] = for_step->reg_spot;
    m->code[m->pos+15] = 0;

    m->pos += 16;

    if (emit->patch_pos == emit->patch_size)
        grow_patches(emit);

    emit->patches[emit->patch_pos] = m->pos-1;
    emit->patch_pos++;
}

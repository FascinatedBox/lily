#include <stdlib.h>
#include <string.h>

#include "lily_ast.h"
#include "lily_impl.h"
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
    s->block_depth = 0;
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
   signature. This should be seen as a helper for try_get_storage, and not used
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

/* try_get_storage
   This attempts to get a storage that contains the given signature. This may
   repurpose an unused storage (wherein the sig is NULL). It will attempt to
   create a new storage if there are no unused ones on the emitter's storage
   chain.
   Additionally, this starts the storage search based on the current block's
   first storage. This is so that methods inside of methods don't touch storages
   of outside methods (which already have registers). */
static lily_storage *try_get_storage(lily_emit_state *emit,
        lily_sig *sig)
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
                ret = start;
                break;
            }

            /* The first case is for simple signatures, and the second is for
               complex ones (methods, functions, and lists). */
            if ((start->sig == sig || lily_sigequal(start->sig, sig))
                && start->expr_num != expr_num) {
                start->expr_num = expr_num;
                ret = start;
                break;
            }

            start = start->next;
        }
    }

    if (ret == NULL) {
        ret = try_add_storage(emit, sig);
        if (ret == NULL)
            return NULL;

        ret->expr_num = expr_num;
        /* Non-method blocks inherit their storage start from the method block
           that they are in. */
        if (emit->current_block->storage_start == NULL) {
            if (emit->current_block->block_type == BLOCK_METHOD)
                /* Easy mode: Just fill in for the method. */
                emit->current_block->storage_start = ret;
            else {
                /* Non-method block, so keep setting storage_start until the
                   method block is reached. This will allow other blocks to use
                   this storage. This is also important because not doing this
                   causes the method block to miss this storage when the method
                   is being finalized. */
                lily_block *block = emit->current_block;
                while (block->block_type != BLOCK_METHOD) {
                    block->storage_start = ret;
                    block = block->prev;
                }

                block->storage_start = ret;
            }
        }
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

/** Signature helper functions **/
/* sigcast
   This function is called after lily_sigequal. This checks to see if the
   signature on the left can become the signature on the right. */
static int sigcast(lily_emit_state *emit, lily_ast *lhs_ast, lily_sig *rhs)
{
    int ret;

    if (rhs->cls->id == SYM_CLASS_OBJECT) {
        ret = 1;
        lily_method_val *m = emit->top_method;
        lily_storage *storage = try_get_storage(emit, rhs);
        if (storage == NULL) {
            emit->raiser->line_adjust = lhs_ast->line_num;
            lily_raise_nomem(emit->raiser);
        }

        WRITE_4(o_obj_assign,
                lhs_ast->line_num,
                lhs_ast->result->reg_spot,
                storage->reg_spot)

        lhs_ast->result = (lily_sym *)storage;
    }
    else
        ret = 0;

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
        lily_call_sig *csig)
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

    if (csig->is_varargs)
        va_text = "at least ";
    else
        va_text = "";

    emit->raiser->line_adjust = ast->line_num;
    lily_raise(emit->raiser, lily_ErrSyntax,
               "%s expects %s%d args, but got %d.\n", call_name, va_text,
               csig->num_args, ast->args_collected);
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
    else
        opcode = -1;

    if (opcode == -1) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "Invalid operation: %s %s %s.\n", lhs_class->name,
                   opname(ast->op), rhs_class->name);
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

    s = try_get_storage(emit, storage_class->sig);
    if (s == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

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

/* eval_assign
   This handles asts with type tree_binary, wherein the left side is not a
   subscript. This will handle compound assignments, as well as basic
   assignments. */
static void eval_assign(lily_emit_state *emit, lily_ast *ast)
{
    int left_cls_id, opcode;
    lily_sym *left_sym, *right_sym;
    lily_method_val *m = emit->top_method;

    if (ast->left->tree_type != tree_var)
        eval_tree(emit, ast->left);

    if ((ast->left->result->flags & SYM_TYPE_VAR) == 0 &&
        ast->left->tree_type != tree_subscript) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax,
                "Left side of %s is not a var.\n", opname(ast->op));
    }

    if (ast->right->tree_type != tree_local_var)
        eval_tree(emit, ast->right);

    left_sym = ast->left->result;
    right_sym = ast->right->result;
    left_cls_id = left_sym->sig->cls->id;

    if (left_sym->sig != right_sym->sig &&
        lily_sigequal(left_sym->sig, right_sym->sig) == 0) {
        /* These are either completely different, or complex classes where the
           inner bits don't match. If it's object, object can be anything so
           it's fine. */
        if (left_cls_id == SYM_CLASS_OBJECT)
            opcode = o_obj_assign;
        else
            bad_assign_error(emit, ast->line_num, left_sym->sig,
                          right_sym->sig);
    }
    else if (left_cls_id == SYM_CLASS_INTEGER ||
             left_cls_id == SYM_CLASS_NUMBER)
        opcode = o_assign;
    else if (left_cls_id == SYM_CLASS_OBJECT)
        opcode = o_obj_assign;
    else
        opcode = o_ref_assign;

    if (ast->op > expr_assign) {
        emit_op_for_compound(emit, ast);
        right_sym = ast->result;
    }

    if ((left_sym->flags & SYM_SCOPE_GLOBAL) && emit->method_depth > 1)
        opcode = o_set_global;

    WRITE_4(opcode,
            ast->line_num,
            right_sym->reg_spot,
            left_sym->reg_spot)
    ast->result = right_sym;
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
        result = try_get_storage(emit, cls->sig);
        if (result == NULL) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise_nomem(emit->raiser);
        }

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

    if (var_ast->result->sig->cls->id != SYM_CLASS_LIST)
        bad_subs_class(emit, var_ast);

    if (index_ast->tree_type != tree_local_var)
        eval_tree(emit, index_ast);

    if (index_ast->result->sig->cls->id != SYM_CLASS_INTEGER) {
        emit->raiser->line_adjust = index_ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "Subscript index is not an integer.\n");
    }

    /* The subscript assign goes to the element, not the list. So... */
    elem_sig = var_ast->result->sig->node.value_sig;

    if (elem_sig != rhs->sig && !lily_sigequal(elem_sig, rhs->sig) &&
        elem_sig->cls->id != SYM_CLASS_OBJECT) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, elem_sig,
                         rhs->sig);
    }

    if (ast->op > expr_assign) {
        /* For a compound assignment to work, the left side must be subscripted
           to get the value held. */

        lily_storage *subs_storage = try_get_storage(emit, elem_sig);
        if (subs_storage == NULL)
            lily_raise_nomem(emit->raiser);

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

    if (lily_sigequal(cast_sig, var_sig)) {
        ast->result = (lily_sym *)ast->right->result;
        return;
    }
    else if (cast_sig->cls->id == SYM_CLASS_OBJECT) {
        /* An object assign will work here. */
        lily_storage *storage = try_get_storage(emit, cast_sig);
        if (storage == NULL) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise_nomem(emit->raiser);
        }

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
        result = try_get_storage(emit, cast_sig);
    }
    else {
        if ((var_sig->cls->id == SYM_CLASS_INTEGER &&
             cast_sig->cls->id == SYM_CLASS_NUMBER) ||
            (var_sig->cls->id == SYM_CLASS_NUMBER &&
             cast_sig->cls->id == SYM_CLASS_INTEGER))
        {
            cast_opcode = o_intnum_typecast;
            result = try_get_storage(emit, cast_sig);
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

    if (result == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
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

    storage = try_get_storage(emit, integer_cls->sig);
    if (storage == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

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

/* cast_ast_list_to
   This converts the results of the list_ast (type tree_list) to the given sig.
   The caller is expected to verify that the cast is valid. */
static void cast_ast_list_to(lily_emit_state *emit, lily_ast *list_ast,
        lily_class *cls)
{
    lily_ast *arg = list_ast->arg_start;
    lily_method_val *m = emit->top_method;
    int opcode;

    if (cls->id == SYM_CLASS_OBJECT)
        opcode = o_obj_assign;
    else {
        /* This is probably a stub from implementing some new feature. */
        lily_raise(emit->raiser, lily_ErrSyntax,
                "Stub: Unexpected autocast list type.\n");
    }

    for (arg = list_ast->arg_start;
         arg != NULL;
         arg = arg->next_arg) {
        lily_storage *obj_store = try_get_storage(emit, cls->sig);
        if (obj_store == NULL) {
            emit->raiser->line_adjust = arg->line_num;
            lily_raise_nomem(emit->raiser);
        }

        /* This is weird. The ast has to be walked, so technically things on
           future line numbers have been executed. At the same time, it would
           be odd for a typecast to fail and reference a line that's different
           from where the arg is from. */
        WRITE_4(opcode,
                arg->line_num,
                arg->result->reg_spot,
                obj_store->reg_spot)
        arg->result = (lily_sym *)obj_store;
    }
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
    lily_method_val *m = emit->top_method;
    lily_sig *elem_sig = NULL;
    lily_ast *arg;
    int i, make_objs;

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
            if (arg->result->sig != elem_sig &&
                lily_sigequal(arg->result->sig, elem_sig) == 0) {
                make_objs = 1;
            }
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
        cast_ast_list_to(emit, ast, cls);
        elem_sig = cls->sig;
    }

    lily_class *list_cls = lily_class_by_id(emit->symtab, SYM_CLASS_LIST);
    lily_sig *new_sig = lily_try_sig_for_class(emit->symtab, list_cls);
    if (new_sig == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

    new_sig->node.value_sig = elem_sig;
    lily_storage *s = try_get_storage(emit, new_sig);

    if (s == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

    WRITE_PREP_LARGE(ast->args_collected + 4)
    m->code[m->pos] = o_build_list;
    m->code[m->pos+1] = ast->line_num;
    m->code[m->pos+2] = ast->args_collected;

    for (i = 3, arg = ast->arg_start;
        arg != NULL;
        arg = arg->next_arg, i++) {
        m->code[m->pos + i] = arg->result->reg_spot;
    }
    m->code[m->pos+i] = s->reg_spot;

    m->pos += 4 + ast->args_collected;
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
    if (var_sig->cls->id != SYM_CLASS_LIST)
        bad_subs_class(emit, var_ast);

    if (index_ast->tree_type != tree_local_var)
        eval_tree(emit, index_ast);

    if (index_ast->result->sig->cls->id != SYM_CLASS_INTEGER) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax,
                "Subscript index is not an integer.\n");
    }

    lily_sig *sig_for_result = var_sig->node.value_sig;
    lily_storage *result;

    result = try_get_storage(emit, sig_for_result);
    if (result == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

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
        lily_call_sig *csig)
{
    lily_ast *arg = ast->arg_start;
    int have_args, i, is_varargs, num_args;

    /* Ast doesn't check the call args. It can't check types, so why do only
       half of the validation? */
    have_args = ast->args_collected;
    is_varargs = csig->is_varargs;
    /* Take the last arg off of the arg count. This will be verified using the
       var arg signature. */
    num_args = csig->num_args - is_varargs;

    if ((is_varargs && (have_args <= num_args)) ||
        (is_varargs == 0 && (have_args != num_args)))
        bad_num_args(emit, ast, csig);

    for (i = 0;i != num_args;arg = arg->next_arg, i++) {
        if (arg->tree_type != tree_local_var)
            /* Walk the subexpressions so the result gets calculated. */
            eval_tree(emit, arg);

        if (!lily_sigequal(arg->result->sig, csig->args[i])) {
            if (!sigcast(emit, arg, csig->args[i]))
                bad_arg_error(emit, ast, arg->result->sig, csig->args[i], i);
        }
    }

    if (is_varargs) {
        int is_method = (ast->result->sig->cls->id == SYM_CLASS_METHOD);
        lily_sig *va_comp_sig = csig->args[i];
        lily_ast *save_arg = arg;
        lily_sig *save_sig;

        /* Methods handle var-args by shoving them into a list so that they can
           have a name. So the extra args need to verify against that type. */
        if (is_method) {
            save_sig = va_comp_sig;
            va_comp_sig = va_comp_sig->node.value_sig;
        }

        for (;arg != NULL;arg = arg->next_arg) {
            if (arg->tree_type != tree_local_var)
                /* Walk the subexpressions so the result gets calculated. */
                eval_tree(emit, arg);

            if (!lily_sigequal(arg->result->sig, va_comp_sig)) {
                if (!sigcast(emit, arg, va_comp_sig))
                    bad_arg_error(emit, ast, arg->result->sig, va_comp_sig, i);
            }
        }

        i = (have_args - i);
        if (is_method) {
            lily_storage *s;
            s = try_get_storage(emit, save_sig);
            if (s == NULL) {
                emit->raiser->line_adjust = ast->line_num;
                lily_raise_nomem(emit->raiser);
            }

            arg = save_arg;
            lily_method_val *m = emit->top_method;
            int j = 0;
            /* This -must- be a large prep, because it could be a very big
               var arg call at the start of a method. */
            WRITE_PREP_LARGE(i + 4)
            m->code[m->pos] = o_build_list;
            m->code[m->pos+1] = ast->line_num;
            m->code[m->pos+2] = i;
            for (j = 3;arg != NULL;arg = arg->next_arg, j++)
                m->code[m->pos+j] = arg->result->reg_spot;

            m->code[m->pos+j] = s->reg_spot;
            m->pos += j+1;
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
    lily_call_sig *csig;
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
    csig = call_sym->sig->node.call;
    arg = ast->arg_start;
    is_method = (call_sym->sig->cls->id == SYM_CLASS_METHOD);
    expect_size = 5 + ast->args_collected;

    check_call_args(emit, ast, csig);

    if ((call_sym->flags & SYM_SCOPE_GLOBAL) && emit->method_depth > 1) {
        lily_storage *storage = try_get_storage(emit, call_sym->sig);
        if (storage == NULL) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise_nomem(emit->raiser);
        }

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
    m->code[m->pos+2] = call_sym->reg_spot;
    m->code[m->pos+3] = ast->args_collected;

    for (i = 4, arg = ast->arg_start;
        arg != NULL;
        arg = arg->next_arg, i++) {
        m->code[m->pos + i] = arg->result->reg_spot;
    }

    if (csig->ret != NULL) {
        lily_storage *storage = try_get_storage(emit, csig->ret);
        if (storage == NULL) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise_nomem(emit->raiser);
        }

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

    m->pos += 5 + ast->args_collected;
}

/* emit_nonlocal_var
   This handles vars that are not local and are on the right hand side of an
   expression. This handles loading both literals and globals into a local
   register. */
static void emit_nonlocal_var(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->top_method;
    lily_storage *ret;

    if (ast->result->flags & SYM_TYPE_LITERAL) {
        ret = try_get_storage(emit, ast->result->sig);
        if (ret == NULL) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise_nomem(emit->raiser);
        }

        WRITE_4(o_get_const,
                ast->line_num,
                (uintptr_t)ast->result,
                ret->reg_spot)

        ast->result = (lily_sym *)ret;
    }
    else if (ast->result->flags & SYM_TYPE_VAR) {
        ret = try_get_storage(emit, ast->result->sig);
        if (ret == NULL) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise_nomem(emit->raiser);
        }

        /* We'll load this from an absolute position within @main's globals. */
        WRITE_4(o_get_global,
                ast->line_num,
                ast->result->reg_spot,
                ret->reg_spot)

        ast->result = (lily_sym *)ret;
    }
}

/* eval_tree
   This is the main emit function. This doesn't evaluate anything itself, but
   instead determines what call to shove the work off to. */
static void eval_tree(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->tree_type == tree_var)
        emit_nonlocal_var(emit, ast);
    else if (ast->tree_type == tree_call)
        eval_call(emit, ast);
    else if (ast->tree_type == tree_binary) {
        if (ast->op >= expr_assign) {
            if (ast->left->tree_type != tree_subscript)
                eval_assign(emit, ast);
            else
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
    else if (ast->tree_type == tree_subscript)
        eval_subscript(emit, ast);
    else if (ast->tree_type == tree_typecast)
        eval_typecast(emit, ast);
}

/** Emitter API functions **/

/* lily_emit_ast
   API function to call eval_tree on an ast and increment the expr_num of the
   emitter. */
void lily_emit_ast(lily_emit_state *emit, lily_ast *ast)
{
    eval_tree(emit, ast);
    emit->expr_num++;
}

/* lily_emit_ast_to_var
   API function to call eval_tree on an ast to evaluate it. Afterward, the
   result is assigned to the given storage. */
void lily_emit_ast_to_var(lily_emit_state *emit, lily_ast *ast,
        lily_var *var)
{
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

/* lily_emit_conditional
   API function to call eval_tree on an ast and increment the expr_num of the
   emitter. This function writes an if jump afterward, for if conditions. */
void lily_emit_conditional(lily_emit_state *emit, lily_ast *ast)
{
    /* This does emitting for the condition of an if or elif. */
    eval_tree(emit, ast);
    emit->expr_num++;

    /* Calls returning nil check if they're inside of an expression. However,
       they can still be at the top of an if-type statement. This is a problem,
       since they don't return a value at all. */
    if (ast->result == NULL)
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "Conditional statement has no value.\n");

    /* This jump will need to be rewritten with the first part of the next elif,
       else, or the end of the if. Save the position so it can be written over
       later.
       0 for jump_if_false. */
    lily_emit_jump_if(emit, ast, 0);
}

/* lily_eval_do_while_expr
   This handles the eval of a while expression for a do...while loop. */
void lily_eval_do_while_expr(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->top_method;

    eval_tree(emit, ast);
    emit->expr_num++;

    if (ast->result->reg_spot == -1)
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "Conditional statement has no value.\n");

    /* If condition isn't met, jump back to where the loop started. Otherwise,
       fall out of the loop. */
    WRITE_4(o_jump_if,
            0,
            ast->result->reg_spot,
            emit->current_block->loop_start)
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

    if (emit->current_block == emit->first_block) {
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

    /* sigcast will convert it to an object, if it gets that far. */
    if (ast->result->sig != ret_sig &&
        lily_sigequal(ast->result->sig, ret_sig) == 0 &&
        sigcast(emit, ast, ret_sig) == 0) {

        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax,
                "return expected type '%T' but got type '%T'.\n",
                ret_sig, ast->result->sig);
    }

    lily_method_val *m = emit->top_method;
    WRITE_3(o_return_val, ast->line_num, ast->result->reg_spot)
}

/* lily_emit_show
   This evals the given ast, then writes o_show so the vm will show the result
   of the ast. Type-checking is intentionally NOT performed. */
void lily_emit_show(lily_emit_state *emit, lily_ast *ast)
{
    int is_global = (ast->tree_type == tree_var &&
                     ast->result->flags & SYM_SCOPE_GLOBAL);

    /* Don't eval if it's a global var and nothing more. This makes it so
       globals show with their name and their proper register. Otherwise, the
       global gets loaded into a local storage, making show a bit less helpful.
       This also makes sure that global and local vars are treated consistently
       by show. */
    if (is_global == 0)
        eval_tree(emit, ast);

    emit->expr_num++;

    lily_method_val *m = emit->top_method;

    WRITE_4(o_show, ast->line_num, is_global, ast->result->reg_spot)
}

/* lily_emit_return_noval
   This writes the o_return_noval opcode for a method to return without sending
   a value to the caller. */
void lily_emit_return_noval(lily_emit_state *emit)
{
    /* Don't allow 'return' within @main. */
    if (emit->current_block == emit->first_block)
        lily_raise(emit->raiser, lily_ErrSyntax,
                "'return' used outside of a method.\n");

    lily_method_val *m = emit->top_method;
    WRITE_2(o_return_noval, *emit->lex_linenum)
}

/* add_var_chain_to_info
   This adds a chain of vars to a method's info. If not @main, then methods
   declared within the currently-exiting method are skipped. */
static void add_var_chain_to_info(lily_emit_state *emit,
        lily_register_info *info, lily_var *var)
{
    if (emit->method_depth > 1) {
        while (var) {
            if ((var->flags & SYM_SCOPE_GLOBAL) == 0) {
                info[var->reg_spot].sig = var->sig;
                info[var->reg_spot].name = var->name;
                info[var->reg_spot].line_num = var->line_num;
            }

            var = var->next;
        }
    }
    else {
        while (var) {
            info[var->reg_spot].sig = var->sig;
            info[var->reg_spot].name = var->name;
            info[var->reg_spot].line_num = var->line_num;

            var = var->next;
        }
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
        info[storage->reg_spot].line_num = -1;
        storage = storage->next;
    }
}

/* finalize_method_val
   A method is closing (or @main is about to be called). Since this method is
   done, prepare the reg_info part of it. This will be used to allocate the
   registers it needs at vm-time. */
static void finalize_method_val(lily_emit_state *emit, lily_block *method_block)
{
    int register_count = emit->symtab->next_register_spot;
    lily_method_val *m = emit->top_method;
    lily_var *var_iter = method_block->method_var->next;
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

    add_var_chain_to_info(emit, info, method_block->method_var->next);
    add_storage_chain_to_info(info, method_block->storage_start);

    if (emit->method_depth > 1) {
        /* todo: Reuse the var shells instead of destroying. Seems petty, but
                 malloc isn't cheap if there are a lot of vars. */
        lily_var *var_temp;
        var_iter = method_block->method_var->next;
        while (var_iter) {
            var_temp = var_iter->next;
            if ((var_iter->flags & SYM_SCOPE_GLOBAL) == 0)
                lily_free(var_iter);
            else {
                /* This is a declared method that was placed into a register of
                   @main. Instead of destroying it, save it where the vm can
                   find it later for initializing @main's registers. */
                lily_save_declared_method(emit->symtab, var_iter);
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
    }
    else {
        /* If @main, add global functions like str's concat and all global
           vars. */
        int i;
        for (i = 0;i < emit->symtab->class_pos;i++) {
            lily_class *cls = emit->symtab->classes[i];
            if (cls->call_start)
                add_var_chain_to_info(emit, info, cls->call_start);
        }
        add_var_chain_to_info(emit, info, emit->symtab->old_method_chain);
    }

    m->reg_info = info;
    m->reg_count = register_count;
}

/* lily_emit_vm_return
   This writes the o_vm_return opcode at the end of the @main method. */
void lily_emit_vm_return(lily_emit_state *emit)
{
    lily_method_val *m = emit->top_method;

    finalize_method_val(emit, emit->current_block);
    WRITE_1(o_return_from_vm)
}

/* lily_reset_main
   This resets the code position of @main, so it can receive new code. */
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

        /* If this is a method within a method, then put it in @main since it's
           flagged as a global. */
        if (emit->method_depth > 1) {
            lily_block *block = emit->first_block->next;
            emit->symtab->next_register_spot--;
            if (emit->method_depth >= 2) {
                v->reg_spot = block->save_register_spot;
                block->save_register_spot++;
            }
        }

        v->value.method->trace_name = v->name;
        /* All declared methods are loaded into @main's registers for later
           access. */
        v->flags |= SYM_SCOPE_GLOBAL;
        v->flags &= ~(SYM_IS_NIL);
        v->method_depth = 1;

        new_block->save_register_spot = emit->symtab->next_register_spot;

        emit->symtab->method_depth++;
        /* Make sure registers start at 0 again. This will be restored when this
           method leaves. */
        emit->symtab->next_register_spot = 0;
        /* All vars should now be created in a local scope. */
        emit->symtab->scope = 0;
        /* This prevents this method from using the storages of the method it's
           in. Doing so allows each method to be independent of others. */
        new_block->storage_start = NULL;
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

    emit->symtab->var_top = block->method_var;
    block->method_var->next = NULL;
    emit->symtab->method_depth--;
    emit->symtab->next_register_spot = block->save_register_spot;
    emit->top_method = v->value.method;
    emit->top_var = v;
    emit->top_method_ret = v->sig->node.call->ret;
    emit->method_depth--;
    /* If returning to @main, all vars default to a global scope again. */
    if (emit->method_depth == 1)
        emit->symtab->scope = SYM_SCOPE_GLOBAL;
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
   Attempt to create a block representing @main, then enter it. main_var is the
   var representing @main. Returns 1 on success, or 0 on failure.
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

    /* @main is given two refs so that it must go through a custom deref to be
       destroyed. This is because the names in the method info it has are shared
       with vars that are still around. */
    main_var->value.method->refcount++;
    main_var->flags &= ~SYM_IS_NIL;
    main_var->value.method->trace_name = main_var->name;
    main_block->block_type = BLOCK_METHOD;
    main_block->method_var = main_var;
    main_block->storage_start = NULL;
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
        for_step = lily_try_new_var(emit->symtab, cls->sig, "(for step)");
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

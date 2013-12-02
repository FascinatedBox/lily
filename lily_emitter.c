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
    * Ensuring that the vm knows to save important method parameters and
      storages to keep unintended consequences from occuring from a call. It
      also writes the o_restore opcode for the vm to restore values.
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

#define SAVE_PREP(size) \
if ((emit->save_cache_pos + size) > emit->save_cache_size) { \
    uintptr_t *save_cache = emit->save_cache; \
    while ((emit->save_cache_pos + size) > emit->save_cache_size) \
        emit->save_cache_size *= 2; \
    save_cache = lily_realloc(emit->save_cache, sizeof(uintptr_t) * \
            emit->save_cache_size); \
    if (save_cache == NULL) \
        lily_raise_nomem(emit->raiser); \
    emit->save_cache = save_cache; \
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

/** Emitter init and deletion **/
lily_emit_state *lily_new_emit_state(lily_raiser *raiser)
{
    lily_emit_state *s = lily_malloc(sizeof(lily_emit_state));

    if (s == NULL)
        return NULL;

    s->patches = lily_malloc(sizeof(int) * 4);
    s->ctrl_patch_starts = lily_malloc(sizeof(int) * 4);
    s->block_var_starts = lily_malloc(sizeof(lily_var *) * 4);
    s->block_save_starts = lily_malloc(sizeof(int) * 4);
    s->block_types = lily_malloc(sizeof(int) * 4);
    s->method_vars = lily_malloc(sizeof(lily_var *) * 4);
    s->save_cache = lily_malloc(sizeof(uintptr_t) * 4);
    s->storage_cache = lily_malloc(sizeof(lily_storage *) * 4);
    s->while_starts = lily_malloc(sizeof(int) * 4);

    if (s->patches == NULL || s->ctrl_patch_starts == NULL ||
        s->block_var_starts == NULL || s->block_save_starts == NULL ||
        s->block_types == NULL || s->method_vars == NULL ||
        s->save_cache == NULL || s->storage_cache == NULL ||
        s->while_starts == NULL) {
        lily_free_emit_state(s);
        return NULL;
    }

    s->patch_pos = 0;
    s->patch_size = 4;
    s->ctrl_patch_pos = 0;
    s->ctrl_patch_size = 4;
    s->block_pos = 0;
    s->block_size = 4;
    s->save_cache_pos = 0;
    s->save_cache_size = 4;
    s->method_pos = 0;
    s->method_size = 4;
    s->storage_cache_pos = 0;
    s->storage_cache_size = 4;
    s->while_start_pos = 0;
    s->while_start_size = 4;

    s->raiser = raiser;
    s->expr_num = 1;

    return s;
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_free(emit->storage_cache);
    lily_free(emit->patches);
    lily_free(emit->ctrl_patch_starts);
    lily_free(emit->block_var_starts);
    lily_free(emit->block_save_starts);
    lily_free(emit->block_types);
    lily_free(emit->method_vars);
    lily_free(emit->save_cache);
    lily_free(emit->while_starts);
    lily_free(emit);
}

/** Shared helper functions **/
static char *opname(lily_expr_op op)
{
    char *opnames[] = {"+", "-", "==", "<", "<=", ">", ">=", "!=", "*", "/",
                       "!", "-", "&&", "||", "=", "*=", "/="};

    return opnames[op];
}

/* get_simple_storage
   This obtains a storage from the class. The signature of the storage will only
   have the class set, so this cannot be used for lists, methods, functions, or
   any class where sig->node.* is used.
   If unsure, use try_get_proper_storage.
   'try' means this will return NULL if it cannot find a proper storage. */
static lily_storage *try_get_simple_storage(lily_emit_state *emit,
        lily_class *storage_class)
{
    lily_storage *s = storage_class->storage;

    if (s->expr_num == emit->expr_num) {
        /* Storages are circularly linked, so this only occurs when all the
           storages have already been taken. */
        if (!lily_try_add_storage(emit->symtab, storage_class->sig))
            return NULL;

        s = s->next;
    }

    s->expr_num = emit->expr_num;
    /* Make it so the next node is grabbed next time. */
    storage_class->storage = s->next;

    return s;
}

/* try_find_storage_by_sig
   This obtains a storage from the emitter's storage cache with a sig that
   matches the given signature. Each of these storages does not change the sig,
   and will also preserve the signature information given. This is suitable for
   lists, methods, functions, and any type where sig->node.* is used.
   If unsure, use try_get_proper_storage.
   'try' means this will return NULL if it cannot find a proper storage. */
lily_storage *try_get_complex_storage(lily_emit_state *emit, lily_sig *sig)
{
    int i;
    lily_storage *result = NULL;

    for (i = 0;i < emit->storage_cache_pos;i++) {
        /* Don't bother with a sig == sig check, since that will never work for
           complex sigs. Do remember to check the expr_num to make sure that
           the storage isn't reused within the same expression. */
        if (lily_sigequal(emit->storage_cache[i]->sig, sig) &&
            emit->expr_num != emit->storage_cache[i]->expr_num) {
            result = emit->storage_cache[i];
            result->expr_num = emit->expr_num;
            break;
        }
    }

    if (result == NULL) {
        if (i == emit->storage_cache_size) {
            lily_storage **new_cache = lily_realloc(emit->storage_cache,
                    sizeof(lily_storage *) * i * 2);
            if (new_cache == NULL)
                return NULL;

            emit->storage_cache = new_cache;
            emit->storage_cache_size *= 2;
        }

        result = sig->cls->storage;
        if (lily_try_add_storage(emit->symtab, sig)) {
            result = result->next;
            result->expr_num = emit->expr_num;
            emit->storage_cache[i] = result;
            emit->storage_cache_pos++;
        }
        else
            result = NULL;
    }

    return result;
}

/* try_get_proper_storage
   This is used to get a storage based off of the given signature. This picks
   the proper way of getting a storage, and should be used instead of guessing,
   which can be error-prone.
   'try' means this will return NULL if it cannot find a proper storage. */
static lily_storage *try_get_proper_storage(lily_emit_state *emit,
        lily_sig *sig)
{
    int cls_id = sig->cls->id;
    lily_storage *result;

    if (cls_id == SYM_CLASS_METHOD ||
        cls_id == SYM_CLASS_FUNCTION ||
        cls_id == SYM_CLASS_LIST)
        result = try_get_complex_storage(emit, sig);
    else
        result = try_get_simple_storage(emit, sig->cls);

    return result;
}

/* emit_return_expected
   This writes o_return_expected with the current line number. This is done to
   prevent methods that should return values from not doing so. */
static void emit_return_expected(lily_emit_state *emit)
{
    lily_method_val *m = emit->top_method;

    WRITE_2(o_return_expected, *emit->lex_linenum)
}

/* Find the inner-most while loop. Don't count a while loop if it's outside of
   the current method though. Returns zero if a method was hit, or the block
   position of the while. */
static int find_deepest_while(lily_emit_state *emit)
{
    int i;

    for (i = emit->block_pos-1;i >= 0;i--) {
        int block_type = emit->block_types[i];

        if (block_type == BLOCK_WHILE)
            break;
        else if (block_type == BLOCK_METHOD) {
            i = 0;
            break;
        }
    }

    return i;
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
        lily_storage *storage;
        storage = try_get_simple_storage(emit, rhs->cls);
        if (storage == NULL) {
            emit->raiser->line_adjust = lhs_ast->line_num;
            lily_raise_nomem(emit->raiser);
        }

        WRITE_4(o_obj_assign,
                lhs_ast->line_num,
                (uintptr_t)lhs_ast->result,
                (uintptr_t)storage)

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

    s = try_get_simple_storage(emit, storage_class);
    if (s == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

    WRITE_5(opcode,
            ast->line_num,
            (uintptr_t)ast->left->result,
            (uintptr_t)ast->right->result,
            (uintptr_t)s)

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

    if ((ast->left->result->flags & VAR_SYM) == 0 &&
        ast->left->tree_type != tree_subscript) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax,
                "Left side of %s is not a var.\n", opname(ast->op));
    }

    if (ast->right->tree_type != tree_var)
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

    WRITE_4(opcode,
            ast->line_num,
            (uintptr_t)right_sym,
            (uintptr_t)left_sym)
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

    if (ast->left->tree_type != tree_var)
        eval_tree(emit, ast->left);

    /* If the left is the same as this tree, then it's already checked itself
       and doesn't need a retest. However, and/or are opposites, so they have
       to check each other (so the op has to be exactly the same). */
    if ((ast->left->tree_type == tree_binary && ast->left->op == ast->op) == 0)
        lily_emit_jump_if(emit, ast->left, jump_on);

    if (ast->right->tree_type != tree_var)
        eval_tree(emit, ast->right);

    lily_emit_jump_if(emit, ast->right, jump_on);

    if (is_top == 1) {
        /* The symtab adds literals 0 and 1 in that order during its init. */
        int save_pos;
        lily_literal *success, *failure;
        lily_class *cls = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);
        result = try_get_simple_storage(emit, cls);
        if (result == NULL) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise_nomem(emit->raiser);
        }

        success = symtab->lit_start;
        if (ast->op == expr_logical_or)
            failure = success->next;
        else {
            failure = success;
            success = failure->next;
        }

        WRITE_4(o_assign,
                ast->line_num,
                (uintptr_t)success,
                (uintptr_t)result);

        WRITE_2(o_jump, 0);
        save_pos = m->pos-1;

        lily_emit_leave_block(emit);
        WRITE_4(o_assign,
                ast->line_num,
                (uintptr_t)failure,
                (uintptr_t)result);

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

    if (ast->right->tree_type != tree_var)
        eval_tree(emit, ast->right);

    rhs = ast->right->result;

    if (var_ast->tree_type != tree_var)
        eval_tree(emit, var_ast);

    if (var_ast->result->sig->cls->id != SYM_CLASS_LIST)
        bad_subs_class(emit, var_ast);

    if (index_ast->tree_type != tree_var)
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

        lily_storage *subs_storage = try_get_proper_storage(emit, elem_sig);
        if (subs_storage == NULL)
            lily_raise_nomem(emit->raiser);

        WRITE_5(o_subscript,
                ast->line_num,
                (uintptr_t)var_ast->result,
                (uintptr_t)index_ast->result,
                (uintptr_t)subs_storage)

        ast->left->result = (lily_sym *)subs_storage;

        /* Run the compound op now that ->left is set properly. */
        emit_op_for_compound(emit, ast);

        rhs = ast->result;
    }

    WRITE_5(o_sub_assign,
            ast->line_num,
            (uintptr_t)var_ast->result,
            (uintptr_t)index_ast->result,
            (uintptr_t)rhs)

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
    if (ast->right->tree_type != tree_var)
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
        lily_storage *storage = try_get_simple_storage(emit, cast_sig->cls);
        if (storage == NULL) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise_nomem(emit->raiser);
        }

        WRITE_4(o_obj_assign,
                ast->line_num,
                (uintptr_t)ast->right->result,
                (uintptr_t)storage)
        ast->result = (lily_sym *)storage;
        return;
    }

    lily_storage *result;

    if (var_sig->cls->id == SYM_CLASS_OBJECT) {
        result = try_get_proper_storage(emit, cast_sig);
        if (result == NULL) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise_nomem(emit->raiser);
        }
    }
    else {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrBadCast,
                "Cannot cast type '%T' to type '%T'.\n",
                var_sig, cast_sig);
    }

    WRITE_4(o_obj_typecast,
            ast->line_num,
            (uintptr_t)ast->right->result,
            (uintptr_t)result)

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
    storage = try_get_simple_storage(emit, integer_cls);
    if (storage == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

    if (ast->op == expr_unary_minus)
        opcode = o_unary_minus;
    else if (ast->op == expr_unary_not)
        opcode = o_unary_not;

    WRITE_4(opcode,
            (uintptr_t)ast->line_num,
            (uintptr_t)ast->left->result,
            (uintptr_t)storage);

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
        lily_storage *obj_store = try_get_simple_storage(emit, cls);
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
                (uintptr_t)arg->result,
                (uintptr_t)obj_store)
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
        if (arg->tree_type != tree_var)
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
    lily_storage *s = try_get_proper_storage(emit, new_sig);

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
        m->code[m->pos + i] = (uintptr_t)arg->result;
    }
    m->code[m->pos+i] = (intptr_t)s;

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

    if (index_ast->tree_type != tree_var)
        eval_tree(emit, index_ast);

    if (index_ast->result->sig->cls->id != SYM_CLASS_INTEGER) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax,
                "Subscript index is not an integer.\n");
    }

    lily_sig *sig_for_result = var_sig->node.value_sig;
    lily_storage *result;

    result = try_get_proper_storage(emit, sig_for_result);
    if (result == NULL) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise_nomem(emit->raiser);
    }

    WRITE_5(o_subscript,
            ast->line_num,
            (uintptr_t)var_ast->result,
            (uintptr_t)index_ast->result,
            (uintptr_t)result);

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

    /* Important! num_args was dropped, so this is the true count. */
    SAVE_PREP(csig->num_args)
    for (i = 0;i != num_args;arg = arg->next_arg, i++) {
        if (arg->tree_type != tree_var) {
            /* Walk the subexpressions so the result gets calculated. */
            eval_tree(emit, arg);
            if (arg->result != NULL) {
                emit->save_cache[emit->save_cache_pos] = (uintptr_t)arg->result;
                emit->save_cache_pos++;
            }
        }

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
            if (arg->tree_type != tree_var) {
                /* Walk the subexpressions so the result gets calculated. */
                eval_tree(emit, arg);
                if (arg->result != NULL) {
                    emit->save_cache[emit->save_cache_pos] = (uintptr_t)arg->result;
                    emit->save_cache_pos++;
                }
            }
            if (!lily_sigequal(arg->result->sig, va_comp_sig)) {
                if (!sigcast(emit, arg, va_comp_sig))
                    bad_arg_error(emit, ast, arg->result->sig, va_comp_sig, i);
            }
        }

        i = (have_args - i);
        if (is_method) {
            lily_storage *s;
            s = try_get_proper_storage(emit, save_sig);
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
                m->code[m->pos+j] = (uintptr_t)arg->result;

            m->code[m->pos+j] = (uintptr_t)s;
            m->pos += j+1;
            save_arg->result = (lily_sym *)s;
            save_arg->next_arg = NULL;
            ast->args_collected = num_args + 1;
        }
    }
}

/* do_save_for_parent
   This is currently a helper for eval_call. This is used to write out all of
   the saves that the call may need. eval_call first checks that the parent
   tree exists and is either tree_binary or tree_list before calling this.

   This is important because multiple calls might return values that need to be
   held in the middle of a calculation. [a, b(), c()] and a = (b + c) + d()
   are both examples of this. However, it is important to make sure that vars
   are never saved. Local vars are already in emitter's save_cache, and doing
   it to global values would be wrong.

   Consider this:
   * a = 10 # and a is global
   * [a, b(), c()]
   * where a is global, and b modifies a to be 20.
   * If this saves 'a' (a global), it will result in the original value of 'a'
     being restored after b is called.

   The takeaway: Never ever save vars within here, or bad things will happen
   that will be really hard to find. */
static void do_save_for_parent(lily_emit_state *emit, lily_ast *ast)
{
    lily_ast *parent = ast->parent;
    lily_tree_type parent_tt = parent->tree_type;

    if (parent_tt == tree_binary && parent->right == ast &&
        parent->left->tree_type != tree_var) {
        /* This is the right side of a binary op, and the left side has a value
           put in a storage. The left has already eval'd, so only the storage
           needs to be saved.
           Example: fib(n-1) + fib(n-2) */
        SAVE_PREP(1)
        emit->save_cache[emit->save_cache_pos] =
                (uintptr_t)ast->parent->left->result;
        emit->save_cache_pos++;
    }
    else if (parent_tt == tree_list) {
        /* This is a bit tougher. The parent is a static list (ex: [a, b, c]).
           Everything that comes before this ast must be saved. This may
           save/restore more than it needs to, but it's better than having a
           storage overwritten.

           This could be improved by saving only what needs to be saved, and
           also by incrementally saving values as it goes along. */
        lily_ast *iter_ast;

        /* This is likely to be too much. However, doing it this way is simpler
           than doing a counting loop, SAVE_PREP, and then a write loop. */
        SAVE_PREP(parent->args_collected)
        for (iter_ast = parent->arg_start;
             iter_ast != ast;
             iter_ast = iter_ast->next_arg) {
            if (iter_ast->tree_type != tree_var) {
                emit->save_cache[emit->save_cache_pos] =
                        (uintptr_t)iter_ast->result;
                emit->save_cache_pos++;
            }
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
    int expect_size, i, is_method, save_start;
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

    /* check_call_args will register each arg to be saved, in case one of
       the args is a call that would modify previous storages. However, once
       those inner calls are done, the storages of the args do not need to
       be saved. This ensures that storages don't get saved (which is
       pointless). */
    save_start = emit->save_cache_pos;
    check_call_args(emit, ast, csig);
    emit->save_cache_pos = save_start;

    if (is_method) {
        lily_ast *parent = ast->parent;
        /* This ensures that storages used by the parent are not overwritten. */
        if (parent != NULL &&
            (parent->tree_type == tree_binary ||
             parent->tree_type == tree_list))
                do_save_for_parent(emit, ast);

        /* o_save needs 2 + #saves, o_restore needs a flat 2. */
        if (emit->save_cache_pos)
            expect_size += emit->save_cache_pos + 4;
    }

    WRITE_PREP_LARGE(expect_size)

    /* Note that parser doesn't register vars in @main, so a method called
       from @main would never save anything. */
    if (is_method && emit->save_cache_pos) {
        m->code[m->pos] = o_save;
        m->code[m->pos+1] = emit->save_cache_pos;
        m->pos += 2;

        memcpy(m->code + m->pos, emit->save_cache,
               emit->save_cache_pos * sizeof(uintptr_t));

        m->pos += emit->save_cache_pos;
    }

    if (is_method)
        m->code[m->pos] = o_method_call;
    else
        m->code[m->pos] = o_func_call;

    m->code[m->pos+1] = ast->line_num;
    m->code[m->pos+2] = (uintptr_t)call_sym;
    m->code[m->pos+3] = ast->args_collected;

    for (i = 4, arg = ast->arg_start;
        arg != NULL;
        arg = arg->next_arg, i++) {
        m->code[m->pos + i] = (uintptr_t)arg->result;
    }

    if (csig->ret != NULL) {
        lily_storage *storage = try_get_proper_storage(emit, csig->ret);
        if (storage == NULL) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise_nomem(emit->raiser);
        }

        ast->result = (lily_sym *)storage;
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
    }

    m->code[m->pos+i] = (uintptr_t)ast->result;
    m->pos += 5 + ast->args_collected;

    if (is_method && emit->save_cache_pos) {
        m->code[m->pos] = o_restore;
        m->code[m->pos+1] = emit->save_cache_pos;
        m->pos += 2;
        emit->save_cache_pos = save_start;
    }
}

/* eval_tree
   This is the main emit function. This doesn't evaluate anything itself, but
   instead determines what call to shove the work off to. */
static void eval_tree(lily_emit_state *emit, lily_ast *ast)
{
    if (ast->tree_type == tree_call)
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
            if (ast->left->tree_type != tree_var)
                eval_tree(emit, ast->left);

            if (ast->right->tree_type != tree_var)
                eval_tree(emit, ast->right);

            emit_binary_op(emit, ast);
        }
    }
    else if (ast->tree_type == tree_parenth) {
        if (ast->arg_start->tree_type != tree_var)
            eval_tree(emit, ast->arg_start);

        ast->result = ast->arg_start->result;
    }
    else if (ast->tree_type == tree_unary) {
        if (ast->left->tree_type != tree_var)
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

/* lily_emit_add_save_var
   This tells the emitter to add a new var to the save cache. This is used to
   keep track of what vars should be saved. */
void lily_emit_add_save_var(lily_emit_state *emit, lily_var *v)
{
    SAVE_PREP(1)
    emit->save_cache[emit->save_cache_pos] = (uintptr_t)v;
    emit->save_cache_pos++;
}

/* lily_emit_ast
   API function to call eval_tree on an ast and increment the expr_num of the
   emitter. */
void lily_emit_ast(lily_emit_state *emit, lily_ast *ast)
{
    eval_tree(emit, ast);
    emit->expr_num++;
}

/* lily_emit_break
   This writes a break (jump to the end of the while) for the parser. Since it
   is called by parser, it needs to verify that it is called from within a
   while. */
void lily_emit_break(lily_emit_state *emit)
{
    lily_method_val *m = emit->top_method;
    int while_pos;

    while_pos = find_deepest_while(emit);

    if (while_pos == 0) {
        /* This is called by parser on the source line, so do not adjust the
           raiser. */
        lily_raise(emit->raiser, lily_ErrSyntax,
                "'break' used outside of a loop.\n");
    }

    if (emit->patch_pos == emit->patch_size) {
        emit->patch_size *= 2;

        int *new_patches = lily_realloc(emit->patches,
            sizeof(int) * emit->patch_size);

        if (new_patches == NULL)
            lily_raise_nomem(emit->raiser);

        emit->patches = new_patches;
    }

    /* Write the jump, then figure out where to put it. */
    WRITE_2(o_jump, 0)

    /* If the while is the most current, then add it to the end. */
    if (while_pos == emit->block_pos-1) {
        emit->patches[emit->patch_pos] = m->pos-1;
        emit->patch_pos++;
    }
    else {
        /* The while is not on top, so this will be fairly annoying... */
        int j, move_by, move_start, next_start;

        /* We can determine the while's patch spot by doing i - method_pos
           since every block but methods adds to ctrl_patch_starts. */
        next_start = (while_pos - emit->method_pos) + 1;
        move_start = emit->ctrl_patch_starts[next_start];
        move_by = emit->patch_pos - move_start;

        /* Move everything after this patch start over one, so that there's a
           hole after the last while patch to write in a new one. */
        memmove(emit->patches+move_start+1, emit->patches+move_start,
                move_by * sizeof(int));
        emit->patch_pos++;
        emit->patches[move_start] = m->pos-1;

        for (j = next_start;j < emit->ctrl_patch_pos;j++)
            emit->ctrl_patch_starts[j] += 1;
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

/* lily_emit_continue
   This emits a jump to go back up to the top of a while. Since this is called
   by parser, it also checks that emitter is in a while. */
void lily_emit_continue(lily_emit_state *emit)
{
    lily_method_val *m = emit->top_method;
    int jump_to, while_pos;

    while_pos = find_deepest_while(emit);

    if (while_pos == 0) {
        /* This is called by parser on the source line, so do not adjust the
           raiser. */
        lily_raise(emit->raiser, lily_ErrSyntax,
                "'continue' used outside of a loop.\n");
    }

    jump_to = emit->while_starts[emit->while_start_pos-1];

    WRITE_2(o_jump, jump_to)
}

/* lily_emit_change_if_branch
   This is called when an if or elif jump has finished, and a new branch is to
   begin. have_else indicates if it's an else branch or an elif branch. */
void lily_emit_change_if_branch(lily_emit_state *emit, int have_else)
{
    int save_jump;
    lily_method_val *m = emit->top_method;
    lily_var *v = emit->block_var_starts[emit->block_pos-1];

    if (emit->block_pos == 1) {
        char *name = (have_else ? "else" : "elif");
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "'%s' without 'if'.\n", name);
    }

    if (have_else) {
        if (emit->block_types[emit->block_pos-1] == BLOCK_IFELSE)
            lily_raise(emit->raiser, lily_ErrSyntax,
                       "Only one 'else' per 'if' allowed.\n");
        else
            emit->block_types[emit->block_pos-1] = BLOCK_IFELSE;
    }
    else if (emit->block_types[emit->block_pos-1] == BLOCK_IFELSE)
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "'elif' after 'else'.\n");

    if (v->next != NULL) {
        lily_drop_block_vars(emit->symtab, v);
        emit->save_cache_pos = emit->block_save_starts[emit->block_pos-1];
    }

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

/* lily_emit_enter_block
   This enters a block of a given block_type. Values for block_type can be
   found in lily_emitter.h */
void lily_emit_enter_block(lily_emit_state *emit, int block_type)
{
    if (emit->block_pos == emit->block_size) {
        emit->block_size *= 2;
        int *new_types = lily_realloc(emit->block_types,
            sizeof(int) * emit->block_size);
        lily_var **new_var_starts = lily_realloc(emit->block_var_starts,
            sizeof(lily_var *) * emit->block_size);
        int *new_save_starts = lily_realloc(emit->block_save_starts,
            sizeof(int) * emit->block_size);

        if (new_types == NULL || new_var_starts == NULL ||
            new_save_starts == NULL) {
            if (new_types != NULL)
                emit->block_types = new_types;
            if (new_var_starts != NULL)
                emit->block_var_starts = new_var_starts;
            if (new_save_starts != NULL)
                emit->block_save_starts = new_save_starts;

            lily_raise_nomem(emit->raiser);
        }

        emit->block_types = new_types;
        emit->block_var_starts = new_var_starts;
        emit->block_save_starts = new_save_starts;
    }

    if (block_type == BLOCK_IF || block_type == BLOCK_ANDOR ||
        block_type == BLOCK_WHILE) {
        if (emit->ctrl_patch_pos == emit->ctrl_patch_size) {
            emit->ctrl_patch_size *= 2;
            int *new_starts = lily_realloc(emit->ctrl_patch_starts,
                sizeof(int) * emit->ctrl_patch_size);

            if (new_starts == NULL)
                lily_raise_nomem(emit->raiser);

            emit->ctrl_patch_starts = new_starts;
        }

        emit->ctrl_patch_starts[emit->ctrl_patch_pos] = emit->patch_pos;
        emit->ctrl_patch_pos++;

        /* While needs to record where it starts at so that the end of the loop
           can jump back up. */
        if (block_type == BLOCK_WHILE) {
            if (emit->while_start_pos == emit->while_start_size) {
                emit->while_start_size *= 2;

                int *new_starts = lily_realloc(emit->while_starts,
                    sizeof(int) * emit->while_start_size);

                if (new_starts == NULL)
                    lily_raise_nomem(emit->raiser);

                emit->while_starts = new_starts;
            }

            emit->while_starts[emit->while_start_pos] = emit->top_method->pos;
            emit->while_start_pos++;
        }

        emit->block_var_starts[emit->block_pos] = emit->symtab->var_top;
    }
    else if (block_type == BLOCK_METHOD) {
        lily_var *v = emit->method_vars[emit->method_pos-1];
        emit->block_var_starts[emit->block_pos] = v;
    }

    emit->block_save_starts[emit->block_pos] = emit->save_cache_pos;
    emit->block_types[emit->block_pos] = block_type;
    emit->block_pos++;
}

/* emit_final_continue
   This writes in a 'continue'-type jump at the end of the while, so that the
   while runs multiple times. Since the while is definitely the top block, this
   is a bit simpler. */
static void emit_final_continue(lily_emit_state *emit)
{
    lily_method_val *m = emit->top_method;
    int jump_to = emit->while_starts[emit->while_start_pos-1];

    WRITE_2(o_jump, jump_to)
}

/* lily_emit_leave_block
   This closes the last block that was added to the emitter. Any vars that were
   added in the block are dropped. */
void lily_emit_leave_block(lily_emit_state *emit)
{
    lily_var *v;
    int block_type;

    if (emit->block_pos == 1)
        lily_raise(emit->raiser, lily_ErrSyntax, "'}' outside of a block.\n");

    block_type = emit->block_types[emit->block_pos-1];

    /* Write in a fake continue so that the end of a while jumps back up to the
       top. */
    if (block_type == BLOCK_WHILE) {
        emit_final_continue(emit);
        emit->while_start_pos--;
    }

    emit->block_pos--;
    v = emit->block_var_starts[emit->block_pos];

    if (block_type != BLOCK_METHOD) {
        emit->ctrl_patch_pos--;

        int from, to, pos;
        from = emit->patch_pos-1;
        to = emit->ctrl_patch_starts[emit->ctrl_patch_pos];
        pos = emit->top_method->pos;

        for (;from >= to;from--)
            emit->top_method->code[emit->patches[from]] = pos;

        /* Use the space for new patches now. */
        emit->patch_pos = to;
    }
    else
        lily_emit_leave_method(emit);

    if (v->next != NULL) {
        lily_drop_block_vars(emit->symtab, v);
        emit->save_cache_pos = emit->block_save_starts[emit->block_pos];
    }
}

/* lily_emit_enter_method
   This enters a method, and then adds that block to the emitter's state. */
void lily_emit_enter_method(lily_emit_state *emit, lily_var *var)
{
    if (emit->method_pos == emit->method_size) {
        emit->method_size *= 2;
        lily_var **new_vars = lily_realloc(emit->method_vars,
            sizeof(lily_var *) * emit->method_size);

        if (new_vars == NULL)
            lily_raise_nomem(emit->raiser);

        emit->method_vars = new_vars;
    }

    emit->top_method = var->value.method;
    emit->top_var = var;
    /* This is called before the args and return are collected, so don't try to
       set the return. It will work, but only because methods default to nil for
       their returns (NULL here). */

    emit->method_vars[emit->method_pos] = var;
    emit->method_pos++;
    lily_emit_enter_block(emit, BLOCK_METHOD);
}

/* lily_emit_update_return
   This is to be called after lily_emit_enter_method once the return value of
   the method is known. */
void lily_emit_update_return(lily_emit_state *emit)
{
    emit->top_method_ret = emit->top_var->sig->node.call->ret;
}

/* lily_emit_leave_method
   This exits the last method entered. For methods that do not return anything,
   this writes a return before exiting. */
void lily_emit_leave_method(lily_emit_state *emit)
{
    /* If the method returns nil, write an implicit 'return' at the end of it.
       It's easiest to just blindly write it. */
    if (emit->top_method_ret == NULL)
        lily_emit_return_noval(emit);
    else
        /* Ensure that methods that claim to return a value cannot leave without
           doing so. */
        emit_return_expected(emit);

    emit->method_pos--;
    /* The stack is ahead, so use pos-1 to get the correct method. */
    lily_var *v = emit->method_vars[emit->method_pos-1];

    emit->top_method = v->value.method;
    emit->top_var = v;
    emit->top_method_ret = v->sig->node.call->ret;
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

    WRITE_4(o_jump_if, jump_on, (uintptr_t)ast->result, 0);
    if (emit->patch_pos == emit->patch_size) {
        emit->patch_size *= 2;

        int *new_patches = lily_realloc(emit->patches,
            sizeof(int) * emit->patch_size);

        if (new_patches == NULL)
            lily_raise_nomem(emit->raiser);

        emit->patches = new_patches;
    }

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
    WRITE_3(o_return_val, ast->line_num, (uintptr_t)ast->result)
}

/* lily_emit_show
   This evals the given ast, then writes o_show so the vm will show the result
   of the ast. Type-checking is intentionally NOT performed. */
void lily_emit_show(lily_emit_state *emit, lily_ast *ast)
{
    eval_tree(emit, ast);
    emit->expr_num++;

    lily_method_val *m = emit->top_method;
    WRITE_3(o_show, ast->line_num, (uintptr_t)ast->result)
}

/* lily_emit_return_noval
   This writes the o_return_noval opcode for a method to return without sending
   a value to the caller. */
void lily_emit_return_noval(lily_emit_state *emit)
{
    /* Don't allow 'return' within @main. */
    if (emit->method_pos == 1)
        lily_raise(emit->raiser, lily_ErrSyntax,
                "'return' used outside of a method.\n");

    lily_method_val *m = emit->top_method;
    WRITE_2(o_return_noval, *emit->lex_linenum)
}

/* lily_emit_vm_return
   This writes the o_vm_return opcode at the end of the @main method. */
void lily_emit_vm_return(lily_emit_state *emit)
{
    lily_method_val *m = emit->top_method;
    WRITE_1(o_return_from_vm)
}

/* lily_reset_main
   This resets the code position of @main, so it can receive new code. */
void lily_reset_main(lily_emit_state *emit)
{
    ((lily_method_val *)emit->top_method)->pos = 0;
}

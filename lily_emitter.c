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
      the first or to succeed or and to fail). **/

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
    s->block_types = lily_malloc(sizeof(int) * 4);
    s->method_vals = lily_malloc(sizeof(lily_method_val *) * 4);
    s->method_rets = lily_malloc(sizeof(lily_sig *) * 4);
    s->method_targets = lily_malloc(sizeof(lily_var *) * 4);
    s->method_id_offsets = lily_malloc(sizeof(int) * 4);
    s->save_cache = lily_malloc(sizeof(uintptr_t) * 4);

    if (s->patches == NULL || s->ctrl_patch_starts == NULL ||
        s->block_var_starts == NULL || s->block_types == NULL ||
        s->method_vals == NULL || s->method_rets == NULL ||
        s->method_targets == NULL || s->method_id_offsets == NULL ||
        s->save_cache == NULL) {
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

    s->raiser = raiser;
    s->expr_num = 1;

    return s;
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_free(emit->patches);
    lily_free(emit->ctrl_patch_starts);
    lily_free(emit->block_var_starts);
    lily_free(emit->block_types);
    lily_free(emit->method_vals);
    lily_free(emit->method_rets);
    lily_free(emit->method_targets);
    lily_free(emit->method_id_offsets);
    lily_free(emit->save_cache);
    lily_free(emit);
}

/** Shared helper functions **/
static char *opname(lily_expr_op op)
{
    char *opnames[] = {"+", "-", "==", "<", "<=", ">", ">=", "!=", "*", "/",
                       "!", "-", "&&", "||", "=", "*=", "/="};

    return opnames[op];
}

static lily_storage *storage_for_class(lily_emit_state *emit,
        lily_class *storage_class)
{
    lily_storage *s = storage_class->storage;
    if (s->expr_num == emit->expr_num) {
        /* Storages are circularly linked, so this only occurs when all the
           storages have already been taken. */
        if (!lily_try_add_storage(emit->symtab, storage_class))
            lily_raise_nomem(emit->raiser);
        s = s->next;
    }

    s->expr_num = emit->expr_num;
    /* Make it so the next node is grabbed next time. */
    storage_class->storage = s->next;

    return s;
}

/** Signature helper functions **/
/* sigequal
   This function checks to see if two signatures hold the same information. */
static int sigequal(lily_sig *lhs, lily_sig *rhs)
{
    int ret;

    if (lhs == rhs)
        ret = 1;
    else {
        if (lhs->cls->id == rhs->cls->id) {
            if (lhs->cls->id == SYM_CLASS_LIST) {
                if (sigequal(lhs->node.value_sig,
                             rhs->node.value_sig)) {
                    ret = 1;
                }
                else
                    ret = 0;
            }
            else
                /* todo: Need to do an in-depth match for calls. */
                ret = 1;
        }
        else
            ret = 0;
    }

    return ret;
}

/* sigcast
   This function is called after sigequal. This checks to see if the signature
   on the left can become the signature on the right. */
static int sigcast(lily_emit_state *emit, lily_ast *lhs_ast, lily_sig *rhs)
{
    int ret;

    if (rhs->cls->id == SYM_CLASS_OBJECT) {
        ret = 1;
        lily_method_val *m = emit->target;
        lily_storage *storage;
        storage = storage_for_class(emit, rhs->cls);
        WRITE_4(o_obj_assign,
                lhs_ast->line_num,
                (uintptr_t)storage,
                (uintptr_t)lhs_ast->result)

        lhs_ast->result = (lily_sym *)storage;
    }
    else
        ret = 0;

    return ret;
}

/* write_sig
   This writes all detailed information about a signature into a message
   buffer. lily_msgbuf's functions are guaranteed to never overflow, so no
   checks are necessary. */
static void write_sig(lily_msgbuf *mb, lily_sig *sig)
{
    lily_msgbuf_add(mb, sig->cls->name);

    if (sig->cls->id == SYM_CLASS_METHOD ||
        sig->cls->id == SYM_CLASS_FUNCTION) {
        lily_call_sig *csig = sig->node.call;
        lily_msgbuf_add(mb, " (");
        int i;
        for (i = 0;i < csig->num_args-1;i++) {
            write_sig(mb, csig->args[i]);
            lily_msgbuf_add(mb, ", ");
        }
        if (i != csig->num_args) {
            write_sig(mb, csig->args[i]);
            if (csig->is_varargs)
                lily_msgbuf_add(mb, "...");
        }
        lily_msgbuf_add(mb, "):");
        if (csig->ret == NULL)
            lily_msgbuf_add(mb, "nil");
        else
            write_sig(mb, csig->ret);
    }
    else if (sig->cls->id == SYM_CLASS_LIST) {
        lily_msgbuf_add(mb, "[");
        write_sig(mb, sig->node.value_sig);
        lily_msgbuf_add(mb, "]");
    }
}

/** Error helpers **/
static void bad_arg_error(lily_emit_state *emit, lily_ast *ast,
    lily_sig *got, int arg_num)
{
    lily_var *v = (lily_var *)ast->result;
    lily_call_sig *csig = v->sig->node.call;

    lily_msgbuf *mb = lily_new_msgbuf(v->name);
    lily_msgbuf_add(mb, " arg #");
    lily_msgbuf_add_int(mb, arg_num);
    lily_msgbuf_add(mb, " expects type '");
    write_sig(mb, csig->args[arg_num]);
    lily_msgbuf_add(mb, "' but got type '");
    write_sig(mb, got);
    lily_msgbuf_add(mb, "'.\n");

    /* Just in case this arg was on a different line than the call. */
    emit->raiser->line_adjust = ast->line_num;
    lily_raise_msgbuf(emit->raiser, lily_ErrSyntax, mb);
}

static void bad_assign_error(lily_emit_state *emit, int line_num,
                          lily_sig *left_sig, lily_sig *right_sig)
{
    /* Remember that right is being assigned to left, so right should
       get printed first. */
    lily_msgbuf *mb = lily_new_msgbuf("Cannot assign ");
    write_sig(mb, right_sig);
    lily_msgbuf_add(mb, " to ");
    write_sig(mb, left_sig);
    lily_msgbuf_add(mb, ".\n");

    emit->raiser->line_adjust = line_num;
    lily_raise_msgbuf(emit->raiser, lily_ErrSyntax, mb);
}

/* bad_subs_class
   Reports that the class of the value in var_ast cannot be subscripted. */
static void bad_subs_class(lily_emit_state *emit, lily_ast *var_ast)
{
    lily_msgbuf *mb = lily_new_msgbuf("Cannot subscript class ");
    write_sig(mb, var_ast->result->sig);
    lily_msgbuf_add(mb, "\n");

    emit->raiser->line_adjust = var_ast->line_num;
    lily_raise_msgbuf(emit->raiser, lily_ErrSyntax, mb);
}

/** ast walking helpers **/
static void walk_tree(lily_emit_state *, lily_ast *);

static void emit_jump_if(lily_emit_state *emit, lily_ast *ast, int jump_on)
{
    lily_method_val *m = emit->target;

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

static void emit_assign(lily_emit_state *emit, lily_ast *ast)
{
    int opcode;
    lily_sym *left_sym, *right_sym;
    lily_method_val *m = emit->target;

    if (ast->right->tree_type != tree_var)
        walk_tree(emit, ast->right);

    left_sym = ast->left->result;
    right_sym = ast->right->result;

    if (left_sym->sig != right_sym->sig) {
        if (left_sym->sig->cls->id == SYM_CLASS_OBJECT)
            opcode = o_obj_assign;
        else if (sigequal(left_sym->sig, right_sym->sig)) {
            if (left_sym->sig->cls->id == SYM_CLASS_LIST)
                opcode = o_list_assign;
        }
        else
            bad_assign_error(emit, ast->line_num, left_sym->sig,
                          right_sym->sig);
    }
    else if (left_sym->sig->cls->id == SYM_CLASS_STR)
        opcode = o_str_assign;
    else if (left_sym->sig->cls->id == SYM_CLASS_OBJECT)
        opcode = o_obj_assign;
    else
        opcode = o_assign;

    WRITE_4(opcode,
            ast->line_num,
            (uintptr_t)left_sym,
            (uintptr_t)right_sym)
}

static void emit_binary_op(lily_emit_state *emit, lily_ast *ast)
{
    int opcode;
    lily_method_val *m;
    lily_class *lhs_class, *rhs_class, *storage_class;
    lily_storage *s;

    m = emit->target;
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

    s = storage_for_class(emit, storage_class);

    WRITE_5(opcode,
            ast->line_num,
            (uintptr_t)ast->left->result,
            (uintptr_t)ast->right->result,
            (uintptr_t)s)

    ast->result = (lily_sym *)s;
}

/* Forward decls of enter/leave block for emit_logical_op. */
void lily_emit_enter_block(lily_emit_state *, int);
void lily_emit_leave_block(lily_emit_state *);

/* This handles both logical and, and logical or. */
static void emit_logical_op(lily_emit_state *emit, lily_ast *ast)
{
    lily_symtab *symtab = emit->symtab;
    lily_storage *result;
    int is_top, jump_on;
    lily_method_val *m = emit->target;

    jump_on = (ast->op == expr_logical_or);

    /* The first tree must create the block, so that subsequent trees have a
       place to write the patches to. */
    if (ast->parent == NULL || 
        (ast->parent->tree_type == tree_binary && ast->parent->op != ast->op)) {
        is_top = 1;
        lily_emit_enter_block(emit, BLOCK_ANDOR);
    }
    else
        is_top = 0;

    /* The bottom tree is responsible for getting the storage. */
    if (ast->left->tree_type != tree_binary || ast->left->op != ast->op) {
        result = storage_for_class(emit,
            lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER));

        if (ast->left->tree_type != tree_var)
            walk_tree(emit, ast->left);

        emit_jump_if(emit, ast->left, jump_on);
    }
    else {
        /* and/or do not require emit_jump_if, because that would be a
           double-check! */
        walk_tree(emit, ast->left);
        result = (lily_storage *)ast->left->result;
    }

    if (ast->right->tree_type != tree_var)
        walk_tree(emit, ast->right);

    emit_jump_if(emit, ast->right, jump_on);

    if (is_top == 1) {
        /* The symtab adds literals 0 and 1 in that order during its init. */
        int save_pos;
        lily_literal *success, *failure;

        success = symtab->lit_start;
        if (ast->op == expr_logical_or)
            failure = success->next;
        else {
            failure = success;
            success = failure->next;
        }

        WRITE_4(o_assign,
                ast->line_num,
                (uintptr_t)result,
                (uintptr_t)success);
        WRITE_2(o_jump, 0);
        save_pos = m->pos-1;

        lily_emit_leave_block(emit);
        WRITE_4(o_assign,
                ast->line_num,
                (uintptr_t)result,
                (uintptr_t)failure);

        m->code[save_pos] = m->pos;
    }

    ast->result = (lily_sym *)result;
}

/* emit_sub_assign
   This handles subscript assignment, which is a bit tricky. */
static void emit_sub_assign(lily_emit_state *emit, lily_ast *ast)
{
    /* It's impossible to walk the subscripts, then walk the assign and try to
       just assign the two. Recall that subscripts will empty their values out
       into a storage. So doing that would result in assigning a value to a
       storage.
       o_subs_assign is the opcode that does this magic, and it takes a var,
       an index, and an rhs. This makes sure that the rhs is assigned to the
       right place in the list, instead of to a storage. */
    lily_method_val *m = emit->target;

    lily_ast *var_ast = ast->left->arg_start;
    lily_ast *index_ast = var_ast->next_arg;
    lily_sym *rhs;
    lily_sig *elem_sig;

    if (var_ast->tree_type != tree_var)
        walk_tree(emit, var_ast);

    if (var_ast->result->sig->cls->id != SYM_CLASS_LIST)
        bad_subs_class(emit, var_ast);

    if (index_ast->tree_type != tree_var)
        walk_tree(emit, index_ast);

    if (index_ast->result->sig->cls->id != SYM_CLASS_INTEGER) {
        emit->raiser->line_adjust = index_ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax,
                   "Subscript index is not an integer.\n");
    }

    /* The subscript assign goes to the element, not the list. So... */
    elem_sig = var_ast->result->sig->node.value_sig;

    if (ast->right->tree_type != tree_var)
        walk_tree(emit, ast->right);

    rhs = ast->right->result;
    if (elem_sig != rhs->sig && !sigequal(elem_sig, rhs->sig) &&
        elem_sig->cls->id != SYM_CLASS_OBJECT) {
        emit->raiser->line_adjust = ast->line_num;
        bad_assign_error(emit, ast->line_num, elem_sig,
                         rhs->sig);
    }

    WRITE_5(o_sub_assign,
            ast->line_num,
            (uintptr_t)var_ast->result,
            (uintptr_t)index_ast->result,
            (uintptr_t)rhs)
}

static void emit_unary_op(lily_emit_state *emit, lily_ast *ast)
{
    uintptr_t opcode;
    lily_class *lhs_class;
    lily_storage *s;
    lily_method_val *m;

    m = emit->target;
    lhs_class = ast->left->result->sig->cls;
    if (lhs_class->id != SYM_CLASS_INTEGER) {
        emit->raiser->line_adjust = ast->line_num;
        lily_raise(emit->raiser, lily_ErrSyntax, "Invalid operation: %s%s.\n",
                   opname(ast->op), lhs_class->name);
    }

    s = storage_for_class(emit,
            lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER));

    if (ast->op == expr_unary_minus)
        opcode = o_unary_minus;
    else if (ast->op == expr_unary_not)
        opcode = o_unary_not;

    WRITE_4(opcode,
            (uintptr_t)ast->line_num,
            (uintptr_t)ast->left->result,
            (uintptr_t)s);

    ast->result = (lily_sym *)s;
}

/* check_call_args
   This verifies that the args collected in an ast match what is expected. This
   function will walk trees if they are an arg. */
static void check_call_args(lily_emit_state *emit, lily_ast *ast,
        lily_call_sig *csig)
{
    lily_ast *arg = ast->arg_start;
    int i, is_varargs, num_args;

    is_varargs = csig->is_varargs;
    num_args = csig->num_args;
    SAVE_PREP(num_args)
    /* The parser has already verified argument count. */
    for (i = 0;i != num_args;arg = arg->next_arg, i++) {
        if (arg->tree_type != tree_var) {
            /* Walk the subexpressions so the result gets calculated. */
            walk_tree(emit, arg);
            if (arg->result != NULL) {
                emit->save_cache[emit->save_cache_pos] = (uintptr_t)arg->result;
                emit->save_cache_pos++;
            }
        }

        if (!sigequal(arg->result->sig, csig->args[i])) {
            if (!sigcast(emit, arg, csig->args[i]))
                bad_arg_error(emit, ast, arg->result->sig, i);
        }
    }

    if (is_varargs) {
        /* Remember that the above finishes with i == num_args, and the
           args are 0-based. So fix i. */
        i--;
        int j = 0;
        for (;arg != NULL;arg = arg->next_arg, j++) {
            if (arg->tree_type != tree_var) {
                /* Walk the subexpressions so the result gets calculated. */
                walk_tree(emit, arg);
                if (arg->result != NULL) {
                    emit->save_cache[emit->save_cache_pos] = (uintptr_t)arg->result;
                    emit->save_cache_pos++;
                }
            }

            if (!sigequal(arg->result->sig, csig->args[i])) {
                if (!sigcast(emit, arg, csig->args[i]))
                    bad_arg_error(emit, ast, arg->result->sig, i);
            }
        }
    }
}

/* walk_tree
   This is the main emit function. It determines what to do given a particular
   ast type. */
static void walk_tree(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->target;

    if (ast->tree_type == tree_call) {
        lily_ast *arg = ast->arg_start;
        lily_var *v = (lily_var *)ast->result;
        lily_call_sig *csig = v->sig->node.call;
        int expect_size, i, is_method, num_local_saves, num_saves;
        lily_var *local_var;

        is_method = (v->sig->cls->id == SYM_CLASS_METHOD);
        expect_size = 5 + ast->args_collected;

        if (!is_method) {
            int cache_start = emit->save_cache_pos;
            check_call_args(emit, ast, csig);

            /* For functions, the args are pushed to be saved in case one of
               them is a method (which will drain the cache). If there were no
               methods, then remove all of the function's args, since they won't
               need saving. */
            if (emit->save_cache_pos > cache_start)
                emit->save_cache_pos = cache_start;

            num_saves = 0;
        }
        else {
            check_call_args(emit, ast, csig);

            if (ast->parent != NULL && ast->parent->tree_type == tree_binary &&
                ast->parent->right == ast &&
                ast->parent->left->tree_type != tree_var) {
                /* Ex: fib(n-1) + fib(n-2)
                   In this case, the left side of the plus (the first fib call)
                   is something saved to a storage. Since the plus hasn't been
                   completed, this call could modify the storage of the left
                   side. So save that storage. */
                SAVE_PREP(1)
                emit->save_cache[emit->save_cache_pos] = (uintptr_t)ast->parent->left->result;
                emit->save_cache_pos++;
            }

            num_saves = emit->save_cache_pos;
            /* Do not save @main's "local's" (they're globals). */
            if (emit->method_pos > 1) {
                num_local_saves = emit->symtab->var_top->id -
                    emit->method_id_offsets[emit->method_pos-1];
                num_saves += num_local_saves;
                local_var = emit->method_targets[emit->method_pos-1]->next;
            }
            else
                num_local_saves = 0;

            /* o_save needs 2 + #saves, o_restore needs a flat 2. */
            if (num_saves)
                expect_size += num_saves + 4;
        }

        WRITE_PREP_LARGE(expect_size)

        if (num_saves) {
            m->code[m->pos] = o_save;
            m->code[m->pos+1] = num_saves;
            m->pos += 2;

            if (emit->save_cache_pos) {
                memcpy(m->code + m->pos, emit->save_cache,
                       emit->save_cache_pos * sizeof(uintptr_t));

                m->pos += emit->save_cache_pos;
                emit->save_cache_pos = 0;
            }
            if (num_local_saves) {
                for (i = 0;i < num_local_saves;i++) {
                    m->code[m->pos+i] = (uintptr_t)local_var;
                    local_var = local_var->next;
                }

                m->pos += num_local_saves;
            }
        } 

        if (is_method)
            m->code[m->pos] = o_method_call;
        else
            m->code[m->pos] = o_func_call;

        m->code[m->pos+1] = ast->line_num;
        m->code[m->pos+2] = (uintptr_t)v;
        m->code[m->pos+3] = ast->args_collected;

        for (i = 5, arg = ast->arg_start;
            arg != NULL;
            arg = arg->next_arg, i++) {
            m->code[m->pos + i] = (uintptr_t)arg->result;
        }

        if (csig->ret != NULL) {
            lily_storage *s = storage_for_class(emit, csig->ret->cls);

            ast->result = (lily_sym *)s;
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

        m->code[m->pos+4] = (uintptr_t)ast->result;
        m->pos += 5 + ast->args_collected;
        if (num_saves) {
            m->code[m->pos] = o_restore;
            m->code[m->pos+1] = num_saves;
            m->pos += 2;
        }
    }
    else if (ast->tree_type == tree_binary) {
        if (ast->op == expr_assign) {
            if (ast->left->tree_type == tree_var)
                emit_assign(emit, ast);
            else if (ast->left->tree_type == tree_subscript)
                emit_sub_assign(emit, ast);
            else {
                emit->raiser->line_adjust = ast->line_num;
                lily_raise(emit->raiser, lily_ErrSyntax,
                           "Left side of = is not a var.\n");
            }
        }
        else if (ast->op == expr_logical_or || ast->op == expr_logical_and)
            emit_logical_op(emit, ast);
        else if (ast->op < expr_assign) {
            if (ast->left->tree_type != tree_var)
                walk_tree(emit, ast->left);

            if (ast->right->tree_type != tree_var)
                walk_tree(emit, ast->right);

            emit_binary_op(emit, ast);
        }
        else {
            /* op > expr_assign. This space is used to denote ops such as *=,
               /=, and others that do some sort of binary op before an
               assignment.
               lhs ?= rhs is equivalent to 'lhs ? rhs -> storage; lhs = storage'
               Since ?= ops only work for integers and numbers... */
            int spoof_op;

            if (ast->left->tree_type != tree_var) {
                emit->raiser->line_adjust = ast->line_num;
                lily_raise(emit->raiser, lily_ErrSyntax,
                           "Left side of %s is not a var.\n", opname(ast->op));
            }
            if (ast->right->tree_type != tree_var)
                walk_tree(emit, ast->right);

            if (ast->op == expr_mul_assign)
                spoof_op = expr_multiply;
            else if (ast->op == expr_div_assign)
                spoof_op = expr_divide;

            /* Pretend the ast is lhs ? rhs, instead of lhs ?= rhs and give it
               to the binary emitter to write the appropriate binary op. This
               will set the ast with the result needed.
               * Note: emit_binary_op will trap for impossible situations like
                 integer *= function. */
            ast->op = spoof_op;
            emit_binary_op(emit, ast);

            WRITE_4(o_assign,
                    ast->line_num,
                    (uintptr_t)ast->left->result,
                    (uintptr_t)ast->result)

            ast->result = ast->left->result;
        }
    }
    else if (ast->tree_type == tree_parenth) {
        if (ast->arg_start->tree_type != tree_var)
            walk_tree(emit, ast->arg_start);
        ast->result = ast->arg_start->result;
    }
    else if (ast->tree_type == tree_unary) {
        if (ast->left->tree_type != tree_var)
            walk_tree(emit, ast->left);

        emit_unary_op(emit, ast);
    }
    else if (ast->tree_type == tree_list) {
        lily_sig *elem_sig = NULL;
        lily_ast *arg;
        int i;
        /* First, get a list storage to hold the values into. The elements
           will not have a class...yet.  */
        lily_class *store_cls = lily_class_by_id(emit->symtab, SYM_CLASS_LIST);
        lily_storage *s = storage_for_class(emit, store_cls);

        /* Walk through all of the list elements, keeping a note of the class
           of the results. The class of the list elements is determined as
           follows:
           * If all results have the same class, then use that class.
           * If they do not, use object. */
        for (arg = ast->arg_start;arg != NULL;arg = arg->next_arg) {
            if (arg->tree_type != tree_var)
                walk_tree(emit, arg);

            if (elem_sig != NULL) {
                if (arg->result->sig != elem_sig &&
                    sigequal(arg->result->sig, elem_sig) == 0) {
                    emit->raiser->line_adjust = arg->line_num;
                    lily_raise(emit->raiser, lily_ErrSyntax,
                            "STUB: list of objects.\n");
                }
            }
            else
                elem_sig = arg->result->sig;
        }
        /* Deref the old sig. The vm will ref it when this o_build_list is
           called. This is done now so emitter has type info, and later for the
           vm to have type info too. */
        if (s->sig->node.value_sig != NULL)
            lily_deref_sig(s->sig->node.value_sig);

        s->sig->node.value_sig = elem_sig;
        elem_sig->refcount++;
        WRITE_PREP_LARGE(ast->args_collected + 5)
        m->code[m->pos] = o_build_list;
        m->code[m->pos+1] = ast->line_num;
        m->code[m->pos+2] = (intptr_t)s;
        m->code[m->pos+3] = ast->args_collected;
        m->code[m->pos+4] = (intptr_t)elem_sig;

        for (i = 5, arg = ast->arg_start;
            arg != NULL;
            arg = arg->next_arg, i++) {
            m->code[m->pos + i] = (uintptr_t)arg->result;
        }

        m->pos += 5 + ast->args_collected;
        ast->result = (lily_sym *)s;
    }
    else if (ast->tree_type == tree_subscript) {
        lily_ast *var_ast = ast->arg_start;
        lily_ast *index_ast = var_ast->next_arg;

        if (var_ast->tree_type != tree_var)
            walk_tree(emit, var_ast);

        lily_sig *var_sig = var_ast->result->sig;
        if (var_sig->cls->id != SYM_CLASS_LIST)
            bad_subs_class(emit, var_ast);

        if (index_ast->tree_type != tree_var)
            walk_tree(emit, index_ast);

        if (index_ast->result->sig->cls->id != SYM_CLASS_INTEGER) {
            emit->raiser->line_adjust = ast->line_num;
            lily_raise(emit->raiser, lily_ErrSyntax,
                    "Subscript index is not an integer.\n");
        }

        lily_class *cls;
        if (var_sig->cls->id == SYM_CLASS_LIST)
            cls = var_sig->node.value_sig->cls;
        else
            cls = NULL;

        lily_storage *result = storage_for_class(emit, cls);

        /* Check the inner sig of the list(1). If there's another list(2)
           inside, then grab the signature of (2) and use that as the inner
           sig of the list to be returned.
           A subscript peels away a single list layer. Since a list storage is
           grabbed, using (1)'s sig would be wrong. Get (2)'s value_sig, since
           the list is essentially (2) now. */
        if (var_sig->node.value_sig != NULL &&
            var_sig->node.value_sig->cls->id == SYM_CLASS_LIST) {
            if (result->sig->node.value_sig != NULL)
                lily_deref_sig(result->sig->node.value_sig);

            result->sig->node.value_sig = var_sig->node.value_sig->node.value_sig;
            result->sig->node.value_sig->refcount++;
        }

        WRITE_5(o_subscript,
                ast->line_num,
                (uintptr_t)var_ast->result,
                (uintptr_t)index_ast->result,
                (uintptr_t)result);

        ast->result = (lily_sym *)result;
    }
}

/** Emitter API functions **/

/* lily_emit_ast
   API function to call walk_tree on an ast and increment the expr_num of the
   emitter. */
void lily_emit_ast(lily_emit_state *emit, lily_ast *ast)
{
    walk_tree(emit, ast);
    emit->expr_num++;
}

/* lily_emit_conditional
   API function to call walk_tree on an ast and increment the expr_num of the
   emitter. This function writes an if jump afterward, for if conditions. */
void lily_emit_conditional(lily_emit_state *emit, lily_ast *ast)
{
    /* This does emitting for the condition of an if or elif. */
    walk_tree(emit, ast);
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
    emit_jump_if(emit, ast, 0);
}

/* lily_emit_change_if_branch
   This is called when an if or elif jump has finished, and a new branch is to
   begin. have_else indicates if it's an else branch or an elif branch. */
void lily_emit_change_if_branch(lily_emit_state *emit, int have_else)
{
    int save_jump;
    lily_method_val *m = emit->target;
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
        int offset_add = lily_drop_block_vars(emit->symtab, v);
        emit->method_id_offsets[emit->method_pos-1] += offset_add;
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

        if (new_types == NULL || new_var_starts == NULL) {
            if (new_types != NULL)
                emit->block_types = new_types;
            if (new_var_starts != NULL)
                emit->block_var_starts = new_var_starts;

            lily_raise_nomem(emit->raiser);
        }
        emit->block_types = new_types;
        emit->block_var_starts = new_var_starts;
    }

    if (block_type == BLOCK_IF || block_type == BLOCK_ANDOR) {
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
        emit->block_var_starts[emit->block_pos] = emit->symtab->var_top;
    }
    else if (block_type == BLOCK_METHOD) {
        lily_var *v = emit->method_targets[emit->method_pos-1];
        emit->block_var_starts[emit->block_pos] = v;
    }

    emit->block_types[emit->block_pos] = block_type;
    emit->block_pos++;
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

    emit->block_pos--;
    v = emit->block_var_starts[emit->block_pos];
    block_type = emit->block_types[emit->block_pos];

    if (block_type < BLOCK_METHOD) {
        emit->ctrl_patch_pos--;

        int from, to, pos;
        from = emit->patch_pos-1;
        to = emit->ctrl_patch_starts[emit->ctrl_patch_pos];
        pos = emit->target->pos;

        for (;from >= to;from--)
            emit->target->code[emit->patches[from]] = pos;

        /* Use the space for new patches now. */
        emit->patch_pos = to;
    }
    else
        lily_emit_leave_method(emit);

    /* This is done after a method leaves, so that the offset gets added into
       the outer method. Otherwise, if a method declares a variable after the
       inner method is done, then saves will be wrong from then on. */
    if (v->next != NULL) {
        int offset_add;
        offset_add = lily_drop_block_vars(emit->symtab, v);
        emit->method_id_offsets[emit->method_pos-1] += offset_add;
    }
}

/* lily_emit_enter_method
   This enters a method, and then adds that block to the emitter's state. */
void lily_emit_enter_method(lily_emit_state *emit, lily_var *var)
{
    if (emit->method_pos == emit->method_size) {
        emit->method_size *= 2;
        lily_method_val **new_vals = lily_realloc(emit->method_vals,
            sizeof(lily_method_val *) * emit->method_size);
        lily_sig **new_rets = lily_realloc(emit->method_rets,
            sizeof(lily_sig *) * emit->method_size);
        lily_var **new_targets = lily_realloc(emit->method_targets,
            sizeof(lily_var *) * emit->method_size);
        int *new_offsets = lily_realloc(emit->method_id_offsets,
            sizeof(int) * emit->method_size);

        if (new_vals == NULL || new_rets == NULL || new_targets == NULL ||
            new_offsets == NULL) {
            if (new_vals != NULL)
                emit->method_vals = new_vals;
            if (new_rets != NULL)
                emit->method_rets = new_rets;
            if (new_targets != NULL)
                emit->method_targets = new_targets;
            if (new_offsets != NULL)
                emit->method_id_offsets = new_offsets;

            lily_raise_nomem(emit->raiser);
        }

        emit->method_vals = new_vals;
        emit->method_rets = new_rets;
        emit->method_targets = new_targets;
        emit->method_id_offsets = new_offsets;
    }

    emit->target = var->value.method;
    emit->method_rets[emit->method_pos] = var->sig->node.call->ret;
    emit->method_vals[emit->method_pos] = emit->target;
    emit->method_targets[emit->method_pos] = var;
    emit->method_id_offsets[emit->method_pos] = var->id;
    emit->method_pos++;
    emit->target_ret = var->sig->node.call->ret;
    lily_emit_enter_block(emit, BLOCK_METHOD);
}

/* lily_emit_leave_method
   This exits the last method entered. For methods that do not return anything,
   this writes a return before exiting. */
void lily_emit_leave_method(lily_emit_state *emit)
{
    /* If the method returns nil, write an implicit 'return' at the end of it.
       It's easiest to just blindly write it. */
    if (emit->target_ret == NULL)
        lily_emit_return_noval(emit);

    emit->method_pos--;
    emit->target = emit->method_vals[emit->method_pos-1];
    emit->target_ret = emit->method_rets[emit->method_pos-1];
}

/* lily_emit_return
   This writes a return statement for a method. This also checks that the
   value given matches what the method says it returns. */
void lily_emit_return(lily_emit_state *emit, lily_ast *ast, lily_sig *ret_sig)
{
    walk_tree(emit, ast);
    emit->expr_num++;

    /* todo: Expand this to cover complex (ex: func/class) sigs later, but only
       when those can be returned. */
    if (ast->result->sig != ret_sig) {
        lily_msgbuf *mb = lily_new_msgbuf("'return' expected type '");
        write_sig(mb, ret_sig);
        lily_msgbuf_add(mb, "' but got type '");
        write_sig(mb, ast->result->sig);
        lily_msgbuf_add(mb, "'.\n");

        emit->raiser->line_adjust = ast->line_num;
        lily_raise_msgbuf(emit->raiser, lily_ErrSyntax, mb);
    }

    lily_method_val *m = emit->target;
    WRITE_2(o_return_val, (uintptr_t)ast->result)
}

/* lily_emit_return_noval
   This writes the o_return_noval opcode for a method to return without sending
   a value to the caller. */
void lily_emit_return_noval(lily_emit_state *emit)
{
    lily_method_val *m = emit->target;
    WRITE_1(o_return_noval)
}

/* lily_emit_vm_return
   This writes the o_vm_return opcode at the end of the @main method. */
void lily_emit_vm_return(lily_emit_state *emit)
{
    lily_method_val *m = emit->target;
    WRITE_1(o_vm_return)
}

/* lily_reset_main
   This resets the code position of @main, so it can receive new code. */
void lily_reset_main(lily_emit_state *emit)
{
    ((lily_method_val *)emit->target)->pos = 0;
}

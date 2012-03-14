#include <stdlib.h>

#include "lily_ast.h"
#include "lily_impl.h"
#include "lily_emitter.h"
#include "lily_opcode.h"
#include "lily_emit_table.h"

#define WRITE_PREP(size) \
if ((m->pos + size) > m->len) { \
    m->len *= 2; \
    m->code = lily_realloc(m->code, sizeof(int) * m->len); \
    if (m->code == NULL) \
        lily_raise_nomem(emit->error); \
}

#define WRITE_1(one) \
WRITE_PREP(1) \
m->code[m->pos] = one; \
m->pos += 1;

/* No WRITE_2, because nothing uses that. */

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

/* The emitter sets error->line_adjust with a better line number before calling
   lily_raise. This gives debuggers a chance at a more useful line number.
   Example: integer a = 1.0 +
   1.0
   * line_adjust would be 1 (where the assignment happens), whereas the lexer
   would have the line at 2 (where the 1.0 is collected).
   * Note: This currently excludes nomem errors. */
static char *opname(lily_expr_op op)
{
    char *opnames[] = {"+", "-", "==", "<", "<=", ">", ">=", "="};

    return opnames[op];
}

static void generic_binop(lily_emit_state *emit, lily_ast *ast)
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
        emit->error->line_adjust = ast->line_num;
        lily_raise(emit->error, "Invalid operation: %s %s %s.\n",
                   lhs_class->name, opname(ast->op), rhs_class->name);
    }

    if (ast->op == expr_plus || ast->op == expr_minus)
        if (lhs_class->id >= rhs_class->id)
            storage_class = lhs_class;
        else
            storage_class = rhs_class;
    else
        /* Assign is handled elsewhere, so these are just comparison ops. These
           always return 0 or 1, regardless of the classes put in. There's no
           bool class (yet), so an integer class is used instead. */
        storage_class = lily_class_by_id(emit->symtab, SYM_CLASS_INTEGER);

    s = storage_class->storage;

    /* Remember that storages are circularly-linked, so if this one
       doesn't work, then all are currently taken. */
    if (s->expr_num != emit->expr_num) {
        /* Add and use a new one. */
        if (!lily_try_add_storage(emit->symtab, storage_class))
            lily_raise_nomem(emit->error);
        s = s->next;
    }

    s->expr_num = emit->expr_num;
    /* Make it so the next node is grabbed next time. */
    storage_class->storage = s->next;

    WRITE_4(opcode,
            (int)ast->left->result,
            (int)ast->right->result,
            (int)s)

    ast->result = (lily_sym *)s;
}

static void write_type(lily_msgbuf *mb, lily_sig *sig)
{
    /* todo: Dump complex stuff for functions and methods. */
    lily_msgbuf_add(mb, sig->cls->name);
}

static void walk_tree(lily_emit_state *emit, lily_ast *ast)
{
    lily_method_val *m = emit->target;

    if (ast->expr_type == func_call) {
        int i, new_pos;

        lily_ast *arg = ast->arg_start;
        lily_var *v = (lily_var *)ast->result;
        lily_func_sig *func_sig = v->sig->node.func;

        /* The parser has already verified argument count. */
        for (i = 0;arg != NULL;arg = arg->next_arg, i++) {
            if (arg->expr_type != var)
                /* Walk the subexpressions so the result gets calculated. */
                walk_tree(emit, arg);

            /* This currently works because there are no nested funcs or
               methods. */
            if (func_sig->args[i] != arg->result->sig) {
                lily_msgbuf *mb = lily_new_msgbuf("Error : ");
                lily_msgbuf_add(mb, v->name);
                lily_msgbuf_add(mb, " arg #");
                lily_msgbuf_add_int(mb, i);
                lily_msgbuf_add(mb, " expects type '");
                write_type(mb, func_sig->args[i]);
                lily_msgbuf_add(mb, "' but got type '");
                write_type(mb, arg->result->sig);
                lily_msgbuf_add(mb, "'.\n");
                lily_raise_msgbuf(emit->error, mb);
            }
        }

        new_pos = m->pos + 4 + ast->args_collected;
        /* m->pos is implicitly added, so this next line is right. */
        WRITE_PREP(4 + ast->args_collected)

        m->code[m->pos] = o_func_call;
        /* The debugger uses this to show where the func was declared. */
        m->code[m->pos+1] = (int)v;
        /* Do this once, so the vm doesn't have to each pass. */
        m->code[m->pos+2] = (int)v->value.ptr;
        m->code[m->pos+3] = ast->args_collected;
        for (i = 4, arg = ast->arg_start;
             arg != NULL;
             arg = arg->next_arg) {
            m->code[m->pos + i] = (int)arg->result;
        }
        m->pos = new_pos;
    }
    else if (ast->expr_type == binary) {
        /* lhs and rhs must be walked first, regardless of op. */
        if (ast->left->expr_type != var)
            walk_tree(emit, ast->left);

        if (ast->right->expr_type != var)
            walk_tree(emit, ast->right);

        if (ast->op == expr_assign) {
            int opcode;
            lily_sym *left_sym, *right_sym;
            left_sym = ast->left->result;
            right_sym = ast->right->result;

            if (left_sym->sig != right_sym->sig) {
                if (left_sym->sig->cls->id == SYM_CLASS_OBJECT)
                    opcode = o_obj_assign;
                else {
                    emit->error->line_adjust = ast->line_num;
                    lily_raise(emit->error, "Cannot assign %s to %s.\n",
                               left_sym->sig->cls->name,
                               right_sym->sig->cls->name);
                }
            }
            else
                opcode = o_assign;

            WRITE_3(opcode,
                    (int)left_sym,
                    (int)right_sym)
        }
        else
            generic_binop(emit, ast);
    }
}

void lily_emit_ast(lily_emit_state *emit, lily_ast *ast)
{
    walk_tree(emit, ast);
    emit->expr_num++;
}

void lily_emit_conditional(lily_emit_state *emit, lily_ast *ast)
{
    /* This does emitting for the condition of an if or elif. */
    lily_method_val *m = emit->target;

    walk_tree(emit, ast);
    emit->expr_num++;

    /* This jump will need to be rewritten with the first part of the next elif,
       else, or the end of the if. Save the position so it can be written over
       later. */
    WRITE_3(o_jump_if_false, (int)ast->result, 0)

    lily_branches *b = emit->branches;

    if (b->patch_pos == b->patch_size) {
        int *new_patches;

        b->patch_size *= 2;
        new_patches = lily_realloc(b->patches, b->patch_size);
        if (new_patches == NULL)
            lily_raise_nomem(emit->error);

        b->patches = new_patches;
    }

    b->patches[b->patch_pos] = m->pos-1;
    b->patch_pos++;
}

/*  Okay, if handling. This is a tough one.
    VM code is expressed as a series of instructions, but ifs require skipping
    some parts of code. This is done with a jump instruction. Since the future
    location is not known, it has to be fixed later on. Each if/elif/else is
    refered to as a branch.
    Within if/elif/else, there are two kinds of jumps:
    * Branch->branch. When the expression for an if tests false, control must
      transfer to the next elif, or the else. This is the same for elif's.
    * Exit. After the final statement in a branch, a jump is needed to avoid
      execution of any other code within the if.
    */

void lily_emit_new_if(lily_emit_state *emit)
{
    lily_branches *branches = emit->branches;

    if (branches->save_pos + 2 > branches->save_size) {
        lily_raise(emit->error, "todo: realloc saves. Have %d, need %d.\n");
    }

    branches->saved_spots[branches->save_pos] = branches->patch_pos;
    branches->saved_spots[branches->save_pos+1] = branches->patch_pos;
    branches->save_pos += 2;
}

void lily_emit_fix_branch_jumps(lily_emit_state *emit)
{
    int *code;
    int i;
    lily_branches *branches = emit->branches;

    code = emit->target->code;
    for (i = 0;i < branches->patch_pos;i++)
        code[branches->patches[i]] = emit->target->pos;
}

void lily_emit_fix_exit_jumps(lily_emit_state *emit)
{
    lily_branches *branches = emit->branches;
    lily_method_val *m = emit->target;
    int from, to;

    /* When closing an if, the last branch jump will go to the same place as
       the exit jump. */
    from = branches->patch_pos-1;
    to = branches->saved_spots[branches->save_pos-2];

    for (;from >= to;from--)
        m->code[branches->patches[from]] = m->pos;

    branches->patch_pos = from+1;
    fprintf(stderr, "patch pos is %d.\n", from);
    branches->save_pos -= 2;
}

void lily_emit_set_target(lily_emit_state *emit, lily_var *var)
{
    emit->target = (lily_method_val *)var->value.ptr;
}

void lily_emit_vm_return(lily_emit_state *emit)
{
    lily_method_val *m = emit->target;
    WRITE_1(o_vm_return)
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_free(emit->branches->patches);
    lily_free(emit->branches->saved_spots);
    lily_free(emit->branches);
    lily_free(emit);
}

lily_emit_state *lily_new_emit_state(lily_excep_data *excep)
{
    lily_emit_state *s = lily_malloc(sizeof(lily_emit_state));

    if (s == NULL)
        return NULL;

    int *patches = lily_malloc(sizeof(int) * 4);
    int *saves = lily_malloc(sizeof(int) * 4);
    s->branches = lily_malloc(sizeof(lily_branches));

    if (s->branches == NULL || patches == NULL || saves == NULL) {
        lily_free(s->branches);
        lily_free(saves);
        lily_free(patches);
        lily_free(s);
        return NULL;
    }

    s->branches->patches = patches;
    s->branches->saved_spots = saves;
    s->branches->save_pos = 0;
    s->branches->save_size = 4;
    s->branches->patch_pos = 0;
    s->branches->patch_size = 4;
    s->target = NULL;
    s->error = excep;
    s->expr_num = 1;

    return s;
}

#include <stdlib.h>

#include "lily_ast.h"
#include "lily_impl.h"
#include "lily_emitter.h"
#include "lily_opcode.h"
#include "lily_emit_table.h"

/* The emitter sets error->line_adjust with a better line number before calling
   lily_raise. This gives debuggers a chance at a more useful line number.
   Example: integer a = 1.0 +
   1.0
   * line_adjust would be 1 (where the assignment happens), whereas the lexer
   would have the line at 2 (where the 1.0 is collected).
   * Note: This currently excludes nomem errors. */
static char *opname(lily_expr_op op)
{
    char *ret;

    switch (op) {
        case expr_assign:
            ret = "assign";
            break;
        case expr_plus:
            ret = "add";
            break;
        case expr_minus:
            ret = "minus";
            break;
        default:
            ret = "undefined";
            break;
    }

    return ret;
}

static void generic_binop(lily_emit_state *emit, lily_ast *ast)
{
    int opcode;
    lily_func_prop *fp;
    lily_class *lhs_class, *rhs_class, *storage_class;
    lily_storage *s;

    fp = emit->target;
    lhs_class = ast->left->result->sig->cls;
    rhs_class = ast->right->result->sig->cls;

    if (lhs_class->id <= SYM_CLASS_STR &&
        rhs_class->id <= SYM_CLASS_STR)
        opcode = generic_binop_table[ast->op][lhs_class->id][rhs_class->id];
    else
        opcode = -1;

    if (opcode == -1) {
        emit->error->line_adjust = ast->line_num;
        lily_raise(emit->error, "Cannot %s %s and %s.\n", opname(ast->op),
                   lhs_class->name,
                   rhs_class->name);
    }

    if (lhs_class->id >= rhs_class->id)
        storage_class = lhs_class;
    else
        storage_class = rhs_class;

    s = storage_class->storage;

    /* Remember that storages are circularly-linked, so if this one
       doesn't work, then all are currently taken. */
    if (s->expr_num != emit->expr_num) {
        /* Add and use a new one. */
        lily_add_storage(emit->symtab, s);
        s = s->next;
    }

    s->expr_num = emit->expr_num;
    /* Make it so the next node is grabbed next time. */
    storage_class->storage = s->next;

    if ((fp->pos + 4) > fp->len) {
        fp->len *= 2;
        fp->code = lily_realloc(fp->code, sizeof(int) * fp->len);
        if (fp->code == NULL)
            lily_raise_nomem(emit->error);
    }

    fp->code[fp->pos] = opcode;
    fp->code[fp->pos+1] = (int)ast->left->result;
    fp->code[fp->pos+2] = (int)ast->right->result;
    fp->code[fp->pos+3] = (int)s;
    fp->pos += 4;

    ast->result = (lily_sym *)s;
}

static void walk_tree(lily_emit_state *emit, lily_ast *ast)
{
    lily_func_prop *fp = emit->target;

    if (ast->expr_type == func_call) {
        int i, new_pos;

        lily_ast *arg = ast->arg_start;
        while (arg != NULL) {
            if (arg->expr_type != var)
                /* Walk the subexpressions so the result gets calculated. */
                walk_tree(emit, arg);

            arg = arg->next_arg;
        }

        new_pos = fp->pos + 4 + ast->args_collected;
        if (new_pos > fp->len) {
            fp->len *= 2;
            fp->code = lily_realloc(fp->code, sizeof(int) * fp->len);
            if (fp->code == NULL)
                lily_raise_nomem(emit->error);
        }

        lily_var *v = (lily_var *)ast->result;

        fp->code[fp->pos] = o_func_call;
        /* The debugger uses this to show where the func was declared. */
        fp->code[fp->pos+1] = (int)v;
        /* Do this once, so the vm doesn't have to each pass. */
        fp->code[fp->pos+2] = (int)((lily_func_prop *)v->properties)->func;
        fp->code[fp->pos+3] = ast->args_collected;
        for (i = 4, arg = ast->arg_start;
             arg != NULL;
             arg = arg->next_arg) {
            fp->code[fp->pos + i] = (int)arg->result;
        }
        fp->pos = new_pos;
    }
    else if (ast->expr_type == binary) {
        /* lhs and rhs must be walked first, regardless of op. */
        if (ast->left->expr_type != var)
            walk_tree(emit, ast->left);

        if (ast->right->expr_type != var)
            walk_tree(emit, ast->right);

        if (ast->op == expr_assign) {
            if (ast->left->result->sig != ast->right->result->sig) {
                emit->error->line_adjust = ast->line_num;
                lily_raise(emit->error, "Cannot assign %s to %s.\n",
                           ast->right->result->sig->cls->name,
                           ast->left->result->sig->cls->name);
            }

            if ((fp->pos + 3) > fp->len) {
                fp->len *= 2;
                fp->code = lily_realloc(fp->code, sizeof(int) * fp->len);
                if (fp->code == NULL)
                    lily_raise_nomem(emit->error);
            }

            fp->code[fp->pos] = o_assign;
            fp->code[fp->pos+1] = (int)ast->left->result;
            fp->code[fp->pos+2] = (int)ast->right->result;
            fp->pos += 3;
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

void lily_emit_set_target(lily_emit_state *emit, lily_var *var)
{
    emit->target = var->properties;
}

void lily_emit_vm_return(lily_emit_state *emit)
{
    lily_func_prop *fp = emit->target;
    if ((fp->pos + 1) > fp->len) {
        fp->len *= 2;
        fp->code = lily_realloc(fp->code, sizeof(int) * fp->len);
        if (fp->code == NULL)
            lily_raise_nomem(emit->error);
    }

    fp->code[fp->pos] = o_vm_return;
    fp->pos++;
}

void lily_free_emit_state(lily_emit_state *emit)
{
    lily_free(emit);
}

lily_emit_state *lily_new_emit_state(lily_excep_data *excep)
{
    lily_emit_state *s = lily_malloc(sizeof(lily_emit_state));

    if (s == NULL)
        return NULL;

    s->target = NULL;
    s->error = excep;
    s->expr_num = 1;

    return s;
}

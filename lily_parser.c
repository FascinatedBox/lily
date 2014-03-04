#include <string.h>

#include "lily_ast.h"
#include "lily_emitter.h"
#include "lily_impl.h"
#include "lily_lexer.h"
#include "lily_parser.h"
#include "lily_msgbuf.h"
#include "lily_symtab.h"
#include "lily_vm.h"

/** Parser is responsible for:
    * Creating all other major structures (ast pool, emitter, lexer, etc.)
    * Ensuring that all other major structures are deleted.
    * Holding the startup functions (lily_parse_file and others).
    * All parsing.

    Notes:
    * Parser uses a signature stack to hold signatures while processing complex
      var information. This is used to keep parser from leaking memory, since
      parser functions often call lily_raise.
    * Parser checks for proper form, but does not verify call argument counts,
      proper types for assignment, etc. AST handles argument counts, and
      emitter checks types.
    * 'Forward token' is extremely important to parser. This means that
      caller functions will call lily_lexer to get the token ready before
      calling other parser functions. This allows parser to do token lookaheads
      without a penalty: Since a calling function has to get the token ready,
      it can check for a certain value and call lily_lexer again if it needs to
      do so.
**/

/** Flags for expression and collect_var_sig. Expand upon these as necessary
    to add to the functionality of each. **/

/* First, the flags for expression. */

/* Execute only one expression. */
#define EX_SINGLE     0x001
/* Get the lhs via expression_value. */
#define EX_NEED_VALUE 0x002
/* For if and elif to work, the expression has to be tested for being true or
   false. This tells the emitter to write in that test (o_jump_if_false) after
   writing the condition. This must be done within expression, because the
   ast is 'cleaned' by expression after each run. */
#define EX_CONDITION  0x004
/* Don't clear the ast within 'expression'. This allows the finished ast to be
   inspected. */
#define EX_SAVE_AST   0x010

/* These flags are for collect_var_sig. */

/* Expect a name with every class given. Create a var for each class+name pair.
   This is suitable for collecting the args of a method. */
#define CV_MAKE_VARS  0x020

/* This is set if the variable is not inside another variable. This is suitable
   for collecting a method that may have named arguments. */
#define CV_TOPLEVEL   0x040

#define NEED_NEXT_TOK(expected) \
lily_lexer(lex); \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_ErrSyntax, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token)); \

#define NEED_CURRENT_TOK(expected) \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_ErrSyntax, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token));

/* table[token] = true/false. Used to check if a given token can be the start of
   an expression. */
static const int is_start_val[] = {
    0,
    0,
    0,
    0,
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    1,
    1,
    1,
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0
};

/* table[token] = binary_op. -1 indicates invalid. */
static const int bin_op_for_token[] = {
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    expr_bitwise_xor,
    expr_assign,
    expr_eq_eq,
    -1,
    expr_not_eq,
    expr_modulo,
    expr_modulo_assign,
    expr_multiply,
    expr_mul_assign,
    expr_divide,
    expr_div_assign,
    expr_plus,
    expr_plus_assign,
    expr_minus,
    expr_minus_assign,
    expr_lt,
    expr_lt_eq,
    expr_left_shift,
    expr_left_shift_assign,
    expr_gr,
    expr_gr_eq,
    expr_right_shift,
    expr_right_shift_assign,
    -1,
    -1,
    -1,
    -1,
    -1,
    expr_bitwise_and,
    expr_logical_and,
    expr_bitwise_or,
    expr_logical_or,
    -1,
    -1,
    -1,
    -1,
    -1
};

/** Parser initialization and deletion **/
lily_parse_state *lily_new_parse_state()
{
    lily_parse_state *parser = lily_malloc(sizeof(lily_parse_state));
    lily_raiser *raiser = lily_new_raiser();

    if (parser == NULL)
        return NULL;

    parser->sig_stack_pos = 0;
    parser->sig_stack_size = 4;
    parser->raiser = raiser;
    parser->sig_stack = lily_malloc(4 * sizeof(lily_sig *));
    parser->ast_pool = lily_new_ast_pool(raiser, 8);
    parser->symtab = lily_new_symtab(raiser);
    parser->emit = lily_new_emit_state(raiser);
    parser->lex = lily_new_lex_state(raiser);
    parser->vm = lily_new_vm_state(raiser);

    if (parser->raiser == NULL || parser->sig_stack == NULL ||
        parser->lex == NULL || parser->emit == NULL || parser->symtab == NULL ||
        parser->ast_pool == NULL || parser->vm == NULL ||
        lily_emit_try_enter_main(parser->emit,
                                 parser->symtab->var_start) == 0) {
        lily_free_parse_state(parser);

        return NULL;
    }

    parser->vm->main = parser->symtab->var_start;
    parser->symtab->lex_linenum = &parser->lex->line_num;
    parser->ast_pool->lex_linenum = &parser->lex->line_num;
    parser->emit->lex_linenum = &parser->lex->line_num;
    parser->emit->symtab = parser->symtab;

    return parser;
}

void lily_free_parse_state(lily_parse_state *parser)
{
    if (parser->raiser)
        lily_free_raiser(parser->raiser);

    if (parser->ast_pool)
        lily_free_ast_pool(parser->ast_pool);

    if (parser->vm)
        lily_free_vm_state(parser->vm);

    if (parser->symtab)
        lily_free_symtab(parser->symtab);

    if (parser->lex)
        lily_free_lex_state(parser->lex);

    if (parser->emit)
        lily_free_emit_state(parser->emit);

    lily_free(parser->sig_stack);
    lily_free(parser);
}

/** Shared code **/
/* get_named_var
   This calls lexer to get a name, then uses that to declare a new var. The
   var is checked for having a unique name. var_sig is the signature used to
   create the new var.

   Success: The newly created var is returned.
   Failure: An error is raised. */
static lily_var *get_named_var(lily_parse_state *parser, lily_sig *var_sig)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var;
    NEED_NEXT_TOK(tk_word)

    var = lily_var_by_name(parser->symtab, lex->label, lex->label_shorthash);
    if (var != NULL)
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "%s has already been declared.\n", lex->label);

    var = lily_try_new_var(parser->symtab, var_sig, lex->label,
            lex->label_shorthash);
    if (var == NULL)
        lily_raise_nomem(parser->raiser);

    return var;
}

/** Var sig collection
  * collect_* functions are used by collect_var_sig as helpers, and should not
    be called by any function except expression itself.
  * This handles sig collection for complex signatures (lists and methods, for
    example). It shouldn't be called for simple classes.
  * Sigs are put into the parser's sig stack to keep them from being leaked
    in case of lily_raise/lily_raise_nomem being called. **/
static lily_sig *collect_var_sig(lily_parse_state *parser, int flags);

static void grow_sig_stack(lily_parse_state *parser)
{
    parser->sig_stack_size *= 2;

    lily_sig **new_sig_stack = lily_realloc(parser->sig_stack,
        sizeof(lily_sig *) * parser->sig_stack_size);

    if (new_sig_stack == NULL)
        lily_raise_nomem(parser->raiser);

    parser->sig_stack = new_sig_stack;
}

static void collect_call_args(lily_parse_state *parser, int flags,
        lily_sig *call_sig, lily_var *call_var)
{
    int call_flags = 0;
    int i = 0;
    int save_pos = parser->sig_stack_pos;
    lily_lex_state *lex = parser->lex;

    /* A callable's arguments are named if it's not an argument to something
       else. */
    if (flags & CV_TOPLEVEL)
        call_flags |= CV_MAKE_VARS;

    while (1) {
        if (parser->sig_stack_pos == parser->sig_stack_size)
            grow_sig_stack(parser);

        lily_sig *arg_sig = collect_var_sig(parser, call_flags);
        parser->sig_stack[parser->sig_stack_pos] = arg_sig;
        parser->sig_stack_pos++;

        lily_lexer(lex);
        if (lex->token == tk_right_parenth ||
            lex->token == tk_three_dots)
            break;

        NEED_CURRENT_TOK(tk_comma)
        lily_lexer(lex);
    }

    /* +1 for the return which gets added at [0] later. */
    int num_args = parser->sig_stack_pos - save_pos;
    lily_sig **siglist = lily_malloc((num_args + 1) * sizeof(lily_sig *));
    if (siglist == NULL)
        lily_raise_nomem(parser->raiser);

    for (i = 0;i < num_args;i++)
        siglist[i + 1] = parser->sig_stack[save_pos + i];

    siglist[0] = NULL;
    call_sig->siglist_size = i + 1;
    call_sig->siglist = siglist;
    parser->sig_stack_pos = save_pos;

    /* If ... is at the end, then the call is varargs (and the last sig is
       the sig that will use them). */
    if (lex->token == tk_three_dots) {
        if (num_args == 0)
            lily_raise(parser->raiser, lily_ErrSyntax,
                       "Unexpected token %s.\n", tokname(lex->token));

        if (siglist[i]->cls->id != SYM_CLASS_LIST) {
            lily_raise(parser->raiser, lily_ErrSyntax,
                    "A list is required for variable arguments (...).\n");
        }

        call_sig->flags |= SIG_IS_VARARGS;
        NEED_NEXT_TOK(tk_right_parenth)
    }
}

/* collect_var_sig
   This is the entry point for complex signature collection. */
static lily_sig *collect_var_sig(lily_parse_state *parser, int flags)
{
    lily_lex_state *lex = parser->lex;
    lily_class *cls;

    NEED_CURRENT_TOK(tk_word)
    cls = lily_class_by_hash(parser->symtab, lex->label_shorthash);
    if (cls == NULL)
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "unknown class name %s.\n", lex->label);

    lily_sig *result;

    if (cls->id != SYM_CLASS_METHOD && cls->id != SYM_CLASS_FUNCTION &&
        cls->id != SYM_CLASS_LIST) {
        result = cls->sig;
        if (flags & CV_MAKE_VARS)
            get_named_var(parser, cls->sig);
    }
    else if (cls->id == SYM_CLASS_LIST) {
        NEED_NEXT_TOK(tk_left_bracket)
        lily_lexer(lex);
        result = collect_var_sig(parser, 0);
        NEED_NEXT_TOK(tk_right_bracket)

        lily_sig *list_sig = lily_try_sig_for_class(parser->symtab, cls);
        if (list_sig == NULL)
            lily_raise_nomem(parser->raiser);

        list_sig->siglist = lily_malloc(sizeof(lily_sig));
        if (list_sig->siglist == NULL)
            lily_raise_nomem(parser->raiser);

        list_sig->siglist[0] = result;
        list_sig->siglist_size = 1;
        list_sig = lily_ensure_unique_sig(parser->symtab, list_sig);

        result = list_sig;
        if (flags & CV_MAKE_VARS)
            get_named_var(parser, result);
    }
    else if (cls->id == SYM_CLASS_METHOD ||
             cls->id == SYM_CLASS_FUNCTION) {
        lily_sig *call_sig = lily_try_sig_for_class(parser->symtab, cls);
        lily_var *call_var;

        if (call_sig == NULL)
            lily_raise_nomem(parser->raiser);

        if (flags & CV_MAKE_VARS) {
            call_var = get_named_var(parser, call_sig);

            if (flags & CV_TOPLEVEL)
                lily_emit_enter_block(parser->emit, BLOCK_METHOD);
        }

        NEED_NEXT_TOK(tk_left_parenth)
        lily_lexer(lex);
        if (lex->token != tk_right_parenth)
            collect_call_args(parser, flags, call_sig, call_var);
        else {
            call_sig->siglist = lily_malloc(2 * sizeof(lily_sig));
            if (call_sig->siglist == NULL)
                lily_raise_nomem(parser->raiser);

            call_sig->siglist[0] = NULL;
            call_sig->siglist[1] = NULL;
            call_sig->siglist_size = 1;
        }

        NEED_NEXT_TOK(tk_colon)
        NEED_NEXT_TOK(tk_word)
        if (strcmp(lex->label, "nil") == 0)
            call_sig->siglist[0] = NULL;
        else {
            /* It isn't nil, so it's an argument to be scanned. There's a bit of
               an interesting problem here:

               The method currently has NULL as the return, which means there
               is none. It's also been added to symtab's chain of sigs. If the
               return that gets scanned is the same part as the rest, then
               symtab assumes they are the same. This results in the method
               having itself as a return, which causes an infinite loop when
               anything is done with that sig.

               This is rather easy to do: 'method m(): method() :nil'.

               The solution is to temporarily say that the call sig has 1 arg.
               This makes it impossible to match, but also makes sure that the
               siglist will get free'd if there is a problem. */
            int save_size = call_sig->siglist_size;
            call_sig->siglist_size = 0;
            lily_sig *call_ret_sig = collect_var_sig(parser, 0);
            call_sig->siglist[0] = call_ret_sig;
            call_sig->siglist_size = save_size;
        }

        call_sig = lily_ensure_unique_sig(parser->symtab, call_sig);
        if (flags & CV_MAKE_VARS)
            call_var->sig = call_sig;

        /* Let emitter know the true return type. */
        if (flags & CV_TOPLEVEL)
            parser->emit->top_method_ret = call_sig->siglist[0];

        result = call_sig;
    }
    else
        result = NULL;

    return result;
}

/** Expression handling, and helpers.
  * Expressions are assignments, binary ops, unary ops, etc, etc. The building
    blocks of anything and everything.
  * The parser creates an ast (abstract syntax tree) as it goes along to
    represent the expression. The ast is then fed to emitter, which will usually
    reset the pool when done.
  * expression_* functions are used by expression as helpers, and should not be
    called by any function except expression itself. **/

/* expression_typecast
   This function handles a typecast. In Lily, typecasts are done by
   @(type:value)
   @( is used instead of a standard parenth because it allows the parser and the
   programmer to differentiate between a typecast, and parenth expression. */
static void expression_typecast(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_sig *new_sig;

    lily_lexer(lex);
    new_sig = collect_var_sig(parser, 0);

    NEED_NEXT_TOK(tk_colon)

    /* It's possible that the value will be a binary expression. A parenth tree
       is entered so that binary can't parent the current root or do anything
       strange. It also offsets the ending ). */
    lily_ast_enter_tree(parser->ast_pool, tree_parenth, NULL);

    /* The ast owns the sig until the emitter takes it over. */
    lily_ast_push_sig(parser->ast_pool, new_sig);

    /* This should be the value. Yield to expression_value in case the value
       is more than just a var. */
    lily_lexer(lex);
}

static lily_sig *determine_ast_sig(lily_parse_state *parser, lily_ast *ast)
{
    lily_sig *ret;

    while (1) {
        while (ast->tree_type == tree_parenth)
            ast = ast->arg_start;

        /* a.concat("str") */
        if (ast->tree_type == tree_var ||
            ast->tree_type == tree_local_var ||
            ast->tree_type == tree_literal)
            ret = ast->result->sig;
        /* strcall(a, b, c).concat("str") */
        else if (ast->tree_type == tree_call)
            ret = ast->result->sig->siglist[0];
        /* a = b.concat("str") */
        else if (ast->tree_type == tree_binary) {
            ast = ast->right;
            continue;
        }
        /* a = strlist[0].concat(b)
           Return the type used for list elements. */
        else if (ast->tree_type == tree_subscript) {
            lily_sig *var_sig = determine_ast_sig(parser, ast->arg_start);

            /* Ensure that this can be subscripted. This is less to enforce
               proper subscripts, and more to prevent crashing from trying to
               do something like 0[10].abc(). */
            if (var_sig->cls->id != SYM_CLASS_LIST) {
                parser->raiser->line_adjust = ast->line_num;
                lily_raise(parser->raiser, lily_ErrSyntax,
                        "Cannot subscript type '%T'.\n", var_sig);
            }

            ret = var_sig->siglist[0];
        }
        /* @(str: ???).concat("a").
           Assume that the typecast succeeds. Either the emitter or the vm will
           complain about it if it's wrong. */
        else if (ast->tree_type == tree_typecast)
            ret = ast->sig;
        /* Warning: Unary has not been verified yet, because str.concat is the
           only oo call. Fix this when that gets fixed. */
        else if (ast->tree_type == tree_unary) {
            ast = ast->left;
            continue;
        }
        /* [a, b, c].concat(d)
           If a list is empty, use the type specified within it. Otherwise, walk
           the args to determine the type. This is similar to emitter's
           eval_build_list, except that it doesn't eval. */
        else if (ast->tree_type == tree_list) {
            if (ast->arg_start == NULL)
                ret = ast->sig;
            else {
                lily_sig *arg_sig, *common_sig;
                lily_ast *arg;

                common_sig = NULL;

                for (arg = ast->arg_start; arg!= NULL; arg = arg->next_arg) {
                    arg_sig = determine_ast_sig(parser, arg);
                    if (common_sig != NULL) {
                        if (arg_sig != common_sig) {
                            lily_class *cls;

                            cls = lily_class_by_id(parser->symtab,
                                    SYM_CLASS_OBJECT);
                            common_sig = cls->sig;
                            /* Parser can break here, because it's only
                               concerned about the resulting type. */
                            break;
                        }
                    }
                    else
                        common_sig = arg_sig;
                }

                lily_class *cls;
                lily_sig *result_sig;
                cls = lily_class_by_id(parser->symtab, SYM_CLASS_LIST);
                result_sig = lily_try_sig_for_class(parser->symtab, cls);
                if (result_sig == NULL)
                    lily_raise_nomem(parser->raiser);

                result_sig->siglist = lily_malloc(sizeof(lily_sig));
                if (result_sig->siglist == NULL)
                    lily_raise_nomem(parser->raiser);

                result_sig->siglist[0] = common_sig;
                result_sig->siglist_size = 1;
                result_sig = lily_ensure_unique_sig(parser->symtab, result_sig);
                ret = result_sig;
            }
        }

        break;
    }

    return ret;
}

/* expression_oo
   This function handles an 'object-oriented' type of call on an object such as
   'stringA.concat("b")'. */
static void expression_oo(lily_parse_state *parser)
{
    lily_var *call_var;
    lily_sig *ast_sig;
    lily_class *cls;

    ast_sig = determine_ast_sig(parser, parser->ast_pool->active);
    cls = ast_sig->cls;

    call_var = lily_find_class_callable(cls, parser->lex->label,
            parser->lex->label_shorthash);
    if (call_var == NULL) {
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "Class %s has no callable named %s.\n", cls->name,
                   parser->lex->label);
    }

    lily_ast_enter_tree(parser->ast_pool, tree_call, call_var);
}

/* expression_unary
   This function handles unary expressions such as !a and -a. */
static void expression_unary(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    while (1) {
        if (lex->token == tk_minus)
            lily_ast_push_unary_op(parser->ast_pool, expr_unary_minus);
        else if (lex->token == tk_not)
            lily_ast_push_unary_op(parser->ast_pool, expr_unary_not);
        else
            break;

        lily_lexer(lex);
    }
}

static lily_literal *parse_special_keyword(lily_parse_state *parser, int key_id)
{
    lily_symtab *symtab = parser->symtab;
    lily_literal *ret;

    /* So far, these are the only keywords that map to literals.
       Additionally, these literal fetching routines are guaranteed to either
       return a literal with the given value, or raise nomem. */
    if (key_id == KEY__LINE__) {
        lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
        lily_value value;

        value.integer = parser->lex->line_num;

        ret = lily_get_intnum_literal(symtab, cls, value);
    }
    else if (key_id == KEY__FILE__)
        ret = lily_get_str_literal(symtab, parser->lex->filename);
    else if (key_id == KEY__METHOD__)
        ret = lily_get_str_literal(symtab, parser->emit->top_var->name);
    else
        ret = NULL;

    return ret;
}

/* expression_value
   This handles getting a value for expression. It also handles oo calls and
   unary expressions as necessary. It will always push a value to the ast. */
static void expression_value(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;

    while (1) {
        if (lex->token == tk_word) {
            lily_var *var = lily_var_by_name(symtab, lex->label,
                    lex->label_shorthash);

            if (var) {
                lily_lexer(lex);
                if (lex->token == tk_left_parenth) {
                    int cls_id = var->sig->cls->id;
                    if (cls_id != SYM_CLASS_METHOD &&
                        cls_id != SYM_CLASS_FUNCTION)
                        lily_raise(parser->raiser, lily_ErrSyntax,
                                "%s is not callable.\n", var->name);

                    /* New trees will get saved to the args section of this tree
                        when they are done. */
                    lily_ast_enter_tree(parser->ast_pool, tree_call, var);

                    lily_lexer(lex);
                    if (lex->token == tk_right_parenth)
                        /* This call has no args (and therefore is not a value),
                           so let expression handle it. */
                        break;
                    else
                        /* Get the first value of the call. */
                        continue;
                }
                else {
                    if (var->method_depth == parser->emit->method_depth)
                        /* In this current scope? Load as a local var. */
                        lily_ast_push_local_var(parser->ast_pool, var);
                    else if (var->method_depth == 1)
                        /* It's in __main__ as a global. */
                        lily_ast_push_sym(parser->ast_pool, (lily_sym *)var);
                    else
                        /* todo: Handle upvalues later, maybe. */
                        lily_raise(parser->raiser, lily_ErrSyntax,
                                   "Attempt to use %s, which is not in the current scope.\n",
                                   var->name);
                }
            }
            else {
                int key_id = lily_keyword_by_name(lex->label,
                        lex->label_shorthash);
                if (key_id == KEY__LINE__ || key_id == KEY__FILE__ ||
                    key_id == KEY__METHOD__) {
                    lily_literal *lit;
                    lit = parse_special_keyword(parser, key_id);
                    lily_ast_push_literal(parser->ast_pool, lit);
                    lily_lexer(lex);
                }
                else {
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "%s has not been declared.\n", lex->label);
                }
            }
        }
        else if (lex->token == tk_double_quote) {
            lily_literal *lit;
            lit = lily_get_str_literal(symtab, lex->label);

            lily_ast_push_literal(parser->ast_pool, lit);

            lily_lexer(lex);
        }
        else if (lex->token == tk_integer) {
            lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
            lily_literal *lit;
            lit = lily_get_intnum_literal(symtab, cls, lex->value);
            lily_ast_push_literal(parser->ast_pool, lit);

            lily_lexer(lex);
        }
        else if (lex->token == tk_number) {
            lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_NUMBER);
            lily_literal *lit;
            lit = lily_get_intnum_literal(symtab, cls, lex->value);
            lily_ast_push_literal(parser->ast_pool, lit);

            lily_lexer(lex);
        }
        else if (lex->token == tk_minus || lex->token == tk_not) {
            expression_unary(parser);
            continue;
        }
        else if (lex->token == tk_typecast_parenth) {
            expression_typecast(parser);
            continue;
        }
        else if (lex->token == tk_left_parenth) {
            /* A parenth expression is essentially a call, but without the
               var part. */
            lily_ast_enter_tree(parser->ast_pool, tree_parenth, NULL);
            lily_lexer(lex);
            continue;
        }
        else if (lex->token == tk_left_bracket) {
            lily_lexer(lex);

            if (lex->token == tk_right_bracket)
                lily_raise(parser->raiser, lily_ErrSyntax,
                        "Empty lists must specify a type (ex: [str]).\n");
            else if (lex->token == tk_word) {
                lily_class *cls = lily_class_by_hash(symtab,
                        lex->label_shorthash);

                lily_sig *sig;
                if (cls != NULL) {
                    /* Make sure this works with complex signatures as well
                       as simple ones. */
                    sig = collect_var_sig(parser, 0);
                    NEED_NEXT_TOK(tk_right_bracket)
                    lily_lexer(lex);

                    /* Call this to avoid doing enter/leave when there will
                       not be any subtrees. */
                    lily_ast_push_empty_list(parser->ast_pool, sig);
                    break;
                }
            }

            lily_ast_enter_tree(parser->ast_pool, tree_list, NULL);
            continue;
        }
        else
            lily_raise(parser->raiser, lily_ErrSyntax,
                       "Expected a value, not '%s'.\n",
                       tokname(lex->token));

        if (lex->token == tk_dot) {
            NEED_NEXT_TOK(tk_word)
            expression_oo(parser);
            NEED_NEXT_TOK(tk_left_parenth);
            lily_lexer(lex);
            if (lex->token == tk_right_parenth)
                break;
            else
                continue;
        }
        else if (lex->token == tk_left_bracket) {
            lily_ast_enter_tree(parser->ast_pool, tree_subscript, NULL);
            lily_lexer(lex);
            continue;
        }
        break;
    }
}

/* check_valid_close_tok
   This is used to verify that the current lexer token is valid for closing the
   current ast tree. This prevents code like:
   list[integer] lsi = [10, 20, 30)
                                  ^
   by complaining that ) is the wrong close token.

   This should be called before lily_ast_leave_tree in situations where the
   calling tree is not _explicitly_ known. */
static void check_valid_close_tok(lily_parse_state *parser)
{
    lily_token token = parser->lex->token;
    lily_tree_type tt = lily_ast_caller_tree_type(parser->ast_pool);
    lily_token expect;

    if (tt == tree_call || tt == tree_parenth || tt == tree_typecast)
        expect = tk_right_parenth;
    else
        expect = tk_right_bracket;

    if (token != expect)
        lily_raise(parser->raiser, lily_ErrSyntax,
                "Expected closing token %s, not %s.\n", tokname(expect),
                tokname(token));
}

/* expression
   This is the workhorse for handling expressions. 'flags' is used to determine
   if it needs a starting value, if it should run multiple times, etc. */
static void expression(lily_parse_state *parser, int flags)
{
    lily_lex_state *lex = parser->lex;

    if ((flags & EX_NEED_VALUE) != 0)
        expression_value(parser);

    while (1) {
        while (1) {
            if (lex->token == tk_word) {
                if (parser->ast_pool->save_index != 0)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected ')' or a binary op, not a label.\n");

                break;
            }
            else if (lex->token == tk_right_parenth ||
                     lex->token == tk_right_bracket) {
                if (parser->ast_pool->save_index == 0)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                            "Unexpected token %s.\n", tokname(lex->token));

                check_valid_close_tok(parser);
                lily_ast_leave_tree(parser->ast_pool);

                lily_lexer(lex);
                if (parser->ast_pool->save_index == 0 &&
                    is_start_val[lex->token] == 1)
                    /* Since there are no parenths/calls left, then this value
                       must be the first in the next expression. */
                    break;
                else if (lex->token == tk_left_bracket) {
                    lily_ast_enter_tree(parser->ast_pool, tree_subscript, NULL);
                    lily_lexer(lex);
                }
                else if (lex->token == tk_left_parenth) {
                    lily_ast_enter_tree(parser->ast_pool, tree_call, NULL);
                    lily_lexer(lex);
                    if (lex->token == tk_right_parenth) {
                        /* Don't leave the tree: This will do the leave when it
                           jumps back up. */
                        continue;
                    }
                }
                else if (lex->token != tk_dot)
                    /* If not '.', then assume it's a binary token. */
                    continue;
                else {
                    /* 'a.concat("b").concat("c")'. Do a normal oo merge. */
                    NEED_NEXT_TOK(tk_word)
                    expression_oo(parser);
                    NEED_NEXT_TOK(tk_left_parenth);
                    lily_lexer(lex);
                }
            }
            else if (lex->token == tk_comma) {
                if (parser->ast_pool->active == NULL)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected a value, not ','.\n");

                /* If this is inside of a decl list (integer a, b, c...), then
                   the comma is the end of the decl unless it's part of a call
                   used in the decl (integer a = add(1, 2), b = 1...). */
                if (((flags & EX_NEED_VALUE) == 0) &&
                    parser->ast_pool->save_index == 0) {
                    break;
                }

                if (parser->ast_pool->save_index == 0)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                            "Unexpected token %s.\n", tokname(lex->token));

                lily_ast_collect_arg(parser->ast_pool);
                lily_lexer(lex);
            }
            else {
                int expr_op = bin_op_for_token[lex->token];
                if (expr_op != -1) {
                    lily_ast_push_binary_op(parser->ast_pool,
                            (lily_expr_op)expr_op);
                    lily_lexer(lex);
                }
                else if (lex->token == tk_colon ||
                         lex->token == tk_right_curly ||
                         lex->token == tk_end_tag || lex->token == tk_eof ||
                         lex->token == tk_two_dots) {
                    if (parser->ast_pool->save_index != 0)
                        lily_raise(parser->raiser, lily_ErrSyntax,
                                "Unexpected token %s.\n", tokname(lex->token));
                    break;
                }
                else {
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected maybe a binary operator, not %s.\n",
                               tokname(lex->token));
                }
            }
            expression_value(parser);
        }
        if (flags & EX_SINGLE) {
            if (flags & EX_CONDITION) {
                lily_emit_conditional(parser->emit, parser->ast_pool->root);
                lily_ast_reset_pool(parser->ast_pool);
            }
            else if (!(flags & EX_SAVE_AST)) {
                lily_emit_ast(parser->emit, parser->ast_pool->root);
                lily_ast_reset_pool(parser->ast_pool);
            }
        }
        break;
    }
}

/* parse_decl
   This function takes a sig and handles a declaration wherein each var name
   is separated by a comma. Ex:

   integer a, b, c
   number d
   list[integer] e

   The complexity of the signature does not matter. Methods will typically not
   come here (unless they are a named argument to another method).
   Expected token: A label (the first variable name). */
static void parse_decl(lily_parse_state *parser, lily_sig *sig)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var;

    while (1) {
        /* This starts at the class name, or the comma. The label is next. */
        var = get_named_var(parser, sig);

        lily_lexer(parser->lex);
        /* Handle an initializing assignment, if there is one. */
        if (lex->token == tk_equal) {
            lily_ast_push_sym(parser->ast_pool, (lily_sym *)var);
            expression(parser, EX_SINGLE);
        }

        /* This is the start of the next statement. */
        if (lex->token == tk_word || lex->token == tk_end_tag ||
            lex->token == tk_eof || lex->token == tk_right_curly)
            break;
        else if (lex->token != tk_comma) {
            lily_raise(parser->raiser, lily_ErrSyntax,
                       "Expected ',' or ')', not %s.\n", tokname(lex->token));
        }
        /* else comma, so just jump back up. */
    }
}

/* parse_show
   This handles the 'show' keyword. This displays information on a given
   expression. Type checking intentionally not performed. */
static void parse_show(lily_parse_state *parser)
{
    expression(parser, EX_NEED_VALUE | EX_SINGLE | EX_SAVE_AST);
    lily_emit_show(parser->emit, parser->ast_pool->root);
    lily_ast_reset_pool(parser->ast_pool);
}

/* parse_return
   This parses a return statement, and writes the appropriate return info to
   the emitter. */
static void parse_return(lily_parse_state *parser)
{
    lily_sig *ret_sig = parser->emit->top_method_ret;
    if (ret_sig != NULL) {
        expression(parser, EX_NEED_VALUE | EX_SINGLE | EX_SAVE_AST);
        lily_emit_return(parser->emit, parser->ast_pool->root, ret_sig);
        lily_ast_reset_pool(parser->ast_pool);
    }
    else
        lily_emit_return_noval(parser->emit);
}

/* parse_while
   This parses and enters a while statement. While statements are always
   multi-line. */
static void parse_while(lily_parse_state *parser)
{
    /* Syntax: while x: { ... }
       This starts with the token on tk_word (while). */
    lily_lex_state *lex = parser->lex;

    /* First, tell the emitter we're entering a block, so that '}' will close
       it properly. */
    lily_emit_enter_block(parser->emit, BLOCK_WHILE);

    /* Grab the condition after the 'while' keyword. Use EX_SAVE_AST so that
       expression will not emit+dump the ast. */
    expression(parser, EX_NEED_VALUE | EX_SINGLE | EX_SAVE_AST);
    lily_emit_ast(parser->emit, parser->ast_pool->root);
    /* 0 = jump_if_false. This jump will be patched later with the destination
       of the end of the while loop. */
    lily_emit_jump_if(parser->emit, parser->ast_pool->root, 0);
    lily_ast_reset_pool(parser->ast_pool);

    NEED_CURRENT_TOK(tk_colon)
    NEED_NEXT_TOK(tk_left_curly)

    /* Call up the next value for whatever. */
    lily_lexer(lex);
}

/* parse_simple_condition
   This handles parsing for single-line ifs. This makes things a bit faster by
   not having to enter and leave functions as much. */
static void parse_simple_condition(lily_parse_state *parser)
{
    /* In a simple condition, each if, elif, and else have only a single
       expression. This is called when an 'if' is caught, and the token after
       the : is not {. */
    int key_id;
    lily_lex_state *lex = parser->lex;

    while (1) {
        if (lex->token == tk_word)
            key_id = lily_keyword_by_name(parser->lex->label,
                    parser->lex->label_shorthash);
        else
            key_id = -1;

        if (key_id == KEY_RETURN) {
            /* Skip the 'return' keyword. */
            lily_lexer(lex);
            parse_return(parser);
        }
        else if (key_id == KEY_WHILE) {
            lily_raise(parser->raiser, lily_ErrSyntax,
                    "'while' not allowed in single-line if.\n");
        }
        else if (key_id == KEY_CONTINUE) {
            /* Skip past the keyword again. */
            lily_lexer(lex);
            lily_emit_continue(parser->emit);
        }
        else if (key_id == KEY_BREAK) {
            lily_lexer(lex);
            lily_emit_break(parser->emit);
        }
        else if (key_id == KEY_SHOW) {
            lily_lexer(lex);
            parse_show(parser);
        }
        else
            expression(parser, EX_NEED_VALUE | EX_SINGLE);

        if (lex->token == tk_word) {
            key_id = lily_keyword_by_name(parser->lex->label,
                    parser->lex->label_shorthash);
            if (key_id == KEY_IF) {
                /* Close this branch and start another one. */
                lily_emit_leave_block(parser->emit);
                lily_lexer(lex);
                lily_emit_enter_block(parser->emit, BLOCK_IF);
                expression(parser, EX_NEED_VALUE | EX_SINGLE |
                               EX_CONDITION);

                if (lex->token != tk_colon)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected ':', not %s.\n", tokname(lex->token));

                lily_lexer(lex);
                if (lex->token == tk_left_curly) {
                    /* This func handles single-line ifs, but this is a
                       multi-line one. Swallow the {, then yield control. */
                    lily_lexer(lex);
                    return;
                }
            }
            else if (key_id == KEY_ELIF) {
                lily_emit_change_if_branch(parser->emit, /*have_else=*/0);
                lily_lexer(lex);
                expression(parser, EX_NEED_VALUE | EX_SINGLE |
                               EX_CONDITION);

                if (lex->token != tk_colon)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected ':', not %s.\n", tokname(lex->token));

                lily_lexer(lex);
            }
            else if (key_id == KEY_ELSE) {
                lily_emit_change_if_branch(parser->emit, /*have_else=*/1);

                lily_lexer(lex);
                if (lex->token != tk_colon)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected ':', not %s.\n", tokname(lex->token));

                lily_lexer(lex);
            }
            else
                break;
        }
        else
            break;
    }
    lily_emit_leave_block(parser->emit);
}

/* parse_do_while
   This handles the 'do: {' part of a do: { ... } while ...: expression.
   Not much to do since there's no expression yet. */
static void parse_do_while(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, BLOCK_DO_WHILE);

    NEED_CURRENT_TOK(tk_colon)
    NEED_NEXT_TOK(tk_left_curly)

    lily_lexer(lex);
}

/* parse_do_while_expr
   Now that the inner expressions have been parsed, this handles the
   'while ...:' part of the expression. */
static void parse_do_while_expr(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    NEED_NEXT_TOK(tk_word)
    if (strcmp(lex->label, "while") != 0)
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "Expected 'while', not '%s'.\n", lex->label);

    /* Grab the while expression, and feed it into a custom emitter call that
       will write it down, then a jump if false back to the top. */
    lily_lexer(lex);
    expression(parser, EX_NEED_VALUE | EX_SAVE_AST);
    lily_eval_do_while_expr(parser->emit, parser->ast_pool->root);
    /* Finally, reset the ast pool and leave the block. */
    lily_ast_reset_pool(parser->ast_pool);
    lily_emit_leave_block(parser->emit);
}

static lily_var *parse_for_range_value(lily_parse_state *parser, char *name)
{
    lily_ast_pool *ap = parser->ast_pool;
    expression(parser, EX_SINGLE | EX_SAVE_AST | EX_NEED_VALUE);

    /* Don't allow assigning expressions, since that just looks weird.
       ex: for i in a += 10..5
       Also, it makes no real sense to do that. */
    if (ap->root->tree_type == tree_binary &&
        ap->root->op >= expr_assign) {
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "For range value expression contains an assignment.");
    }

    lily_class *cls = lily_class_by_id(parser->symtab, SYM_CLASS_INTEGER);

    /* For loop values are created as vars so there's a name in case of a
       problem. This name doesn't have to be unique, since it will never be
       found by the user. Since it's not user-findable, don't bother making a
       shorthash for it either. */
    lily_var *var = lily_try_new_var(parser->symtab, cls->sig, name, 0);
    if (var == NULL)
        lily_raise_nomem(parser->raiser);

    lily_emit_ast_to_var(parser->emit, ap->root, var);
    lily_ast_reset_pool(parser->ast_pool);

    return var;
}

static void parse_for_in(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_var *loop_var;

    NEED_CURRENT_TOK(tk_word)

    lily_emit_enter_block(parser->emit, BLOCK_FOR_IN);

    loop_var = lily_var_by_name(parser->symtab, lex->label,
            lex->label_shorthash);
    if (loop_var == NULL) {
        lily_class *cls = lily_class_by_id(parser->symtab, SYM_CLASS_INTEGER);
        loop_var = lily_try_new_var(parser->symtab, cls->sig, lex->label,
                lex->label_shorthash);
        if (loop_var == NULL)
            lily_raise_nomem(parser->raiser);
    }
    else if (loop_var->sig->cls->id != SYM_CLASS_INTEGER) {
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "Loop var must be type integer, not type '%T'.\n",
                   loop_var->sig);
    }

    NEED_NEXT_TOK(tk_word)
    if (strcmp(lex->label, "in") != 0)
        lily_raise(parser->raiser, lily_ErrSyntax, "Expected 'in', not '%s'.\n",
                   lex->label);

    lily_lexer(lex);

    lily_var *for_start, *for_end, *for_step;

    for_start = parse_for_range_value(parser, "(for start)");

    NEED_CURRENT_TOK(tk_two_dots)
    lily_lexer(lex);

    for_end = parse_for_range_value(parser, "(for end)");

    if (lex->token == tk_word) {
        if (strcmp(lex->label, "by") != 0)
            lily_raise(parser->raiser, lily_ErrSyntax,
                       "Expected 'by', not '%s'.\n", lex->label);

        lily_lexer(lex);
        for_step = parse_for_range_value(parser, "(for step)");
    }
    else
        for_step = NULL;

    lily_emit_finalize_for_in(parser->emit, loop_var, for_start, for_end,
                              for_step, parser->lex->line_num);

    NEED_CURRENT_TOK(tk_colon)
    NEED_NEXT_TOK(tk_left_curly)
    lily_lexer(lex);
}

/* statement
   This handles anything that starts with a label. Expressions, conditions,
   declarations, etc. */
static void statement(lily_parse_state *parser)
{
    uint64_t label_shorthash = parser->lex->label_shorthash;
    int key_id;
    lily_class *lclass;
    lily_lex_state *lex = parser->lex;

    key_id = lily_keyword_by_name(lex->label, label_shorthash);
    if (key_id != -1) {
        lily_lex_state *lex = parser->lex;
        lily_lexer(lex);

        if (key_id == KEY_RETURN)
            parse_return(parser);
        else if (key_id == KEY_WHILE)
            parse_while(parser);
        else if (key_id == KEY_CONTINUE)
            lily_emit_continue(parser->emit);
        else if (key_id == KEY_BREAK)
            lily_emit_break(parser->emit);
        else if (key_id == KEY_SHOW)
            parse_show(parser);
        else if (key_id == KEY_FOR)
            parse_for_in(parser);
        else if (key_id == KEY_DO)
            parse_do_while(parser);
        else if (key_id == KEY__LINE__ || key_id == KEY__FILE__ ||
                 key_id == KEY__METHOD__)
            /* These are useless outside of an expression anyway... */
            lily_raise(parser->raiser, lily_ErrSyntax,
                       "%s cannot be used outside of an expression.\n",
                       parser->lex->label);
        else {
            if (key_id == KEY_IF) {
                lily_emit_enter_block(parser->emit, BLOCK_IF);
                expression(parser, EX_NEED_VALUE | EX_SINGLE | EX_CONDITION);
            }
            else if (key_id == KEY_ELIF) {
                lily_emit_change_if_branch(parser->emit, /*have_else=*/0);
                expression(parser, EX_NEED_VALUE | EX_SINGLE | EX_CONDITION);
            }
            else if (key_id == KEY_ELSE)
                lily_emit_change_if_branch(parser->emit, /*have_else=*/1);

            NEED_CURRENT_TOK(tk_colon)

            lily_lexer(lex);
            if (key_id == KEY_IF) {
                if (lex->token != tk_left_curly)
                    parse_simple_condition(parser);
                else {
                    NEED_CURRENT_TOK(tk_left_curly)

                    lily_lexer(parser->lex);
                }
            }
        }
    }
    else {
        lclass = lily_class_by_hash(parser->symtab, label_shorthash);

        if (lclass != NULL) {
            int cls_id = lclass->id;

            if (cls_id == SYM_CLASS_METHOD) {
                /* This will enter the method since the method is toplevel. */
                collect_var_sig(parser, CV_TOPLEVEL | CV_MAKE_VARS);
                NEED_NEXT_TOK(tk_left_curly)
                lily_lexer(lex);
            }
            else if (cls_id == SYM_CLASS_LIST) {
                lily_sig *list_sig = collect_var_sig(parser, 0);
                parse_decl(parser, list_sig);
            }
            else if (cls_id == SYM_CLASS_FUNCTION)
                /* As of now, user-declared functions can't do anything except
                   alias the current builtins. Disable them until they're
                   useful. */
                lily_raise(parser->raiser, lily_ErrSyntax,
                           "Cannot declare user functions (yet).\n");
            else
                parse_decl(parser, lclass->sig);
        }
        else
            expression(parser, EX_NEED_VALUE | EX_SINGLE);
    }
}

/** Main parser function, and public calling API.
    Most of the work has been pushed into statement the helpers that statement
    calls. The API serves as a way to launch the parser without the raises
    resulting in a longjmp to an invalid place. **/
void lily_parser(lily_parse_state *parser)
{
    /* Must do this first, in the rare case this next call fails. */
    parser->mode = pm_parse;

    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);

    while (1) {
        if (lex->token == tk_word)
            statement(parser);
        else if (lex->token == tk_right_curly) {
            if (parser->emit->current_block->block_type == BLOCK_DO_WHILE)
                parse_do_while_expr(parser);
            lily_emit_leave_block(parser->emit);
            lily_lexer(parser->lex);
        }
        else if (lex->token == tk_end_tag ||
                 (lex->token == tk_eof && lex->mode != lm_from_file)) {
            /* Make sure that all if/method/etc. blocks have closed before
               executing. This checks for pos at 1 because __main__ will always
               occupy 0. */
            if (parser->emit->current_block != parser->emit->first_block) {
                lily_raise(parser->raiser, lily_ErrSyntax,
                           "Unterminated block(s) at end of parsing.\n");
            }
            lily_emit_vm_return(parser->emit);

            lily_vm_prep(parser->vm, parser->symtab);
            parser->mode = pm_execute;
            lily_vm_execute(parser->vm);
            parser->mode = pm_parse;

            /* Clear __main__ for the next pass. */
            lily_reset_main(parser->emit);

            if (lex->token == tk_end_tag) {
                lily_lexer_handle_page_data(parser->lex);
                if (lex->token == tk_eof)
                    break;
                else
                    lily_lexer(lex);
            }
            else
                break;
        }
        else
            lily_raise(parser->raiser, lily_ErrSyntax, "Unexpected token %s.\n",
                       tokname(lex->token));
    }
}

/* lily_parse_file
   This function will begin parsing based off of the given filename. Returns 1
   on success, and 0 on failure. */
int lily_parse_file(lily_parse_state *parser, char *filename)
{
    if (setjmp(parser->raiser->jumps[parser->raiser->jump_pos]) == 0) {
        parser->raiser->jump_pos++;
        lily_load_file(parser->lex, filename);
        if (parser->lex->token != tk_eof)
            lily_parser(parser);

        return 1;
    }

    return 0;
}

/* lily_parse_string
   This function will begin parsing from a given str. The caller is responsible
   for destroying the str as needed. */
int lily_parse_string(lily_parse_state *parser, char *str)
{
    if (setjmp(parser->raiser->jumps[parser->raiser->jump_pos]) == 0) {
        parser->raiser->jump_pos++;
        lily_load_str(parser->lex, str);
        lily_parser(parser);
        parser->lex->lex_buffer = NULL;
        return 1;
    }

    return 0;
}

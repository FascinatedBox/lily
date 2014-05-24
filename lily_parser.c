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

/* Get the lhs via expression_value. */
#define EX_NEED_VALUE 0x001
/* For if and elif to work, the expression has to be tested for being true or
   false. This tells the emitter to write in that test (o_jump_if_false) after
   writing the condition. This must be done within expression, because the
   ast is 'cleaned' by expression after each run. */
#define EX_CONDITION  0x002
/* Don't clear the ast within 'expression'. This allows the finished ast to be
   inspected. */
#define EX_SAVE_AST   0x004

/* These flags are for collect_var_sig. */

/* Expect a name with every class given. Create a var for each class+name pair.
   This is suitable for collecting the args of a method. */
#define CV_MAKE_VARS  0x010

/* This is set if the variable is not inside another variable. This is suitable
   for collecting a method that may have named arguments. */
#define CV_TOPLEVEL   0x020

#define NEED_NEXT_TOK(expected) \
lily_lexer(lex); \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_ErrSyntax, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token));

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
    expr_assign,
    expr_eq_eq,
    -1,
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

    /* Lily saves some global info in vars, and also in literals as well as the
       registers. Vars and literals are linked lists, while the registers are
       one really large block. Splitting things up like this is weird, but it
       allows Lily to create exactly the amount of register info all at once and
       without guessing at it with malloc + reallocs.
       The downside is that the vm and symtab need to be torn down in a rather
       specific order. Start off by blasting the registers, because those came
       after the symtab's literals and vars. */
    if (parser->vm)
        lily_vm_free_registers(parser->vm);

    /* The symtab's literals and vars go next. This includes __main__, builtins,
       and the like. Past this point, nothing is callable. */
    if (parser->symtab)
        lily_free_symtab_lits_and_vars(parser->symtab);

    /* The vm gets freed next. This will invoke the gc and clear out any
       circular references as well as gc entries on stuff. Past this point, no
       values should be alive. */
    if (parser->vm)
        lily_free_vm_state(parser->vm);

    /* Finally, tear down the symtab. This clears out classes and signatures, so
       it's VERY important that this go later on, because classes and signature
       info is so crucial. */
    if (parser->symtab)
        lily_free_symtab(parser->symtab);

    /* Order doesn't matter for the rest of this. */

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
static lily_var *get_named_var(lily_parse_state *parser, lily_sig *var_sig,
        int flags)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var;
    NEED_NEXT_TOK(tk_word)

    var = lily_var_by_name(parser->symtab, lex->label, lex->label_shorthash);
    if (var != NULL)
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "%s has already been declared.\n", lex->label);

    var = lily_try_new_var(parser->symtab, var_sig, lex->label,
            lex->label_shorthash, flags);
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
        cls->id != SYM_CLASS_LIST && cls->id != SYM_CLASS_HASH) {
        result = cls->sig;
        if (flags & CV_MAKE_VARS)
            get_named_var(parser, cls->sig, 0);
    }
    else if (cls->id == SYM_CLASS_LIST || cls->id == SYM_CLASS_HASH) {
        int i;
        lily_sig *new_sig = lily_try_sig_for_class(parser->symtab, cls);
        if (new_sig == NULL)
            lily_raise_nomem(parser->raiser);

        lily_sig **siglist;
        siglist = lily_malloc(cls->template_count * sizeof(lily_sig));
        if (siglist == NULL)
            lily_raise_nomem(parser->raiser);

        new_sig->siglist = siglist;
        new_sig->siglist_size = 0;

        NEED_NEXT_TOK(tk_left_bracket)
        lily_lexer(lex);

        for (i = 0;i < cls->template_count;i++) {
            lily_sig *inner_sig = collect_var_sig(parser, 0);
            siglist[i] = inner_sig;
            if (i != (cls->template_count - 1)) {
                lily_lexer(lex);
                NEED_CURRENT_TOK(tk_comma)
                lily_lexer(lex);
            }
        }
        NEED_NEXT_TOK(tk_right_bracket)

        new_sig->siglist_size = cls->template_count;
        new_sig = lily_ensure_unique_sig(parser->symtab, new_sig);

        /* For hashes, make sure that the first argument (the key) is a valid
           key type. Valid key types are ones which are primitive, or ones that
           are immutable. */
        if (cls->id == SYM_CLASS_HASH) {
            /* Must use new_sig->siglist because lily_ensure_unique_sig can
               destroy the old sig and make 'siglist' itself invalid. */
            int key_class_id = new_sig->siglist[0]->cls->id;
            if (key_class_id != SYM_CLASS_INTEGER &&
                key_class_id != SYM_CLASS_NUMBER &&
                key_class_id != SYM_CLASS_STR) {
                lily_raise(parser->raiser, lily_ErrSyntax,
                        "'%T' is not a valid hash key.\n", new_sig->siglist[0]);
            }
        }

        result = new_sig;
        if (flags & CV_MAKE_VARS)
            get_named_var(parser, result, 0);
    }
    else if (cls->id == SYM_CLASS_METHOD ||
             cls->id == SYM_CLASS_FUNCTION) {
        lily_sig *call_sig = lily_try_sig_for_class(parser->symtab, cls);
        lily_var *call_var;

        if (call_sig == NULL)
            lily_raise_nomem(parser->raiser);

        if (flags & CV_MAKE_VARS) {
            if (flags & CV_TOPLEVEL) {
                call_var = get_named_var(parser, call_sig, VAR_IS_READONLY);
                lily_emit_enter_block(parser->emit, BLOCK_METHOD);
            }
            else
                call_var = get_named_var(parser, call_sig, 0);
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
            ast->tree_type == tree_readonly)
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

    call_var = lily_find_class_callable(parser->symtab, cls,
            parser->lex->label, parser->lex->label_shorthash);
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
        lily_raw_value value;

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
                    if (var->method_depth == 1)
                        /* It's in __main__ as a global. */
                        lily_ast_push_sym(parser->ast_pool, (lily_sym *)var);
                    else if (var->method_depth == parser->emit->method_depth)
                        /* In this current scope? Load as a local var. */
                        lily_ast_push_local_var(parser->ast_pool, var);
                    else if (var->method_depth == -1)
                        /* This is a call that's not in a register. It's kind of
                           like a literal. */
                        lily_ast_push_readonly(parser->ast_pool, (lily_sym *)var);
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
                    lily_ast_push_readonly(parser->ast_pool, (lily_sym *)lit);
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

            lily_ast_push_readonly(parser->ast_pool, (lily_sym *)lit);

            lily_lexer(lex);
        }
        else if (lex->token == tk_integer) {
            lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
            lily_literal *lit;
            lit = lily_get_intnum_literal(symtab, cls, lex->value);
            lily_ast_push_readonly(parser->ast_pool, (lily_sym *)lit);

            lily_lexer(lex);
        }
        else if (lex->token == tk_number) {
            lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_NUMBER);
            lily_literal *lit;
            lit = lily_get_intnum_literal(symtab, cls, lex->value);
            lily_ast_push_readonly(parser->ast_pool, (lily_sym *)lit);

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
        if (lex->token == tk_word) {
            if (parser->ast_pool->save_depth != 0)
                lily_raise(parser->raiser, lily_ErrSyntax,
                           "Expected ')' or a binary op, not a label.\n");

            break;
        }
        else if (lex->token == tk_right_parenth ||
                 lex->token == tk_right_bracket) {
            if (parser->ast_pool->save_depth == 0)
                lily_raise(parser->raiser, lily_ErrSyntax,
                        "Unexpected token %s.\n", tokname(lex->token));

            check_valid_close_tok(parser);
            lily_ast_leave_tree(parser->ast_pool);

            lily_lexer(lex);
            if (parser->ast_pool->save_depth == 0 &&
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
        else if (lex->token == tk_comma || lex->token == tk_arrow) {
            if (parser->ast_pool->active == NULL)
                lily_raise(parser->raiser, lily_ErrSyntax,
                           "Expected a value, not ','.\n");

            /* If this is inside of a decl list (integer a, b, c...), then
               the comma is the end of the decl unless it's part of a call
               used in the decl (integer a = add(1, 2), b = 1...). */
            if (((flags & EX_NEED_VALUE) == 0) &&
                parser->ast_pool->save_depth == 0 &&
                lex->token == tk_comma) {
                break;
            }

            if (parser->ast_pool->save_depth == 0)
                lily_raise(parser->raiser, lily_ErrSyntax,
                        "Unexpected token %s.\n", tokname(lex->token));

            lily_ast *last_tree = lily_ast_get_saved_tree(parser->ast_pool);
            if (lex->token == tk_comma) {
                if (last_tree->tree_type == tree_hash &&
                    (last_tree->args_collected & 0x1) == 0)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                            "Expected a key => value pair before ','.\n");
            }
            else if (lex->token == tk_arrow) {
                if (last_tree->tree_type == tree_list) {
                    if (last_tree->args_collected == 0)
                        last_tree->tree_type = tree_hash;
                    else
                        lily_raise(parser->raiser, lily_ErrSyntax,
                                "Unexpected token '%s'.\n", tokname(tk_arrow));
                }
                else if (last_tree->tree_type != tree_hash ||
                         (last_tree->args_collected & 0x1) == 1)
                        lily_raise(parser->raiser, lily_ErrSyntax,
                                "Unexpected token '%s'.\n", tokname(tk_arrow));
            }

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
                     lex->token == tk_end_tag ||
                     lex->token == tk_eof ||
                     lex->token == tk_two_dots) {
                if (parser->ast_pool->save_depth != 0)
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

    if (flags & EX_CONDITION) {
        lily_emit_conditional(parser->emit, parser->ast_pool->root);
        lily_ast_reset_pool(parser->ast_pool);
    }
    else if (!(flags & EX_SAVE_AST)) {
        lily_emit_ast(parser->emit, parser->ast_pool->root);
        lily_ast_reset_pool(parser->ast_pool);
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
        var = get_named_var(parser, sig, 0);

        lily_lexer(parser->lex);
        /* Handle an initializing assignment, if there is one. */
        if (lex->token == tk_equal) {
            lily_ast_push_sym(parser->ast_pool, (lily_sym *)var);
            expression(parser, 0);
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
    expression(parser, EX_SAVE_AST | EX_NEED_VALUE);

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
    lily_var *var = lily_try_new_var(parser->symtab, cls->sig, name, 0, 0);
    if (var == NULL)
        lily_raise_nomem(parser->raiser);

    lily_emit_ast_to_var(parser->emit, ap->root, var);
    lily_ast_reset_pool(parser->ast_pool);

    return var;
}

/** Statement and statement helpers.
    These *_handler functions are used to handle a keyword. This allows one to
    grab a keyword, then do 'handlers[key_id](parser, is_multiline)' and not
    have to worry about anything.
    These handler functions should not be called directly: In most cases,
    statement should be called, because it's fairly smart.
    Each of the helper functions gets the parser state and a 0/1 indicating if
    the current block is a multi-line block or not. **/

static void if_handler(lily_parse_state *, int);
static void elif_handler(lily_parse_state *, int);
static void else_handler(lily_parse_state *, int);
static void return_handler(lily_parse_state *, int);
static void while_handler(lily_parse_state *, int);
static void continue_handler(lily_parse_state *, int);
static void break_handler(lily_parse_state *, int);
static void show_handler(lily_parse_state *, int);
static void file_kw_handler(lily_parse_state *, int);
static void line_kw_handler(lily_parse_state *, int);
static void method_kw_handler(lily_parse_state *, int);
static void for_handler(lily_parse_state *, int);
static void do_handler(lily_parse_state *, int);

typedef void (keyword_handler)(lily_parse_state *, int);

static keyword_handler *handlers[] = {
    if_handler,
    elif_handler,
    else_handler,
    return_handler,
    while_handler,
    continue_handler,
    break_handler,
    show_handler,
    file_kw_handler,
    line_kw_handler,
    method_kw_handler,
    for_handler,
    do_handler
};

/*  statement
    This is a magic function that handles keywords outside of expression,
    as well as getting declarations started.
    If in_multiline is set, this function will do the above until it finds
    a starting token that isn't a label. */
static void statement(lily_parse_state *parser, int in_multiline)
{
    int key_id;
    lily_class *lclass;
    lily_lex_state *lex = parser->lex;

    do {
        uint64_t label_shorthash = parser->lex->label_shorthash;
        key_id = lily_keyword_by_name(lex->label, label_shorthash);

        if (key_id != -1) {
            /* Ask the handler for this keyword what to do. */
            lily_lexer(lex);
            handlers[key_id](parser, in_multiline);
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
                else if (cls_id == SYM_CLASS_LIST || cls_id == SYM_CLASS_HASH) {
                    lily_sig *cls_sig = collect_var_sig(parser, 0);
                    parse_decl(parser, cls_sig);
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
                expression(parser, EX_NEED_VALUE);
        }
    } while (in_multiline && lex->token == tk_word);
}

/*  if_handler
    This handles parsing 'if'. There are two kinds of if blocks:
    A single-line if block begins like this:
        if x: ...
    A multi-line if block begins like this:
        if x: { ... }

    Each elif and else of a multi-line block is also multiline. */
static void if_handler(lily_parse_state *parser, int in_multiline)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, BLOCK_IF);
    expression(parser, EX_NEED_VALUE | EX_CONDITION);
    NEED_CURRENT_TOK(tk_colon)

    lily_lexer(lex);
    if (lex->token == tk_left_curly) {
        lily_lexer(lex);
        /* The multi-line version of statement handles inner blocks and all
           that fancy stuff, (hopefully) finishing with }. */
        if (lex->token != tk_right_curly)
            statement(parser, 1);
        NEED_CURRENT_TOK(tk_right_curly)

        lily_emit_leave_block(parser->emit);
        lily_lexer(lex);
    }
    else {
        /* Single-line statement won't jump into other blocks though. */
        statement(parser, 0);
        if (lex->token == tk_word) {
            uint64_t label_shorthash = parser->lex->label_shorthash;
            int key_id = lily_keyword_by_name(lex->label, label_shorthash);

            if (key_id == KEY_ELIF || key_id == KEY_ELSE) {
                /* Don't go back into statement, because this is a single-line
                   block and may be within a multi-line block. Instead, call
                   these directly. */
                lily_lexer(parser->lex);
                handlers[key_id](parser, 0);
            }
            else
                lily_emit_leave_block(parser->emit);
        }
        else
            lily_emit_leave_block(parser->emit);
    }
}

/*  elif_handler
    This handles the elif keyword. The { after if decides if this is
    multi-line, so elif doesn't have very much to do. The only tricky part is
    making sure that single-line elif's call each other. They can't fall back
    to statement because parsing goes wrong if it's a single-line block in a
    multi-line block. */
static void elif_handler(lily_parse_state *parser, int in_multiline)
{
    lily_lex_state *lex = parser->lex;
    lily_emit_change_if_branch(parser->emit, /*have_else=*/0);
    expression(parser, EX_NEED_VALUE | EX_CONDITION);
    NEED_CURRENT_TOK(tk_colon)

    lily_lexer(lex);
    statement(parser, in_multiline);

    if (in_multiline == 0) {
        if (lex->token == tk_word) {
            uint64_t label_shorthash = parser->lex->label_shorthash;
            int key_id = lily_keyword_by_name(lex->label, label_shorthash);

            if (key_id == KEY_ELIF || key_id == KEY_ELSE) {
                /* Don't go back into statement, because this is a single-line
                   block and may be within a multi-line block. Instead, call
                   these directly. */
                lily_lexer(parser->lex);
                handlers[key_id](parser, 0);
            }
            else
                lily_emit_leave_block(parser->emit);
        }
        else
            lily_emit_leave_block(parser->emit);
    }
}

/*  else_handler
    This handles the else keyword. Since is the last part of the if/elif/else
    combo, there's VERY little to do here except one statement and leaving the
    if block. */
static void else_handler(lily_parse_state *parser, int in_multiline)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_change_if_branch(parser->emit, /*have_else=*/1);
    NEED_CURRENT_TOK(tk_colon)
    lily_lexer(lex);

    statement(parser, in_multiline);
    if (in_multiline == 0)
        lily_emit_leave_block(parser->emit);
}

/*  return_handler
    This handles the return keyword. It'll look up the current method to see
    if an expression is needed, or if just 'return' alone is fine. */
static void return_handler(lily_parse_state *parser, int is_multiline)
{
    lily_sig *ret_sig = parser->emit->top_method_ret;
    if (ret_sig != NULL) {
        expression(parser, EX_NEED_VALUE | EX_SAVE_AST);
        lily_emit_return(parser->emit, parser->ast_pool->root, ret_sig);
        lily_ast_reset_pool(parser->ast_pool);
    }
    else
        lily_emit_return_noval(parser->emit);
}

/*  while_handler
    Syntax: 'while x: { ... }'
    This handles entering and parsing of a while statement. These are always
    multi-line. */
static void while_handler(lily_parse_state *parser, int is_multiline)
{
    if (is_multiline == 0)
        lily_raise(parser->raiser, lily_ErrSyntax, "While in single-line block.\n");

    /* Syntax: while x: { ... }
       This starts with the token on tk_word (while). */
    lily_lex_state *lex = parser->lex;

    /* First, tell the emitter we're entering a block, so that '}' will close
       it properly. */
    lily_emit_enter_block(parser->emit, BLOCK_WHILE);

    /* Grab the condition after the 'while' keyword. Use EX_SAVE_AST so that
       expression will not emit+dump the ast. */
    expression(parser, EX_NEED_VALUE | EX_SAVE_AST);
    lily_emit_ast(parser->emit, parser->ast_pool->root);
    /* 0 = jump_if_false. This jump will be patched later with the destination
       of the end of the while loop. */
    lily_emit_jump_if(parser->emit, parser->ast_pool->root, 0);
    lily_ast_reset_pool(parser->ast_pool);

    NEED_CURRENT_TOK(tk_colon)
    NEED_NEXT_TOK(tk_left_curly)

    /* Prep things for statement. */
    lily_lexer(lex);
    if (lex->token != tk_right_curly)
        statement(parser, 1);

    NEED_CURRENT_TOK(tk_right_curly)
    lily_emit_leave_block(parser->emit);
    lily_lexer(lex);
}

/*  continue_handler
    This handles a 'continue' command. This just tells the emitter to insert a
    continue, nothing fancy. */
static void continue_handler(lily_parse_state *parser, int is_multiline)
{
    lily_emit_continue(parser->emit);
}

/*  break_handler
    This handles the 'break' statement. Just a wrapper for emitter to call
    to emit a break. */
static void break_handler(lily_parse_state *parser, int is_multiline)
{
    lily_emit_break(parser->emit);
}

/*  show_handler
    This handles the show keyword. Show is a builtin command (not a function)
    that will print detailed information about a particular value. This is able
    to handle any kind of value: vars, literals, results of commands, etc. */
static void show_handler(lily_parse_state *parser, int is_multiline)
{
    expression(parser, EX_NEED_VALUE | EX_SAVE_AST);
    lily_emit_show(parser->emit, parser->ast_pool->root);
    lily_ast_reset_pool(parser->ast_pool);
}

/*  line_kw_handler
    This handles __line__. This raises an error because it's not considered all
    that useful outside of an expression. */
static void line_kw_handler(lily_parse_state *parser, int is_multiline)
{
    lily_raise(parser->raiser, lily_ErrSyntax,
               "__line__ cannot be used outside of an expression.\n");
}

/*  file_kw_handler
    This handles __file__. This raises an error because it's not considered all
    that useful outside of an expression. */
static void file_kw_handler(lily_parse_state *parser, int is_multiline)
{
    lily_raise(parser->raiser, lily_ErrSyntax,
               "__file__ cannot be used outside of an expression.\n");
}

/*  line_kw_handler
    This handles __method__. This raises an error because it's not considered
    all that useful outside of an expression. */
static void method_kw_handler(lily_parse_state *parser, int is_multiline)
{
    lily_raise(parser->raiser, lily_ErrSyntax,
               "__method__ cannot be used outside of an expression.\n");
}

/*  for_handler
    This handles a for..in statement, which is always multi-line.
    Syntax: 'for i in x..y: { ... }'
    If the loop var given does not exist, it is created as an integer and will
    fall out of scope when the loop is done.
    If it does not, then the loop var will be left alive at the end. */
static void for_handler(lily_parse_state *parser, int is_multiline)
{
    if (is_multiline == 0)
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "for..in within single-line block.\n");

    lily_lex_state *lex = parser->lex;
    lily_var *loop_var;

    NEED_CURRENT_TOK(tk_word)

    lily_emit_enter_block(parser->emit, BLOCK_FOR_IN);

    loop_var = lily_var_by_name(parser->symtab, lex->label,
            lex->label_shorthash);
    if (loop_var == NULL) {
        lily_class *cls = lily_class_by_id(parser->symtab, SYM_CLASS_INTEGER);
        loop_var = lily_try_new_var(parser->symtab, cls->sig, lex->label,
                lex->label_shorthash, 0);
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
    if (lex->token != tk_right_curly)
        statement(parser, 1);
    NEED_CURRENT_TOK(tk_right_curly)
    lily_emit_leave_block(parser->emit);
    lily_lexer(lex);
}

/*  do_handler
    This handles a do...while expression, and is always multi-line.
    Syntax: 'do: { ... } while ...'
    This starts at the ':' after 'do'. */
static void do_handler(lily_parse_state *parser, int is_multiline)
{
    if (is_multiline == 0)
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "do...while in non-multi-line block.\n");

    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, BLOCK_DO_WHILE);
    NEED_CURRENT_TOK(tk_colon)
    NEED_NEXT_TOK(tk_left_curly)

    /* Pull up the next token to get statement started. */
    lily_lexer(lex);
    if (lex->token != tk_right_curly)
        statement(parser, 1);

    NEED_CURRENT_TOK(tk_right_curly)
    NEED_NEXT_TOK(tk_word)
    /* This could do a keyword scan, but there's only one correct answer
       so...nah. */
    if (strcmp(lex->label, "while") != 0)
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "Expected 'while', not '%s'.\n", lex->label);

    /* Now prep the token for expression. Save the resulting tree so that
       it can be eval'd specially. */
    lily_lexer(lex);
    expression(parser, EX_NEED_VALUE | EX_SAVE_AST);
    lily_eval_do_while_expr(parser->emit, parser->ast_pool->root);
    /* Drop the tree and leave the block. */
    lily_ast_reset_pool(parser->ast_pool);
    lily_emit_leave_block(parser->emit);
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
            statement(parser, 1);
        else if (lex->token == tk_right_curly) {
            if (parser->emit->current_block->block_type == BLOCK_DO_WHILE)
                parse_do_while_expr(parser);
            lily_emit_leave_block(parser->emit);
            lily_lexer(parser->lex);
        }
        else if (lex->token == tk_end_tag ||
                 (lex->token == tk_eof && lex->mode != lm_from_file)) {
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

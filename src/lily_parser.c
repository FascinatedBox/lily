#include <string.h>

#include "lily_impl.h"
#include "lily_parser.h"
#include "lily_parser_tok_table.h"
#include "lily_pkg_sys.h"

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

/* These flags are for collect_var_sig. */

/* Expect a name with every class given. Create a var for each class+name pair.
   This is suitable for collecting the args of a function. */
#define CV_MAKE_VARS  0x1

/* This is set if the variable is not inside another variable. This is suitable
   for collecting a function that may have named arguments. */
#define CV_TOPLEVEL   0x2

#define NEED_NEXT_TOK(expected) \
lily_lexer(lex); \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_ErrSyntax, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token));

#define NEED_CURRENT_TOK(expected) \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_ErrSyntax, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token));

/* This flag is for expression. If it's set, then expression won't try to get
   a value. */
#define EXPR_DONT_NEED_VALUE 0x1

/** Parser initialization and deletion **/
lily_parse_state *lily_new_parse_state(void *data, int argc, char **argv)
{
    lily_parse_state *parser = lily_malloc(sizeof(lily_parse_state));
    lily_raiser *raiser = lily_new_raiser();

    if (parser == NULL)
        return NULL;

    /* This ensures that runners always have a valid parser mode when trying to
       figure out how to show an error. */
    parser->mode = pm_init;
    parser->sig_stack_pos = 0;
    parser->sig_stack_size = 4;
    parser->raiser = raiser;
    parser->sig_stack = lily_malloc(4 * sizeof(lily_sig *));
    parser->ast_pool = lily_new_ast_pool(raiser, 8);
    parser->symtab = lily_new_symtab(raiser);
    parser->emit = lily_new_emit_state(raiser);
    parser->lex = lily_new_lex_state(raiser, data);
    parser->vm = lily_new_vm_state(raiser, data);

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
    parser->emit->oo_name_pool = parser->ast_pool->oo_name_pool;

    /* This creates a new var, so it has to be done after symtab's lex_linenum
       is set. */
    if (lily_pkg_sys_init(parser->symtab, argc, argv) == 0) {
        lily_free_parse_state(parser);
        return NULL;
    }

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

    var = lily_var_by_name(parser->symtab, lex->label);
    if (var != NULL)
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "%s has already been declared.\n", lex->label);

    var = lily_try_new_var(parser->symtab, var_sig, lex->label, flags);
    if (var == NULL)
        lily_raise_nomem(parser->raiser);

    return var;
}

/** Var sig collection
  * collect_* functions are used by collect_var_sig as helpers, and should not
    be called by any function except expression itself.
  * This handles sig collection for complex signatures (lists and function, for
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
    lily_sig *last_arg_sig = NULL;

    /* A callable's arguments are named if it's not an argument to something
       else. */
    if (flags & CV_TOPLEVEL)
        call_flags |= CV_MAKE_VARS;

    if (lex->token != tk_arrow) {
        while (1) {
            if (parser->sig_stack_pos == parser->sig_stack_size)
                grow_sig_stack(parser);

            last_arg_sig = collect_var_sig(parser, call_flags);
            parser->sig_stack[parser->sig_stack_pos] = last_arg_sig;
            parser->sig_stack_pos++;

            lily_lexer(lex);
            if (lex->token == tk_arrow ||
                lex->token == tk_right_parenth ||
                lex->token == tk_three_dots)
                break;

            NEED_CURRENT_TOK(tk_comma)
            lily_lexer(lex);
        }
    }

    int num_args = parser->sig_stack_pos - save_pos;

    /* ... at the end means it's varargs. It must be a list of some type,
       because the interpreter boxes the varargs into a list. */
    if (lex->token == tk_three_dots) {
        if (num_args == 0)
            lily_raise(parser->raiser, lily_ErrSyntax,
                       "Unexpected token %s.\n", tokname(lex->token));

        if (last_arg_sig->cls->id != SYM_CLASS_LIST) {
            lily_raise(parser->raiser, lily_ErrSyntax,
                    "A list is required for variable arguments (...).\n");
        }

        call_sig->flags |= SIG_IS_VARARGS;
        lily_lexer(lex);
        if (lex->token != tk_right_parenth &&
            lex->token != tk_arrow)
            lily_raise(parser->raiser, lily_ErrSyntax,
                    "Expected either '=>' or ')' after '...' .\n");
    }

    /* This doesn't need to be registered in the sig stack because there's
       only going to be one return. */
    lily_sig *return_sig = NULL;
    if (lex->token == tk_arrow) {
        lily_lexer(lex);
        return_sig = collect_var_sig(parser, 0);
        NEED_NEXT_TOK(tk_right_parenth);
    }

    /* +1 for the return. */
    lily_sig **siglist = lily_malloc((num_args + 1) * sizeof(lily_sig *));
    if (siglist == NULL)
        lily_raise_nomem(parser->raiser);

    for (i = 0;i < num_args;i++)
        siglist[i + 1] = parser->sig_stack[save_pos + i];

    siglist[0] = return_sig;
    call_sig->siglist_size = i + 1;
    call_sig->siglist = siglist;
    parser->sig_stack_pos = save_pos;
}

/* collect_var_sig
   This is the entry point for complex signature collection. */
static lily_sig *collect_var_sig(lily_parse_state *parser, int flags)
{
    lily_lex_state *lex = parser->lex;
    lily_class *cls;

    NEED_CURRENT_TOK(tk_word)
    cls = lily_class_by_name(parser->symtab, lex->label);
    if (cls == NULL)
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "unknown class name %s.\n", lex->label);

    lily_sig *result;

    if (cls->id != SYM_CLASS_FUNCTION && cls->id != SYM_CLASS_TUPLE &&
        cls->id != SYM_CLASS_LIST && cls->id != SYM_CLASS_HASH) {
        result = cls->sig;
        if (flags & CV_MAKE_VARS)
            get_named_var(parser, cls->sig, 0);
    }
    else if (cls->id == SYM_CLASS_TUPLE) {
        int i, save_pos;

        save_pos = parser->sig_stack_pos;
        lily_sig *new_sig = lily_try_sig_for_class(parser->symtab, cls);
        if (new_sig == NULL)
            lily_raise_nomem(parser->raiser);

        NEED_NEXT_TOK(tk_left_bracket)
        lily_lexer(lex);
        for (i = 1;    ;i++) {
            if (parser->sig_stack_pos == parser->sig_stack_size)
                grow_sig_stack(parser);

            lily_sig *sig = collect_var_sig(parser, 0);
            parser->sig_stack[parser->sig_stack_pos] = sig;
            parser->sig_stack_pos++;
            lily_lexer(lex);
            if (lex->token == tk_comma) {
                lily_lexer(lex);
                continue;
            }
            else if (lex->token == tk_right_bracket)
                break;
        }

        lily_sig **siglist = lily_malloc(i * sizeof(lily_sig *));
        if (siglist == NULL)
            lily_raise_nomem(parser->raiser);

        new_sig->siglist = siglist;
        new_sig->siglist_size = i;
        int j;
        for (j = 0;j < i;j++)
            siglist[j] = parser->sig_stack[save_pos + j];

        parser->sig_stack_pos = save_pos;
        result = lily_ensure_unique_sig(parser->symtab, new_sig);
        if (flags & CV_MAKE_VARS)
            get_named_var(parser, result, 0);
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

        if (cls->id == SYM_CLASS_HASH) {
            /* Don't use siglist for this check, because lily_ensure_unique_sig
               could have destroyed it. Instead, do new_sig->siglist.
               Classes that are valid hash keys are flagged as such, so check
               for that flag. */
            if ((new_sig->siglist[0]->cls->flags & CLS_VALID_HASH_KEY) == 0) {
                lily_raise(parser->raiser, lily_ErrSyntax,
                        "'%T' is not a valid hash key.\n", new_sig->siglist[0]);
            }
        }

        result = new_sig;
        if (flags & CV_MAKE_VARS)
            get_named_var(parser, result, 0);
    }
    else if (cls->id == SYM_CLASS_FUNCTION) {
        lily_sig *call_sig = lily_try_sig_for_class(parser->symtab, cls);
        lily_var *call_var;

        if (call_sig == NULL)
            lily_raise_nomem(parser->raiser);

        if (flags & CV_MAKE_VARS) {
            if (flags & CV_TOPLEVEL) {
                call_var = get_named_var(parser, call_sig, VAR_IS_READONLY);
                lily_emit_enter_block(parser->emit, BLOCK_FUNCTION);
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

        call_sig = lily_ensure_unique_sig(parser->symtab, call_sig);
        if (flags & CV_MAKE_VARS)
            call_var->sig = call_sig;

        /* Let emitter know the true return type. */
        if (flags & CV_TOPLEVEL)
            parser->emit->top_function_ret = call_sig->siglist[0];

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

/* expression_oo
   This handles calls to a member of a particular value, as well as typecasts.
   Calls to a member look like this: `value.member()`
   * This enters the member, which will later take the value as the first
     argument to it.
   Typecasts look like this: `value.@(newtype)`.
   * This handles calling for type collection and adding the typecast tree. */
static void expression_oo(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);
    if (lex->token == tk_word) {
        /* Syntax: `value.member()`. Add this as a special 'oo_call' tree. The
           emitter will handle doing the member lookup at emit-time. */
        lily_ast_push_oo_call(parser->ast_pool, parser->lex->label);
        lily_ast_enter_tree(parser->ast_pool, tree_call, NULL);
        NEED_NEXT_TOK(tk_left_parenth);
        lily_lexer(lex);
    }
    else if (lex->token == tk_typecast_parenth) {
        /* Syntax: `value.@(type)`. This is at the @(, so prep for
           collect_var_sig. */
        lily_lexer(lex);
        lily_sig *new_sig = collect_var_sig(parser, 0);
        lily_ast_enter_typecast(parser->ast_pool, new_sig);
        /* Verify that ')' is next so that the caller will close the tree. This
           causes the typecast result to be seen as a proper value.sub */
        NEED_NEXT_TOK(tk_right_parenth)
    }
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
        ret = lily_get_string_literal(symtab, parser->lex->filename);
    else if (key_id == KEY__FUNCTION__)
        ret = lily_get_string_literal(symtab, parser->emit->top_var->name);
    else
        ret = NULL;

    return ret;
}

/*  expression_package
    This handles x::y kinds of expressions. This is called when a var is seen
    that is a package. There are a few caveats to this:
    * A check for :: is forced so that an inner var can be collected, instead
      of letting packages be assignable. This was done so that package accesses
      can be effectively computed at emit time (given that packages are
      initialized through parser and not assignable or able to be put in lists).
    * This does not check for packages in packages, because those don't
      currently exist.
    * For the same reason, a callable inner var is also not checked for.
    * This enters a package tree to stay consistent with all non-(binary/unary)
      trees.
    * This enters tree_package to be consistent with how other things
      (subscripts, list building, typecasts, etc.) all create enterable trees
      to handle things. */
static void expression_package(lily_parse_state *parser, lily_var *package_var)
{
    lily_ast_pool *ap = parser->ast_pool;
    lily_lex_state *lex = parser->lex;
    lily_var *scope = package_var->value.package->vars[0];
    int depth = 1;

    while (1) {
        lily_ast_enter_tree(ap, tree_package, NULL);
        /* For the first pass, push the var given as the package. Subsequent
           entries will swallow the last tree entered as their first argument,
           so this next bit isn't necessary. */
        if (depth == 1) {
            lily_ast_push_sym(ap, (lily_sym *)package_var);
            lily_ast_collect_arg(ap);
        }

        NEED_CURRENT_TOK(tk_colon_colon)
        NEED_NEXT_TOK(tk_word)

        lily_var *inner_var = lily_scoped_var_by_name(parser->symtab, scope,
                lex->label);
        if (inner_var == NULL) {
            lily_raise(parser->raiser, lily_ErrSyntax,
                    "Package %s has no member %s.\n",
                    package_var->name, lex->label);
        }

        lily_ast_push_sym(ap, (lily_sym *)inner_var);
        lily_ast_collect_arg(ap);
        lily_ast_leave_tree(ap);
        lily_lexer(lex);
        if (lex->token == tk_colon_colon) {
            depth++;
            scope = inner_var->value.package->vars[0];
            if (inner_var->sig->cls->id != SYM_CLASS_PACKAGE) {
                lily_raise(parser->raiser, lily_ErrSyntax,
                    "'%s' is not a package.\n", inner_var->name);
            }
            continue;
        }

        break;
    }
}

/*  maybe_digit_fixup
    This is called when parser is expecting a binary op and gets an integer or
    number constant. The value may be part of the next expression, or it may be
    something like '+1'. If the former, there's no fixup to do. If the latter,
    it's fixed up and added to the ast as '+' and the value.
    This allows 'a = 1+1' to work correctly.

    * did_fixup: This is set to 1 if a fixup was done, 0 otherwise. */
static void maybe_digit_fixup(lily_parse_state *parser, int *did_fixup)
{
    /* The lexer records where the last digit scan started. So check if it
       started with '+' or '-'. */
    lily_lex_state *lex = parser->lex;
    char ch = lex->input_buffer[lex->last_digit_start];

    if (ch == '-' || ch == '+') {
        int expr_op;
        lily_class *cls;
        lily_symtab *symtab = parser->symtab;

        if (ch == '-')
            expr_op = parser_tok_table[tk_minus].expr_op;
        else
            expr_op = parser_tok_table[tk_plus].expr_op;

        if (lex->token == tk_integer)
            cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
        else
            cls = lily_class_by_id(symtab, SYM_CLASS_DOUBLE);

        lily_ast_push_binary_op(parser->ast_pool, (lily_expr_op)expr_op);
        /* Call this to force a rescan from the proper starting point, yielding
           a proper new token. */
        lily_lexer_digit_rescan(lex);
        lily_literal *lit;
        lit = lily_get_intnum_literal(symtab, cls, lex->value);
        lily_ast_push_readonly(parser->ast_pool, (lily_sym *)lit);
        *did_fixup = 1;
    }
    else
        *did_fixup = 0;
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
            lily_var *var = lily_var_by_name(symtab, lex->label);

            if (var) {
                lily_lexer(lex);
                if (lex->token == tk_left_parenth) {
                    int cls_id = var->sig->cls->id;
                    if (cls_id != SYM_CLASS_FUNCTION)
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
                    if (var->function_depth == 1) {
                        if (var->sig->cls->id == SYM_CLASS_PACKAGE)
                            expression_package(parser, var);
                        else {
                            /* It's in __main__ as a global. */
                            lily_ast_push_sym(parser->ast_pool,
                                    (lily_sym *)var);
                        }
                    }
                    else if (var->function_depth == parser->emit->function_depth)
                        /* In this current scope? Load as a local var. */
                        lily_ast_push_local_var(parser->ast_pool, var);
                    else if (var->function_depth == -1)
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
                int key_id = lily_keyword_by_name(lex->label);
                if (key_id == KEY__LINE__ || key_id == KEY__FILE__ ||
                    key_id == KEY__FUNCTION__) {
                    lily_literal *lit;
                    lit = parse_special_keyword(parser, key_id);
                    lily_ast_push_readonly(parser->ast_pool, (lily_sym *)lit);
                    lily_lexer(lex);
                }
                else if (key_id == KEY_ISNIL) {
                    lily_ast_enter_tree(parser->ast_pool, tree_isnil, NULL);
                    NEED_NEXT_TOK(tk_left_parenth)
                    lily_lexer(lex);
                    continue;
                }
                else {
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "%s has not been declared.\n", lex->label);
                }
            }
        }
        else if (lex->token == tk_double_quote) {
            lily_literal *lit;
            lit = lily_get_string_literal(symtab, lex->label);

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
        else if (lex->token == tk_double) {
            lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_DOUBLE);
            lily_literal *lit;
            lit = lily_get_intnum_literal(symtab, cls, lex->value);
            lily_ast_push_readonly(parser->ast_pool, (lily_sym *)lit);

            lily_lexer(lex);
        }
        else if (lex->token == tk_minus || lex->token == tk_not) {
            expression_unary(parser);
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
                        "Empty lists must specify a type (ex: [string]).\n");
            else if (lex->token == tk_word) {
                lily_class *cls = lily_class_by_name(symtab, lex->label);

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
        else if (lex->token == tk_tuple_open) {
            lily_ast_enter_tree(parser->ast_pool, tree_tuple, NULL);
            lily_lexer(lex);
            continue;
        }
        else
            lily_raise(parser->raiser, lily_ErrSyntax,
                       "Expected a value, not '%s'.\n",
                       tokname(lex->token));

        if (lex->token == tk_dot) {
            expression_oo(parser);
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

    if (tt == tree_call || tt == tree_parenth || tt == tree_typecast ||
        tt == tree_isnil)
        expect = tk_right_parenth;
    else if (tt == tree_tuple)
        expect = tk_tuple_close;
    else
        expect = tk_right_bracket;

    if (token != expect)
        lily_raise(parser->raiser, lily_ErrSyntax,
                "Expected closing token '%s', not '%s'.\n", tokname(expect),
                tokname(token));
}

/*  expression

    This is the function to call for parsing an expression of any sort. This
    will call the helpers it has (expression_* functions) when it needs to.

    This function expects that the token is setup for it. So it may be
    necessary to call lily_lexer to prep the token before calling this.

    This will not cause the ast to be evaluated. Use a lily_emit_eval_*
    function to actually evaluate the built up expression. Those functions
    will also clear the ast pool when they emit, so that expression never has
    to worry about clearing the pool. */
static void expression(lily_parse_state *parser, int options)
{
    lily_lex_state *lex = parser->lex;

    if ((options & EXPR_DONT_NEED_VALUE) == 0)
        expression_value(parser);

    while (1) {
        int expr_op = parser_tok_table[lex->token].expr_op;
        if (expr_op != -1) {
            lily_ast_push_binary_op(parser->ast_pool, (lily_expr_op)expr_op);
            lily_lexer(lex);
        }
        else if (parser->ast_pool->save_depth == 0 &&
                 parser_tok_table[lex->token].val_or_end)
            break;
        else if (lex->token == tk_right_parenth ||
                 lex->token == tk_right_bracket ||
                 lex->token == tk_tuple_close) {
            if (parser->ast_pool->save_depth == 0)
                lily_raise(parser->raiser, lily_ErrSyntax,
                        "Unexpected token %s.\n", tokname(lex->token));

            check_valid_close_tok(parser);
            lily_ast_leave_tree(parser->ast_pool);

            lily_lexer(lex);
            if (parser->ast_pool->save_depth == 0 &&
                parser_tok_table[lex->token].val_or_end == 1)
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
            else if (lex->token == tk_tuple_open) {
                lily_ast_enter_tree(parser->ast_pool, tree_tuple, NULL);
                lily_lexer(lex);
                if (lex->token == tk_tuple_close)
                    continue;
            }
            else if (lex->token != tk_dot)
                /* If not '.', then assume it's a binary token. */
                continue;
            else {
                /* 'a.concat("b").concat("c")'. Do a normal oo merge. */
                expression_oo(parser);
                /* Jump back up in case of (), like above. */
                if (lex->token == tk_right_parenth)
                    continue;
            }
        }
        else if (lex->token == tk_comma || lex->token == tk_arrow) {
            if (parser->ast_pool->active == NULL)
                lily_raise(parser->raiser, lily_ErrSyntax,
                           "Expected a value, not ','.\n");

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
        else if (lex->token == tk_integer || lex->token == tk_double) {
            int did_fixup = 0;
            maybe_digit_fixup(parser, &did_fixup);
            if (did_fixup) {
                /* If it gets broken down, then it becomes a binary op and a
                   value. Call up the next token, and go back up since there's
                   no state change. */
                lily_lexer(lex);
                continue;
            }
            /* It's the start of the next expression so long as there's no
               currently-running expressions. */
            else if (parser->ast_pool->save_depth == 0)
                break;
            else
                lily_raise(parser->raiser, lily_ErrSyntax,
                           "Unexpected token %s during expression.\n",
                           tokname(lex->token));
        }
        else {
            lily_raise(parser->raiser, lily_ErrSyntax,
                       "Unexpected token %s during expression.\n",
                       tokname(lex->token));
        }
        expression_value(parser);
    }
}

/* parse_decl
   This function takes a sig and handles a declaration wherein each var name
   is separated by a comma. Ex:

   integer a, b, c
   double d
   list[integer] e

   This handles anything but function declarations.
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
            /* Push the value and the assignment, then call up expresison. */
            lily_ast_push_sym(parser->ast_pool, (lily_sym *)var);
            lily_ast_push_binary_op(parser->ast_pool, expr_assign);
            lily_lexer(lex);
            expression(parser, 0);
            lily_emit_eval_expr(parser->emit, parser->ast_pool);
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

static lily_var *parse_for_range_value(lily_parse_state *parser, char *name)
{
    lily_ast_pool *ap = parser->ast_pool;
    expression(parser, 0);

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
       found by the user. */
    lily_var *var = lily_try_new_var(parser->symtab, cls->sig, name, 0);
    if (var == NULL)
        lily_raise_nomem(parser->raiser);

    lily_emit_eval_expr_to_var(parser->emit, ap, var);

    return var;
}

/** Statement and statement helpers.
    These *_handler functions are used to handle a keyword. This allows one to
    grab a keyword, then do 'handlers[key_id](parser, multi)' and not have to
    worry about anything.
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
static void function_kw_handler(lily_parse_state *, int);
static void for_handler(lily_parse_state *, int);
static void do_handler(lily_parse_state *, int);
static void isnil_handler(lily_parse_state *, int);

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
    line_kw_handler,
    file_kw_handler,
    function_kw_handler,
    for_handler,
    do_handler,
    isnil_handler
};

/*  statement
    This is a magic function that handles keywords outside of expression,
    as well as getting declarations started.
    If multi is set, this function will do the above until it finds a starting
    token that isn't a label. */
static void statement(lily_parse_state *parser, int multi)
{
    int key_id;
    lily_class *lclass;
    lily_lex_state *lex = parser->lex;

    do {
        key_id = lily_keyword_by_name(lex->label);

        if (key_id != -1) {
            /* Ask the handler for this keyword what to do. */
            lily_lexer(lex);
            handlers[key_id](parser, multi);
        }
        else {
            lclass = lily_class_by_name(parser->symtab, lex->label);

            if (lclass != NULL) {
                int cls_id = lclass->id;

                if (cls_id == SYM_CLASS_FUNCTION) {
                    /* This will enter the function since the function is toplevel. */
                    collect_var_sig(parser, CV_TOPLEVEL | CV_MAKE_VARS);
                    NEED_NEXT_TOK(tk_left_curly)
                    lily_lexer(lex);
                }
                else if (cls_id == SYM_CLASS_LIST || cls_id == SYM_CLASS_HASH ||
                         cls_id == SYM_CLASS_TUPLE) {
                    lily_sig *cls_sig = collect_var_sig(parser, 0);
                    parse_decl(parser, cls_sig);
                }
                else
                    parse_decl(parser, lclass->sig);
            }
            else {
                expression(parser, 0);
                lily_emit_eval_expr(parser->emit, parser->ast_pool);
            }
        }
    } while (multi && lex->token == tk_word);
}

/*  parse_block_body
    This is a helper function for parsing the body of a simple (but multi-line)
    block. This is suitable for 'while', 'do while', and 'for...in'.

    This is called when the current token is the ':'. It will handle the '{',
    call statement, then check that '}' is found after the statement. Finally,
    it calls up the next token for the parent.

    for i in 1..10: { ... }
                  ^
    do: {  ... } while 1:
      ^
    while 1: { ... }
           ^
    if 1: { ... }
        ^
    */
static void parse_multiline_block_body(lily_parse_state *parser,
        int multi)
{
    lily_lex_state *lex = parser->lex;

    if (multi == 0)
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "Multi-line block within single-line block.\n");

    lily_lexer(lex);
    /* statement expects the token to be ready. */
    if (lex->token != tk_right_curly)
        statement(parser, 1);
    NEED_CURRENT_TOK(tk_right_curly)
    lily_lexer(lex);
}

/*  if_handler
    This handles parsing 'if'. There are two kinds of if blocks:
    (multi-line)  if expr { expr... }
    (single-line) if expr: expr

    'elif' and 'else' are multi-line if 'if' is multi-line. The 'if' is closed
    by a single '}', rather than by each 'elif'/'else' (like with C). */
static void if_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, BLOCK_IF);
    expression(parser, 0);
    lily_emit_eval_condition(parser->emit, parser->ast_pool);
    NEED_CURRENT_TOK(tk_colon)

    lily_lexer(lex);
    if (lex->token == tk_left_curly) {
        parse_multiline_block_body(parser, multi);
        lily_emit_leave_block(parser->emit);
    }
    else {
        /* Single-line statement won't jump into other blocks though. */
        statement(parser, 0);
        if (lex->token == tk_word) {
            int key_id = lily_keyword_by_name(lex->label);

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
static void elif_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;
    lily_emit_change_if_branch(parser->emit, /*have_else=*/0);
    expression(parser, 0);
    lily_emit_eval_condition(parser->emit, parser->ast_pool);

    NEED_CURRENT_TOK(tk_colon)

    lily_lexer(lex);
    statement(parser, multi);

    if (multi == 0) {
        if (lex->token == tk_word) {
            int key_id = lily_keyword_by_name(lex->label);

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
static void else_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_change_if_branch(parser->emit, /*have_else=*/1);
    NEED_CURRENT_TOK(tk_colon)
    lily_lexer(lex);

    statement(parser, multi);
    if (multi == 0)
        lily_emit_leave_block(parser->emit);
}

/*  return_handler
    This handles the return keyword. It'll look up the current function to see
    if an expression is needed, or if just 'return' alone is fine. */
static void return_handler(lily_parse_state *parser, int multi)
{
    lily_sig *ret_sig = parser->emit->top_function_ret;
    if (ret_sig != NULL) {
        expression(parser, 0);
        lily_emit_return(parser->emit, parser->ast_pool->root, ret_sig);
        lily_ast_reset_pool(parser->ast_pool);
    }
    else
        lily_emit_return_noval(parser->emit);
}

/*  while_handler
    Syntax:
        (multi-line)  while expr: { expr... }
        (single-line) while expr: expr
    */
static void while_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, BLOCK_WHILE);

    expression(parser, 0);
    lily_emit_eval_condition(parser->emit, parser->ast_pool);

    NEED_CURRENT_TOK(tk_colon)
    lily_lexer(lex);
    if (lex->token == tk_left_curly)
        parse_multiline_block_body(parser, multi);
    else
        statement(parser, 0);

    lily_emit_leave_block(parser->emit);
}

/*  continue_handler
    This handles a 'continue' command. This just tells the emitter to insert a
    continue, nothing fancy. */
static void continue_handler(lily_parse_state *parser, int multi)
{
    lily_emit_continue(parser->emit);
}

/*  break_handler
    This handles the 'break' statement. Just a wrapper for emitter to call
    to emit a break. */
static void break_handler(lily_parse_state *parser, int multi)
{
    lily_emit_break(parser->emit);
}

/*  show_handler
    This handles the show keyword. Show is a builtin command (not a function)
    that will print detailed information about a particular value. This is able
    to handle any kind of value: vars, literals, results of commands, etc. */
static void show_handler(lily_parse_state *parser, int multi)
{
    expression(parser, 0);
    lily_emit_show(parser->emit, parser->ast_pool->root);
    lily_ast_reset_pool(parser->ast_pool);
}

/*  do_keyword
    This handles simple keywords that can start expressions. It unifies common
    code in __line__, __file__, and __function__.

    key_id: The id of the keyword to handle. */
static void do_keyword(lily_parse_state *parser, int key_id)
{
    lily_literal *lit;
    lit = parse_special_keyword(parser, key_id);
    lily_ast_push_readonly(parser->ast_pool, (lily_sym *)lit);

    expression(parser, EXPR_DONT_NEED_VALUE);
    lily_emit_eval_expr(parser->emit, parser->ast_pool);
}

/*  line_kw_handler
    This handles __line__. This raises an error because it's not considered all
    that useful outside of an expression. */
static void line_kw_handler(lily_parse_state *parser, int multi)
{
    do_keyword(parser, KEY__LINE__);
}

/*  file_kw_handler
    This handles __file__. This raises an error because it's not considered all
    that useful outside of an expression. */
static void file_kw_handler(lily_parse_state *parser, int multi)
{
    do_keyword(parser, KEY__FILE__);
}

/*  function_kw_handler
    This handles __function__. This raises an error because it's not considered
    all that useful outside of an expression. */
static void function_kw_handler(lily_parse_state *parser, int multi)
{
    do_keyword(parser, KEY__FUNCTION__);
}

/*  for_handler
    Syntax:
        (multi-line)  for var in start..end: { expr... }
        (single-line) for var in start..end: expr

    This handles for..in statements. These only accept integers for var, start,
    and end. Additionally, start and end can be expressions, but may not
    contain any sort of assignment.

    (So this would be invalid: for i in a = 10..11: ...)
    (But this is okay: for i in 1+2..4*4: ...)

    If var does not exist, it is created as an integer, and falls out of scope
    when the loop exits.
    If var does exist, then it will exist after the loop. */
static void for_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;
    lily_var *loop_var;

    NEED_CURRENT_TOK(tk_word)

    lily_emit_enter_block(parser->emit, BLOCK_FOR_IN);

    loop_var = lily_var_by_name(parser->symtab, lex->label);
    if (loop_var == NULL) {
        lily_class *cls = lily_class_by_id(parser->symtab, SYM_CLASS_INTEGER);
        loop_var = lily_try_new_var(parser->symtab, cls->sig, lex->label, 0);
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
    lily_lexer(lex);
    if (lex->token == tk_left_curly)
        parse_multiline_block_body(parser, multi);
    else
        statement(parser, 0);

    lily_emit_leave_block(parser->emit);
}

/*  do_handler
    Syntax:
        (multi-line)  do: { expr... } while expr:
        (single-line) do: expr while expr:
    This is like while, except there's no check on entry and the while check
    jumps to the top if successful. */
static void do_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, BLOCK_DO_WHILE);

    NEED_CURRENT_TOK(tk_colon)
    lily_lexer(lex);
    if (lex->token == tk_left_curly)
        parse_multiline_block_body(parser, multi);
    else
        statement(parser, 0);

    NEED_CURRENT_TOK(tk_word)
    /* This could do a keyword scan, but there's only one correct answer
       so...nah. */
    if (strcmp(lex->label, "while") != 0)
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "Expected 'while', not '%s'.\n", lex->label);

    /* Now prep the token for expression. Save the resulting tree so that
       it can be eval'd specially. */
    lily_lexer(lex);
    expression(parser, 0);
    lily_emit_eval_do_while_expr(parser->emit, parser->ast_pool);
    lily_emit_leave_block(parser->emit);
}

static void isnil_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;
    lily_ast_enter_tree(parser->ast_pool, tree_isnil, NULL);
    NEED_CURRENT_TOK(tk_left_parenth)
    lily_lexer(lex);
    expression(parser, 0);
    lily_emit_eval_expr(parser->emit, parser->ast_pool);
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
            lily_emit_leave_block(parser->emit);
            lily_lexer(lex);
        }
        else if (lex->token == tk_end_tag ||
                 (lex->token == tk_eof && lex->mode == lm_no_tags)) {
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
        /* This makes it possible to have expressions that don't start with a
           var. This may be useful later for building a repl. */
        else if (lex->token == tk_integer || lex->token == tk_double ||
                 lex->token == tk_double_quote ||
                 lex->token == tk_left_parenth ||
                 lex->token == tk_left_bracket) {
            expression(parser, 0);
            lily_emit_eval_expr(parser->emit, parser->ast_pool);
        }
        else
            lily_raise(parser->raiser, lily_ErrSyntax, "Unexpected token %s.\n",
                       tokname(lex->token));
    }
}

/*  lily_parse_file
    This function starts parsing from a file indicated by the given filename.
    The file is opened through fopen, and is automatically destroyed when the
    parser is free'd.

    parser:  The parser that will be used to parse and run the data.
    mode:    This determines if <@lily @> tags are parsed or not.
    str:     The string to parse.

    Returns 1 if successful, or 0 if an error was raised. */
int lily_parse_file(lily_parse_state *parser, lily_lex_mode mode, char *filename)
{
    if (setjmp(parser->raiser->jumps[parser->raiser->jump_pos]) == 0) {
        parser->raiser->jump_pos++;
        lily_load_file(parser->lex, mode, filename);
        if (parser->lex->token != tk_eof)
            lily_parser(parser);

        return 1;
    }

    return 0;
}

/*  lily_parse_string
    This function starts parsing from a source that is a string passed. The caller
    is responsible for destroying the string if it needs to be destroyed.

    parser:  The parser that will be used to parse and run the data.
    mode:    This determines if <@lily @> tags are parsed or not.
    str:     The string to parse.

    Returns 1 if successful, or 0 if some error occured. */
int lily_parse_string(lily_parse_state *parser, lily_lex_mode mode, char *str)
{
    if (setjmp(parser->raiser->jumps[parser->raiser->jump_pos]) == 0) {
        parser->raiser->jump_pos++;
        lily_load_str(parser->lex, mode, str);
        lily_parser(parser);
        return 1;
    }

    return 0;
}

/*  lily_parse_special
    This function starts parsing from a source given by the runner. Use this if
    a given source isn't a file or a str.

    parser:       The parser that will be used to parse and run the data.
    mode:         This determines if <@lily @> tags are parsed or not.
    source:       The source providing text for the lexer to read.
    filename:     A filename for this source.
    read_line_fn: A function for the lexer to call to read a line from the
                  source.
    close_fn:     A function for the lexer to call to close the data source. If
                  the source does not need to be closed, this should be a no-op
                  function, not NULL. */
int lily_parse_special(lily_parse_state *parser, lily_lex_mode mode,
    void *source, char *filename, lily_reader_fn read_line_fn,
    lily_close_fn close_fn)
{
    if (setjmp(parser->raiser->jumps[parser->raiser->jump_pos]) == 0) {
        parser->raiser->jump_pos++;
        lily_load_special(parser->lex, mode, source, filename, read_line_fn,
                close_fn);
        lily_parser(parser);
        return 1;
    }

    return 0;
}
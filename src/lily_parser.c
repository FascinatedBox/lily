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
    lily_raise(parser->raiser, lily_SyntaxError, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token));

#define NEED_CURRENT_TOK(expected) \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_SyntaxError, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token));

/*****************************************************************************/
/* Parser creation and teardown                                              */
/*****************************************************************************/

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
    parser->vm->symtab = parser->symtab;
    parser->symtab->lex_linenum = &parser->lex->line_num;
    parser->ast_pool->lex_linenum = &parser->lex->line_num;
    parser->emit->lex_linenum = &parser->lex->line_num;
    parser->emit->symtab = parser->symtab;
    parser->emit->oo_name_pool = parser->ast_pool->oo_name_pool;

    /* When declaring a new function, initially give it the same signature as
       __main__. This ensures that, should building the proper sig fail, the
       symtab will still see the function as a function and destroy the
       contents. */
    parser->default_call_sig = parser->vm->main->sig;

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

/*****************************************************************************/
/* Shared code                                                               */
/*****************************************************************************/

/*  get_named_var
    Attempt to create a var with the given signature. This will call lexer to
    get the name, as well as ensuring that the given var is unique. */
static lily_var *get_named_var(lily_parse_state *parser, lily_sig *var_sig,
        int flags)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var;

    var = lily_var_by_name(parser->symtab, lex->label);
    if (var != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                   "%s has already been declared.\n", lex->label);

    var = lily_try_new_var(parser->symtab, var_sig, lex->label, flags);
    if (var == NULL)
        lily_raise_nomem(parser->raiser);

    lily_lexer(lex);
    return var;
}

/*  grow_sig_stack
    Make the stack holding type information bigger for more types. */
static void grow_sig_stack(lily_parse_state *parser)
{
    parser->sig_stack_size *= 2;

    lily_sig **new_sig_stack = lily_realloc(parser->sig_stack,
        sizeof(lily_sig *) * parser->sig_stack_size);

    if (new_sig_stack == NULL)
        lily_raise_nomem(parser->raiser);

    parser->sig_stack = new_sig_stack;
}

/*****************************************************************************/
/* Type collection                                                           */
/*****************************************************************************/

static lily_sig *collect_var_sig(lily_parse_state *parser, lily_class *cls,
        int flags);

#define TC_DEMAND_VALUE  1
#define TC_WANT_VALUE    2
/* In this case, an operator is => or , or ... */
#define TC_WANT_OPERATOR 3
#define TC_BAD_TOKEN     4
#define TC_DONE          5

/*  inner_type_collector
    Given a class that takes inner types (like list, hash, function, etc.),
    collect those inner types. A valid, unique signature is returned. */
static lily_sig *inner_type_collector(lily_parse_state *parser, lily_class *cls,
        int flags)
{
    int i;
    int state = TC_WANT_VALUE, stack_start = parser->sig_stack_pos;
    int sig_flags = 0, have_arrow = 0, have_dots = 0;
    lily_token end_token;
    if (cls->id == SYM_CLASS_FUNCTION) {
        /* Functions have their return as the first type, so leave a hole. */
        if ((parser->sig_stack_pos + 2) == parser->sig_stack_size)
            grow_sig_stack(parser);

        /* No value returned by default. */
        parser->sig_stack[parser->sig_stack_pos] = NULL;
        parser->sig_stack_pos++;
        /* No value needed either. But don't bump the pos again so this can be
           overwritten by incoming args. */
        parser->sig_stack[parser->sig_stack_pos + 1] = NULL;

        end_token = tk_right_parenth;
        i = 1;
    }
    else {
        end_token = tk_right_bracket;
        i = 0;
    }

    if (flags & CV_TOPLEVEL) {
        flags |= CV_MAKE_VARS;
        flags &= ~CV_TOPLEVEL;
    }
    else
        flags &= ~CV_MAKE_VARS;

    lily_lex_state *lex = parser->lex;
    while (1) {
        if (lex->token == tk_word) {
            if (parser->sig_stack_pos == parser->sig_stack_size)
                grow_sig_stack(parser);

            if (have_arrow)
                flags &= ~(CV_MAKE_VARS);

            lily_sig *sig = collect_var_sig(parser, NULL, flags);
            if (have_arrow == 0) {
                parser->sig_stack[parser->sig_stack_pos] = sig;
                parser->sig_stack_pos++;
                i++;
            }
            else
                parser->sig_stack[stack_start] = sig;

            state = TC_WANT_OPERATOR;
            continue;
        }
        else if (lex->token == tk_comma) {
            if (have_arrow || have_dots ||
                state != TC_WANT_OPERATOR)
                state = TC_BAD_TOKEN;
            else
                state = TC_DEMAND_VALUE;
        }
        else if (lex->token == tk_arrow) {
            if (state == TC_DEMAND_VALUE || have_arrow ||
                end_token == tk_right_bracket)
                state = TC_BAD_TOKEN;
            else if (state == TC_WANT_VALUE || state == TC_WANT_OPERATOR)
                state = TC_DEMAND_VALUE;

            have_arrow = 1;
        }
        else if (lex->token == end_token) {
            if (state == TC_DEMAND_VALUE)
                state = TC_BAD_TOKEN;
            else
                state = TC_DONE;
        }
        else if (lex->token == tk_three_dots) {
            if (have_dots || end_token == tk_right_bracket ||
                state != TC_WANT_OPERATOR)
                state = TC_BAD_TOKEN;
            else {
                lily_sig *last_sig;
                last_sig = parser->sig_stack[parser->sig_stack_pos - 1];
                if (last_sig->cls->id != SYM_CLASS_LIST)
                    lily_raise(parser->raiser, lily_SyntaxError,
                        "A list is required for variable arguments (...).\n");

                have_dots = 1;
                sig_flags |= SIG_IS_VARARGS;
                state = TC_WANT_OPERATOR;
            }
        }
        else
            state = TC_BAD_TOKEN;

        if (state == TC_DONE)
            break;
        else if (state == TC_BAD_TOKEN)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Unexpected token '%s'.\n", tokname(lex->token));
        else
            lily_lexer(lex);
    }

    if (parser->sig_stack_pos - stack_start != cls->template_count &&
        cls->template_count != -1) {
        lily_raise(parser->raiser, lily_SyntaxError,
                "Class %s expects %d type(s), but got %d type(s).\n",
                cls->name, cls->template_count,
                parser->sig_stack_pos - stack_start);
    }

    if (cls->id == SYM_CLASS_HASH) {
        /* For hash, make sure that the key (the first type) is valid. */
        lily_sig *check_sig = parser->sig_stack[stack_start];
        if ((check_sig->cls->flags & CLS_VALID_HASH_KEY) == 0) {
            lily_raise(parser->raiser, lily_SyntaxError,
                    "'%T' is not a valid hash key.\n", check_sig);
        }
    }

    lily_sig *result = lily_build_ensure_sig(parser->symtab, cls, sig_flags,
            parser->sig_stack, stack_start, i);
    parser->sig_stack_pos = stack_start;

    return result;
}

/*  collect_var_sig
    This is the outer part of type collection. This takes flags (CV_* defines)
    which tell it how to act. Additionally, if the parser has already scanned
    the class info, then 'cls' should be the scanned class. Otherwise, 'cls' 
    will be NULL. This is so parser can check if it's 'sometype T' or
    'sometype::member' without rewinding. */
static lily_sig *collect_var_sig(lily_parse_state *parser, lily_class *cls,
        int flags)
{
    lily_lex_state *lex = parser->lex;
    if (cls == NULL) {
        NEED_CURRENT_TOK(tk_word)
        cls = lily_class_by_name(parser->symtab, lex->label);
        if (cls == NULL)
            lily_raise(parser->raiser, lily_SyntaxError,
                       "unknown class name %s.\n", lex->label);

        lily_lexer(lex);
    }

    lily_sig *result;

    if (cls->template_count == 0) {
        result = cls->sig;
        if (flags & CV_MAKE_VARS)
            get_named_var(parser, cls->sig, 0);
    }
    else if (cls->id == SYM_CLASS_TUPLE ||
             cls->id == SYM_CLASS_LIST ||
             cls->id == SYM_CLASS_HASH) {

        NEED_CURRENT_TOK(tk_left_bracket)
        lily_lexer(lex);
        result = inner_type_collector(parser, cls, flags);

        lily_lexer(lex);
        if (flags & CV_MAKE_VARS)
            get_named_var(parser, result, 0);
    }
    else if (cls->id == SYM_CLASS_FUNCTION) {
        /* This is a dummy until the real signature is known. */
        lily_sig *call_sig = parser->default_call_sig;
        lily_var *call_var;

        if (flags & CV_MAKE_VARS) {
            if (flags & CV_TOPLEVEL) {
                call_var = get_named_var(parser, call_sig,
                        VAR_IS_READONLY);
                /* This creates a function value to hold new code, so it's
                   essential that call_var have a function signature...or the
                   symtab may not free the function value. */
                lily_emit_enter_block(parser->emit, BLOCK_FUNCTION);
            }
            else
                call_var = get_named_var(parser, call_sig, 0);
        }

        NEED_CURRENT_TOK(tk_left_parenth)
        lily_lexer(lex);
        call_sig = inner_type_collector(parser, cls, flags);

        if (flags & CV_MAKE_VARS)
            call_var->sig = call_sig;

        /* Let emitter know the true return type. */
        if (flags & CV_TOPLEVEL)
            parser->emit->top_function_ret = call_sig->siglist[0];

        result = call_sig;
        lily_lexer(lex);
    }
    else
        result = NULL;

    return result;
}

/*****************************************************************************/
/* Expression handling                                                       */
/*****************************************************************************/

/* I need a value to work with. */
#define ST_DEMAND_VALUE  1
/* A binary op or an operation (dot call, call, subscript), or a close. */
#define ST_WANT_OPERATOR 2
/* A value is nice, but not required (ex: call arguments). */
#define ST_WANT_VALUE    3
#define ST_DONE          4
#define ST_BAD_TOKEN     5

/*  expression_static_call
    This handles expressions like `<type>::member`. */
static void expression_static_call(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    NEED_NEXT_TOK(tk_word)

    lily_var *v = lily_find_class_callable(parser->symtab, cls, lex->label);
    if (v == NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "%s::%s does not exist.\n", cls->name, lex->label);

    NEED_NEXT_TOK(tk_left_parenth)

    lily_ast_push_readonly(parser->ast_pool, (lily_sym *) v);
    /* TODO: As of right now, the only things that can be accessed like this
             are functions. So this is safe to do. However, it would be nice
             to be able to call this. Unfortunately, that requires doing a lot
             of adjusting to the parser. */
    lily_ast_enter_tree(parser->ast_pool, tree_call);
}

/*  parse_special_keyword
    This handles all the simple keywords that map to a string/integer value. */
static lily_literal *parse_special_keyword(lily_parse_state *parser, int key_id)
{
    lily_symtab *symtab = parser->symtab;
    lily_literal *ret;

    /* So far, these are the only keywords that map to literals.
       Additionally, these literal fetching routines are guaranteed to either
       return a literal with the given value, or raise nomem. */
    if (key_id == KEY__LINE__)
        ret = lily_get_integer_literal(symtab, parser->lex->line_num);
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
        lily_ast_enter_tree(ap, tree_package);
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
            lily_raise(parser->raiser, lily_SyntaxError,
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
                lily_raise(parser->raiser, lily_SyntaxError,
                    "'%s' is not a package.\n", inner_var->name);
            }
            continue;
        }

        break;
    }
}

/*  expression_word
    This is a helper function that handles words in expressions. These are
    sort of complicated. :( */
static void expression_word(lily_parse_state *parser, int *state)
{
    lily_symtab *symtab = parser->symtab;
    lily_lex_state *lex = parser->lex;
    lily_var *var = lily_var_by_name(symtab, lex->label);

    if (var) {
        if (var->function_depth == 1) {
            /* It's in __main__ as a global. */
            if (var->sig->cls->id == SYM_CLASS_PACKAGE)
                expression_package(parser, var);
            else
                lily_ast_push_sym(parser->ast_pool, (lily_sym *)var);
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
            lily_raise(parser->raiser, lily_SyntaxError,
                       "Attempt to use %s, which is not in the current scope.\n",
                       var->name);

        *state = ST_WANT_OPERATOR;
    }
    else {
        int key_id = lily_keyword_by_name(lex->label);
        if (key_id == KEY_ISNIL) {
            lily_ast_enter_tree(parser->ast_pool, tree_isnil);
            NEED_NEXT_TOK(tk_left_parenth)
            *state = ST_WANT_VALUE;
        }
        else if (key_id != -1) {
            lily_literal *lit = parse_special_keyword(parser, key_id);
            lily_ast_push_readonly(parser->ast_pool, (lily_sym *)lit);
            *state = ST_WANT_OPERATOR;
        }
        else {
            lily_class *cls = lily_class_by_name(parser->symtab, lex->label);

            if (cls != NULL) {
                lily_lexer(lex);
                expression_static_call(parser, cls);
                *state = ST_WANT_VALUE;
            }
            else
                lily_raise(parser->raiser, lily_SyntaxError,
                       "%s has not been declared.\n", lex->label);
        }
    }
}

/*  check_valid_close_tok
    This is a helper function that makes sure blocks get the right close token.
    It prevents things like 'abc(1, 2, 3]>' and '[1, 2, 3)'. */
static void check_valid_close_tok(lily_parse_state *parser)
{
    lily_token token = parser->lex->token;
    lily_ast *ast = lily_ast_get_saved_tree(parser->ast_pool);
    lily_tree_type tt = ast->tree_type;
    lily_token expect;

    if (tt == tree_call || tt == tree_parenth || tt == tree_typecast ||
        tt == tree_isnil)
        expect = tk_right_parenth;
    else if (tt == tree_tuple)
        expect = tk_tuple_close;
    else
        expect = tk_right_bracket;

    if (token != expect)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Expected closing token '%s', not '%s'.\n", tokname(expect),
                tokname(token));
}

/*  maybe_digit_fixup
    Sometimes 1+1 should actually be 1 + 1 instead of 1 +1. This will either
    split it into two things or it won't if it can't. */
static void maybe_digit_fixup(lily_parse_state *parser, int *did_fixup)
{
    /* The lexer records where the last digit scan started. So check if it
       started with '+' or '-'. */
    lily_lex_state *lex = parser->lex;
    char ch = lex->input_buffer[lex->last_digit_start];

    if (ch == '-' || ch == '+') {
        int expr_op;
        lily_symtab *symtab = parser->symtab;

        if (ch == '-')
            expr_op = parser_tok_table[tk_minus].expr_op;
        else
            expr_op = parser_tok_table[tk_plus].expr_op;

        lily_ast_push_binary_op(parser->ast_pool, (lily_expr_op)expr_op);
        /* Call this to force a rescan from the proper starting point, yielding
           a proper new token. */
        lily_lexer_digit_rescan(lex);

        lily_literal *lit;
        if (lex->token == tk_integer)
            lit = lily_get_integer_literal(symtab, lex->value.integer);
        else
            lit = lily_get_double_literal(symtab, lex->value.doubleval);

        lily_ast_push_readonly(parser->ast_pool, (lily_sym *)lit);
        *did_fixup = 1;
    }
    else
        *did_fixup = 0;
}

/*  expression_literal
    This handles all literals: integer, double, and string. */
static void expression_literal(lily_parse_state *parser, int *state)
{
    lily_literal *lit;
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;
    lily_token token = parser->lex->token;

    if (*state == ST_WANT_OPERATOR && token != tk_double_quote) {
        int did_fixup;
        maybe_digit_fixup(parser, &did_fixup);
        if (did_fixup == 0)
            *state = ST_DONE;
    }
    else {
        if (token == tk_double_quote)
            lit = lily_get_string_literal(symtab, lex->label);
        else if (token == tk_integer)
            lit = lily_get_integer_literal(symtab, lex->value.integer);
        else if (token == tk_double)
            lit = lily_get_double_literal(symtab, lex->value.doubleval);
        else
            lit = NULL;

        lily_ast_push_readonly(parser->ast_pool, (lily_sym *)lit);
        *state = ST_WANT_OPERATOR;
    }
}

/*  expression_comma_arrow
    This handles commas and arrows. The & 0x1 is nothing magical: a proper
    hash will always have pairs of values. The values to the left side of
    the arrow will always be odd, and the right ones will be even. */
static void expression_comma_arrow(lily_parse_state *parser, int *state)
{
    lily_lex_state *lex = parser->lex;

    if (parser->ast_pool->active == NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                    "Expected a value, not ','.\n");

    lily_ast *last_tree = lily_ast_get_saved_tree(parser->ast_pool);
    if (lex->token == tk_comma) {
        if (last_tree->tree_type == tree_hash &&
            (last_tree->args_collected & 0x1) == 0)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Expected a key => value pair before ','.\n");
    }
    else if (lex->token == tk_arrow) {
        if (last_tree->tree_type == tree_list) {
            if (last_tree->args_collected == 0)
                last_tree->tree_type = tree_hash;
            else
                lily_raise(parser->raiser, lily_SyntaxError,
                        "Unexpected token '%s'.\n", tokname(tk_arrow));
        }
        else if (last_tree->tree_type != tree_hash ||
                 (last_tree->args_collected & 0x1) == 1)
                lily_raise(parser->raiser, lily_SyntaxError,
                        "Unexpected token '%s'.\n", tokname(tk_arrow));
    }

    lily_ast_collect_arg(parser->ast_pool);
    *state = ST_DEMAND_VALUE;
}

static void expression_unary(lily_parse_state *parser, int *state)
{
    if (*state == ST_WANT_OPERATOR)
        *state = ST_BAD_TOKEN;
    else {
        lily_token token = parser->lex->token;
        if (token == tk_minus)
            lily_ast_push_unary_op(parser->ast_pool, expr_unary_minus);
        else if (token == tk_not)
            lily_ast_push_unary_op(parser->ast_pool, expr_unary_not);

        *state = ST_DEMAND_VALUE;
    }
}

/*  expression_dot
    This handles "oo-style" calls: `abc.xyz()`.
    It also handles typecasts: `abc.@(type)`. */
static void expression_dot(lily_parse_state *parser, int *state)
{
    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);
    if (lex->token == tk_word) {
        /* Create a magic oo call tree. The lookup gets deferred until
           emit-time when the type is known. */
        lily_ast_push_oo_call(parser->ast_pool, parser->lex->label);
        lily_ast_enter_tree(parser->ast_pool, tree_call);
        NEED_NEXT_TOK(tk_left_parenth);
        *state = ST_WANT_VALUE;
    }
    else if (lex->token == tk_typecast_parenth) {
        lily_lexer(lex);
        lily_sig *new_sig = collect_var_sig(parser, NULL, 0);
        lily_ast_enter_typecast(parser->ast_pool, new_sig);
        lily_ast_leave_tree(parser->ast_pool);
        *state = ST_WANT_OPERATOR;
    }
}

/*  expression_raw
    BEHOLD! This is the magic function that handles expressions. The expression
    state is viewed as being in one of a handful of states. The ast pool takes
    care of knowing how deep a current expression is.

    It is recommended that expression be used instead of this, unless the
    caller -really- needs to have a starting state that requires a word (yes,
    this does happen). */
static void expression_raw(lily_parse_state *parser, int state)
{
    lily_lex_state *lex = parser->lex;
    while (1) {
        int expr_op = parser_tok_table[lex->token].expr_op;
        if (lex->token == tk_word) {
            if (state == ST_WANT_OPERATOR)
                state = ST_DONE;
            else
                expression_word(parser, &state);
        }
        else if (expr_op != -1) {
            if (state == ST_WANT_OPERATOR) {
                lily_ast_push_binary_op(parser->ast_pool, (lily_expr_op)expr_op);
                state = ST_DEMAND_VALUE;
            }
            else if (lex->token == tk_minus)
                expression_unary(parser, &state);
            else
                state = ST_BAD_TOKEN;
        }
        else if (lex->token == tk_left_parenth) {
            if (state == ST_WANT_VALUE || state == ST_DEMAND_VALUE) {
                lily_ast_enter_tree(parser->ast_pool, tree_parenth);
                state = ST_DEMAND_VALUE;
            }
            else if (state == ST_WANT_OPERATOR) {
                lily_ast_enter_tree(parser->ast_pool, tree_call);
                state = ST_WANT_VALUE;
            }
        }
        else if (lex->token == tk_left_bracket) {
            if (state == ST_WANT_VALUE || state == ST_DEMAND_VALUE) {
                lily_ast_enter_tree(parser->ast_pool, tree_list);
                state = ST_WANT_VALUE;
            }
            else if (state == ST_WANT_OPERATOR) {
                lily_ast_enter_tree(parser->ast_pool, tree_subscript);
                state = ST_DEMAND_VALUE;
            }
        }
        else if (lex->token == tk_tuple_open) {
            if (state == ST_WANT_OPERATOR)
                state = ST_DONE;
            else {
                lily_ast_enter_tree(parser->ast_pool, tree_tuple);
                state = ST_WANT_VALUE;
            }
        }
        else if (lex->token == tk_right_parenth ||
                 lex->token == tk_right_bracket ||
                 lex->token == tk_tuple_close) {
            if (state == ST_DEMAND_VALUE ||
                parser->ast_pool->save_depth == 0) {
                state = ST_BAD_TOKEN;
            }
            else {
                check_valid_close_tok(parser);
                lily_ast_leave_tree(parser->ast_pool);
                state = ST_WANT_OPERATOR;
            }
        }
        else if (lex->token == tk_integer || lex->token == tk_double ||
                 lex->token == tk_double_quote)
            expression_literal(parser, &state);
        else if (lex->token == tk_dot)
            expression_dot(parser, &state);
        else if (lex->token == tk_minus || lex->token == tk_not)
            expression_unary(parser, &state);
        else if (parser_tok_table[lex->token].val_or_end &&
                 parser->ast_pool->save_depth == 0 &&
                 state == ST_WANT_OPERATOR)
            state = ST_DONE;
        else if (lex->token == tk_comma || lex->token == tk_arrow)
            expression_comma_arrow(parser, &state);
        else
            state = ST_BAD_TOKEN;

        if (state == ST_DONE)
            break;
        else if (state == ST_BAD_TOKEN)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Unexpected token '%s'.\n", tokname(lex->token));
        else
            lily_lexer(lex);
    }
}

/*  expression
    This calls expression_raw with a starting state of ST_DEMAND_VALUE. 99%
    of the time, that's what's needed.

    Calling this function is preferred, as there's no weird 'ST_DEMAND_VALUE'
    showing up everywhere. */
static void expression(lily_parse_state *parser)
{
    expression_raw(parser, ST_DEMAND_VALUE);
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

        /* Handle an initializing assignment, if there is one. */
        if (lex->token == tk_equal) {
            /* This makes a difference: The emitter cannot do opcode optimizing
               for global reads/writes. */
            if (parser->emit->function_depth == 1)
                lily_ast_push_sym(parser->ast_pool, (lily_sym *)var);
            else
                lily_ast_push_local_var(parser->ast_pool, var);

            lily_ast_push_binary_op(parser->ast_pool, expr_assign);
            lily_lexer(lex);
            expression(parser);
            lily_emit_eval_expr(parser->emit, parser->ast_pool);
        }

        /* This is the start of the next statement. */
        if (lex->token == tk_word || lex->token == tk_end_tag ||
            lex->token == tk_eof || lex->token == tk_right_curly)
            break;
        else if (lex->token != tk_comma) {
            lily_raise(parser->raiser, lily_SyntaxError,
                       "Expected ',' or ')', not %s.\n", tokname(lex->token));
        }
        /* else it's a comma, so make sure a word is next. */

        NEED_NEXT_TOK(tk_word)
    }
}

static lily_var *parse_for_range_value(lily_parse_state *parser, char *name)
{
    lily_ast_pool *ap = parser->ast_pool;
    expression(parser);

    /* Don't allow assigning expressions, since that just looks weird.
       ex: for i in a += 10..5
       Also, it makes no real sense to do that. */
    if (ap->root->tree_type == tree_binary &&
        ap->root->op >= expr_assign) {
        lily_raise(parser->raiser, lily_SyntaxError,
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

/*****************************************************************************/
/* Statement handling                                                        */
/*****************************************************************************/

/* Every keyword has an associated handler, even if it's something rather
   simple. */
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
static void try_handler(lily_parse_state *, int);
static void except_handler(lily_parse_state *, int);
static void raise_handler(lily_parse_state *, int);

typedef void (keyword_handler)(lily_parse_state *, int);

/* This is setup so that handlers[key_id] is the handler for that keyword. */
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
    isnil_handler,
    try_handler,
    except_handler,
    raise_handler
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
        lily_token token = lex->token;

        if (token == tk_word) {
            key_id = lily_keyword_by_name(lex->label);
            if (key_id != -1) {
                /* Ask the handler for this keyword what to do. */
                lily_lexer(lex);
                handlers[key_id](parser, multi);
            }
            else {
                lclass = lily_class_by_name(parser->symtab, lex->label);

                if (lclass != NULL) {
                    lily_lexer(lex);
                    if (lex->token == tk_colon_colon) {
                        expression_static_call(parser, lclass);
                        expression(parser);
                        lily_emit_eval_expr(parser->emit, parser->ast_pool);
                    }
                    else {
                        int cls_id = lclass->id;

                        if (cls_id == SYM_CLASS_FUNCTION) {
                            /* This will enter the function since the function is
                               toplevel. */
                            collect_var_sig(parser, lclass,
                                    CV_TOPLEVEL | CV_MAKE_VARS);
                            NEED_CURRENT_TOK(tk_left_curly)
                            lily_lexer(lex);
                        }
                        else {
                            lily_sig *cls_sig = collect_var_sig(parser, lclass, 0);
                            parse_decl(parser, cls_sig);
                        }
                    }
                }
                else {
                    expression(parser);
                    lily_emit_eval_expr(parser->emit, parser->ast_pool);
                }
            }
        }
        else if (token == tk_integer || token == tk_double ||
                 token == tk_double_quote || token == tk_left_parenth ||
                 token == tk_left_bracket || token == tk_tuple_open) {
            expression(parser);
            lily_emit_eval_expr(parser->emit, parser->ast_pool);
        }
        /* The caller will be expecting '}' or maybe @> / EOF if it's the main
           parse loop. */
        else if (multi)
            break;
        /* Single-line expressions need a value to prevent things like
           'if 1: }' and 'if 1: @>'. */
        else
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Expected a value, not '%s'.\n", tokname(token));
    } while (multi);
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
        lily_raise(parser->raiser, lily_SyntaxError,
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
    expression(parser);
    lily_emit_eval_condition(parser->emit, parser->ast_pool);
    NEED_CURRENT_TOK(tk_colon)

    lily_lexer(lex);
    if (lex->token == tk_left_curly)
        parse_multiline_block_body(parser, multi);
    else {
        statement(parser, 0);
        while (lex->token == tk_word) {
            int key_id = lily_keyword_by_name(lex->label);

            /* Jump directly into elif/else. Doing it this way (instead of
               through statement) means that the 'if' block can be popped in a
               single place. */
            if (key_id == KEY_ELIF || key_id == KEY_ELSE) {
                lily_lexer(parser->lex);
                handlers[key_id](parser, 0);
            }
            else
                break;
        }
    }

    lily_emit_leave_block(parser->emit);
}

/*  elif_handler
    This handles elif. Both elif and else don't call the block because they're
    always called somehow through if_handler calling statement.
    The multi-line-ness has already been determined by the if block. */
static void elif_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;
    lily_emit_change_block_to(parser->emit, BLOCK_IF_ELIF);
    expression(parser);
    lily_emit_eval_condition(parser->emit, parser->ast_pool);

    NEED_CURRENT_TOK(tk_colon)

    lily_lexer(lex);
    statement(parser, multi);
}

/*  else_handler
    This handles the else keyword. Doesn't get much easier. */
static void else_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_change_block_to(parser->emit, BLOCK_IF_ELSE);
    NEED_CURRENT_TOK(tk_colon)
    lily_lexer(lex);

    statement(parser, multi);
}

/*  return_handler
    This handles the return keyword. It'll look up the current function to see
    if an expression is needed, or if just 'return' alone is fine. */
static void return_handler(lily_parse_state *parser, int multi)
{
    lily_sig *ret_sig = parser->emit->top_function_ret;
    lily_ast *ast;

    if (ret_sig != NULL) {
        expression(parser);
        ast = parser->ast_pool->root;
    }
    else
        ast = NULL;

    lily_emit_return(parser->emit, ast);
    if (ast)
        lily_ast_reset_pool(parser->ast_pool);
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

    expression(parser);
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
    expression(parser);
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

    expression_raw(parser, ST_WANT_OPERATOR);
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
        lily_raise(parser->raiser, lily_SyntaxError,
                   "Loop var must be type integer, not type '%T'.\n",
                   loop_var->sig);
    }

    NEED_NEXT_TOK(tk_word)
    if (strcmp(lex->label, "in") != 0)
        lily_raise(parser->raiser, lily_SyntaxError, "Expected 'in', not '%s'.\n",
                   lex->label);

    lily_lexer(lex);

    lily_var *for_start, *for_end, *for_step;

    for_start = parse_for_range_value(parser, "(for start)");

    NEED_CURRENT_TOK(tk_two_dots)
    lily_lexer(lex);

    for_end = parse_for_range_value(parser, "(for end)");

    if (lex->token == tk_word) {
        if (strcmp(lex->label, "by") != 0)
            lily_raise(parser->raiser, lily_SyntaxError,
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
        lily_raise(parser->raiser, lily_SyntaxError,
                   "Expected 'while', not '%s'.\n", lex->label);

    /* Now prep the token for expression. Save the resulting tree so that
       it can be eval'd specially. */
    lily_lexer(lex);
    expression(parser);
    lily_emit_eval_condition(parser->emit, parser->ast_pool);
    lily_emit_leave_block(parser->emit);
}

static void isnil_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;
    lily_ast_enter_tree(parser->ast_pool, tree_isnil);
    NEED_CURRENT_TOK(tk_left_parenth)
    lily_lexer(lex);
    expression(parser);
    lily_emit_eval_expr(parser->emit, parser->ast_pool);
}

static void except_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    NEED_CURRENT_TOK(tk_word)
    lily_class *exception_class = lily_class_by_name(parser->symtab, lex->label);
    if (exception_class == NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'%s' is not a class.\n", lex->label);

    /* Exception is likely to always be the base exception class. */
    lily_class *exception_base = lily_class_by_name(parser->symtab,
            "Exception");

    int is_valid = lily_check_right_inherits_or_is(exception_base,
            exception_class);
    if (is_valid == 0)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'%s' is not a valid exception class.\n");

    lily_var *exception_var = NULL;

    lily_lexer(lex);
    if (lex->token == tk_word) {
        if (strcmp(parser->lex->label, "as") != 0)
            lily_raise(parser->raiser, lily_SyntaxError,
                "Expected 'as', not '%s'.\n", lex->label);

        NEED_NEXT_TOK(tk_word)
        exception_var = lily_var_by_name(parser->symtab, lex->label);
        if (exception_var != NULL)
            lily_raise(parser->raiser, lily_SyntaxError,
                "%s has already been declared.\n", exception_var->name);

        exception_var = lily_try_new_var(parser->symtab, exception_class->sig,
                lex->label, 0);

        lily_lexer(lex);
    }

    NEED_CURRENT_TOK(tk_colon)
    lily_emit_change_block_to(parser->emit, BLOCK_TRY_EXCEPT);
    lily_emit_except(parser->emit, exception_class, exception_var,
            lex->line_num);

    lily_lexer(lex);
    if (lex->token != tk_right_curly)
        statement(parser, 1);
}

static void try_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, BLOCK_TRY);
    lily_emit_try(parser->emit, parser->lex->line_num);

    NEED_CURRENT_TOK(tk_colon)
    lily_lexer(lex);
    if (lex->token == tk_left_curly)
        parse_multiline_block_body(parser, multi);
    else {
        statement(parser, 0);
        while (lex->token == tk_word) {
            if (strcmp("except", lex->label) == 0) {
                lily_lexer(parser->lex);
                except_handler(parser, multi);
            }
            else
                break;
        }
    }

    /* The vm expects that the last except block will have a 'next' of 0 to
       indicate the end of the 'except' chain. Remove the patch that the last
       except block installed so it doesn't get patched. */
    parser->emit->patch_pos--;

    lily_emit_leave_block(parser->emit);
}

static void raise_handler(lily_parse_state *parser, int multi)
{
    expression(parser);
    lily_emit_raise(parser->emit, parser->ast_pool->root);
    lily_ast_reset_pool(parser->ast_pool);
}

/*  parser_loop
    This is the main parsing function. This is called by a lily_parse_*
    function which will set the raiser and give the lexer a file before calling
    this function. */
static void parser_loop(lily_parse_state *parser)
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
            if (parser->emit->current_block->prev != NULL) {
                lily_raise(parser->raiser, lily_SyntaxError,
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
                 lex->token == tk_left_bracket ||
                 lex->token == tk_tuple_open) {
            expression(parser);
            lily_emit_eval_expr(parser->emit, parser->ast_pool);
        }
        else
            lily_raise(parser->raiser, lily_SyntaxError, "Unexpected token %s.\n",
                       tokname(lex->token));
    }
}

/*****************************************************************************/
/* Exported API                                                              */
/*****************************************************************************/

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
            parser_loop(parser);

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
        parser_loop(parser);
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
        parser_loop(parser);
        return 1;
    }

    return 0;
}
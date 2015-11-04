#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lily_alloc.h"
#include "lily_config.h"
#include "lily_library.h"
#include "lily_parser.h"
#include "lily_parser_tok_table.h"
#include "lily_keyword_table.h"
#include "lily_pkg_sys.h"
#include "lily_value.h"
#include "lily_membuf.h"
#include "lily_seed.h"

#include "lily_cls_function.h"

/** Parser is responsible for:
    * Creating all other major structures (ast pool, emitter, lexer, etc.)
    * Ensuring that all other major structures are deleted.
    * Holding the startup functions (lily_parse_file and others).
    * Processing expressions and ensuring they have the proper form. Actual
      type-checking is done within the emitter.
    * Handling importing (what links to what, making new ones, etc.)
    * Dynamic load of class methods.

    Most functions here will expect to have the current token when they start,
    and call up the next token at exit. This allows the parser to do some
    lookahead without having to save tokens.
**/

#define NEED_NEXT_TOK(expected) \
lily_lexer(lex); \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_SyntaxError, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token));

#define NEED_CURRENT_TOK(expected) \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_SyntaxError, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token));

/* Note: At some point in the future, both Exception and Tainted should both be
   entirely dynaloaded (to save memory). For now, however, they're both
   bootstrapped into the interpreter by force.
   Sorry. */
static char *bootstrap =
"class Exception(message: string) {\n"
"    var @message = message\n"
"    var @traceback: list[string] = []\n"
"}\n"
"class Tainted[A](value: A) {\n"
"    private var @value = value\n"
"    define sanitize[A, B](f: function(A => B)):B {\n"
"         return f(@value)\n"
"    }\n"
"}\n";

static void statement(lily_parse_state *, int);
static lily_import_entry *make_new_import_entry(lily_parse_state *,
        const char *, char *);
static void link_import_to(lily_import_entry *, lily_import_entry *, const char *);
static void create_new_class(lily_parse_state *);
static lily_type *type_by_name(lily_parse_state *, char *);
static lily_class *resolve_class_name(lily_parse_state *);
static lily_type *get_type(lily_parse_state *);
static void collect_optarg_for(lily_parse_state *, lily_var *);

/*****************************************************************************/
/* Parser creation and teardown                                              */
/*****************************************************************************/

/* Create a new path link which has the 'initial' link as the next. The path of
   the new length is the given path_str, which has 'length' chars copied for the
   path. */
static lily_path_link *add_path_slice_to(lily_parse_state *parser,
        lily_path_link *initial, const char *path_str, unsigned int length)
{
    lily_path_link *new_link = lily_malloc(sizeof(lily_path_link));
    char *buffer = lily_malloc(length + 1);
    strncpy(buffer, path_str, length);
    buffer[length] = '\0';

    new_link->path = buffer;
    new_link->next = initial;

    return new_link;
}

/* This prepares a proper lily_path_link by reading a ';' delimited seed in. */
static lily_path_link *prepare_path_by_seed(lily_parse_state *parser,
        const char *seed)
{
    const char *path_base = seed;
    lily_path_link *result = NULL;
    while (1) {
        const char *search = strchr(path_base, ';');
        if (search == NULL)
            break;

        unsigned int diff = search - path_base;
        result = add_path_slice_to(parser, result, path_base, diff);
        path_base = search + 1;
    }

    return result;
}

/*  This will load the Exception and Tainted classes into the current import
    (which is the builtin one). */
static void run_bootstrap(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_load_str(lex, "[builtin]", lm_no_tags, bootstrap);
    lily_lexer(lex);
    statement(parser, 1);
    lily_pop_lex_entry(lex);
}

/* This function creates a new options type with the interpreter's
   default values set. */
lily_options *lily_new_default_options(void)
{
    lily_options *options = lily_malloc(sizeof(lily_options));
    options->version = 1;
    options->gc_threshold = 100; /* Totally arbitrary. */
    options->argc = 0;
    options->argv = NULL;
    options->data = NULL;

    return options;
}

lily_parse_state *lily_new_parse_state(lily_options *options)
{
    lily_parse_state *parser = lily_malloc(sizeof(lily_parse_state));
    parser->data = options->data;
    parser->import_top = NULL;
    parser->import_start = NULL;

    lily_import_entry *builtin_import = make_new_import_entry(parser, "",
            "[builtin]");
    lily_raiser *raiser = lily_new_raiser();

    parser->optarg_stack_pos = 0;
    parser->optarg_stack_size = 4;
    parser->first_pass = 1;
    parser->class_self_type = NULL;
    parser->raiser = raiser;
    parser->optarg_stack = lily_malloc(4 * sizeof(uint16_t));
    parser->ast_pool = lily_new_ast_pool();
    parser->symtab = lily_new_symtab(builtin_import);
    parser->emit = lily_new_emit_state(parser->symtab, raiser);
    parser->lex = lily_new_lex_state(options, raiser);
    parser->vm = lily_new_vm_state(options, raiser);
    parser->msgbuf = lily_new_msgbuf();
    parser->options = options;

    parser->tm = parser->emit->tm;

    parser->vm->symtab = parser->symtab;
    parser->vm->ts = parser->emit->ts;
    parser->vm->vm_buffer = parser->raiser->msgbuf;
    parser->vm->parser = parser;

    parser->symtab->lex_linenum = &parser->lex->line_num;

    parser->ast_pool->lex_linenum = &parser->lex->line_num;

    parser->emit->lex_linenum = &parser->lex->line_num;
    parser->emit->symtab = parser->symtab;
    parser->emit->ast_membuf = parser->ast_pool->ast_membuf;
    parser->emit->parser = parser;

    parser->lex->symtab = parser->symtab;
    parser->lex->membuf = parser->ast_pool->ast_membuf;

    parser->import_paths = prepare_path_by_seed(parser, LILY_PATH_SEED);
    parser->library_import_paths = prepare_path_by_seed(parser,
            LILY_LIBRARY_PATH_SEED);

    lily_emit_enter_main(parser->emit);

    parser->vm->main = parser->symtab->main_var;

    /* When declaring a new function, initially give it the same type as
       __main__. This ensures that, should building the proper type fail, the
       symtab will still see the function as a function and destroy the
       contents. */
    parser->default_call_type = parser->vm->main->type;

    /* This creates a new var, so it has to be done after symtab's lex_linenum
       is set. */
    lily_pkg_sys_init(parser, options);

    run_bootstrap(parser);

    /* The vm uses this for tagging the inner values when building traceback. */
    parser->vm->traceback_type = type_by_name(parser, "list[string]");

    parser->executing = 0;

    return parser;
}

static void free_paths(lily_parse_state *parser, lily_path_link *path_iter)
{
    lily_path_link *path_next;
    while (path_iter) {
        path_next = path_iter->next;
        lily_free(path_iter->path);
        lily_free(path_iter);
        path_iter = path_next;
    }
}

void lily_free_parse_state(lily_parse_state *parser)
{
    lily_free_raiser(parser->raiser);

    lily_free_ast_pool(parser->ast_pool);

    lily_free_vm(parser->vm);

    lily_free_symtab(parser->symtab);

    /* Order doesn't matter for the rest of this. */

    lily_free_lex_state(parser->lex);

    lily_free_emit_state(parser->emit);

    free_paths(parser, parser->import_paths);
    free_paths(parser, parser->library_import_paths);

    lily_free(parser->optarg_stack);

    lily_import_entry *import_iter = parser->import_start;
    lily_import_entry *import_next = NULL;
    while (import_iter) {
        import_next = import_iter->root_next;
        lily_import_link *link_iter = import_iter->import_chain;
        lily_import_link *link_next = NULL;
        while (link_iter) {
            link_next = link_iter->next_import;
            lily_free(link_iter->as_name);
            lily_free(link_iter);
            link_iter = link_next;
        }
        if (import_iter->library)
            lily_library_free(import_iter->library);

        lily_free(import_iter->path);
        lily_free(import_iter->loadname);
        lily_free(import_iter);

        import_iter = import_next;
    }

    lily_free_msgbuf(parser->msgbuf);
    lily_free_type_maker(parser->tm);
    lily_free(parser);
}

/*****************************************************************************/
/* Shared code                                                               */
/*****************************************************************************/

static lily_import_entry *make_new_import_entry(lily_parse_state *parser,
        const char *loadname, char *path)
{
    lily_import_entry *new_entry = lily_malloc(sizeof(lily_import_entry));
    if (parser->import_top) {
        parser->import_top->root_next = new_entry;
        parser->import_top = new_entry;
    }
    else {
        parser->import_start = new_entry;
        parser->import_top = new_entry;
    }

    new_entry->loadname = lily_malloc(strlen(loadname) + 1);
    strcpy(new_entry->loadname, loadname);

    new_entry->path = lily_malloc(strlen(path) + 1);
    strcpy(new_entry->path, path);

    new_entry->library = NULL;
    new_entry->root_next = NULL;
    new_entry->import_chain = NULL;
    new_entry->class_chain = NULL;
    new_entry->var_chain = NULL;
    new_entry->dynaload_table = NULL;
    new_entry->var_load_fn = NULL;
    new_entry->flags = ITEM_TYPE_IMPORT;

    return new_entry;
}

static void link_import_to(lily_import_entry *target,
        lily_import_entry *to_link, const char *as_name)
{
    lily_import_link *new_link = lily_malloc(sizeof(lily_import_link));
    char *link_name;
    if (as_name == NULL)
        link_name = NULL;
    else {
        link_name = lily_malloc(strlen(as_name) + 1);
        strcpy(link_name, as_name);
    }

    new_link->entry = to_link;
    new_link->next_import = target->import_chain;
    new_link->as_name = link_name;

    target->import_chain = new_link;
}

/*  fixup_import_basedir
    This function is called to set the first import path to the path used by the
    first file parsed (assuming a file parse mode. String parsing does not go
    through this). This makes it so that imports will look first at the path of
    the file imported, THEN relative to where the interpreter is.

    Ex: ./lily test/pass/import/test_deep_access.lly

    In such a case, test/pass/import/ is searched before '.' is searched. */
static void fixup_import_basedir(lily_parse_state *parser, char *path)
{
    char *search_str = strrchr(path, '/');
    if (search_str == NULL)
        return;

    int length = (search_str - path) + 1;
    parser->import_paths = add_path_slice_to(parser, parser->import_paths, path,
        length);
}

/*  make_type_of_class
    This takes a class that takes a single subtype and creates a new type which
    wraps around the class sent. For example, send 'list' and an integer type to
    get 'list[integer]'. */
static lily_type *make_type_of_class(lily_parse_state *parser, lily_class *cls,
        lily_type *type)
{
    lily_tm_add(parser->tm, type);
    return lily_tm_make(parser->tm, 0, cls, 1);
}

static void ensure_valid_type(lily_parse_state *parser, lily_type *type)
{
    if (type->subtype_count != type->cls->generic_count &&
        type->cls->generic_count != -1)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Class %s expects %d type(s), but got %d type(s).\n",
                type->cls->name, type->cls->generic_count,
                type->subtype_count);

    /* Unfortunately, Lily does not (yet!) understand constraints, and the
       hashing function only works on certain builtin types. Because of these
       restrictions, make sure that hashes do not specify a key which is
       definitely going to be not-hashable.
       This isn't perfect, because it allows generics which may be replaced by
       something that is or isn't hashable.
       Sorry. :( */
    if (type->cls == parser->symtab->hash_class) {
        lily_type *check_type = type->subtypes[0];
        if ((check_type->cls->flags & CLS_VALID_HASH_KEY) == 0 &&
            check_type->cls->id != SYM_CLASS_GENERIC)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "'^T' is not a valid hash key.\n", check_type);
    }
}

/** The two argument collectors (get_named_arg and get_nameless_arg) both use
    the same set of flags. However, they do not have custom flags. The only
    flags they're interested in are TYPE_HAS_OPTARGS and TYPE_IS_VARARGS. Using
    those instead of custom flags allows the flags (after arg processing) to be
    directly passed to lily_tm_make. It's a small speedup, but in a very used
    area. **/

static lily_type *get_nameless_arg(lily_parse_state *parser, int *flags)
{
    lily_lex_state *lex = parser->lex;

    if (lex->token == tk_multiply) {
        *flags |= TYPE_HAS_OPTARGS;
        lily_lexer(lex);
    }
    else if (*flags & TYPE_HAS_OPTARGS)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Non-optional argument follows optional argument.\n");

    lily_type *type = get_type(parser);

    /* get_type ends with a call to lily_lexer, so don't call that again. */

    if (*flags & TYPE_HAS_OPTARGS) {
        if ((type->cls->flags & CLS_VALID_OPTARG) == 0)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Type '^T' cannot have a default value.\n", type);

        type = make_type_of_class(parser, parser->symtab->optarg_class, type);
    }
    else if (lex->token == tk_three_dots) {
        type = make_type_of_class(parser, parser->symtab->list_class, type);

        lily_lexer(lex);
        /* Varargs can't be optional, and they have to be at the end. So the
           next thing should be either ')' to close, or an '=>' to designate
           the return type. */
        if (lex->token != tk_arrow && lex->token != tk_right_parenth)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Expected either '=>' or ')' after varargs.\n");

        *flags |= TYPE_IS_VARARGS;
    }

    return type;
}

static lily_type *get_named_arg(lily_parse_state *parser, int *flags)
{
    lily_lex_state *lex = parser->lex;

    /* The caller is responsible for ensuring that this starts with the token
       as tk_label (and thus having a valid name). */
    lily_var *var;

    var = lily_find_var(parser->symtab, NULL, lex->label);
    if (var != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                   "%s has already been declared.\n", lex->label);

    var = lily_emit_new_scoped_var(parser->emit, NULL, lex->label);
    NEED_NEXT_TOK(tk_colon)

    lily_lexer(lex);
    lily_type *type = get_nameless_arg(parser, flags);

    if (*flags & TYPE_HAS_OPTARGS) {
        /* Optional arguments are created by making an optarg type which holds
           a type that is supposed to be optional. For simplicity, give the
           var the concrete underlying type, and the caller the true optarg
           containing type. */
        var->type = type->subtypes[0];
        collect_optarg_for(parser, var);
    }
    else
        var->type = type;

    return type;
}

static lily_type *get_type(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_type *result;

    lily_class *cls = resolve_class_name(parser);

    if (cls->flags & CLS_IS_VARIANT)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Variant types not allowed in a declaration.\n");

    if (cls->generic_count == 0)
        result = cls->type;
    else if (cls->id != SYM_CLASS_FUNCTION) {
        NEED_NEXT_TOK(tk_left_bracket)
        int i = 0;
        while (1) {
            lily_lexer(lex);
            lily_tm_add(parser->tm, get_type(parser));
            i++;

            if (lex->token == tk_comma)
                continue;
            else if (lex->token == tk_right_bracket)
                break;
            else
                lily_raise(parser->raiser, lily_SyntaxError,
                        "Expected either ',' or ']', not '%s'.\n",
                        tokname(lex->token));
        }

        result = lily_tm_make(parser->tm, 0, cls, i);
        ensure_valid_type(parser, result);
    }
    else if (cls->id == SYM_CLASS_FUNCTION) {
        NEED_NEXT_TOK(tk_left_parenth)
        lily_lexer(lex);
        int arg_flags = 0;
        int i = 0;
        int result_pos = parser->tm->pos;

        lily_tm_add(parser->tm, NULL);

        if (lex->token != tk_arrow && lex->token != tk_right_parenth) {
            while (1) {
                lily_tm_add(parser->tm, get_nameless_arg(parser, &arg_flags));
                i++;
                if (lex->token == tk_comma) {
                    lily_lexer(lex);
                    continue;
                }

                break;
            }
        }

        if (lex->token == tk_arrow) {
            lily_lexer(lex);
            lily_tm_insert(parser->tm, result_pos, get_type(parser));
        }

        NEED_CURRENT_TOK(tk_right_parenth)

        result = lily_tm_make(parser->tm, arg_flags, cls, i + 1);
    }
    else
        result = NULL;

    lily_lexer(lex);
    return result;
}

static int collect_generics(lily_parse_state *parser)
{
    char name[] = "A";
    char ch = name[0];
    lily_lex_state *lex = parser->lex;

    while (1) {
        NEED_NEXT_TOK(tk_word)
        if (lex->label[0] != ch || lex->label[1] != '\0') {
            name[0] = ch;
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Invalid generic name (wanted %s, got %s).\n",
                    name, lex->label);
        }

        ch++;
        lily_lexer(lex);
        if (lex->token == tk_right_bracket) {
            lily_lexer(lex);
            break;
        }
        else if (lex->token != tk_comma)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Expected either ',' or ']', not '%s'.\n",
                    tokname(lex->token));
    }

    int seen = ch - 'A';

    lily_ts_generics_seen(parser->emit->ts, seen);
    return seen;
}

static lily_base_seed *find_dynaload_entry(lily_item *item, char *name)
{
    const void *raw_iter;
    if (item->flags & ITEM_TYPE_IMPORT)
        raw_iter = ((lily_import_entry *)item)->dynaload_table;
    else
        raw_iter = ((lily_class *)item)->dynaload_table;

    while (raw_iter) {
        lily_base_seed *base_seed = (lily_base_seed *)raw_iter;
        if (strcmp(base_seed->name, name) == 0)
            break;

        raw_iter = base_seed->next;
    }

    return raw_iter;
}

static lily_var *dynaload_function(lily_parse_state *parser, lily_item *source,
        lily_base_seed *seed)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;
    lily_func_seed *func_seed = (lily_func_seed *)seed;
    lily_import_entry *save_active = parser->symtab->active_import;
    int save_generics = parser->emit->block->generic_count;
    int generics_used;
    lily_var *call_var;

    lily_load_str(lex, "[builtin]", lm_no_tags, func_seed->func_definition);
    lily_lexer(lex);

    /* Lookups for classes need to be done relative to what's being imported,
       because the thing being imported won't know it's name at compile time. */
    if (source->flags & ITEM_TYPE_IMPORT)
        parser->symtab->active_import = (lily_import_entry *)source;
    else
        parser->symtab->active_import = ((lily_class *)source)->import;

    if (parser->lex->token == tk_left_bracket)
        generics_used = collect_generics(parser);
    else
        generics_used = 0;

    lily_update_symtab_generics(symtab, NULL, generics_used);
    int result_pos = parser->tm->pos;
    int i = 1;
    int flags = 0;

    /* This means the function doesn't return anything. This is mostly about
       reserving a slot to maybe be overwritten later. */
    lily_tm_add(parser->tm, NULL);

    if (lex->token == tk_left_parenth) {
        lily_lexer(lex);
        while (1) {
            lily_tm_add(parser->tm, get_nameless_arg(parser, &flags));
            i++;
            if (lex->token == tk_comma) {
                lily_lexer(lex);
                continue;
            }
            else if (lex->token == tk_right_parenth) {
                lily_lexer(lex);
                break;
            }
            else
                lily_raise(parser->raiser, lily_SyntaxError,
                        "Expected either ',' or ')', not '%s'.\n",
                        tokname(lex->token));
        }
    }

    if (lex->token == tk_colon) {
        lily_lexer(lex);
        lily_tm_insert(parser->tm, result_pos, get_type(parser));
    }

    lily_type *type = lily_tm_make(parser->tm, flags,
            parser->symtab->function_class, i);

    call_var = lily_emit_new_tied_dyna_var(parser->emit, func_seed->func,
            source, type, func_seed->name);

    lily_update_symtab_generics(symtab, NULL, save_generics);

    parser->symtab->active_import = save_active;

    lily_pop_lex_entry(lex);

    return call_var;
}

lily_class *dynaload_exception(lily_parse_state *parser,
        lily_import_entry *import, const char *name)
{
    lily_symtab *symtab = parser->symtab;
    lily_import_entry *saved_active = symtab->active_import;
    lily_class *result;

    /* This causes lookups and the class insertion to be done into the scope of
       whatever import that wanted this dynaload. This will make it so the
       dynaload lasts, instead of scoping out. */
    symtab->active_import = import;
    lily_msgbuf_flush(parser->msgbuf);
    lily_msgbuf_add_fmt(parser->msgbuf, "class %s(msg: string) < Exception(msg) { }\n", name);
    lily_load_str(parser->lex, "[dynaload]", lm_no_tags, parser->msgbuf->message);
    /* This calls up the first token, which will be 'class'. */
    lily_lexer(parser->lex);
    /* This fixes it to be the class name. */
    lily_lexer(parser->lex);

    lily_ast_pool *ap = parser->ast_pool;
    /* create_new_class will turn control over to statement. Before that
       happens, freeze the ast's state in case this is in the middle of an
       expression. */
    lily_ast_freeze_state(ap);
    create_new_class(parser);
    result = symtab->active_import->class_chain;
    lily_ast_thaw_state(ap);

    lily_pop_lex_entry(parser->lex);
    symtab->active_import = saved_active;

    return result;
}

static lily_import_entry *resolve_import(lily_parse_state *parser)
{
    lily_import_entry *result = NULL, *search_entry = NULL;
    lily_symtab *symtab = parser->symtab;
    lily_lex_state *lex = parser->lex;

    search_entry = lily_find_import(symtab, result, lex->label);
    while (search_entry) {
        result = search_entry;
        NEED_NEXT_TOK(tk_colon_colon)
        NEED_NEXT_TOK(tk_word)
        search_entry = lily_find_import(symtab, result, lex->label);
    }

    return result;
}

/*  shorthash_for_name
    Copied from symtab for keyword_by_name. This gives (up to) the first 8
    bytes of the name as an int for doing fast comparisons. */
static uint64_t shorthash_for_name(const char *name)
{
    const char *ch = &name[0];
    int i, shift;
    uint64_t ret;
    for (i = 0, shift = 0, ret = 0;
         *ch != '\0' && i != 8;
         ch++, i++, shift += 8) {
        ret |= ((uint64_t)*ch) << shift;
    }
    return ret;
}

/*  keyword_by_name
    Do a fast lookup through the keyword table to see if the name given is a
    keyword. Returns -1 if not found, or something higher than that if the name
    is a keyword. */
static int keyword_by_name(char *name)
{
    int i;
    uint64_t shorthash = shorthash_for_name(name);

    for (i = 0;i <= KEY_LAST_ID;i++) {
        if (keywords[i].shorthash == shorthash &&
            strcmp(keywords[i].name, name) == 0)
            return i;
        else if (keywords[i].shorthash > shorthash)
            break;
    }

    return -1;
}

/*  Is this keyword a value that can be part of an expression?*/
static int is_key_a_value(int key_id)
{
    switch(key_id) {
        case KEY_TRUE:
        case KEY_FALSE:
        case KEY__FILE__:
        case KEY__FUNCTION__:
        case KEY__LINE__:
        case KEY_SELF:
            return 1;
        default:
            return 0;
    }
}

static void ensure_unique_method_name(lily_parse_state *parser, char *name)
{
    if (lily_find_var(parser->symtab, NULL, name) != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                   "%s has already been declared.\n", name);

    if (parser->class_self_type) {
        lily_class *current_class = parser->class_self_type->cls;

        if (lily_find_property(current_class, name)) {
            lily_raise(parser->raiser, lily_SyntaxError,
                "A property in class '%s' already has the name '%s'.\n",
                current_class->name, name);
        }
    }
}

/*  get_named_var
    Attempt to create a var with the given type. This will call lexer to
    get the name, as well as ensuring that the given var is unique. */
static lily_var *get_named_var(lily_parse_state *parser, lily_type *var_type)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var;

    var = lily_find_var(parser->symtab, NULL, lex->label);
    if (var != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                   "%s has already been declared.\n", lex->label);

    var = lily_emit_new_scoped_var(parser->emit, var_type, lex->label);

    lily_lexer(lex);
    return var;
}

/*  get_named_property
    The same thing as get_named_var, but with a property instead. */
static lily_prop_entry *get_named_property(lily_parse_state *parser,
        lily_type *prop_type, int flags)
{
    char *name = parser->lex->label;
    lily_class *current_class = parser->class_self_type->cls;

    lily_prop_entry *prop = lily_find_property(current_class, name);

    if (prop != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Property %s already exists in class %s.\n", name,
                current_class->name);

    /* Like with get_named_var, prevent properties from having the same name as
       what will become a class method. This is because they are both accessed
       in the same manner outside the class. */
    lily_var *lookup_var = lily_find_method(current_class, name);

    if (lookup_var)
        lily_raise(parser->raiser, lily_SyntaxError,
                "A method in class '%s' already has the name '%s'.\n",
                current_class->name, name);

    prop = lily_add_class_property(parser->symtab, current_class, prop_type,
            name, flags & ~SYM_NOT_INITIALIZED);

    lily_lexer(parser->lex);
    return prop;
}

/*  bad_decl_token
    This is a function called when var_handler is expecting a var name and gets
    a property name, or vice versa. For either case, give the user a more
    useful error message.
    This is particularly important for classes: A new user may expect that
    class properties don't have an @ starter. This gives a useful error message
    in that case. */
static void bad_decl_token(lily_parse_state *parser)
{
    char *message;

    if (parser->lex->token == tk_word)
        message = "Class properties must start with @.\n";
    else
        message = "Cannot use a class property outside of a constructor.\n";

    lily_raise(parser->raiser, lily_SyntaxError, message);
}

/*  grow_optarg_stack
    Make the optstack holding type information bigger for more values. */
static void grow_optarg_stack(lily_parse_state *parser)
{
    parser->optarg_stack_size *= 2;
    parser->optarg_stack = lily_realloc(parser->optarg_stack,
            sizeof(uint16_t) * parser->optarg_stack_size);
}

/*****************************************************************************/
/* Type collection                                                           */
/*****************************************************************************/

static lily_tie *get_optarg_value(lily_parse_state *parser,
        lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;
    lily_token expect;
    if (cls == symtab->integer_class)
        expect = tk_integer;
    else if (cls == symtab->double_class)
        expect = tk_double;
    else if (cls == symtab->string_class)
        expect = tk_double_quote;
    else if (cls == symtab->bytestring_class)
        expect = tk_bytestring;
    else
        expect = tk_word;

    NEED_NEXT_TOK(expect)

    lily_tie *result;
    if (expect == tk_word) {
        int key_id = keyword_by_name(lex->label);
        if (key_id != KEY_TRUE && key_id != KEY_FALSE)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "'%s' is not a valid default value for a boolean.\n",
                    lex->label);

        result = lily_get_boolean_literal(symtab, key_id == KEY_TRUE);
    }
    else
        result = lex->last_literal;

    return result;
}

/*  collect_optarg_for
    This collects a default value for 'var', which has been tagged as needing
    one. This first makes sure that the var is a type that actually can do that.
    If so, it will grab the '=<value>' and register the value into the optarg
    stack. */
static void collect_optarg_for(lily_parse_state *parser, lily_var *var)
{
    lily_lex_state *lex = parser->lex;
    NEED_CURRENT_TOK(tk_equal)

    if (parser->optarg_stack_pos + 1 >= parser->optarg_stack_size)
        grow_optarg_stack(parser);

    lily_tie *lit = get_optarg_value(parser, var->type->cls);

    parser->optarg_stack[parser->optarg_stack_pos] = lit->reg_spot;
    parser->optarg_stack[parser->optarg_stack_pos + 1] = var->reg_spot;
    parser->optarg_stack_pos += 2;

    lily_lexer(lex);
}

/* This is called after processing the generics for either a normal class or an
   enum class. It will build a type that describes the class with those
   generics.
   For example, `class Test[A, B] ...` will create the type `Test[A, B]`. This
   is needed because this type is the 'self' type that will be injected as the
   first argument to class methods. It's also the type of the keyword `self`'s
   value. */
static lily_type *build_self_type(lily_parse_state *parser, lily_class *cls,
        int generics_used)
{
    lily_type *result;
    if (generics_used) {
        char name[] = {'A', '\0'};
        while (generics_used) {
            lily_class *lookup_cls = lily_find_class(parser->symtab, NULL, name);
            lily_tm_add(parser->tm, lookup_cls->type);
            name[0]++;
            generics_used--;
        }

        result = lily_tm_make(parser->tm, 0, cls, (name[0] - 'A'));
    }
    else
        result = lily_tm_make_default_for(parser->tm, cls);

    return result;
}

/* This is used by collect_var_type to collect a class when there may be one or
   more package entries before the type. This allows using package access within
   class declaration (ex: 'a::class'/'a::b::class'), as well as typical class
   declaration. */
static lily_class *resolve_class_name(lily_parse_state *parser)
{
    lily_symtab *symtab = parser->symtab;
    lily_lex_state *lex = parser->lex;

    NEED_CURRENT_TOK(tk_word)

    lily_import_entry *search_import = resolve_import(parser);
    lily_class *result = lily_find_class(symtab, search_import, lex->label);
    if (result == NULL) {
        if (search_import == NULL)
            search_import = symtab->builtin_import;

        /* Is this a class that hasn't been loaded just yet? First try the
           builtins to figure that out... */
        lily_base_seed *call_seed =
                find_dynaload_entry((lily_item *)search_import, lex->label);

        /* If that doesn't work, then this could be a situation of being in a
           method dynaload that references types that have yet to be dynaloaded.
           In such a case, what's marked as the active import will have a
           dynaload table that isn't NULL. */
        if (call_seed == NULL)
            call_seed = find_dynaload_entry((lily_item *)symtab->active_import,
                    lex->label);

        if (call_seed && call_seed->seed_type == dyna_exception)
            result = dynaload_exception(parser, search_import, lex->label);
        else if (call_seed && call_seed->seed_type == dyna_class)
            result = lily_new_class_by_seed(parser->symtab, call_seed);
        else
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Class '%s' does not exist.\n", lex->label);
    }

    return result;
}

static void parse_define_header(lily_parse_state *parser, int modifiers)
{
    lily_lex_state *lex = parser->lex;
    NEED_CURRENT_TOK(tk_word)

    ensure_unique_method_name(parser, lex->label);

    /* The type will be overwritten with the right thing later on. However, it's
       necessary to have some function-like entity there instead of, say, NULL.
       The reason is that a dynaload may be triggered, which may push a block.
       The emitter will attempt to restore the return type via the type of the
       define var here. */
    lily_var *define_var = lily_emit_new_define_var(parser->emit,
            parser->default_call_type, lex->label);

    int i = 0;
    int arg_flags = 0;
    int result_pos = parser->tm->pos;
    int generics_used;

    /* This is the initial result. NULL means the function doesn't return
       anything. If it does, then this spot will be overwritten. */
    lily_tm_add(parser->tm, NULL);

    lily_lexer(lex);

    if (lex->token == tk_left_bracket)
        generics_used = collect_generics(parser);
    else
        generics_used = parser->emit->block->generic_count;

    lily_emit_enter_block(parser->emit, block_define);
    lily_update_symtab_generics(parser->symtab, NULL, generics_used);

    if (parser->class_self_type) {
        /* This is a method of a class. It should implicitly take 'self' as
           the first argument, and be registered to be within that class.
           It may also have a private/protected modifier, so add that too. */
        lily_tm_add(parser->tm, parser->class_self_type);
        i++;

        lily_var *self_var = lily_emit_new_scoped_var(parser->emit,
                parser->class_self_type, "(self)");
        define_var->parent = parser->class_self_type->cls;
        define_var->flags |= modifiers;

        parser->emit->block->self = (lily_storage *)self_var;
    }

    if (lex->token == tk_left_parenth) {
        lily_lexer(lex);

        /* If () is omitted, then it's assumed that the function will not take
           any arguments (unless it implicitly takes self). */
        if (lex->token == tk_right_parenth)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Empty () not needed for a define.\n");

        while (1) {
            NEED_CURRENT_TOK(tk_word)
            lily_tm_add(parser->tm, get_named_arg(parser, &arg_flags));
            i++;
            if (lex->token == tk_comma) {
                lily_lexer(lex);
                continue;
            }
            else if (lex->token == tk_right_parenth) {
                lily_lexer(lex);
                break;
            }
            else
                lily_raise(parser->raiser, lily_SyntaxError,
                        "Expected either ',' or ')', not '%s'.\n",
                        tokname(lex->token));
        }
    }

    if (lex->token == tk_colon) {
        lily_lexer(lex);
        lily_tm_insert(parser->tm, result_pos, get_type(parser));
    }

    NEED_CURRENT_TOK(tk_left_curly)

    define_var->type = lily_tm_make(parser->tm, arg_flags,
            parser->symtab->function_class, i + 1);

    lily_emit_update_function_block(parser->emit, NULL,
            generics_used, define_var->type->subtypes[0]);

    if (parser->optarg_stack_pos != 0) {
        lily_emit_write_optargs(parser->emit, parser->optarg_stack,
                parser->optarg_stack_pos);

        parser->optarg_stack_pos = 0;
    }
}

/*****************************************************************************/
/* Expression handling                                                       */
/*****************************************************************************/

/* I need a value to work with. */
#define ST_DEMAND_VALUE         1
/* A binary op or an operation (dot call, call, subscript), or a close. */
#define ST_WANT_OPERATOR        2
/* A value is nice, but not required (ex: call arguments). */
#define ST_WANT_VALUE           3
/* This is a special value that's passed to expression, but never set by
   expression internally. If this is initially passed to expression, then ')'
   can finish the expression.
   This is needed because otherwise...
   class Bird(...) > Animal(...)[0]
                                ^^^
                                This is allowed. */
#define ST_MAYBE_END_ON_PARENTH 4

#define ST_DONE                 5
#define ST_BAD_TOKEN            6

/*  expression_static_call
    This handles expressions like `<type>::member`. */
static void expression_static_call(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    NEED_NEXT_TOK(tk_colon_colon)
    NEED_NEXT_TOK(tk_word)

    /* Begin by checking if this is a class member. If it's not, attempt a
       dynaload in case it's a method that hasn't been loaded yet. */
    lily_var *v = lily_find_method(cls, lex->label);
    if (v == NULL)
        v = lily_parser_dynamic_load(parser, cls, lex->label);

    /* Is this the class that's currently being read in? If it is, then its
       methods may not have been injected into the class just yet (especially if
       it is the current method).
       Don't push it as a defined_func, or emitter will assume that 'self' is to
       be automatically injected into it. This allows class methods to call
       other methods (like ::new) that do not take a class instance. */
    if (v == NULL && parser->emit->block->class_entry == cls) {
        v = lily_find_var(parser->symtab, NULL, lex->label);
        if (v != NULL && v->parent != cls)
            /* Ignore it, it doesn't belong to this class. */
            v = NULL;
    }

    if (v) {
        lily_ast_push_static_func(parser->ast_pool, v);
        return;
    }

    /* Enums allow scoped variants through `<enum>::<variant>`. */
    if (cls->flags & CLS_IS_ENUM) {
        lily_class *variant_cls = lily_find_scoped_variant(cls, lex->label);
        if (variant_cls) {
            lily_ast_push_variant(parser->ast_pool, variant_cls);
            return;
        }
    }

    lily_raise(parser->raiser, lily_SyntaxError,
            "%s::%s does not exist.\n", cls->name, lex->label);
}

/*  parse_special_keyword
    This handles all the simple keywords that map to a string/integer value. */
static lily_sym *parse_special_keyword(lily_parse_state *parser, int key_id)
{
    lily_symtab *symtab = parser->symtab;
    lily_sym *ret;

    /* These literal fetching routines are guaranteed to return a literal with
       the given value. */
    if (key_id == KEY__LINE__)
        ret = (lily_sym *) lily_get_integer_literal(symtab, parser->lex->line_num);
    else if (key_id == KEY__FILE__) {
        /* This is necessary because lambdas register as file-like things. This
           will grab the first REAL file. */
        lily_lex_entry *entry = parser->lex->entry;
        while (strcmp(entry->filename, "[lambda]") == 0)
            entry = entry->prev;

        ret = (lily_sym *) lily_get_string_literal(symtab, entry->filename);
    }
    else if (key_id == KEY__FUNCTION__)
        ret = (lily_sym *) lily_get_string_literal(symtab, parser->emit->top_var->name);
    else if (key_id == KEY_TRUE)
        ret = (lily_sym *) lily_get_boolean_literal(symtab, 1);
    else if (key_id == KEY_FALSE)
        ret = (lily_sym *) lily_get_boolean_literal(symtab, 0);
    else if (key_id == KEY_SELF) {
        if (parser->class_self_type == NULL) {
            lily_raise(parser->raiser, lily_SyntaxError,
                    "'self' must be used within a class.\n");
        }
        ret = (lily_sym *) parser->emit->block->self;
    }
    else
        ret = NULL;

    return ret;
}

/*  expression_variant
    This is called when expression_word hits a label that's a class that's
    marked as a variant. They're used like a function, sometimes. Not actually
    a function though. */
static void expression_variant(lily_parse_state *parser,
        lily_class *variant_cls)
{
    lily_ast_push_variant(parser->ast_pool, variant_cls);
}

static void dispatch_word_as_class(lily_parse_state *parser, lily_class *cls,
        int *state)
{
    if (cls->flags & CLS_IS_VARIANT)
        expression_variant(parser, cls);
    else
        expression_static_call(parser, cls);

    *state = ST_WANT_OPERATOR;
}

static void dispatch_word_as_var(lily_parse_state *parser, lily_var *var,
        int *state)
{
    if (var->flags & SYM_NOT_INITIALIZED)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Attempt to use uninitialized value '%s'.\n",
                var->name);

    /* Defined functions have a depth of one, so they have to be first. */
    else if (var->flags & VAR_IS_READONLY)
        lily_ast_push_defined_func(parser->ast_pool, var);
    else if (var->function_depth == 1)
        lily_ast_push_global_var(parser->ast_pool, var);
    else if (var->function_depth == parser->emit->function_depth)
        lily_ast_push_local_var(parser->ast_pool, var);
    else
        lily_ast_push_upvalue(parser->ast_pool, var);

    *state = ST_WANT_OPERATOR;
}

static void dispatch_word_as_dynaloaded(lily_parse_state *parser,
        lily_import_entry *import, lily_base_seed *seed, int *state)
{
    lily_import_entry *saved_active = parser->symtab->active_import;
    lily_symtab *symtab = parser->symtab;

    symtab->active_import = import;

    if (seed->seed_type == dyna_var) {
        lily_var_seed *var_seed = (lily_var_seed *)seed;
        /* Note: This is currently fine because there are no modules which
           create vars of a type not found in the interpreter's core. However,
           if that changes, this must change as well. */
        lily_type *var_type = type_by_name(parser, var_seed->type);
        lily_var *new_var = lily_emit_new_dyna_var(parser->emit, import,
                var_type, seed->name);

        /* This will tie some sort of value to the newly-made var. It doesn't
           matter what though: The vm will figure that out later. */
        import->var_load_fn(parser, new_var);
        lily_ast_push_global_var(parser->ast_pool, new_var);
        *state = ST_WANT_OPERATOR;
    }
    else if (seed->seed_type == dyna_function) {
        lily_var *new_var = dynaload_function(parser, (lily_item *)import, seed);
        lily_ast_push_defined_func(parser->ast_pool, new_var);
        *state = ST_WANT_OPERATOR;
    }
    else if (seed->seed_type == dyna_class) {
        lily_class *new_cls = lily_new_class_by_seed(symtab, seed);
        dispatch_word_as_class(parser, new_cls, state);
    }
    else if (seed->seed_type == dyna_exception) {
        lily_class *new_cls = dynaload_exception(parser, import,
                seed->name);
        dispatch_word_as_class(parser, new_cls, state);
    }

    symtab->active_import = saved_active;
}

/*  expression_word
    This is a helper function that handles words in expressions. These are
    sort of complicated. :( */
static void expression_word(lily_parse_state *parser, int *state)
{
    lily_symtab *symtab = parser->symtab;
    lily_lex_state *lex = parser->lex;
    lily_import_entry *search_entry = resolve_import(parser);

    lily_var *var = lily_find_var(symtab, search_entry, lex->label);
    if (var) {
        dispatch_word_as_var(parser, var, state);
        return;
    }

    if (search_entry == NULL) {
        int key_id = keyword_by_name(lex->label);
        if (key_id != -1) {
            lily_sym *sym = parse_special_keyword(parser, key_id);
            if (sym != NULL) {
                if (sym->flags & ITEM_TYPE_TIE)
                    lily_ast_push_literal(parser->ast_pool, (lily_tie *)sym);
                else
                    lily_ast_push_self(parser->ast_pool);

                *state = ST_WANT_OPERATOR;
                return;
            }
        }
    }

    lily_class *cls = lily_find_class(parser->symtab, search_entry, lex->label);

    if (cls) {
        dispatch_word_as_class(parser, cls, state);
        return;
    }

    if (search_entry == NULL && parser->class_self_type) {
        var = lily_find_method(parser->symtab->active_import->class_chain,
                lex->label);

        if (var) {
            lily_ast_push_defined_func(parser->ast_pool, var);
            *state = ST_WANT_OPERATOR;
            return;
        }
    }

    if (search_entry == NULL)
        search_entry = symtab->builtin_import;

    lily_base_seed *base_seed = find_dynaload_entry((lily_item *)search_entry,
            lex->label);

    if (base_seed) {
        dispatch_word_as_dynaloaded(parser, search_entry, base_seed, state);
        return;
    }

    lily_raise(parser->raiser, lily_SyntaxError, "%s has not been declared.\n",
            lex->label);
}

/*  expression_property
    Within a class declaration, the properties of the class are referred to
    by using a @ in front of the name.

    Example:
        class Point(integer inX, integer inY) { @x = inX    @y = inY }
        Point p = Point::new(1, 2)
        # @x now availble as 'p.x', @y as 'p.y'.

    Similar to expression_word, minus the complexity. */
static void expression_property(lily_parse_state *parser, int *state)
{
    if (parser->class_self_type == NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Properties cannot be used outside of a class constructor.\n");

    char *name = parser->lex->label;
    lily_class *current_class = parser->class_self_type->cls;

    lily_prop_entry *prop = lily_find_property(current_class, name);
    if (prop == NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Property %s is not in class %s.\n", name, current_class->name);

    lily_ast_push_property(parser->ast_pool, prop);
    *state = ST_WANT_OPERATOR;
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

    if (tt == tree_call || tt == tree_parenth || tt == tree_typecast)
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

        if (ch == '-')
            expr_op = parser_tok_table[tk_minus].expr_op;
        else
            expr_op = parser_tok_table[tk_plus].expr_op;

        lily_ast_push_binary_op(parser->ast_pool, (lily_expr_op)expr_op);
        /* Call this to force a rescan from the proper starting point, yielding
           a proper new token. */
        lily_lexer_digit_rescan(lex);

        lily_ast_push_literal(parser->ast_pool, lex->last_literal);
        *did_fixup = 1;
    }
    else
        *did_fixup = 0;
}

/*  expression_literal
    This handles all literals: integer, double, and string. */
static void expression_literal(lily_parse_state *parser, int *state)
{
    lily_lex_state *lex = parser->lex;
    lily_token token = parser->lex->token;

    if (*state == ST_WANT_OPERATOR &&
         (token == tk_integer || token == tk_double)) {
        int did_fixup;
        maybe_digit_fixup(parser, &did_fixup);
        if (did_fixup == 0)
            *state = ST_DONE;
    }
    else if (*state == ST_WANT_OPERATOR)
        /* Disable multiple strings without dividing commas. */
        *state = ST_BAD_TOKEN;
    else {
        lily_ast_push_literal(parser->ast_pool, lex->last_literal);
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
    This handles "oo-style" accesses.
        Those can be either for a callable member:
            string x = "abc"
            abc.concat("def")
        Or for getting properties of a class:
            ValueError v = ValueError::new("test")
            show (v.message)
    It also handles typecasts: `abc.@(type)`. */
static void expression_dot(lily_parse_state *parser, int *state)
{
    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);
    if (lex->token == tk_word) {
        /* Create a magic oo access tree and expect an operator. This allows
           the property to be called or not, important for implementing
           properties and callables through dot. */
        lily_ast_push_oo_access(parser->ast_pool, parser->lex->label);

        *state = ST_WANT_OPERATOR;
    }
    else if (lex->token == tk_typecast_parenth) {
        lily_lexer(lex);
        lily_type *new_type = get_type(parser);
        lily_ast_enter_typecast(parser->ast_pool, new_type);
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
    int maybe_end_on_parenth = 0;
    if (state == ST_MAYBE_END_ON_PARENTH) {
        maybe_end_on_parenth = 1;
        state = ST_WANT_VALUE;
    }

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
        else if (lex->token == tk_prop_word) {
            if (state == ST_WANT_OPERATOR)
                state = ST_DONE;
            else
                expression_property(parser, &state);
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
                if (maybe_end_on_parenth == 0 ||
                    lex->token != tk_right_parenth ||
                    parser->ast_pool->save_depth != 0)
                    state = ST_WANT_OPERATOR;
                else {
                    state = ST_DONE;
                }
            }
        }
        else if (lex->token == tk_integer || lex->token == tk_double ||
                 lex->token == tk_double_quote || lex->token == tk_bytestring)
            expression_literal(parser, &state);
        else if (lex->token == tk_dot)
            expression_dot(parser, &state);
        else if (lex->token == tk_minus || lex->token == tk_not)
            expression_unary(parser, &state);
        else if (lex->token == tk_lambda) {
            /* This is to allow `x.some_call{|x| ... }`
               to act as        `x.some_call({|x| ... })`
               This is a little thing that helps a lot. Oh, and make sure this
               goes before the 'val_or_end' case, because lambdas are starting
               tokens. */
            if (state == ST_WANT_OPERATOR)
                lily_ast_enter_tree(parser->ast_pool, tree_call);

            lily_ast_push_lambda(parser->ast_pool, parser->lex->lambda_start_line,
                    parser->lex->lambda_data);

            if (state == ST_WANT_OPERATOR)
                lily_ast_leave_tree(parser->ast_pool);

            state = ST_WANT_OPERATOR;
        }
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

/*  var_handler
    Syntax: var <name> <: type> = <value>

    If <:type> is not provided, then the type is initially set to NULL and the
    emitter will set the var's type based upon the result of the expression.

    In many cases, the type is not necessary.
        var a = 10        # inferred as integer
        var b = 10.0      # inferred as double
        var c = [1, 2, 3] # inferred as list[integer]

    However, there are cases where it is useful:
        var d: list[integer] = []

    Within a class constructor, the name of the var must start with @ (because
    accesses of class variables is done with @ too). Otherwise, @ must -not-
    appear.

    This function accepts modifiers for the var. This should be either 'private'
    or 'protected' (a public scope is the default). These are only allowed for
    class properties though.

    Additionally, all values MUST have an initializing assignment. This is
    mandatory so that the interpreter does not have to worry about uninitialized
    values. */
static void parse_var(lily_parse_state *parser, int modifiers)
{
    lily_lex_state *lex = parser->lex;
    lily_sym *sym = NULL;
    /* This prevents variables from being used to initialize themselves. */
    int flags = SYM_NOT_INITIALIZED | modifiers;

    lily_token token, want_token, other_token;
    if (parser->emit->block->block_type == block_class) {
        want_token = tk_prop_word;
        other_token = tk_word;
    }
    else {
        want_token = tk_word;
        other_token = tk_prop_word;
    }

    while (1) {
        /* For this special case, give a useful error message. */
        if (lex->token == other_token)
            bad_decl_token(parser);

        NEED_CURRENT_TOK(want_token)

        if (lex->token == tk_word)
            sym = (lily_sym *)get_named_var(parser, NULL);
        else
            sym = (lily_sym *)get_named_property(parser, NULL, flags);

        if (sym->flags & ITEM_TYPE_VAR) {
            /* It's important to add locals and globals differently, because
               the emitter can't optimize stuff with globals. */
            if (parser->emit->function_depth == 1)
                lily_ast_push_global_var(parser->ast_pool, (lily_var *)sym);
            else
                lily_ast_push_local_var(parser->ast_pool, (lily_var *)sym);
        }
        else
            lily_ast_push_property(parser->ast_pool, (lily_prop_entry *)sym);

        if (lex->token == tk_colon) {
            lily_lexer(lex);
            sym->type = get_type(parser);
        }

        if (lex->token != tk_equal) {
            lily_raise(parser->raiser, lily_SyntaxError,
                    "An initialization expression is required here.\n");
        }

        lily_ast_push_binary_op(parser->ast_pool, expr_assign);
        lily_lexer(lex);
        expression(parser);
        lily_emit_eval_expr(parser->emit, parser->ast_pool);

        if (want_token == tk_prop_word && sym->flags & TYPE_MAYBE_CIRCULAR)
            lily_tm_set_circular(parser->class_self_type->cls);

        token = lex->token;
        /* This is the start of the next statement (or, for 'var', only allow
           one decl at a time to discourage excessive use of 'var'). */
        if (token == tk_word || token == tk_prop_word || token == tk_end_tag ||
            token == tk_eof || token == tk_right_curly)
            break;
        else if (token != tk_comma) {
            lily_raise(parser->raiser, lily_SyntaxError,
                       "Expected ',' or ')', not %s.\n", tokname(lex->token));
        }
        /* else it's a comma, so make sure a word is next. */

        lily_lexer(lex);
    }
}

static void var_handler(lily_parse_state *parser, int multi)
{
    parse_var(parser, 0);
}

/*  Given a name, return the type that represents it. This is used for dynaload
    of vars (the module provides a string that describes the var). */
static lily_type *type_by_name(lily_parse_state *parser, char *name)
{
    lily_load_copy_string(parser->lex, "[api]", lm_no_tags, name);
    lily_lexer(parser->lex);
    lily_type *result = get_type(parser);
    lily_pop_lex_entry(parser->lex);

    return result;
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

    lily_class *cls = parser->symtab->integer_class;

    /* For loop values are created as vars so there's a name in case of a
       problem. This name doesn't have to be unique, since it will never be
       found by the user. */
    lily_var *var = lily_emit_new_scoped_var(parser->emit, cls->type, name);

    lily_emit_eval_expr_to_var(parser->emit, ap, var);

    return var;
}

/*****************************************************************************/
/* Statement handling                                                        */
/*****************************************************************************/

/* Every keyword has an associated handler, even if it's something rather
   simple. */
static void if_handler(lily_parse_state *, int);
static void do_handler(lily_parse_state *, int);
static void var_handler(lily_parse_state *, int);
static void for_handler(lily_parse_state *, int);
static void try_handler(lily_parse_state *, int);
static void case_handler(lily_parse_state *, int);
static void else_handler(lily_parse_state *, int);
static void true_handler(lily_parse_state *, int);
static void elif_handler(lily_parse_state *, int);
static void self_handler(lily_parse_state *, int);
static void enum_handler(lily_parse_state *, int);
static void while_handler(lily_parse_state *, int);
static void raise_handler(lily_parse_state *, int);
static void false_handler(lily_parse_state *, int);
static void match_handler(lily_parse_state *, int);
static void break_handler(lily_parse_state *, int);
static void class_handler(lily_parse_state *, int);
static void define_handler(lily_parse_state *, int);
static void return_handler(lily_parse_state *, int);
static void except_handler(lily_parse_state *, int);
static void import_handler(lily_parse_state *, int);
static void private_handler(lily_parse_state *, int);
static void file_kw_handler(lily_parse_state *, int);
static void line_kw_handler(lily_parse_state *, int);
static void protected_handler(lily_parse_state *, int);
static void continue_handler(lily_parse_state *, int);
static void function_kw_handler(lily_parse_state *, int);

typedef void (keyword_handler)(lily_parse_state *, int);

/* This is setup so that handlers[key_id] is the handler for that keyword. */
static keyword_handler *handlers[] = {
    if_handler,
    do_handler,
    var_handler,
    for_handler,
    try_handler,
    case_handler,
    else_handler,
    true_handler,
    elif_handler,
    self_handler,
    enum_handler,
    while_handler,
    raise_handler,
    false_handler,
    match_handler,
    break_handler,
    class_handler,
    define_handler,
    return_handler,
    except_handler,
    import_handler,
    private_handler,
    file_kw_handler,
    line_kw_handler,
    protected_handler,
    continue_handler,
    function_kw_handler
};

static void parse_multiline_block_body(lily_parse_state *, int);

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
            key_id = keyword_by_name(lex->label);
            if (key_id != -1) {
                /* Ask the handler for this keyword what to do. */
                lily_lexer(lex);
                handlers[key_id](parser, multi);
            }
            else {
                lclass = lily_find_class(parser->symtab, NULL, lex->label);

                if (lclass != NULL &&
                    (lclass->flags & CLS_IS_VARIANT) == 0) {
                    expression_static_call(parser, lclass);
                    lily_lexer(lex);
                    expression_raw(parser, ST_WANT_OPERATOR);
                    lily_emit_eval_expr(parser->emit, parser->ast_pool);
                }
                else {
                    expression(parser);
                    lily_emit_eval_expr(parser->emit, parser->ast_pool);
                }
            }
        }
        else if (token == tk_integer || token == tk_double ||
                 token == tk_double_quote || token == tk_left_parenth ||
                 token == tk_left_bracket || token == tk_tuple_open ||
                 token == tk_prop_word || token == tk_bytestring) {
            expression(parser);
            lily_emit_eval_expr(parser->emit, parser->ast_pool);
        }
        /* The caller will be expecting '}' or maybe ?> / EOF if it's the main
           parse loop. */
        else if (multi)
            break;
        /* Single-line expressions need a value to prevent things like
           'if 1: }' and 'if 1: ?>'. */
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

    lily_emit_enter_block(parser->emit, block_if);
    expression(parser);
    lily_emit_eval_condition(parser->emit, parser->ast_pool);
    NEED_CURRENT_TOK(tk_colon)

    lily_lexer(lex);
    if (lex->token == tk_left_curly)
        parse_multiline_block_body(parser, multi);
    else {
        statement(parser, 0);
        while (lex->token == tk_word) {
            int key_id = keyword_by_name(lex->label);

            /* Jump directly into elif/else. Doing it this way (instead of
               through statement) means that the 'if' block can be popped in a
               single place. */
            if (key_id == KEY_ELIF || key_id == KEY_ELSE) {
                lily_lexer(parser->lex);
                handlers[key_id](parser, 0);

                /* Whatever comes next cannot be for the current if block. */
                if (key_id == KEY_ELSE)
                    break;
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
    lily_emit_change_block_to(parser->emit, block_if_elif);
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

    lily_emit_change_block_to(parser->emit, block_if_else);
    NEED_CURRENT_TOK(tk_colon)
    lily_lexer(lex);

    statement(parser, multi);
}

/*  do_keyword
    This handles simple keywords that can start expressions. It unifies common
    code in __line__, __file__, __function__, true, and false.

    key_id: The id of the keyword to handle. */
static void do_keyword(lily_parse_state *parser, int key_id)
{
    lily_sym *sym;
    sym = parse_special_keyword(parser, key_id);
    if (sym->flags & ITEM_TYPE_TIE)
        lily_ast_push_literal(parser->ast_pool, (lily_tie *)sym);
    else
        lily_ast_push_self(parser->ast_pool);

    expression_raw(parser, ST_WANT_OPERATOR);
    lily_emit_eval_expr(parser->emit, parser->ast_pool);
}

static void true_handler(lily_parse_state *parser, int multi)
{
    do_keyword(parser, KEY_TRUE);
}

static void false_handler(lily_parse_state *parser, int multi)
{
    do_keyword(parser, KEY_FALSE);
}

/*  This function is called in a multi-line block after either a return or a
    raise is done. This ensures that there is not a statement/expression that is
    obviously not going to execute. */
static void ensure_no_code_after_exit(lily_parse_state *parser,
        const char *name)
{
    lily_token token = parser->lex->token;
    if (token != tk_right_curly && token != tk_eof && token != tk_end_tag) {
        int key_id;
        if (token == tk_word)
            key_id = keyword_by_name(parser->lex->label);
        else
            key_id = -1;

        /* These are not part of a statement, but instead the start of a
           different branch. This is okay. */
        if (key_id != KEY_ELIF && key_id != KEY_ELSE && key_id != KEY_EXCEPT &&
            key_id != KEY_CASE) {
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Statement(s) after '%s' will not execute.\n", name);
        }
    }
}

/*  return_handler
    This handles the return keyword. It'll look up the current function to see
    if an expression is needed, or if just 'return' alone is fine. */
static void return_handler(lily_parse_state *parser, int multi)
{
    lily_block_type block_type = parser->emit->function_block->block_type;
    if (block_type == block_class)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'return' not allowed in a class constructor.\n");
    else if (block_type == block_lambda)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'return' not allowed in a lambda.\n");

    lily_type *ret_type = parser->emit->top_function_ret;
    lily_ast *ast;

    if (ret_type != NULL) {
        expression(parser);
        ast = parser->ast_pool->root;
    }
    else
        ast = NULL;

    lily_emit_return(parser->emit, ast);
    if (ast)
        lily_ast_reset_pool(parser->ast_pool);

    if (multi)
        ensure_no_code_after_exit(parser, "return");
}

/*  while_handler
    Syntax:
        (multi-line)  while expr: { expr... }
        (single-line) while expr: expr
    */
static void while_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, block_while);

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

    if (multi && parser->lex->token != tk_right_curly)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'continue' not at the end of a multi-line block.\n");
}

/*  break_handler
    This handles the 'break' statement. Just a wrapper for emitter to call
    to emit a break. */
static void break_handler(lily_parse_state *parser, int multi)
{
    lily_emit_break(parser->emit);

    if (multi && parser->lex->token != tk_right_curly)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'break' not at the end of a multi-line block.\n");
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

    lily_emit_enter_block(parser->emit, block_for_in);

    loop_var = lily_find_var(parser->symtab, NULL, lex->label);
    if (loop_var == NULL) {
        lily_class *cls = parser->symtab->integer_class;
        loop_var = lily_emit_new_scoped_var(parser->emit, cls->type,
                lex->label);
    }
    else if (loop_var->type->cls->id != SYM_CLASS_INTEGER) {
        lily_raise(parser->raiser, lily_SyntaxError,
                   "Loop var must be type integer, not type '^T'.\n",
                   loop_var->type);
    }

    NEED_NEXT_TOK(tk_word)
    if (strcmp(lex->label, "in") != 0)
        lily_raise(parser->raiser, lily_SyntaxError, "Expected 'in', not '%s'.\n",
                   lex->label);

    lily_lexer(lex);

    lily_var *for_start, *for_end, *for_step;

    for_start = parse_for_range_value(parser, "(for start)");

    NEED_CURRENT_TOK(tk_three_dots)
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

    lily_emit_enter_block(parser->emit, block_do_while);

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

    /* This is important, because it prevents the condition of this block from
       using variables declared within it. This is necessary because, like with
       try+except, it cannot be determined that all the variables declared
       within this block have been initialized.
       Ex: 'do: { continue var v = 10 } while v == 10'. */
    lily_hide_block_vars(parser->symtab, parser->emit->block->var_start);

    expression(parser);
    lily_emit_eval_condition(parser->emit, parser->ast_pool);
    lily_emit_leave_block(parser->emit);
}

static void except_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_type *except_type = get_type(parser);
    /* Exception is likely to always be the base exception class. */
    lily_class *base_cls = lily_find_class(parser->symtab, NULL, "Exception");
    lily_type *base_type = base_cls->type;
    lily_block_type new_type = block_try_except;

    if (except_type == base_type)
        new_type = block_try_except_all;
    else if (lily_class_greater_eq(base_type->cls, except_type->cls) == 0)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'%s' is not a valid exception class.\n",
                except_type->cls->name);

    /* The block change has to come before the var is made, or the var will be
       made in the wrong scope. */
    lily_emit_change_block_to(parser->emit, new_type);

    lily_var *exception_var = NULL;
    if (lex->token == tk_word) {
        if (strcmp(parser->lex->label, "as") != 0)
            lily_raise(parser->raiser, lily_SyntaxError,
                "Expected 'as', not '%s'.\n", lex->label);

        NEED_NEXT_TOK(tk_word)
        exception_var = lily_find_var(parser->symtab, NULL, lex->label);
        if (exception_var != NULL)
            lily_raise(parser->raiser, lily_SyntaxError,
                "%s has already been declared.\n", exception_var->name);

        exception_var = lily_emit_new_scoped_var(parser->emit,
                except_type, lex->label);

        lily_lexer(lex);
    }

    NEED_CURRENT_TOK(tk_colon)
    lily_emit_except(parser->emit, except_type, exception_var,
            lex->line_num);

    lily_lexer(lex);
    if (lex->token != tk_right_curly)
        statement(parser, multi);
}

static lily_import_entry *load_native(lily_parse_state *parser,
        const char *name, char *path)
{
    lily_import_entry *result = NULL;
    if (lily_try_load_file(parser->lex, parser->msgbuf->message))
        result = make_new_import_entry(parser, name, path);

    return result;
}

static lily_import_entry *load_foreign(lily_parse_state *parser,
        const char *name, char *path)
{
    lily_import_entry *result = NULL;
    void *library = lily_library_load(path);
    if (library) {
        result = make_new_import_entry(parser, name, path);
        result->library = library;
    }

    return result;
}

static lily_import_entry *attempt_import(lily_parse_state *parser,
        lily_path_link *path_iter, const char *name, const char *suffix,
        lily_import_entry *(*callback)(lily_parse_state *, const char *, char *))
{
    lily_import_entry *result = NULL;
    lily_msgbuf *msgbuf = parser->msgbuf;

    while (path_iter) {
        lily_msgbuf_flush(msgbuf);
        lily_msgbuf_add_fmt(msgbuf, "%s%s%s", path_iter->path, name,
                suffix);
        result = (*callback)(parser, name, msgbuf->message);
        if (result)
            break;

        path_iter = path_iter->next;
    }

    return result;
}

static void write_import_paths(lily_msgbuf *msgbuf,
        lily_path_link *path_iter, const char *name, const char *suffix)
{
    while (path_iter) {
        lily_msgbuf_add_fmt(msgbuf, "    no file '%s%s%s'\n",
                path_iter->path, name, suffix);
        path_iter = path_iter->next;
    }
}

static lily_import_entry *load_import(lily_parse_state *parser, char *name)
{
    lily_import_entry *result = NULL;
    result = attempt_import(parser, parser->import_paths, name, ".lly",
            load_native);
    if (result == NULL) {
        result = attempt_import(parser, parser->import_paths, name,
                LILY_LIB_SUFFIX, load_foreign);
        if (result == NULL) {
            /* The parser's msgbuf is used for doing class dynaloading, so it
               should not be in use here. Also, as credit, this idea comes from
               seeing lua do the same. */
            lily_msgbuf *msgbuf = parser->msgbuf;
            lily_msgbuf_flush(msgbuf);
            lily_msgbuf_add_fmt(msgbuf, "Cannot import '%s':\n", name);
            lily_msgbuf_add_fmt(msgbuf, "no builtin module '%s'\n", name);
            write_import_paths(msgbuf, parser->import_paths, name, ".lly");
            write_import_paths(msgbuf, parser->library_import_paths, name,
                    LILY_LIB_SUFFIX);
            lily_raise(parser->raiser, lily_SyntaxError, parser->msgbuf->message);
        }
    }

    parser->symtab->active_import = result;
    return result;
}

static void import_handler(lily_parse_state *parser, int multi)
{
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file &&
        block->prev != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Cannot import a file here.\n");

    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;
    lily_import_entry *link_target = symtab->active_import;

    while (1) {
        NEED_CURRENT_TOK(tk_word)

        char *import_name = parser->lex->label;

        /* Start off by determining if a module with this name has been imported
           in this file. This search is done by checking the name the module was
           imported as, not the true name.
           This is so that 'import x as y    import y' fails, even if 'y' is not
           actually loaded (because there would be a clash). */
        lily_import_entry *load_search = lily_find_import(symtab,
                symtab->active_import, import_name);
        if (load_search)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Package '%s' has already been imported in this file.\n",
                    import_name);

        /* Has anything imported this module anywhere? If it has, then just make
           a new link to it.
           Note: lily_find_import_anywhere is unique in that it searches by the
           REAL names of the modules, not the 'as' names. */
        lily_import_entry *new_import = lily_find_import_anywhere(symtab,
                import_name);
        if (new_import == NULL) {
            new_import = load_import(parser, import_name);
            if (new_import->library == NULL) {
                /* lily_emit_enter_block will write new code to this special
                   var. */
                lily_var *import_var = lily_emit_new_define_var(parser->emit,
                        parser->default_call_type, "__import__");

                lily_emit_enter_block(parser->emit, block_file);

                /* The whole of the file can be thought of as one large
                   statement. */
                lily_lexer(lex);
                statement(parser, 1);

                /* Since this is processing an import, the lexer will raise an
                   error if ?> is found. Because of that, multi-line statement
                   can only end with either } or eof. Only one is right. */
                if (lex->token == tk_right_curly)
                    lily_raise(parser->raiser, lily_SyntaxError,
                            "'}' outside of a block.\n");

                if (parser->emit->block->block_type != block_file)
                    lily_raise(parser->raiser, lily_SyntaxError,
                            "Unterminated block(s) at end of file.\n");

                lily_emit_leave_block(parser->emit);
                lily_pop_lex_entry(parser->lex);

                lily_emit_write_import_call(parser->emit, import_var);
            }
            else
                new_import->dynaload_table = new_import->library->dynaload_table;

            parser->symtab->active_import = link_target;
        }

        lily_lexer(parser->lex);
        if (lex->token == tk_word && strcmp(lex->label, "as") == 0) {
            NEED_NEXT_TOK(tk_word)
            /* This link must be done now, because the next token may be a word
               and lex->label would be modified. */
            link_import_to(link_target, new_import, lex->label);
            lily_lexer(lex);
        }
        else
            link_import_to(link_target, new_import, NULL);

        if (lex->token == tk_comma) {
            lily_lexer(parser->lex);
            continue;
        }
        else
            break;
    }
}

static void try_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, block_try);
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
                except_handler(parser, 0);
                if (parser->emit->block->block_type == block_try_except_all)
                    break;
            }
            else
                break;
        }
    }

    lily_emit_leave_block(parser->emit);
}

static void raise_handler(lily_parse_state *parser, int multi)
{
    if (parser->emit->function_block->block_type == block_lambda)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'raise' not allowed in a lambda.\n");

    expression(parser);
    lily_emit_raise(parser->emit, parser->ast_pool->root);

    if (multi)
        ensure_no_code_after_exit(parser, "raise");

    lily_ast_reset_pool(parser->ast_pool);
}

static void ensure_valid_class(lily_parse_state *parser, char *name)
{
    if (name[1] == '\0')
        lily_raise(parser->raiser, lily_SyntaxError,
                "'%s' is not a valid class name (too short).\n", name);

    lily_block *block = parser->emit->block;

    if (block->block_type != block_file && block->prev != NULL) {
        lily_raise(parser->raiser, lily_SyntaxError,
                "Cannot declare a class here.\n");
    }

    lily_class *lookup_class = lily_find_class(parser->symtab, NULL, name);
    if (lookup_class != NULL) {
        lily_raise(parser->raiser, lily_SyntaxError,
                "Class '%s' has already been declared.\n", name);
    }
}

/*  This handles everything needed to create a class, up until figuring out
    inheritance (if that's indicated). This is where the class ::new is
    created, the class_self_type is set, and the class args are collected. If
    The class ::new takes optargs, those are handled here. */
static void parse_class_header(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;
    /* Use the default call type (function ()) in case one of the types listed
       triggers a dynaload. If a dynaload is triggered, emitter tries to
       restore the current return type from the last define's return type. */
    lily_var *call_var = lily_emit_new_define_var(parser->emit,
            parser->default_call_type, "new");

    int generics_used;

    lily_lexer(lex);
    if (lex->token == tk_left_bracket)
        generics_used = collect_generics(parser);
    else
        generics_used = parser->emit->block->generic_count;

    lily_emit_enter_block(parser->emit, block_class);
    lily_update_symtab_generics(symtab, cls, generics_used);

    parser->class_self_type = build_self_type(parser, cls, generics_used);

    int i = 1;
    int flags = 0;
    lily_tm_add(parser->tm, parser->class_self_type);

    if (lex->token == tk_left_parenth) {
        lily_lexer(lex);
        if (lex->token == tk_right_parenth)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Empty () not needed for a class.\n");

        while (1) {
            lily_tm_add(parser->tm, get_named_arg(parser, &flags));
            i++;
            if (lex->token == tk_comma) {
                lily_lexer(lex);
                continue;
            }
            else if (lex->token == tk_right_parenth) {
                lily_lexer(lex);
                break;
            }
            else
                lily_raise(parser->raiser, lily_SyntaxError,
                        "Expected either ',' or ')', not '%s'.\n",
                        tokname(lex->token));
        }
    }

    call_var->type = lily_tm_make(parser->tm, flags,
            parser->symtab->function_class, i);

    lily_emit_update_function_block(parser->emit, parser->class_self_type,
            generics_used, call_var->type->subtypes[0]);

    if (parser->optarg_stack_pos != 0) {
        lily_emit_write_optargs(parser->emit, parser->optarg_stack,
                parser->optarg_stack_pos);

        parser->optarg_stack_pos = 0;
    }
}

/*  parse_inheritance
    Syntax: class Bird(args...) > Animal(args...) {
                       ^                 ^
                       Start             End

    This function is responsible making sure that the class to be inherited
    from is valid and inheritable. This also collects the arguments to call
    the ::new of the inherited class and executes the call. */
static void parse_inheritance(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    NEED_NEXT_TOK(tk_word)

    lily_class *super_class = resolve_class_name(parser);

    if (super_class == NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Class '%s' does not exist.\n", lex->label);
    else if (super_class == cls)
        lily_raise(parser->raiser, lily_SyntaxError,
                "A class cannot inherit from itself!\n");
    else if (super_class->is_builtin ||
             super_class->flags & (CLS_IS_ENUM | CLS_IS_VARIANT))
        lily_raise(parser->raiser, lily_SyntaxError,
                "'%s' cannot be inherited from.\n", super_class->name);

    lily_var *class_new = lily_find_method(super_class, "new");

    /* There's a small problem here. The idea of being able to pass expressions
       as well as values is great. However, expression cannot be trusted to
       collect what's inside of the parentheses because it may allow a subscript
       afterward.
       Ex: class Point(integer value) > Parent(value)[0].
       This is avoided by passing a special flag to expression and calling it
       directly. */

    lily_ast_pool *ap = parser->ast_pool;
    lily_ast_enter_tree(ap, tree_call);
    lily_ast_push_inherited_new(ap, class_new);
    lily_ast_collect_arg(ap);

    lily_lexer(lex);

    if (lex->token == tk_left_parenth) {
        /* Since the call was already entered, skip the first '(' or the parser
           will attempt to enter it again. */
        lily_lexer(lex);
        if (lex->token == tk_right_parenth)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Empty () not needed here for inherited new.\n");

        expression_raw(parser, ST_MAYBE_END_ON_PARENTH);

        lily_lexer(lex);
    }
    else
        lily_ast_leave_tree(parser->ast_pool);

    lily_emit_eval_expr(parser->emit, ap);
    lily_change_parent_class(super_class, cls);
}

static void create_new_class(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_class *created_class = lily_new_class(parser->symtab, lex->label);
    lily_type *save_class_self_type = parser->class_self_type;

    parse_class_header(parser, created_class);

    if (lex->token == tk_lt)
        parse_inheritance(parser, created_class);

    NEED_CURRENT_TOK(tk_left_curly)

    parse_multiline_block_body(parser, 1);

    lily_finish_class(parser->symtab, created_class);

    parser->class_self_type = save_class_self_type;
    lily_emit_leave_block(parser->emit);
}

static void class_handler(lily_parse_state *parser, int multi)
{
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file &&
        block->prev != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Cannot define a class here.\n");

    lily_lex_state *lex = parser->lex;
    NEED_CURRENT_TOK(tk_word);

    ensure_valid_class(parser, lex->label);

    create_new_class(parser);
}

/*  This is called when a variant class has '(' after the name, indicating that
    it will take parameters of some sort. The variant can take as many
    parameters as it wants, and varargs is allowed. However, optional arguments
    are not. That may change in the future.

    Variants aren't quite functions though (that would add a lot of overhead).
    This is different than, say, define or class init, because the return type
    of the function-like thing this returns describes what generics that the
    variant uses.

    `enum Option[A] { Some(A) None }`

    In this case, tm will create a result that is `function(A => Some(A))`,
    because Some uses the A of Option. */
static lily_type *parse_variant_header(lily_parse_state *parser,
        lily_class *variant_cls)
{
    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);
    if (lex->token == tk_right_parenth)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Empty () not needed for a variant.\n");

    int result_pos = parser->tm->pos;
    int i = 1;
    int flags = 0;

    /* This reserves a slot for the return that will be written. */
    lily_tm_add(parser->tm, NULL);

    while (1) {
        lily_tm_add(parser->tm, get_nameless_arg(parser, &flags));

        if (flags & TYPE_HAS_OPTARGS)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Variant types cannot have default values.\n");

        i++;
        if (lex->token == tk_comma) {
            lily_lexer(lex);
            continue;
        }
        else if (lex->token == tk_right_parenth)
            break;
        else
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Expected either ',' or ')', not '%s'.\n",
                    tokname(lex->token));
    }

    lily_lexer(lex);

    lily_type *variant_return = lily_tm_make_variant_result(parser->tm,
            variant_cls, result_pos, i);
    lily_tm_insert(parser->tm, result_pos, variant_return);

    lily_type *result = lily_tm_make(parser->tm, flags,
            parser->symtab->function_class, i);

    return result;
}

static void enum_handler(lily_parse_state *parser, int multi)
{
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file &&
        block->prev != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Cannot define an enum here.\n");

    lily_lex_state *lex = parser->lex;

    NEED_CURRENT_TOK(tk_word)
    ensure_valid_class(parser, lex->label);

    lily_class *enum_cls = lily_new_class(parser->symtab, lex->label);

    lily_lexer(lex);
    int save_generics = parser->emit->block->generic_count;
    int generics_used;
    if (lex->token == tk_left_bracket)
        generics_used = collect_generics(parser);
    else
        generics_used = 0;

    lily_update_symtab_generics(parser->symtab, enum_cls, generics_used);
    lily_emit_enter_block(parser->emit, block_enum);

    lily_type *result_type = build_self_type(parser, enum_cls, generics_used);
    lily_type *save_self_type = parser->class_self_type;
    parser->class_self_type = result_type;

    NEED_CURRENT_TOK(tk_left_curly)
    lily_lexer(lex);

    int inner_class_count = 0;
    int is_scoped = (lex->token == tk_colon_colon);

    while (1) {
        if (is_scoped) {
            NEED_CURRENT_TOK(tk_colon_colon)
            lily_lexer(lex);
        }

        NEED_CURRENT_TOK(tk_word)
        lily_class *variant_cls = lily_find_class(parser->symtab, NULL, lex->label);
        if (variant_cls != NULL)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "A class with the name '%s' already exists.\n",
                    variant_cls->name);

        variant_cls = lily_new_variant(parser->symtab, enum_cls, lex->label);
        lily_type *variant_type;

        lily_lexer(lex);
        if (lex->token == tk_left_parenth)
            variant_type = parse_variant_header(parser, variant_cls);
        else
            variant_type = lily_tm_make_default_for(parser->tm, variant_cls);

        lily_finish_variant(parser->symtab, variant_cls, variant_type);

        inner_class_count++;

        if (lex->token == tk_right_curly)
            break;
        else if (lex->token == tk_word && lex->label[0] == 'd' &&
                 keyword_by_name(lex->label) == KEY_DEFINE)
            break;
    }

    if (inner_class_count < 2) {
        lily_raise(parser->raiser, lily_SyntaxError,
                "An enum must have at least two variants.\n");
    }

    /* This marks the enum as one (allowing match), and registers the variants
       as being within the enum. Because of that, it has to go before pulling
       the member functions. */
    lily_finish_enum(parser->symtab, enum_cls, is_scoped, result_type);

    if (lex->token == tk_word) {
        while (1) {
            lily_lexer(lex);
            define_handler(parser, 1);
            if (lex->token == tk_right_curly)
                break;
            else if (lex->token != tk_word ||
                keyword_by_name(lex->label) != KEY_DEFINE)
                lily_raise(parser->raiser, lily_SyntaxError,
                        "Expected '}' or 'define', not '%s'.\n",
                        tokname(lex->token));
        }
    }

    lily_emit_leave_block(parser->emit);
    parser->class_self_type = save_self_type;

    lily_update_symtab_generics(parser->symtab, NULL, save_generics);
    lily_lexer(lex);
}

/*  match_handler
    Syntax:
        'match <expr>: { ... }'

    The match block is an outlier compared to other blocks because it must
    always have the { and } after it. This is so that the inner case entries
    can automagically be multi-line. */
static void match_handler(lily_parse_state *parser, int multi)
{
    if (multi == 0)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Match block cannot be in a single-line block.\n");

    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, block_match);

    expression(parser);
    lily_emit_eval_match_expr(parser->emit, parser->ast_pool);

    NEED_CURRENT_TOK(tk_colon)
    NEED_NEXT_TOK(tk_left_curly)

    parse_multiline_block_body(parser, multi);

    lily_emit_leave_block(parser->emit);
}

/*  case_handler
    Syntax:
        For variants that do not take values:
            'case <variant>: ...'

        For those that do:
            'case <variant>(<var name>, <var name>...):'

    Each case in a match block is multi-line, so that users don't have to put
    { and } around a lot of cases (because that would probably be annoying).

    The emitter will check that, within a match block, each variant is seen
    exactly once (lily_emit_check_match_case).

    Some variants may have inner values. For those, parser will collect the
    appropriate number of identifiers and determine what the type of those
    identifiers should be! The variant's values are then decomposed to those
    identifiers.

    Checking for incomplete match blocks is done within emitter when the match
    block closes.

    The section for a case is done when the next case is seen. */
static void case_handler(lily_parse_state *parser, int multi)
{
    lily_block *block = parser->emit->block;
    if (block->block_type != block_match)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'case' not allowed outside of 'match'.\n");

    lily_type *match_input_type = block->match_sym->type;
    lily_class *match_class = match_input_type->cls;
    lily_lex_state *lex = parser->lex;
    lily_class *case_class = NULL;

    NEED_CURRENT_TOK(tk_word)

    int i;
    for (i = 0;i < match_class->variant_size;i++) {
        if (strcmp(lex->label, match_class->variant_members[i]->name) == 0) {
            case_class = match_class->variant_members[i];
            break;
        }
    }

    if (i == match_class->variant_size)
        lily_raise(parser->raiser, lily_SyntaxError,
                "%s is not a member of enum %s.\n", lex->label,
                match_class->name);

    if (lily_emit_add_match_case(parser->emit, i) == 0)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Already have a case for variant %s.\n", lex->label);

    lily_type *variant_type = case_class->variant_type;
    lily_type_system *ts = parser->emit->ts;
    if (variant_type->subtype_count != 0) {
        NEED_NEXT_TOK(tk_left_parenth)
        /* There should be as many identifiers as there are arguments to this
           variant's creation type.
           Also, start at 1 so that the return at [0] is skipped. */
        NEED_NEXT_TOK(tk_word)

        for (i = 1;i < variant_type->subtype_count;i++) {
            lily_type *var_type = lily_ts_resolve_by_second(ts,
                    match_input_type, variant_type->subtypes[i]);

            /* It doesn't matter what the var is, only that it's unique. The
               emitter will grab the vars it needs from the symtab when writing
               the decompose.
               This function also calls up the next token. */
            get_named_var(parser, var_type);
            if (i != variant_type->subtype_count - 1) {
                NEED_CURRENT_TOK(tk_comma)
                NEED_NEXT_TOK(tk_word)
            }
        }
        NEED_CURRENT_TOK(tk_right_parenth)

        lily_emit_variant_decompose(parser->emit, variant_type);
    }
    /* else the variant does not take arguments, and cannot decompose because
       there is nothing inside to decompose. */

    NEED_NEXT_TOK(tk_colon)
    lily_lexer(lex);
}

static void parse_define(lily_parse_state *parser, int modifiers)
{
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file &&
        block->block_type != block_define &&
        block->block_type != block_class &&
        block->block_type != block_enum &&
        block->prev != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Cannot define a function here.\n");

    lily_lex_state *lex = parser->lex;
    parse_define_header(parser, modifiers);

    NEED_CURRENT_TOK(tk_left_curly)
    parse_multiline_block_body(parser, 1);
    lily_emit_leave_block(parser->emit);

    /* If the function defined is at the top level of a class, then immediately
       make that function a member of the class.
       This is safe because 'define' always exits with the top-most variable
       being what was just defined. */
    if (parser->emit->block->block_type == block_class ||
        parser->emit->block->block_type == block_enum) {
        lily_add_class_method(parser->symtab,
                parser->class_self_type->cls,
                parser->symtab->active_import->var_chain);
    }
}

static void define_handler(lily_parse_state *parser, int multi)
{
    parse_define(parser, 0);
}

static void parse_modifier(lily_parse_state *parser, char *name, int modifier)
{
    if (parser->emit->block->block_type != block_class)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'%s' is not allowed here.", name);

    lily_lex_state *lex = parser->lex;
    NEED_CURRENT_TOK(tk_word)
    int key = keyword_by_name(parser->lex->label);
    if (key == KEY_VAR) {
        lily_lexer(lex);
        parse_var(parser, modifier);
    }
    else if (key == KEY_DEFINE) {
        lily_lexer(lex);
        parse_define(parser, modifier);
    }
    else
        lily_raise(parser->raiser, lily_SyntaxError,
                "Expected either 'var' or 'define', but got '%s'.\n",
                lex->label);
}

static void private_handler(lily_parse_state *parser, int multi)
{
    parse_modifier(parser, "private", SYM_SCOPE_PRIVATE);
}

static void protected_handler(lily_parse_state *parser, int multi)
{
    parse_modifier(parser, "protected", SYM_SCOPE_PROTECTED);
}

static void self_handler(lily_parse_state *parser, int multi)
{
    do_keyword(parser, KEY_SELF);
}

/*  parser_loop
    This is the main parsing function. This is called by a lily_parse_*
    function which will set the raiser and give the lexer a file before calling
    this function. */
static void parser_loop(lily_parse_state *parser)
{
    /* The first pass of the interpreter starts with the current namespace being
       the builtin namespace. */
    if (parser->first_pass) {
        lily_import_entry *main_import = make_new_import_entry(parser, "",
                parser->lex->entry->filename);

        /* This is necessary because __main__ is created within the builtin
           package (so that exceptions can be bootstrapped). However, since
           __main__ holds all the global code for the first file, fix the path
           of it to target the first file. */
        parser->symtab->main_function->import = main_import;

        parser->symtab->active_import = main_import;
        parser->first_pass = 0;
    }

    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);

    while (1) {
        if (lex->token == tk_word)
            statement(parser, 1);
        else if (lex->token == tk_right_curly) {
            lily_emit_leave_block(parser->emit);
            lily_lexer(lex);
        }
        else if (lex->token == tk_end_tag || lex->token == tk_eof) {
            if (parser->emit->block->prev != NULL) {
                lily_raise(parser->raiser, lily_SyntaxError,
                           "Unterminated block(s) at end of parsing.\n");
            }

            lily_prepare_main(parser->emit, parser->import_start);
            lily_vm_prep(parser->vm, parser->symtab);

            parser->executing = 1;
            lily_vm_execute(parser->vm);
            parser->executing = 0;

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
                 lex->token == tk_bytestring ||
                 lex->token == tk_tuple_open) {
            expression(parser);
            lily_emit_eval_expr(parser->emit, parser->ast_pool);
        }
        else
            lily_raise(parser->raiser, lily_SyntaxError, "Unexpected token %s.\n",
                       tokname(lex->token));
    }
}

static lily_type *parse_lambda_body(lily_parse_state *parser,
        lily_type *expect_type)
{
    /* The expressions/statements that this may run may cause emitter's expr_num
       to increase. It's vital that expr_num be restored to what it was when
       control returns to the caller.
       If this does not happen, the caller will re-use storages that should
       actually be considered to be claimed. */
    int save_expr_num = parser->emit->expr_num;
    lily_lex_state *lex = parser->lex;
    int key_id = -1;
    lily_type *result_type = NULL;

    lily_lexer(parser->lex);
    while (1) {
        if (lex->token == tk_word)
            key_id = keyword_by_name(lex->label);

        if (key_id == -1 || is_key_a_value(key_id)) {
            expression(parser);
            if (lex->token != tk_right_curly)
                /* This expression isn't the last one, so it can do whatever it
                   wants to do. */
                lily_emit_eval_expr(parser->emit, parser->ast_pool);
            else {
                /* The last expression is what will be returned, so give it the
                   inference information of the lambda. */
                lily_emit_eval_lambda_body(parser->emit, parser->ast_pool,
                        expect_type);

                if (parser->ast_pool->root->result)
                    result_type = parser->ast_pool->root->result->type;

                break;
            }
        }
        else {
            statement(parser, 0);
            key_id = -1;
            if (lex->token == tk_right_curly)
                break;
        }
    }

    parser->emit->expr_num = save_expr_num;
    return result_type;
}

/*****************************************************************************/
/* Exported API                                                              */
/*****************************************************************************/

/*  lily_parser_lambda_eval
    This function is called by the emitter to process the body of a lambda. The
    type that the emitter expects is given so that the types of the
    lambda's arguments can be inferred. */
lily_var *lily_parser_lambda_eval(lily_parse_state *parser,
        int lambda_start_line, char *lambda_body, lily_type *expect_type)
{
    lily_lex_state *lex = parser->lex;
    int args_collected = 0, tm_return = parser->tm->pos;
    lily_type *root_result;

    /* Process the lambda as if it were a file with a slightly adjusted
       starting line number. The line number is patched so that multi-line
       lambdas show the right line number for errors.
       Additionally, lambda_body is a shallow copy of data within the ast's
       string pool. A deep copy MUST be made because expressions within this
       lambda may cause the ast's string pool to be resized. */
    lily_load_copy_string(lex, "[lambda]", lm_no_tags, lambda_body);
    lex->line_num = lambda_start_line;

    /* Block entry assumes that the most recent var added is the var to bind
       the function to. For the type of the lambda, use the default call
       type (a function with no args and no output) because expect_type may
       be NULL if the emitter doesn't know what it wants. */
    lily_var *lambda_var = lily_emit_new_define_var(parser->emit,
            parser->default_call_type, "(lambda)");

    /* From here on, vars created will be in the scope of the lambda. Also,
       this binds a function value to lambda_var. */
    lily_emit_enter_block(parser->emit, block_lambda);

    lily_lexer(lex);

    lily_tm_add(parser->tm, NULL);

    if (expect_type && expect_type->subtype_count > 1) {
        if (lex->token == tk_logical_or)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Lambda expected %d args, but got 0.\n",
                    expect_type->subtype_count - 1);

        /* -1 because the return isn't an arg. */
        int num_args = expect_type->subtype_count - 1;
        lily_token wanted_token = tk_comma;

        while (1) {
            NEED_NEXT_TOK(tk_word)
            lily_type *arg_type = expect_type->subtypes[args_collected + 1];
            if (arg_type->flags & TYPE_IS_INCOMPLETE) {
                lily_raise(parser->raiser, lily_SyntaxError,
                        "Cannot infer type of '%s'.\n", lex->label);
            }

            lily_tm_add(parser->tm, arg_type);

            get_named_var(parser, arg_type);
            args_collected++;
            if (args_collected == num_args)
                wanted_token = tk_bitwise_or;

            NEED_CURRENT_TOK(wanted_token)
            if (wanted_token == tk_bitwise_or)
                break;
        }
    }
    else if (lex->token == tk_bitwise_or) {
        NEED_NEXT_TOK(tk_bitwise_or)
    }
    else if (lex->token != tk_logical_or)
        lily_raise(parser->raiser, lily_SyntaxError, "Unexpected token '%s'.\n",
                lex->token);

    /* It's time to process the body of the lambda. Before this is done, freeze
       the ast pool's state so that the save depth is 0 and such. This allows
       the expression function to ensure that the body of the lambda is valid. */
    lily_ast_freeze_state(parser->ast_pool);
    root_result = parse_lambda_body(parser, expect_type);

    lily_ast_thaw_state(parser->ast_pool);

    NEED_CURRENT_TOK(tk_right_curly)
    lily_lexer(lex);

    lily_tm_insert(parser->tm, tm_return, root_result);
    int flags = 0;
    if (expect_type && expect_type->cls->id == SYM_CLASS_FUNCTION &&
        expect_type->flags & TYPE_IS_VARARGS)
        flags = TYPE_IS_VARARGS;

    lambda_var->type = lily_tm_make(parser->tm, flags,
            parser->symtab->function_class, args_collected + 1);

    lily_emit_leave_block(parser->emit);
    lily_pop_lex_entry(lex);

    return lambda_var;
}

lily_var *lily_parser_dynamic_load(lily_parse_state *parser, lily_class *cls,
        char *name)
{
    lily_base_seed *base_seed = find_dynaload_entry((lily_item *)cls, name);
    lily_var *ret;

    if (base_seed != NULL)
        ret = dynaload_function(parser, (lily_item *)cls, base_seed);
    else
        ret = NULL;

    return ret;
}

/*  lily_parse_file
    This function starts parsing from a file indicated by the given filename.
    The file is opened through fopen, and is automatically destroyed when the
    parser is free'd.

    parser:  The parser that will be used to parse and run the data.
    mode:    This determines if <?lily ?> tags are parsed or not.
    str:     The string to parse.

    Returns 1 if successful, or 0 if an error was raised. */
int lily_parse_file(lily_parse_state *parser, lily_lex_mode mode, char *filename)
{
    /* It is safe to do this, because the parser will always occupy the first
       jump. All others should use lily_jump_setup instead. */
    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        lily_load_file(parser->lex, mode, filename);
        fixup_import_basedir(parser, filename);
        parser_loop(parser);
        lily_pop_lex_entry(parser->lex);

        return 1;
    }

    return 0;
}

/*  lily_parse_string
    This function starts parsing from a source that is a string passed. The caller
    is responsible for destroying the string if it needs to be destroyed.

    parser:  The parser that will be used to parse and run the data.
    name:    The name for this file, for when trace is printed.
    mode:    This determines if <?lily ?> tags are parsed or not.
    str:     The string to parse.

    Returns 1 if successful, or 0 if some error occured. */
int lily_parse_string(lily_parse_state *parser, char *name, lily_lex_mode mode,
        char *str)
{
    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        lily_load_str(parser->lex, name, mode, str);
        parser_loop(parser);
        lily_pop_lex_entry(parser->lex);
        return 1;
    }

    return 0;
}

lily_class *lily_maybe_dynaload_class(lily_parse_state *parser,
        lily_import_entry *import, const char *name)
{
    lily_symtab *symtab = parser->symtab;
    if (import == NULL)
        import = symtab->builtin_import;

    lily_class *cls = lily_find_class(parser->symtab, import, name);

    if (cls == NULL)
        cls = dynaload_exception(parser, import, name);

    return cls;
}

void lily_register_import(lily_parse_state *parser, const char *name,
        const void *dynaload_table, var_loader var_load_fn)
{
    lily_import_entry *entry = make_new_import_entry(parser, name, "[builtin]");
    entry->dynaload_table = dynaload_table;
    entry->var_load_fn = var_load_fn;
}

char *lily_build_error_message(lily_parse_state *parser)
{
    lily_raiser *raiser = parser->raiser;
    lily_msgbuf *msgbuf = parser->msgbuf;

    lily_msgbuf_flush(parser->msgbuf);

    if (raiser->exception_type) {
        /* If this error is not one of the builtin ones, then show the package
           from where it came. The reason for this is that different packages
           may wish to export a general error class (ex: pg::Error,
           mysql::Error, etc). So making it clear -which- one can be useful. */
        char *loadname = raiser->exception_type->cls->import->loadname;
        if (strcmp(loadname, "") != 0)
            lily_msgbuf_add_fmt(msgbuf, "%s::", loadname);
    }

    lily_msgbuf_add(msgbuf, lily_name_for_error(raiser));
    if (raiser->msgbuf->message[0] != '\0')
        lily_msgbuf_add_fmt(msgbuf, ": %s", raiser->msgbuf->message);
    else
        lily_msgbuf_add_char(msgbuf, '\n');

    if (parser->executing == 0) {
        lily_lex_entry *iter = parser->lex->entry;

        int fixed_line_num = (raiser->line_adjust == 0 ?
                parser->lex->line_num : raiser->line_adjust);

        /* The parser handles lambda processing by putting entries with the
           name [lambda]. Don't show these. */
        while (strcmp(iter->filename, "[lambda]") == 0)
            iter = iter->prev;

        if (strcmp(iter->filename, "[builtin]") != 0) {
            iter->saved_line_num = fixed_line_num;
            lily_msgbuf_add_fmt(msgbuf, "    from %s:%d\n",
                    iter->filename, iter->saved_line_num);
        }
        /* The entry is only [builtin] if there was a failure to load the first
           file. Don't show any filename because, well...there isn't one. */
    }
    else {
        lily_call_frame *frame = parser->vm->call_chain;

        lily_msgbuf_add(msgbuf, "Traceback:\n");

        while (frame) {
            lily_function_val *func = frame->function;
            char *class_name = func->class_name;
            char *separator;
            if (class_name == NULL) {
                class_name = "";
                separator = "";
            }
            else
                separator = "::";

            if (frame->function->code == NULL)
                lily_msgbuf_add_fmt(msgbuf, "    from [C]: in %s%s%s\n",
                        class_name, separator, func->trace_name);
            else
                lily_msgbuf_add_fmt(msgbuf,
                        "    from %s:%d: in %s%s%s\n",
                        func->import->path, frame->line_num, class_name,
                        separator, func->trace_name);

            frame = frame->prev;
        }
    }

    return msgbuf->message;
}

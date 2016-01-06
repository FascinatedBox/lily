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

/***
 *      ____       _               
 *     / ___|  ___| |_ _   _ _ __  
 *     \___ \ / _ \ __| | | | '_ \ 
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/ 
 *                          |_|    
 */

static void statement(lily_parse_state *, int);
static lily_import_entry *make_new_import_entry(lily_parse_state *,
        const char *, char *);
static lily_type *type_by_name(lily_parse_state *, char *);

/** This area is where the parser is initialized. These first functions create
    the two paths that the parser reads from. One path is the native path, and
    the other path is the C/library path. These two are split from each other
    because the latter path set is more restricted. Namely, .so/library files
    cannot be loaded relative to the file run. This is to prevent .so files
    from being stuck in a server. **/


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

/* This loads the Exception and Tainted classes into the parser now. Eventually,
   those two classes should be dynaloaded. */
static void run_bootstrap(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_load_str(lex, "[builtin]", lm_no_tags, bootstrap);
    lily_lexer(lex);
    statement(parser, 1);
    lily_pop_lex_entry(lex);
}

/* Not sure what options to give the parser? Call this to get a 'reasonable'
   starting point. */
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

/* This sets up the core of the interpreter. It's pretty rough around the edges,
   especially with how the parser is assigning into all sorts of various structs
   when it shouldn't. */
lily_parse_state *lily_new_parse_state(lily_options *options)
{
    lily_parse_state *parser = lily_malloc(sizeof(lily_parse_state));
    parser->data = options->data;
    parser->import_top = NULL;
    parser->import_start = NULL;

    /* Booting up Lily is rather tricky. To begin with, there's a special-cased
       import that's right here on this next line. This import will receive all
       of the classes that are builtin (integer, string, function, etc). The
       interpreter is special-cased to first search in the current import, then
       this one if there is a problem. */
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

    /* Here's the awful part where parser digs in and links everything that different
       sections need. */
    parser->tm = parser->emit->tm;

    parser->vm->symtab = parser->symtab;
    parser->vm->ts = parser->emit->ts;
    parser->vm->vm_buffer = parser->raiser->msgbuf;
    parser->vm->parser = parser;
    parser->vm->type_block = parser->emit->type_block;

    parser->symtab->lex_linenum = &parser->lex->line_num;

    parser->ast_pool->lex_linenum = &parser->lex->line_num;

    parser->emit->lex_linenum = &parser->lex->line_num;
    parser->emit->symtab = parser->symtab;
    parser->emit->ast_membuf = parser->ast_pool->ast_membuf;
    parser->emit->parser = parser;

    parser->lex->symtab = parser->symtab;
    parser->lex->membuf = parser->ast_pool->ast_membuf;

    /* Before things get carried away, define the import paths that will be
       available. This isn't final: If the interpreter runs from a file, that
       file's relative path will be added to the native set of paths as the
       first one. */
    parser->import_paths = prepare_path_by_seed(parser, LILY_PATH_SEED);
    parser->library_import_paths = prepare_path_by_seed(parser,
            LILY_LIBRARY_PATH_SEED);

    /* All code that isn't within a function is grouped together in a special
       function called __main__. Since that function is the first kind of a
       block, it needs a special function to initialize that block. */
    lily_emit_enter_main(parser->emit);

    parser->vm->main = parser->symtab->main_var;

    /* This type represents a function that has no input and no outputs. This is
       used when creating new functions so that they have a type. */
    parser->default_call_type = parser->vm->main->type;

    /* This allows the internal sys package to be located later. */
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

/***
 *      ___                            _   
 *     |_ _|_ __ ___  _ __   ___  _ __| |_ 
 *      | || '_ ` _ \| '_ \ / _ \| '__| __|
 *      | || | | | | | |_) | (_) | |  | |_ 
 *     |___|_| |_| |_| .__/ \___/|_|   \__|
 *                   |_|                   
 */

/** Lily largely copies import semantics from Python, including the keyword.
    Currently, the biggest differences are likely to be the lack of features:
    No support for `import x.y`, and no `from x import *`. The former will be
    changed hopefully in the near future. The latter, however, is unlikely to be
    changed soon. The only other big difference is that access does not use '.',
    but instead '::'.

    An important function here is 'lily_register_import'. This function can be
    used by a Lily 'runner' to provide a module that the script can import.
    However, the script will still have to explicitly load the module. Why?

    Suppose there is a script in tag mode that runs from Apache. Apache provides
    a server module, and the script does not explicitly import it. If the script
    is run through the command line, it fails because 'server' is not defined.
    But worse, the script has to be manually patched to have 'import server'.

    By requiring explicit import of modules that are registered, the script can
    be moved elsewhere with ease. The interpreter will search for either
    server.lly or a server library from the usual paths. So long as that module
    provides what Apache provides, it just works.

    One other thing: When importing files, the files that are imported are
    always imported in non-tag mode. That is also intentional. **/

/* This creates a new import entry within the parser and links it to existing
   import entries. The loadname and path given are copied over. */
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

/* This creates a new module using the information provided. The name supplied
   will become the name used to load the module (the path will be "[builtin]").
   Be aware that this does not automatically make the module loaded: It will
   still need to be imported by the script. That is intentional. */
void lily_register_import(lily_parse_state *parser, const char *name,
        const void *dynaload_table, var_loader var_load_fn)
{
    lily_import_entry *entry = make_new_import_entry(parser, name, "[builtin]");
    entry->dynaload_table = dynaload_table;
    entry->var_load_fn = var_load_fn;
}

/* This adds 'to_link' as an entry within 'target' so that 'target' is able to
   reference it later on. If 'as_name' is not NULL, then 'to_link' will be
   available through that name. Otherwise, it will be available as the name it
   actually has. */
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

/* This is called when the first file is loaded, and adds the path of the first
   file to the imports. */
static void fixup_import_basedir(lily_parse_state *parser, char *path)
{
    char *search_str = strrchr(path, '/');
    if (search_str == NULL)
        return;

    int length = (search_str - path) + 1;
    parser->import_paths = add_path_slice_to(parser, parser->import_paths, path,
        length);
}

/* This attempts to load a native file using the path + name combo given.
   Success: A newly-made import entry
   Failure: NULL */
static lily_import_entry *load_native(lily_parse_state *parser,
        const char *name, char *path)
{
    lily_import_entry *result = NULL;
    if (lily_try_load_file(parser->lex, parser->msgbuf->message))
        result = make_new_import_entry(parser, name, path);

    return result;
}

/* This attempts to load a foreign file using the path + name combo given. If it
   works, then a newly-made import is returned (which has the library added).
   Success: A newly-made import entry
   Failure: NULL */
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

/* This takes one of parser's paths (native or foreign) and tries to import
   the given name + suffix for each path that there is. The callback given is
   used to attempt the import, and will be either 'load_native' or
   'load_foreign'.
   Success: A newly-made import entry
   Failure: NULL */
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

/* This is called when all attempts to load a name have failed. This walks down
   the path given, and writes all of the different combonations that have been
   tried. */
static void write_import_paths(lily_msgbuf *msgbuf,
        lily_path_link *path_iter, const char *name, const char *suffix)
{
    while (path_iter) {
        lily_msgbuf_add_fmt(msgbuf, "    no file '%s%s%s'\n",
                path_iter->path, name, suffix);
        path_iter = path_iter->next;
    }
}

/* This is called when `import x` or `import x as y` has been seen. It tries to
   find 'x' at any place along the paths that it has.
   Success: A newly-made import entry is returned.
   Failure: The paths tried are printed, and SyntaxError is raised. */
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

/***
 *      _____                     ____      _ _           _   _             
 *     |_   _|   _ _ __   ___    / ___|___ | | | ___  ___| |_(_) ___  _ __  
 *       | || | | | '_ \ / _ \  | |   / _ \| | |/ _ \/ __| __| |/ _ \| '_ \ 
 *       | || |_| | |_) |  __/  | |__| (_) | | |  __/ (__| |_| | (_) | | | |
 *       |_| \__, | .__/ \___|   \____\___/|_|_|\___|\___|\__|_|\___/|_| |_|
 *           |___/|_|                                                       
 */

static lily_type *get_type(lily_parse_state *);
static lily_class *resolve_class_name(lily_parse_state *);
static int keyword_by_name(char *);

/** Type collection can be roughly dividied into two subparts. One half deals
    with general collection of types that either do or don't have a name. The
    other half deals with optional arguments (optargs) and optional argument
    value collection.
    A common thing you'll see mentioned throughout type-related code is the idea
    of a default type.
    Before any type is created, the type maker module checks to see if there is
    a type that describes what is trying to be made. If so, the existing type is
    returned.
    Some types have no subtypes, and thus only need a single type to describe
    them. This type is their 'default type'. **/

static void grow_optarg_stack(lily_parse_state *parser)
{
    parser->optarg_stack_size *= 2;
    parser->optarg_stack = lily_realloc(parser->optarg_stack,
            sizeof(uint16_t) * parser->optarg_stack_size);
}

/* So you've got a class that wants to have an optional argument. This takes
   that class, and grabs a valid default value for it. This is semi-tricky when
   it comes to booleans, because booleans need a word, but the word must be
   either `true` or `false`.
   The requirement that default values have to be constants is intentional, with
   the goal being to prevent complex expressions and/or emit-time evaluation of
   functions/etc.
   This is a helper function. No direct calls. */
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
    if (cls == symtab->boolean_class) {
        int key_id = keyword_by_name(lex->label);
        if (key_id != KEY_TRUE && key_id != KEY_FALSE)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "'%s' is not a valid default value for a boolean.\n",
                    lex->label);

        result = lily_get_boolean_literal(symtab, key_id == KEY_TRUE);
    }
    else if (expect == tk_word) {
        /* It's an enum. Allow any variant to be a default argument if that
           variant doesn't take arguments. Those variants have a backing literal
           which means they work like other arguments.
           Is the enum scoped? It should not matter: It's patently obvious what
           enum that this variant should be a member of. It's also consistent
           with match (which ignores scoping for the same reason). */
        lily_class *variant = lily_find_scoped_variant(cls, lex->label);
        if (variant == NULL)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "'%s' does not have a variant named '%s'.\n", cls->name, lex->label);

        if (variant->default_value == NULL)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Only variants that take no arguments can be default arguments.\n");

        result = variant->default_value;
    }
    else
        result = lex->last_literal;

    return result;
}

/* This takes a var that has been marked as being optional and collects a
   default value for it.
   Assumptions: var->type->cls is a valid optional argument class. */
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

/* This takes a class that takes a single subtype and creates a new type which
   wraps around the class sent. For example, send 'list' and an integer type to
   get 'list[integer]'. */
static lily_type *make_type_of_class(lily_parse_state *parser, lily_class *cls,
        lily_type *type)
{
    lily_tm_add(parser->tm, type);
    return lily_tm_make(parser->tm, 0, cls, 1);
}

/* This checks to see if 'type' got as many subtypes as it was supposed to. If
   it did not, then SyntaxError is raised.
   For now, this also includes an extra check. It attempts to ensure that the
   key of a hash is something that is hashable (or a generic type). */
static void ensure_valid_type(lily_parse_state *parser, lily_type *type)
{
    if (type->subtype_count != type->cls->generic_count &&
        type->cls->generic_count != -1)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Class %s expects %d type(s), but got %d type(s).\n",
                type->cls->name, type->cls->generic_count,
                type->subtype_count);

    /* Hack: This exists because Lily does not understand constraints. */
    if (type->cls == parser->symtab->hash_class) {
        lily_type *check_type = type->subtypes[0];
        if ((check_type->cls->flags & CLS_VALID_HASH_KEY) == 0 &&
            check_type->cls->id != SYM_CLASS_GENERIC)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "'^T' is not a valid hash key.\n", check_type);
    }
}

/* Call this if you need a type that may/may not have optargs/varargs, but no
   name is reqired. This is useful for, say, doing collection of optargs/varargs
   in nested parameter functions (`function(function(*integer))`).
   'flags' has TYPE_HAS_OPTARGS or TYPE_IS_VARARGS set onto it if either of
   those things was found. */
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

/* Call this if you have a need for a type that has a name attached. One example
   would be argument collection of functions `<name>: <type>`.
   'flags' has TYPE_HAS_OPTARGS or TYPE_IS_VARARGS set onto it if either of
   those things was found.
   Assumptions: lex->token should be lex->label (whatever <name> is). */
static lily_type *get_named_arg(lily_parse_state *parser, int *flags)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var = lily_find_var(parser->symtab, NULL, lex->label);
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

/* Call this if you just need a type but no optional argument stuff to go along
   with it. If there is any resolution needed (ex: `a::b::c`), then that is done
   here. This is relied upon by get_named_arg and get_nameless_arg (which add
   optarg/vararg functionality).
   You probably don't want to call this directly, unless you just need a type
   and it cannot be optargs/varargs (ex: `: <type>` of a var decl). */
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

/* Get a type represented by the name given. Largely used by dynaload. */
static lily_type *type_by_name(lily_parse_state *parser, char *name)
{
    lily_load_copy_string(parser->lex, "[api]", lm_no_tags, name);
    lily_lexer(parser->lex);
    lily_type *result = get_type(parser);
    lily_pop_lex_entry(parser->lex);

    return result;
}

/* This should be called when `[` is found after the name of a define/class. The
   purpose of this function is to collect the generic types and return how many
   were found. This is easy now, because Lily currently requires that generics
   are ordered from A...Z. */
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

/* This is called when creating a class and after any generics have been
   collected.
   If the class has generics, then the self type will be a type of the class
   which has all of those generics:
   `class Box[A]` == `Box[A]`
   `enum Either[A, B]` == `Either[A, B]`.
   If the class doesn't have generics, then the self type will be the default
   type of a class. */
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

    cls->self_type = result;

    return result;
}

/***
 *      ____                    _                 _ 
 *     |  _ \ _   _ _ __   __ _| | ___   __ _  __| |
 *     | | | | | | | '_ \ / _` | |/ _ \ / _` |/ _` |
 *     | |_| | |_| | | | | (_| | | (_) | (_| | (_| |
 *     |____/ \__, |_| |_|\__,_|_|\___/ \__,_|\__,_|
 *            |___/                                 
 */

static void create_new_class(lily_parse_state *);
static lily_class *dynaload_exception(lily_parse_state *, lily_import_entry *,
        const char *);

/** Lily is a statically-typed language, which carries benefits as well as
    drawbacks. One drawback is that creating a new function or a new var is
    quite costly. A var needs a type, and that type may include subtypes.
    Binding foreign functions includes creating a tie that the vm can later use
    to associate 'this foreign function has that value', a type, and a var.
    This can be rather wasteful if you're not going to use all of that. In fact,
    it's unlikely that you'll use all API functions, all builtin functions, and
    all builtin packages in a single program.

    Consider a call to string::lower. This can be invoked as either "".lower or
    string::lower. Since Lily is a statically-typed language, it's possible to
    know if something is going to be used, or if it won't through a combo of
    parse-time guessing (with static calls), and emit-time post-type-solving
    knowledge (with anything else).

    Lily's solution to the problem is to allow classes and modules to have seeds
    that describe what they will provide. These seeds are a static linked list
    that provide the useful information. For a foreign function, that would be
    the name, the type, and the C function to call.

    The loading is done by parser, either directly or when called from emitter.
    These functions below handle dynaloading in various areas. The parser will
    generally attempt to load existing vars before attempting to check for a
    dynaload.

    This may seem like overkill, but the memory saving is enormous. Furthermore,
    it allows Lily to (eventually) have a generous standard library without fear
    of wasting memory if such functions are loaded but never used.

    An unfortunate side-effect of this is that it requires a lot of forward
    definitions. Sorry about that. **/

/* This function is called when the current label could potentially be a module.
   If it is, then this function will continue digging until all of the modules
   have been seen.
   The result of this is the context from which to continue looking up. */
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

/* See if the given item has a dynaload table that contains the given name.
   Success: A seed with a name matching 'name'
   Failure: NULL
   Assumptions: 'item' is either a class or a module. */
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

/* This is used to collect class names. Trying to just get a class name isn't
   possible because there could be a module before the class name (`a::b::c`).
   To make things more complicated, there could be a dynaload of a class. */
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

        /* Is it a dynaload from the builtin/given package? */
        lily_base_seed *call_seed =
                find_dynaload_entry((lily_item *)search_import, lex->label);

        /* The active import could be a foreign package, which might have a
           dynaload entry. Try that. */
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

/* Load the function described by 'seed' into 'source'. 'source' should either
   be an import entry, or a class. */
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

/* This is a bit gross. It's used to dynaload classes that are derived directly
   from 'Exception'. 'import' is likely to be the builtin import, but could be a
   foreign module that wants to make a custom exception (ex: postgres). */
static lily_class *dynaload_exception(lily_parse_state *parser,
        lily_import_entry *import, const char *name)
{
    lily_symtab *symtab = parser->symtab;
    lily_import_entry *saved_active = symtab->active_import;
    lily_class *result;

    /* Hack: This exists because I originally thought it was really clever to
       get classes to boot without using internal functions directly. However,
       this is pretty awful. */

    /* This causes lookups and the class insertion to be done into the scope of
       whatever import that wanted this dynaload. This will make it so the
       dynaload lasts, instead of scoping out. */
    symtab->active_import = import;
    lily_msgbuf_flush(parser->msgbuf);
    lily_msgbuf_add_fmt(parser->msgbuf,
            "class %s(msg: string) < Exception(msg) { }\n", name);
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

/* Given a name, try to find that within the dynaload table of 'import' (which
   may be a class). If a seed is found, run it in the context of 'import' and
   return the result as an item. If nothing is found, return NULL. */
static lily_item *find_run_dynaload(lily_parse_state *parser,
        lily_import_entry *import, char *name)
{
    lily_import_entry *saved_active = parser->symtab->active_import;
    lily_symtab *symtab = parser->symtab;
    lily_item *result;

    lily_base_seed *seed = find_dynaload_entry((lily_item *)import, name);
    if (seed == NULL)
        return NULL;

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
        result = (lily_item *)new_var;
    }
    else if (seed->seed_type == dyna_function) {
        lily_var *new_var = dynaload_function(parser, (lily_item *)import, seed);
        result = (lily_item *)new_var;
    }
    else if (seed->seed_type == dyna_class) {
        lily_class *new_cls = lily_new_class_by_seed(symtab, seed);
        result = (lily_item *)new_cls;
    }
    else if (seed->seed_type == dyna_exception) {
        lily_class *new_cls = dynaload_exception(parser, import,
                seed->name);
        result = (lily_item *)new_cls;
    }

    symtab->active_import = saved_active;
    return result;
}

/* Given a class, attempt to find 'name' as a member of that class. The result
   may be that it's a property OR a var.
   The search order is: Property, methods in progress, existing methods, and
   dynaloads. There's no real importance to the search order here...aside from
   methods in progress needing to go before the rest. The reason is that methods
   in progress will be for the most recent class.
   If, say, there's a Two class being declared that inherits from One, then it
   will have a ::new already (One::new). However, the Two::new should be
   considered first. */
lily_item *lily_find_or_dl_member(lily_parse_state *parser, lily_class *cls,
        char *name)
{
    lily_prop_entry *prop = lily_find_property(cls, name);
    if (prop)
        return (lily_item *)prop;

    lily_var *var = lily_find_var(parser->symtab, NULL, name);
    if (var && var->parent == cls)
        return (lily_item *)var;

    var = lily_find_method(cls, name);
    if (var)
        return (lily_item *)var;

    /* This should return a var if it succeeds, as nested classes are not
       allowed. */
    var = (lily_var *)find_run_dynaload(parser, (lily_import_entry *)cls, name);
    if (var)
        return (lily_item *)var;

    return NULL;
}

/* This is called by the vm because the exception raised either was raised
   through a code or by a module seed. So the vm needs parser to find the class
   or dynaload it if it can't be found. One of those two should work.
   'import' is likely to be the builtin import, but could be one for a foreign
   module if a module wanted to make an exception (ex: postgres). */
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

/* This gets (up to) the first 8 bytes of a name and puts it into a numeric
   value. The numeric value is compared before comparing names to speed things
   up just a bit. */
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

/***
 *      _____                              _                 
 *     | ____|_  ___ __  _ __ ___  ___ ___(_) ___  _ __  ___ 
 *     |  _| \ \/ / '_ \| '__/ _ \/ __/ __| |/ _ \| '_ \/ __|
 *     | |___ >  <| |_) | | |  __/\__ \__ \ | (_) | | | \__ \
 *     |_____/_/\_\ .__/|_|  \___||___/___/_|\___/|_| |_|___/
 *                |_|                                        
 */

/** Expression handling is hard. It's largely broken up into three different
    parts. The first, here, is parsing an expression to make sure that the form
    is correct. The second, in ast, is making sure that expressions merge right.
    The last, in emitter, is making sure the types make sense and writing out
    the bytecode.

    Parser's job is to make sure that things have a relatively correct form. The
    expression should finish without a right side to a binary +, and shouldn't
    finish without a balance in ')' or ']', etc.

    Lambdas are not completely handled here. Lexer scoops their internal
    definition up as a string and hands it off to parser. As such, that part is
    not included here. Besides, it's long and complex anyway. **/


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

/* This handles expressions like `<type>::member`. */
static void expression_static_call(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    NEED_NEXT_TOK(tk_colon_colon)
    NEED_NEXT_TOK(tk_word)

    lily_item *item = lily_find_or_dl_member(parser, cls, lex->label);

    /* Breaking this down:
       If there's a var, then it needs to be a member of THIS exact class, not
       a subclass. I suspect that would be an unwelcome surprise.
       No properties. Properties need to be accessed through a dot, since they
       are per-instance. */

    if (item &&
        (
         (item->flags & ITEM_TYPE_VAR &&
          ((lily_var *)item)->parent != cls) ||
         (item->flags & ITEM_TYPE_PROPERTY)
        ))
        item = NULL;

    if (item) {
        lily_ast_push_static_func(parser->ast_pool, (lily_var *)item);
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

/* This handles all the simple keywords that map to a string/integer value. */
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

/* This is called when a class (enum or regular) is found. This determines how
   to handle the class: Either push the variant or run a static call, then
   updates the state. */
static void dispatch_word_as_class(lily_parse_state *parser, lily_class *cls,
        int *state)
{
    if (cls->flags & CLS_IS_VARIANT)
        lily_ast_push_variant(parser->ast_pool, cls);
    else
        expression_static_call(parser, cls);

    *state = ST_WANT_OPERATOR;
}

/* This function is to be called when 'func' could be a class method or just a
   plain function. The emitter's call handling special-cases tree_method to
   do an auto-injection of 'self'.  */
static void push_maybe_method(lily_parse_state *parser, lily_var *func)
{
    if (func->parent &&
        parser->class_self_type &&
        lily_class_greater_eq(func->parent, parser->class_self_type->cls))
        lily_ast_push_method(parser->ast_pool, func);
    else
        lily_ast_push_defined_func(parser->ast_pool, func);
}

/* This function takes a var and determines what kind of tree to put it into.
   The tree type is used by emitter to group vars into different types as a
   small optimization. */
static void dispatch_word_as_var(lily_parse_state *parser, lily_var *var,
        int *state)
{
    if (var->flags & SYM_NOT_INITIALIZED)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Attempt to use uninitialized value '%s'.\n",
                var->name);

    /* Defined functions have a depth of one, so they have to be first. */
    else if (var->flags & VAR_IS_READONLY)
        push_maybe_method(parser, var);
    else if (var->function_depth == 1)
        lily_ast_push_global_var(parser->ast_pool, var);
    else if (var->function_depth == parser->emit->function_depth)
        lily_ast_push_local_var(parser->ast_pool, var);
    else
        lily_ast_push_upvalue(parser->ast_pool, var);

    *state = ST_WANT_OPERATOR;
}

/* Something was dynaloaded. Push it into the ast and update state. */
static void dispatch_dynaload(lily_parse_state *parser, lily_item *dl_item,
        int *state)
{
    lily_ast_pool *ap = parser->ast_pool;

    if (dl_item->flags & ITEM_TYPE_VAR) {
        lily_var *v = (lily_var *)dl_item;
        if (v->flags & VAR_IS_READONLY)
            lily_ast_push_defined_func(ap, v);
        else
            lily_ast_push_global_var(ap, v);

        *state = ST_WANT_OPERATOR;
    }
    else
        dispatch_word_as_class(parser, (lily_class *)dl_item, state);
}

/* This is called by expression when there is a word. This is complicated,
   because a word could be a lot of things.  */
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
            lily_ast_push_method(parser->ast_pool, var);
            *state = ST_WANT_OPERATOR;
            return;
        }
    }

    if (search_entry == NULL)
        search_entry = symtab->builtin_import;

    lily_item *dl_result = find_run_dynaload(parser, search_entry, lex->label);

    if (dl_result) {
        dispatch_dynaload(parser, dl_result, state);
        return;
    }

    lily_raise(parser->raiser, lily_SyntaxError, "%s has not been declared.\n",
            lex->label);
}

/* This is called to handle `@<prop>` accesses. */
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

/* This makes sure that the current token is the right kind of token for closing
   the current tree. If it is not, then SyntaxError is raised. */
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

/* There's this annoying problem where 1-1 can be 1 - 1 or 1 -1. This is called
   if an operator is wanted but a digit is given instead. It checks to see if
   the numeric token can be broken up into an operator and a value, instead of
   just an operator. */
static int maybe_digit_fixup(lily_parse_state *parser)
{
    /* The lexer records where the last digit scan started. So check if it
       started with '+' or '-'. */
    lily_lex_state *lex = parser->lex;
    char ch = lex->input_buffer[lex->last_digit_start];
    int fixed = 0;

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
        fixed = 1;
    }

    return fixed;
}

/* This handles literals, and does that fixup thing if that's necessary. */
static void expression_literal(lily_parse_state *parser, int *state)
{
    lily_lex_state *lex = parser->lex;
    lily_token token = parser->lex->token;

    if (*state == ST_WANT_OPERATOR &&
         (token == tk_integer || token == tk_double)) {
        if (maybe_digit_fixup(parser) == 0)
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

/* Both comma and arrow do similar-ish things, so they're both handled here. The
   & 0x1 trick is used to detect even/odd-ness. A properly-formed hash should
   look like `[1 => 1, 2 => 2, 3 => 3...]`. If it isn't, then args_collected
   will be odd/even when it shouldn't be. */
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

/* Unary expressions! These are easy, because it's only two. */
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

/* This handles two rather different things. It could be an `x.y` access, OR
   `x.@(<type>)`. The emitter will have type information, so don't bother
   checking if either of them is correct. */
static void expression_dot(lily_parse_state *parser, int *state)
{
    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);
    if (lex->token == tk_word)
        lily_ast_push_oo_access(parser->ast_pool, parser->lex->label);
    else if (lex->token == tk_typecast_parenth) {
        lily_lexer(lex);
        lily_type *new_type = get_type(parser);
        lily_ast_enter_typecast(parser->ast_pool, new_type);
        lily_ast_leave_tree(parser->ast_pool);
    }

    *state = ST_WANT_OPERATOR;
}

/* This is the magic function that handles expressions. The states it uses are
   defined above. Most callers will use expression instead of this. */
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
                if (parser->ast_pool->save_depth == 0)
                    state = ST_DONE;
                else
                    state = ST_BAD_TOKEN;
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
        /* Make sure this case stays lower down. If it doesn't, then certain
           expressions will exit before they really should. */
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

/* This calls expression_raw demanding a value. If you need that (and most
   callers do), then use this. If you don't, then call it raw. */
static void expression(lily_parse_state *parser)
{
    expression_raw(parser, ST_DEMAND_VALUE);
}

/***
 *      _                    _         _           
 *     | |    __ _ _ __ ___ | |__   __| | __ _ ___ 
 *     | |   / _` | '_ ` _ \| '_ \ / _` |/ _` / __|
 *     | |__| (_| | | | | | | |_) | (_| | (_| \__ \
 *     |_____\__,_|_| |_| |_|_.__/ \__,_|\__,_|___/
 *                                                 
 */

static lily_var *get_named_var(lily_parse_state *, lily_type *);

/** Lambdas are really cool, but they're really difficult for Lily to parse.
    Since lambda arguments require type information, this means that they can't
    be solved up front like other values. Instead, parser collects the body of
    a lambda and hands it off to the emitter later on. When the emitter has type
    information, it's handed back to parser.

    There's a lot to contend with. There's currently no ability to supply an
    expected type to a lambda. The lambda may not provide complete types, and it
    may or may not want a value to be pushed back. If the lambda's target is a
    var, then it will have no type information at all (but returning a value is
    okay.

    Then there's the trouble of a lambda's return being the last thing that
    runs. While loops, if statements, for loops, and raise make this tough
    though. Worse, a huge if branch may not be at the very end. So, for now, any
    block is considered to just not return a value.

    Lambdas also can get statement to run, so various keywords have guards
    against being run within a lambda (ex: class decl, import, define, and
    more). Aside from that, lambdas can do much of what typical defines do. **/

/* Is this keyword a value that can be part of an expression? */
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

/* This runs through the body of a lambda, running any statements inside. The
   result of this function is the type of the last expression that was run.
   If the last thing was a block, or did not return a value, then NULL is
   returned. */
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

/* This is the main workhorse of lambda handling. It takes the lambda body and
   works through it. This is fairly complicated, because this happens during
   tree eval. As such, the current state has to be saved and a lambda has to be
   made too. When this is done, it has to build the resulting type of the lambda
   as well. */
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

/***
 *     __     __             
 *     \ \   / /_ _ _ __ ___ 
 *      \ \ / / _` | '__/ __|
 *       \ V / (_| | |  \__ \
 *        \_/ \__,_|_|  |___/
 *                           
 */

/** Var declaration gets a special block because there's a fair amount of
    complexity involved in it. They're slightly annoying because of the **/

/* This tries to make a var with the given type, but won't if a var with that
   name already exists. */
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

/* The same thing as get_named_var, but with a property instead. */
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

/* This is called when @<name> is given outside of a class or <name> is given at
   the top of a class. */
static void bad_decl_token(lily_parse_state *parser)
{
    char *message;

    if (parser->lex->token == tk_word)
        message = "Class properties must start with @.\n";
    else
        message = "Cannot use a class property outside of a constructor.\n";

    lily_raise(parser->raiser, lily_SyntaxError, message);
}

/* Syntax `var <name> [:<type>] = <value>
   This is where vars are handled. Providing a type is a nice thing, but not
   required. If there are modifiers, then they'll call this function with
   whatever modifiers that they'd like.

   All vars are required to have an initial value. A flag is set on them when
   they are created to prevent them from being used in their own initialization
   expression. */
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

        if (lex->token == tk_word) {
            sym = (lily_sym *)get_named_var(parser, NULL);
            sym->flags |= SYM_NOT_INITIALIZED;
            if (parser->emit->function_depth == 1)
                lily_ast_push_global_var(parser->ast_pool, (lily_var *)sym);
            else
                lily_ast_push_local_var(parser->ast_pool, (lily_var *)sym);
        }
        else {
            sym = (lily_sym *)get_named_property(parser, NULL, flags);
            lily_ast_push_property(parser->ast_pool, (lily_prop_entry *)sym);
        }

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

        if (want_token == tk_prop_word &&
            sym->type->flags & TYPE_MAYBE_CIRCULAR)
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

/***
 *      ____  _        _                            _       
 *     / ___|| |_ __ _| |_ ___ _ __ ___   ___ _ __ | |_ ___ 
 *     \___ \| __/ _` | __/ _ \ '_ ` _ \ / _ \ '_ \| __/ __|
 *      ___) | || (_| | ||  __/ | | | | |  __/ | | | |_\__ \
 *     |____/ \__\__,_|\__\___|_| |_| |_|\___|_| |_|\__|___/
 *                                                          
 */

/** The rest of this focuses on handling handling keywords and blocks. Much of
    this is straightforward and kept in small functions that rely on the above
    stuff. As such, there's no real special attention paid to the rest.
    Near the bottom is parser_loop, which is the entry point of the parser. **/

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

static void var_handler(lily_parse_state *parser, int multi)
{
    parse_var(parser, 0);
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

/* This is a magic function that will either run one expression or multiple
   ones. If it's going to run multiple ones, then it stops on eof or '}'. */
static void statement(lily_parse_state *parser, int multi)
{
    int key_id;
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
                expression(parser);
                lily_emit_eval_expr(parser->emit, parser->ast_pool);
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

/* This handles the '{' ... '}' part for blocks that are multi-lined. */
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

static void else_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_change_block_to(parser->emit, block_if_else);
    NEED_CURRENT_TOK(tk_colon)
    lily_lexer(lex);

    statement(parser, multi);
}

/* This handles keywords that start expressions (true, false, __line__, etc.) */
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

/* Call this to make sure there's no obviously-dead code. */
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

static void continue_handler(lily_parse_state *parser, int multi)
{
    lily_emit_continue(parser->emit);

    if (multi && parser->lex->token != tk_right_curly)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'continue' not at the end of a multi-line block.\n");
}

static void break_handler(lily_parse_state *parser, int multi)
{
    lily_emit_break(parser->emit);

    if (multi && parser->lex->token != tk_right_curly)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'break' not at the end of a multi-line block.\n");
}

static void line_kw_handler(lily_parse_state *parser, int multi)
{
    do_keyword(parser, KEY__LINE__);
}

static void file_kw_handler(lily_parse_state *parser, int multi)
{
    do_keyword(parser, KEY__FILE__);
}

static void function_kw_handler(lily_parse_state *parser, int multi)
{
    do_keyword(parser, KEY__FUNCTION__);
}

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

    lily_var *for_start, *for_end;
    lily_sym *for_step;

    for_start = parse_for_range_value(parser, "(for start)");

    NEED_CURRENT_TOK(tk_three_dots)
    lily_lexer(lex);

    for_end = parse_for_range_value(parser, "(for end)");

    if (lex->token == tk_word) {
        if (strcmp(lex->label, "by") != 0)
            lily_raise(parser->raiser, lily_SyntaxError,
                       "Expected 'by', not '%s'.\n", lex->label);

        lily_lexer(lex);
        for_step = (lily_sym *)parse_for_range_value(parser, "(for step)");
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

/* This handles everything needed to create a class, up until figuring out
   inheritance (if that's indicated). */
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

/* This is called when one class wants to inherit from another. It makes sure
   that the inheritance is valid, and then runs the expression necessary to make
   the inheritance work. */
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

/* This is called when a variant takes arguments. It parses those arguments to
   spit out the 'variant_type' conversion type of the variant. These types are
   internally really going to make a tuple instead of being a call. */
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

        variant_cls->variant_type = variant_type;

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

/* This is the entry point of the parser. It parses the thing that it was given
   and then runs the code. This shouldn't be called directly, but instead by
   one of the lily_parse_* functions that will set it up right. */
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
            /* The emitter's type_block is not always used, and thus is not
               always allocated. Make sure the vm has it if it's needed. */
            parser->vm->type_block = parser->emit->type_block;
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

/* This is provided for runners (such as the standalone runner provided in the
   run directory). This puts together the current error message so that the
   runner is able to use it. The error message (and stack) are returned in full
   in the string.
   The string returned is a shallow reference (it's really
   parser->msgbuf->message). If the caller wants to keep the message, then the
   caller needs to copy it. If the caller does not, the message will get blasted
   by the next run. */
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

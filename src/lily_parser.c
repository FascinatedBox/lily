#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lily_config.h"
#include "lily_library.h"
#include "lily_parser.h"
#include "lily_parser_tok_table.h"
#include "lily_keyword_table.h"
#include "lily_opcode.h"
#include "lily_string_pile.h"

#include "lily_pkg_builtin.h"
#include "lily_pkg_sys.h"

#include "lily_api_alloc.h"
#include "lily_api_value_ops.h"
#include "lily_api_options.h"

#define NEED_NEXT_TOK(expected) \
lily_lexer(lex); \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_SyntaxError, "Expected '%s', not '%s'.\n", \
               tokname(expected), tokname(lex->token));

#define NEED_CURRENT_TOK(expected) \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_SyntaxError, "Expected '%s', not '%s'.\n", \
               tokname(expected), tokname(lex->token));

/***
 *      ____       _               
 *     / ___|  ___| |_ _   _ _ __  
 *     \___ \ / _ \ __| | | | '_ \ 
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/ 
 *                          |_|    
 */

static void statement(lily_parse_state *, int);
static lily_type *type_by_name(lily_parse_state *, const char *);
static lily_package *new_package(lily_parse_state *, const char *,
        const char *, const char **);

/* This sets up the core of the interpreter. It's pretty rough around the edges,
   especially with how the parser is assigning into all sorts of various structs
   when it shouldn't. */
lily_parse_state *lily_new_parse_state(lily_options *options)
{
    lily_parse_state *parser = lily_malloc(sizeof(lily_parse_state));
    parser->data = options->data;
    parser->package_top = NULL;
    parser->package_start = NULL;

    lily_raiser *raiser = lily_new_raiser();

    parser->first_pass = 1;
    parser->generic_count = 0;
    parser->class_self_type = NULL;
    parser->raiser = raiser;
    parser->exception_type = NULL;
    parser->first_expr = lily_new_expr_state();
    parser->symtab = lily_new_symtab();

    lily_register_pkg_builtin(parser);
    lily_set_first_package(parser->symtab, parser->package_top);
    lily_init_pkg_builtin(parser->symtab);

    parser->emit = lily_new_emit_state(parser->symtab, raiser);
    parser->lex = lily_new_lex_state(options, raiser);
    parser->vm = lily_new_vm_state(options, raiser);
    parser->msgbuf = lily_new_msgbuf();
    parser->options = options;
    parser->optarg_stack = lily_new_buffer_u16(4);
    parser->expr = parser->first_expr;
    parser->foreign_values = lily_new_value_stack();

    /* Here's the awful part where parser digs in and links everything that different
       sections need. */
    parser->tm = parser->emit->tm;

    parser->vm->symtab = parser->symtab;
    parser->vm->vm_buffer = parser->raiser->msgbuf;
    parser->vm->parser = parser;

    parser->symtab->lex_linenum = &parser->lex->line_num;

    parser->expr->lex_linenum = &parser->lex->line_num;

    parser->emit->lex_linenum = &parser->lex->line_num;
    parser->emit->symtab = parser->symtab;
    parser->emit->parser = parser;

    parser->lex->symtab = parser->symtab;

    parser->expr_strings = parser->emit->expr_strings;

    /* All code that isn't within a function is grouped together in a special
       function called __main__. Since that function is the first kind of a
       block, it needs a special function to initialize that block. */
    lily_emit_enter_main(parser->emit);

    /* This type represents a function that has no input and no outputs. This is
       used when creating new functions so that they have a type. */
    parser->default_call_type = parser->symtab->main_var->type;

    lily_package *main_package = new_package(parser, "", NULL, NULL);
    parser->main_module = main_package->first_module;

    /* This puts __main__ into the scope of the first thing to be executed, and
       makes it the context for var/class/etc. searching. */
    parser->symtab->main_function->module = parser->main_module;
    parser->symtab->active_module = parser->main_module;

    if (options->allow_sys)
        lily_pkg_sys_init(parser, options);

    parser->executing = 0;

    return parser;
}

void lily_free_parse_state(lily_parse_state *parser)
{
    lily_free_raiser(parser->raiser);

    /* The root expression is the only one that is allocated (the rest are on
       stack and thus vanish when the stack is gone). */
    lily_free_expr_state(parser->first_expr);

    lily_free_vm(parser->vm);

    lily_free_symtab(parser->symtab);

    lily_free_lex_state(parser->lex);

    lily_free_emit_state(parser->emit);

    lily_free_buffer_u16(parser->optarg_stack);

    /* The path for the first module is always a shallow copy of the loadname
       that was sent. Make sure that doesn't get free'd. */
    parser->package_start->root_next->first_module->path = NULL;

    lily_package *package_iter = parser->package_start;
    lily_package *package_next = NULL;
    while (package_iter) {
        lily_module_entry *module_iter = package_iter->first_module;
        lily_module_entry *module_next = NULL;

        while (module_iter) {
            lily_module_link *link_iter = module_iter->module_chain;
            lily_module_link *link_next = NULL;
            while (link_iter) {
                link_next = link_iter->next_module;
                lily_free(link_iter->as_name);
                lily_free(link_iter);
                link_iter = link_next;
            }

            module_next = module_iter->root_next;

            if (module_iter->handle)
                lily_library_free(module_iter->handle);

            lily_free(module_iter->path);
            lily_free(module_iter->dirname);
            lily_free(module_iter->loadname);
            lily_free(module_iter->cid_table);
            lily_free(module_iter);

            module_iter = module_next;
        }

        lily_package_link *link_iter = package_iter->linked_packages;
        lily_package_link *link_next = NULL;
        while (link_iter) {
            link_next = link_iter->next;
            lily_free(link_iter);
            link_iter = link_next;
        }

        package_next = package_iter->root_next;
        lily_free(package_iter->name);
        lily_free(package_iter);
        package_iter = package_next;
    }

    lily_free_value_stack(parser->foreign_values);
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

/** Lily has two keywords for loading code: `import` and `use`.

    To understand why there are two keywords, one must first understand how Lily
    looks at code. Here are two terms:

    A module is a 1:1 mapping between a source file, and some code. The names
    inside of a module are accessible through using it as a namespace. For
    example 'import foo' will give everything in foo, using 'foo.*' as a
    namespace. Modules are loaded relative to the current module.

    A package, however, is designed to hold 1+ modules inside of itself. The
    first module can 'use' other packages to bring them into scope, and import
    other modules. The modules, classes, and definitions that are available in
    the first module are available through the name of the package.

    There are some rules. Within a package, only the first module can use
    subpackages. In doing so, one can look at the first package, and have a
    good idea of what it requires. Similarly, the exports of a package are what
    is visible from the first module.

    Additionally, a Lily runner may also provide packages. However, those
    packages must be explicitly use'd. This allows a script designed for, say,
    the mod_lily runner to be moved and run from the normal 'lily' runner. All
    one need to is provide a package with the same api that mod_lily provides,
    and the package runs without a hitch.

    It should be also be noted that the first file loaded is considered the
    first module of a nameless root package. Therefore, 'use' is allowed in the
    first file loaded, but not subsequent ones. **/

static void set_module_names_by_path(lily_module_entry *module,
        const char *path)
{
    if (path[0] == '[') {
        module->loadname = lily_malloc(1);
        module->loadname[0] = '\0';
        module->dirname = lily_malloc(3);
        strcpy(module->dirname, "." LILY_PATH_SLASH);
        module->cmp_len = 0;
    }
    else {
        const char *slash = strrchr(path, LILY_PATH_CHAR);

        if (slash == NULL) {
            module->dirname = lily_malloc(1);
            module->dirname[0] = '\0';
            slash = path;
        }
        else {
            slash += 1;
            int bare_len = slash - path;
            module->dirname = lily_malloc(bare_len + 1);

            strncpy(module->dirname, path, bare_len);
            module->dirname[bare_len] = '\0';
        }

        char *dot = strrchr(slash, '.');
        int load_len = dot - slash;

        module->loadname = lily_malloc(load_len + 1);
        strncpy(module->loadname, slash, load_len);
        module->loadname[load_len] = '\0';

        module->cmp_len = strlen(module->dirname) + load_len;
    }
}

static lily_module_entry *new_module(const char *path,
        const char **dynaload_table)
{
    lily_module_entry *module = lily_malloc(sizeof(lily_module_entry));

    if (path) {
        module->path = lily_malloc(strlen(path) + 1);
        strcpy(module->path, path);

        set_module_names_by_path(module, path);
    }
    else {
        module->loadname = NULL;
        module->dirname = NULL;
        module->path = NULL;
        module->cmp_len = 0;
    }

    module->dynaload_table = dynaload_table;

    if (dynaload_table && dynaload_table[0][0]) {
        unsigned char cid_count = dynaload_table[0][0];

        module->cid_table = lily_malloc(cid_count * sizeof(uint16_t));
        memset(module->cid_table, 0, cid_count * sizeof(uint16_t));
    }
    else
        module->cid_table = NULL;

    module->root_next = NULL;
    module->module_chain = NULL;
    module->class_chain = NULL;
    module->var_chain = NULL;
    module->handle = NULL;
    module->loader = NULL;
    module->item_kind = ITEM_TYPE_MODULE;

    return module;
}

/* This creates a new package using the name provided. The package is
   intentionally NOT linked anywhere, even to the first import. The reason for
   this is that it prevents scripts from assuming that a package exists and then
   breaking when another runner doesn't provide it. */
void lily_register_package(lily_parse_state *parser, const char *name,
        const char **dynaload_table, lily_loader loader)
{
    lily_package *package = new_package(parser, name, NULL, dynaload_table);
    package->first_module->loader = loader;
}

/* This adds 'to_link' as an entry within 'target' so that 'target' is able to
   reference it later on. If 'as_name' is not NULL, then 'to_link' will be
   available through that name. Otherwise, it will be available as the name it
   actually has. */
static void link_module_to(lily_module_entry *target, lily_module_entry *to_link,
        const char *as_name)
{
    lily_module_link *new_link = lily_malloc(sizeof(lily_module_link));
    char *link_name;
    if (as_name == NULL)
        link_name = NULL;
    else {
        link_name = lily_malloc(strlen(as_name) + 1);
        strcpy(link_name, as_name);
    }

    new_link->module = to_link;
    new_link->next_module = target->module_chain;
    new_link->as_name = link_name;

    target->module_chain = new_link;
}

static lily_package *new_empty_package(lily_parse_state *parser,
        const char *name)
{
    lily_package *package = lily_malloc(sizeof(lily_package));

    if (parser->package_top) {
        parser->package_top->root_next = package;
        parser->package_top = package;
    }
    else {
        parser->package_start = package;
        parser->package_top = package;
    }

    package->linked_packages = NULL;
    package->root_next = NULL;
    package->name = lily_malloc(strlen(name) + 1);
    strcpy(package->name, name);

    return package;
}

static lily_package *new_package(lily_parse_state *parser,
        const char *name, const char *module_path, const char **dynaload_table)
{
    lily_package *package = new_empty_package(parser, name);
    lily_module_entry *module = new_module(module_path, dynaload_table);

    package->first_module = module;
    module->parent = package;

    return package;
}

static lily_module_entry *load_file(lily_parse_state *parser, const char *path)
{
    lily_module_entry *result = NULL;
    if (lily_try_load_file(parser->lex, path))
        result = new_module(path, NULL);

    return result;
}

static lily_module_entry *load_library(lily_parse_state *parser,
        const char *path)
{
    lily_module_entry *result = NULL;
    lily_library *library = lily_library_load(path);
    if (library) {
        result = new_module(path, library->dynaload_table);
        result->handle = library->source;

        lily_free(library);
    }

    return result;
}

typedef struct {
    const char *suffix;
    lily_module_entry *(*load_fn)(lily_parse_state *, const char *);
} module_loader;

static module_loader builtin_module_loaders[] =
{
    {".lly",          load_file},
    {LILY_LIB_SUFFIX, load_library}
};

/* Try loading 'name' using 'dirpath' as a prefix to it. Attempt to load 'name'
   using one of the loaders mentioned above, which will use either the .lly or
   the appropriate library suffix. */
static lily_module_entry *load_module(lily_parse_state *parser,
        const char *dirpath, const char *name)
{
    lily_module_entry *result;

    lily_msgbuf *msgbuf = parser->msgbuf;
    lily_msgbuf_flush(parser->msgbuf);
    int i;

    for (i = 0;i < 2;i++) {
        module_loader l = builtin_module_loaders[i];

        lily_msgbuf_add_fmt(msgbuf, "%s%s%s", dirpath, name, l.suffix);
        result = l.load_fn(parser, msgbuf->message);
        lily_msgbuf_flush(msgbuf);

        if (result)
            break;
    }

    if (result == NULL) {
        lily_msgbuf_add_fmt(msgbuf, "Cannot import '%s':\n", name);
        for (i = 0;i < 2;i++) {
            module_loader l = builtin_module_loaders[i];
            lily_msgbuf_add_fmt(msgbuf, "    no file '%s%s%s'\n",
                    dirpath, name, l.suffix);
        }

        lily_raise(parser->raiser, lily_SyntaxError, msgbuf->message);
    }

    /* Put this module in the current package. */
    lily_module_entry *active = parser->symtab->active_module;
    result->root_next = active->root_next;
    active->root_next = result;

    result->parent = active->parent;

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
static int constant_by_name(const char *);

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

/* Given a var, collect the optional argument that goes with it. This will push
   information to parser's optarg_stack to link the value to the var. The token
   is expected to start on the '=', and will be set at the token after the
   optional value. */
static void collect_optarg_for(lily_parse_state *parser, lily_var *var)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;
    lily_token expect;
    lily_buffer_u16 *optarg_stack = parser->optarg_stack;
    lily_class *cls = var->type->cls;

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

    NEED_CURRENT_TOK(tk_equal)
    NEED_NEXT_TOK(expect)

    lily_u16_write_1(optarg_stack, var->reg_spot);

    if (cls == symtab->boolean_class) {
        int key_id = constant_by_name(lex->label);
        if (key_id != CONST_TRUE && key_id != CONST_FALSE)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "'%s' is not a valid default value for a Boolean.\n",
                    lex->label);

        lily_u16_write_2(optarg_stack, o_get_boolean, key_id == CONST_TRUE);
    }
    else if (expect == tk_word) {
        /* It's an enum. Allow any variant to be a default argument if that
           variant doesn't take arguments. Those variants have a backing literal
           which means they work like other arguments.
           Is the enum scoped? It should not matter: It's patently obvious what
           enum that this variant should be a member of. It's also consistent
           with match (which ignores scoping for the same reason). */
        lily_variant_class *variant = lily_find_scoped_variant(cls, lex->label);
        if (variant == NULL)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "'%s' does not have a variant named '%s'.\n", cls->name, lex->label);

        if ((variant->flags & CLS_EMPTY_VARIANT) == 0)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Only variants that take no arguments can be default arguments.\n");

        lily_u16_write_2(optarg_stack, o_get_readonly,
                variant->default_value->reg_spot);
    }
    else if (expect != tk_integer) {
        lily_u16_write_2(optarg_stack, o_get_readonly,
                lex->last_literal->reg_spot);
    }
    else if (lex->last_integer <= INT16_MAX &&
             lex->last_integer >= INT16_MIN) {
        lily_u16_write_2(optarg_stack, o_get_integer,
                (uint16_t)lex->last_integer);
    }
    else {
        lily_literal *lit = lily_get_integer_literal(symtab, lex->last_integer);
        lily_u16_write_2(optarg_stack, o_get_readonly, lit->reg_spot);
    }

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

static lily_class *get_scoop_class(lily_parse_state *parser, int which)
{
    if (which > 2)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Numeric scoop type must be between 0 and 2.");

    lily_class *old_class_iter = parser->symtab->old_class_chain;
    int id = UINT16_MAX - which;

    while (old_class_iter) {
        if (old_class_iter->id == id)
            break;

        old_class_iter = old_class_iter->next;
    }

    return old_class_iter;
}

/* Flags is initially set to this is getting scoop types is A-OK. This is set to
   an arbitrarily high value so that it doesn't clash with type flags. */
#define F_SCOOP_OK 0x4000

/* Call this if you need a type that may/may not have optargs/varargs, but no
   name is reqired. This is useful for, say, doing collection of optargs/varargs
   in nested parameter functions (`function(function(*integer))`).
   'flags' is updated with information about optargs/varargs/scoop if one of
   those was collected. */
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
    else if (type->flags & TYPE_HAS_SCOOP) {
        if ((*flags & F_SCOOP_OK) == 0)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Numeric scooping types only available to the backend.\n");

        *flags |= TYPE_HAS_SCOOP;
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
   with it. If there is any resolution needed (ex: `a.b.c`), then that is done
   here. This is relied upon by get_named_arg and get_nameless_arg (which add
   optarg/vararg functionality).
   You probably don't want to call this directly, unless you just need a type
   and it cannot be optargs/varargs (ex: `: <type>` of a var decl). */
static lily_type *get_type(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_type *result;
    lily_class *cls = NULL;

    if (lex->token == tk_word)
        cls = resolve_class_name(parser);
    else if (lex->token == tk_integer)
        cls = get_scoop_class(parser, lex->last_integer);
    else {
        NEED_CURRENT_TOK(tk_word)
    }

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
static lily_type *type_by_name(lily_parse_state *parser, const char *name)
{
    lily_load_copy_string(parser->lex, lm_no_tags, name);
    lily_lexer(parser->lex);
    lily_type *result = get_type(parser);
    lily_pop_lex_entry(parser->lex);

    return result;
}

/* This is called at the start of a class or define. If '[' is present, then
   generics are collected. They currently are required be ordered from A to Z
   and be finished with ']'. If '[' is not present, then generics are set to
   the fallback value passed.
   After a generic count is established, parser's generic count and symtab's
   generic count are both updated. */
static void collect_generics_or(lily_parse_state *parser, int fallback)
{
    lily_lex_state *lex = parser->lex;
    int new_count;

    if (lex->token == tk_left_bracket) {
        char name[] = "A";
        char ch = name[0];

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
        new_count = seen;
    }
    else
        new_count = fallback;

    parser->generic_count = new_count;
    lily_update_symtab_generics(parser->symtab, new_count);
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

static void parse_class_body(lily_parse_state *, lily_class *);
static lily_class *find_run_class_dynaload(lily_parse_state *,
        lily_module_entry *, const char *);
static void parse_variant_header(lily_parse_state *, lily_variant_class *);
static lily_class *parse_enum(lily_parse_state *, int);
static lily_item *try_toplevel_dynaload(lily_parse_state *, lily_module_entry *,
        const char *);

static void init_expr_state(lily_parse_state *, lily_expr_state *);
static void fini_expr_state(lily_parse_state *);

/* [0...1] of a dynaload are used for header information. */
#define DYNA_NAME_OFFSET 2

/** Lily is a statically-typed language, which carries benefits as well as
    drawbacks. One drawback is that creating a new function or a new var is
    quite costly. A var needs a type, and that type may include subtypes.
    Binding foreign functions involves creating a lily_value with the contents
    of the foreign function inside. This can be rather wasteful if you're not
    going to use all of that. In fact, it's unlikely that you'll use all API
    functions, all builtin functions, and all builtin packages in a single
    program.

    Consider a call to String.lower. This can be invoked as either "".lower or
    String.lower. Since Lily is a statically-typed language, it's possible to
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

/* This function scans through the first line in the dynaload table to find the
   cid entries listed. For each of those cid entries, the ones currently
   available are loaded into the appropriate place in the cid table. */
static void update_cid_table(lily_parse_state *parser, lily_module_entry *m)
{
    const char *cid_entry = m->dynaload_table[0] + 1;
    int counter = 0;
    int stop = cid_entry[-1];
    uint16_t *cid_table = m->cid_table;
    lily_symtab *symtab = parser->symtab;
    lily_module_entry *builtin = parser->package_start->first_module;

    while (counter < stop) {
        if (cid_table[counter] == 0) {
            lily_class *cls = lily_find_class(symtab, m, cid_entry);
            if (cls == NULL)
                cls = lily_find_class(symtab, builtin, cid_entry);

            if (cls)
                cid_table[counter] = cls->id;
        }
        cid_entry += strlen(cid_entry) + 1;
        counter++;
    }
}

static void update_all_cid_tables(lily_parse_state *parser)
{
    lily_package *package_iter = parser->package_start;
    while (package_iter) {
        lily_module_entry *entry_iter = package_iter->first_module;
        while (entry_iter) {
            if (entry_iter->cid_table)
                update_cid_table(parser, entry_iter);
            
            entry_iter = entry_iter->root_next;
        }

        package_iter = package_iter->root_next;
    }
}

/* This function is called when the current label could potentially be a module.
   If it is, then this function will continue digging until all of the modules
   have been seen.
   The result of this is the context from which to continue looking up. */
static lily_module_entry *resolve_module(lily_parse_state *parser)
{
    lily_module_entry *result = NULL, *search_entry = NULL;
    lily_symtab *symtab = parser->symtab;
    lily_lex_state *lex = parser->lex;
    lily_package *package;

    package = lily_find_package(parser->symtab->active_module, lex->label);
    if (package)
        search_entry = package->first_module;
    else
        search_entry = lily_find_module(symtab, result, lex->label);

    while (search_entry) {
        result = search_entry;
        NEED_NEXT_TOK(tk_dot)
        NEED_NEXT_TOK(tk_word)
        search_entry = lily_find_module(symtab, result, lex->label);
    }

    return result;
}

/* This is used to collect class names. Trying to just get a class name isn't
   possible because there could be a module before the class name (`a.b.c`).
   To make things more complicated, there could be a dynaload of a class. */
static lily_class *resolve_class_name(lily_parse_state *parser)
{
    lily_symtab *symtab = parser->symtab;
    lily_lex_state *lex = parser->lex;

    NEED_CURRENT_TOK(tk_word)

    lily_module_entry *search_module = resolve_module(parser);
    lily_class *result = lily_find_class(symtab, search_module, lex->label);
    if (result == NULL) {
        if (search_module == NULL)
            search_module = symtab->builtin_module;

        if (search_module->dynaload_table)
            result = find_run_class_dynaload(parser, search_module, lex->label);

        if (result == NULL && symtab->active_module->dynaload_table)
            result = find_run_class_dynaload(parser, symtab->active_module,
                    lex->label);

        if (result == NULL)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Class '%s' does not exist.\n", lex->label);
    }

    return result;
}


static lily_var *dynaload_function(lily_parse_state *parser,
        lily_module_entry *m, lily_class *cls, int dyna_index)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;
    lily_var *call_var;

    const char *entry = m->dynaload_table[dyna_index];
    const char *name = entry + DYNA_NAME_OFFSET;
    const char *body = name + strlen(name) + 1;

    lily_module_entry *save_active = parser->symtab->active_module;
    int save_generics = parser->generic_count;

    lily_load_str(lex, lm_no_tags, body);
    lily_lexer(lex);

    lily_item *source;
    if (cls == NULL)
        source = (lily_item *)m;
    else
        source = (lily_item *)cls;

    lily_foreign_func func;

    if (m->loader)
        func = (lily_foreign_func)m->loader(parser->options, m->cid_table,
                dyna_index);
    else {
        lily_msgbuf *msgbuf = parser->msgbuf;
        lily_msgbuf_flush(msgbuf);

        char *cls_name = "";
        if (cls)
            cls_name = cls->name;

        lily_msgbuf_add_fmt(msgbuf, "lily_%s_%s_%s", m->loadname, cls_name,
                name);

        func = (lily_foreign_func)lily_library_get(m->handle, msgbuf->message);
    }

    parser->symtab->active_module = m;
    collect_generics_or(parser, 0);

    int result_pos = parser->tm->pos;
    int i = 1;
    int flags = 0 | F_SCOOP_OK;

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

    flags &= ~F_SCOOP_OK;
    lily_type *type = lily_tm_make(parser->tm, flags,
            parser->symtab->function_class, i);

    call_var = lily_emit_new_tied_dyna_var(parser->emit, func, source, type,
            name);

    lily_update_symtab_generics(symtab, save_generics);

    lily_pop_lex_entry(lex);

    parser->generic_count = save_generics;
    parser->symtab->active_module = save_active;

    return call_var;
}

/* This dynaloads an enum that is represented by 'seed' with 'import' as the
   context. The result of this is the enum class that was dynaloaded. */
static lily_class *dynaload_enum(lily_parse_state *parser, lily_module_entry *m,
        int dyna_index)
{
    const char **table = m->dynaload_table;
    const char *entry = table[dyna_index];
    const char *name = entry + DYNA_NAME_OFFSET;
    int entry_index = dyna_index;

    lily_msgbuf *msgbuf = parser->msgbuf;
    lily_msgbuf_flush(msgbuf);
    lily_msgbuf_add(msgbuf, name);
    lily_msgbuf_add(msgbuf, name + strlen(name) + 1);
    lily_msgbuf_add_char(msgbuf, '{');

    do {
        entry_index++;
        entry = table[entry_index];
    } while (entry[0] != 'V');

    do {
        name = entry + DYNA_NAME_OFFSET;
        lily_msgbuf_add(msgbuf, name);
        lily_msgbuf_add(msgbuf, name + strlen(name) + 1);
        lily_msgbuf_add_char(msgbuf, ' ');

        entry_index++;
        entry = table[entry_index];
    } while (entry[0] == 'V');

    lily_msgbuf_add_char(msgbuf, '}');

    lily_load_copy_string(parser->lex, lm_no_tags, msgbuf->message);
    lily_lexer(parser->lex);

    int save_next_class_id;
    /* Option and Either have specific ids set aside for them so they don't need
       to be included in cid tables.
       The id must be set -before- parsing the enum, because variant default
       values rely on the id of an enum. If it's fixed later, they'll have the
       wrong id, and possibly crash. */
    if (m->parent == parser->package_start) {
        save_next_class_id = parser->symtab->next_class_id;

        name = table[dyna_index] + DYNA_NAME_OFFSET;
        if (name[0] == 'O')
            parser->symtab->next_class_id = SYM_CLASS_OPTION;
        else
            parser->symtab->next_class_id = SYM_CLASS_EITHER;
    }
    else
        save_next_class_id = 0;

    lily_class *result = parse_enum(parser, 1);

    result->dyna_start = dyna_index + 1;

    if (save_next_class_id)
        parser->symtab->next_class_id = save_next_class_id;

    lily_pop_lex_entry(parser->lex);

    return result;
}

/* Dynaload a variant, represented by 'seed', into the context 'import'. The
   result of this is the variant. As a side-effect, this calls dynaload_enum to
   ensure all variants of the parent enum are loaded. */
static lily_class *dynaload_variant(lily_parse_state *parser,
        lily_module_entry *m, int dyna_index)
{
    int enum_pos = dyna_index - 1;
    const char **table = m->dynaload_table;
    const char *entry = table[dyna_index - 1];
    while (entry[0] != 'E') {
        entry = table[enum_pos];
        enum_pos--;
    }

    entry = table[dyna_index];
    dynaload_enum(parser, m, enum_pos + 1);
    return lily_find_class(parser->symtab, m, entry + DYNA_NAME_OFFSET);
}

static lily_class *dynaload_class(lily_parse_state *parser,
        lily_module_entry *m, int dyna_index)
{
    const char *entry = m->dynaload_table[dyna_index];
    lily_class *cls = lily_new_class(parser->symtab, entry + 2);

    cls->flags |= CLS_IS_BUILTIN;
    cls->dyna_start = dyna_index;

    lily_tm_make_default_for(parser->tm, cls);
    return cls;
}

static lily_class *dynaload_bootstrap(lily_parse_state *parser,
        lily_module_entry *m, int dyna_index)
{
    const char *entry = m->dynaload_table[dyna_index];
    const char *name = entry + DYNA_NAME_OFFSET;
    const char *body = name + strlen(name) + 1;

    lily_load_str(parser->lex, lm_no_tags, body);
    lily_class *cls = lily_new_class(parser->symtab, name);
    cls->dyna_start = dyna_index + 1;
    if (m->parent == parser->package_start) {
        if (strcmp(name, "Exception") == 0) {
            parser->symtab->next_class_id--;
            cls->id = SYM_CLASS_EXCEPTION;
        }
        else if (strcmp(name, "Tainted") == 0) {
            parser->symtab->next_class_id--;
            cls->id = SYM_CLASS_TAINTED;
        }
        else if (strstr(name, "Error")) {
            /* This is a little hacky. It loads exceptions at specific indexes
               so that the vm can check by id if the exception exists to raise
               it during runtime.
               For the moment, exceptions are strictly after Exception, and all
               have Error as their suffix. */
            int ex_index = dyna_index - 1;
            while (1) {
                const char *ex_str = m->dynaload_table[ex_index] +
                        DYNA_NAME_OFFSET;

                if (*ex_str == 'E' && strcmp("Exception", ex_str) == 0)
                    break;

                ex_index--;
            }

            ex_index++;

            /* This depends on a three things:
             * Dynaloaded exceptions always come after Exception.
             * Their ids also come after Exception.
             * Space is reserved for their ids, in order. */
            parser->symtab->next_class_id--;
            cls->id = SYM_CLASS_EXCEPTION + (cls->dyna_start - ex_index);
        }
    }

    lily_expr_state es;

    init_expr_state(parser, &es);
    parse_class_body(parser, cls);
    fini_expr_state(parser);

    lily_pop_lex_entry(parser->lex);

    return cls;
}

static lily_item *run_dynaload(lily_parse_state *parser, lily_module_entry *m,
        int dyna_pos)
{
    lily_symtab *symtab = parser->symtab;
    lily_item *result;

    char letter = m->dynaload_table[dyna_pos][0];
    lily_module_entry *saved_active = parser->symtab->active_module;
    symtab->active_module = m;

    if (letter == 'R') {
        const char *entry = m->dynaload_table[dyna_pos];
        /* Note: This is currently fine because there are no modules which
           create vars of a type not found in the interpreter's core. However,
           if that changes, this must change as well. */
        const char *name = entry + DYNA_NAME_OFFSET;
        lily_type *var_type = type_by_name(parser, name + strlen(name) + 1);
        lily_var *new_var = lily_emit_new_dyna_var(parser->emit, m, var_type,
                name);

        /* Vars should not be uncommon, and they may need cid information.
           mod_lily (Apache) for example, uses Tainted which ends up being
           dynaloaded. Make sure that cid information is up-to-date. */
        update_cid_table(parser, m);

        void *value = m->loader(parser->options, m->cid_table, dyna_pos);
        lily_foreign_value *foreign = (lily_foreign_value *)value;

        /* Values are saved in parser space until the vm is ready to receive
           them. Make use of the extra space in a lily_value to write down what
           register this value will target later on. */
        foreign->reg_spot = new_var->reg_spot;
        lily_vs_push(parser->foreign_values, (lily_value *)foreign);

        result = (lily_item *)new_var;
    }
    else if (letter == 'F') {
        lily_var *new_var = dynaload_function(parser, m, NULL, dyna_pos);
        result = (lily_item *)new_var;
    }
    else if (letter == 'C') {
        lily_class *new_cls = dynaload_class(parser, m, dyna_pos);
        result = (lily_item *)new_cls;
    }
    else if (letter == 'B') {
        lily_class *new_cls = dynaload_bootstrap(parser, m, dyna_pos);
        result = (lily_item *)new_cls;
    }
    else if (letter == 'V') {
        lily_class *new_cls = dynaload_variant(parser, m, dyna_pos);
        result = (lily_item *)new_cls;
    }
    else if (letter == 'E') {
        lily_class *new_cls = dynaload_enum(parser, m, dyna_pos);
        result = (lily_item *)new_cls;
    }
    else
        result = NULL;

    symtab->active_module = saved_active;
    return result;
}

static lily_item *try_toplevel_dynaload(lily_parse_state *parser,
        lily_module_entry *m, const char *name)
{
    int i = 1;
    const char **table = m->dynaload_table;
    const char *entry = table[i];
    lily_item *result = NULL;

    do {
        if (strcmp(entry + DYNA_NAME_OFFSET, name) == 0) {
            result = run_dynaload(parser, m, i);
            break;
        }

        i += (unsigned char)entry[1] + 1;
        entry = table[i];
    } while (entry[0] != 'Z');

    return result;
}

lily_class *lily_dynaload_exception(lily_parse_state *parser, const char *name)
{
    lily_module_entry *m = parser->package_start->first_module;
    return (lily_class *)try_toplevel_dynaload(parser, m, name);
}

/* Like find_run_dynaload, but only do the dynaload if the entity to be loaded
   is a class-like entity. */
static lily_class *find_run_class_dynaload(lily_parse_state *parser,
        lily_module_entry *m, const char *name)
{
    lily_item *result = try_toplevel_dynaload(parser, m, name);
    if (result && result->item_kind != ITEM_TYPE_VAR)
        return (lily_class *)result;
    else
        return NULL;
}

/* Try to find 'name' within 'cls', wherein 'cls' may be the currently entered
   class. If it's the current class, then it may have methods that are in the
   current module (because parser doesn't automatically immediately add them).
   If the entity exists as a dynaload, then the dynaload is run (hence the dl
   part of the name). */
lily_item *lily_find_or_dl_member(lily_parse_state *parser, lily_class *cls,
        const char *name)
{
    /* The var search needs to come first because this may be a .new call. If it
       is, then the var is in the current var chain. Doing the member lookup
       first means that the .new of a parent class might instead be found. */
    lily_var *var = lily_find_var(parser->symtab, NULL, name);
    if (var && var->parent == cls)
        return (lily_item *)var;

    lily_named_sym *member = lily_find_member(cls, name);
    if (member)
        return (lily_item *)member;

    if (cls->dyna_start) {
        int index = cls->dyna_start;
        lily_module_entry *m = cls->module;
        const char **table = m->dynaload_table;
        const char *entry = table[index];

        do {
            if (strcmp(name, entry + 2) == 0)
                break;
            index++;
            entry = table[index];
        } while (entry[0] == 'm');

        if (entry[0] == 'm')
            return (lily_item *)dynaload_function(parser, m, cls, index);
    }

    return NULL;
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

static int constant_by_name(const char *name)
{
    int i;
    uint64_t shorthash = shorthash_for_name(name);

    for (i = 0;i <= CONST_LAST_ID;i++) {
        if (constants[i].shorthash == shorthash &&
            strcmp(constants[i].name, name) == 0)
            return i;
        else if (constants[i].shorthash > shorthash)
            break;
    }

    return -1;
}

static int keyword_by_name(const char *name)
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
/* Normally, the next token is pulled up after an expression_* helper has been
   called. If this is or'd onto the state, then it's assumed that the next token
   has already been pulled up. */
#define ST_FORWARD              0x8

/* This initializes a new expression state based off of the currently existing
   one, and makes the new one the current one. No allocation is done. */
static void init_expr_state(lily_parse_state *parser, lily_expr_state *new_es)
{
    lily_expr_state *old_es = parser->expr;

    new_es->pile_start = old_es->pile_current;
    new_es->pile_current = old_es->pile_current;
    new_es->first_tree = old_es->next_available;
    new_es->next_available = old_es->next_available;
    new_es->prev = old_es;
    new_es->lex_linenum = &parser->lex->line_num;
    new_es->save_chain = old_es->save_chain;
    new_es->save_depth = 0;
    new_es->root = NULL;
    new_es->active = NULL;

    parser->expr = new_es;
}

/* Rewind the expression state for another run. This only needs to be done when
   calling expression_raw. Most functions will call the toplevel expression
   instead, which does this before expression_raw for them. */
static void rewind_expr_state(lily_expr_state *es)
{
    es->root = NULL;
    es->active = NULL;
    es->next_available = es->first_tree;
}

/* Finishes the current expression state, restoring the last one. */
static void fini_expr_state(lily_parse_state *parser)
{
    parser->expr = parser->expr->prev;
}

/* This handles when a class is seen within an expression. Any import qualifier
   has already been scanned and is unimportant. The key here is to figure out if
   this is `<class>.member` or `<class>()`. The first is a static access, while
   the latter is an implicit `<class>.new()`. */
static void expression_class_access(lily_parse_state *parser, lily_class *cls,
        int *state)
{
    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);
    if (lex->token != tk_dot) {
        if (cls->flags & CLS_IS_ENUM)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Cannot implicitly use the constructor of an enum.\n");

        lily_item *target = lily_find_or_dl_member(parser, cls, "new");
        if (target == NULL)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Cannot implicitly use %s.new (it doesn't exist).\n", cls->name);

        lily_es_push_static_func(parser->expr, (lily_var *)target);
        *state = ST_FORWARD | ST_WANT_OPERATOR;
        return;
    }

    *state = ST_WANT_OPERATOR;

    NEED_NEXT_TOK(tk_word)
    lily_item *item = lily_find_or_dl_member(parser, cls, lex->label);

    /* Breaking this down:
       If there's a var, then it needs to be a member of THIS exact class, not
       a subclass. I suspect that would be an unwelcome surprise.
       No properties. Properties need to be accessed through a dot, since they
       are per-instance. */

    if (item &&
        (
         (item->item_kind == ITEM_TYPE_VAR &&
          ((lily_var *)item)->parent != cls) ||
         (item->item_kind == ITEM_TYPE_PROPERTY)
        ))
        item = NULL;

    if (item) {
        lily_es_push_static_func(parser->expr, (lily_var *)item);
        return;
    }

    /* Enums allow scoped variants through `<enum>.<variant>`. */
    if (cls->flags & CLS_IS_ENUM) {
        lily_variant_class *variant = lily_find_scoped_variant(cls, lex->label);
        if (variant) {
            lily_es_push_variant(parser->expr, variant);
            return;
        }
    }

    lily_raise(parser->raiser, lily_SyntaxError,
            "%s.%s does not exist.\n", cls->name, lex->label);
}

/* This is a wrapper function that handles pushing the given literal into the
   parser's ast pool. This function exists because literals may, in the future,
   not have a type associated with them (and be just a lily_value). */
static void push_literal(lily_parse_state *parser, lily_literal *lit)
{
    lily_class *literal_cls;

    if (lit->flags == VAL_IS_INTEGER)
        literal_cls = parser->symtab->integer_class;
    else if (lit->flags == VAL_IS_DOUBLE)
        literal_cls = parser->symtab->double_class;
    else if (lit->flags == VAL_IS_STRING)
        literal_cls = parser->symtab->string_class;
    else if (lit->flags == VAL_IS_BYTESTRING)
        literal_cls = parser->symtab->bytestring_class;
    else
        /* Impossible, but keeps the compiler from complaining. */
        literal_cls = parser->symtab->question_class;

    lily_es_push_literal(parser->expr, literal_cls->type, lit->reg_spot);
}

/* This takes an id that corresponds to some id in the table of magic constants.
   From that, it determines that value of the magic constant, and then adds that
   value to the current ast pool. */
static void push_constant(lily_parse_state *parser, int key_id)
{
    lily_expr_state *es = parser->expr;
    lily_symtab *symtab = parser->symtab;
    lily_literal *lit;

    /* These literal fetching routines are guaranteed to return a literal with
       the given value. */
    if (key_id == CONST__LINE__) {
        int num = parser->lex->line_num;

        if ((int16_t)num <= INT16_MAX)
            lily_es_push_integer(es, (int16_t)num);
        else {
            lit = lily_get_integer_literal(symtab, parser->lex->line_num);
            push_literal(parser, lit);
        }
    }
    else if (key_id == CONST__FILE__) {
        lit = lily_get_string_literal(symtab, parser->symtab->active_module->path);
        push_literal(parser, lit);
    }
    else if (key_id == CONST__FUNCTION__) {
        lit = lily_get_string_literal(symtab, parser->emit->top_var->name);
        push_literal(parser, lit);
    }
    else if (key_id == CONST_TRUE)
        lily_es_push_boolean(es, 1);
    else if (key_id == CONST_FALSE)
        lily_es_push_boolean(es, 0);
    else if (key_id == CONST_SELF)
        lily_es_push_self(es);
}

/* This is called when a class (enum or regular) is found. This determines how
   to handle the class: Either push the variant or run a static call, then
   updates the state. */
static void dispatch_word_as_class(lily_parse_state *parser, lily_class *cls,
        int *state)
{
    if (cls->flags & CLS_IS_VARIANT) {
        lily_es_push_variant(parser->expr, (lily_variant_class *)cls);
        *state = ST_WANT_OPERATOR;
    }
    else
        expression_class_access(parser, cls, state);
}

/* This function is to be called when 'func' could be a class method or just a
   plain function. The emitter's call handling special-cases tree_method to
   do an auto-injection of 'self'.  */
static void push_maybe_method(lily_parse_state *parser, lily_var *func)
{
    if (func->parent &&
        parser->class_self_type &&
        lily_class_greater_eq(func->parent, parser->class_self_type->cls))
        lily_es_push_method(parser->expr, func);
    else
        lily_es_push_defined_func(parser->expr, func);
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
    else if (var->flags & VAR_IS_GLOBAL)
        lily_es_push_global_var(parser->expr, var);
    else if (var->function_depth == parser->emit->function_depth)
        lily_es_push_local_var(parser->expr, var);
    else
        lily_es_push_upvalue(parser->expr, var);

    *state = ST_WANT_OPERATOR;
}

/* Something was dynaloaded. Push it into the ast and update state. */
static void dispatch_dynaload(lily_parse_state *parser, lily_item *dl_item,
        int *state)
{
    lily_expr_state *es = parser->expr;

    if (dl_item->item_kind == ITEM_TYPE_VAR) {
        lily_var *v = (lily_var *)dl_item;
        if (v->flags & VAR_IS_READONLY)
            lily_es_push_defined_func(es, v);
        else
            lily_es_push_global_var(es, v);

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
    lily_module_entry *search_module = resolve_module(parser);

    lily_var *var = lily_find_var(symtab, search_module, lex->label);
    if (var) {
        dispatch_word_as_var(parser, var, state);
        return;
    }

    if (search_module == NULL) {
        int const_id = constant_by_name(lex->label);
        if (const_id != -1) {
            if (const_id == CONST_SELF && parser->class_self_type == NULL)
                lily_raise(parser->raiser, lily_SyntaxError,
                        "'self' must be used within a class.\n");

            push_constant(parser, const_id);
            *state = ST_WANT_OPERATOR;
            return;
        }
    }

    lily_class *cls = lily_find_class(parser->symtab, search_module, lex->label);

    if (cls) {
        dispatch_word_as_class(parser, cls, state);
        return;
    }

    if (search_module == NULL && parser->class_self_type) {
        var = lily_find_method(parser->class_self_type->cls, lex->label);

        if (var) {
            lily_es_push_method(parser->expr, var);
            *state = ST_WANT_OPERATOR;
            return;
        }
    }

    if (search_module == NULL)
        search_module = symtab->builtin_module;

    lily_item *dl_result = try_toplevel_dynaload(parser, search_module,
            lex->label);

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

    lily_es_push_property(parser->expr, prop);
    *state = ST_WANT_OPERATOR;
}

/* This makes sure that the current token is the right kind of token for closing
   the current tree. If it is not, then SyntaxError is raised. */
static void check_valid_close_tok(lily_parse_state *parser)
{
    lily_token token = parser->lex->token;
    lily_ast *ast = lily_es_get_saved_tree(parser->expr);
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

        lily_es_push_binary_op(parser->expr, (lily_expr_op)expr_op);
        /* Call this to force a rescan from the proper starting point, yielding
           a proper new token. */
        lily_lexer_digit_rescan(lex);

        if (lex->token == tk_integer) {
            if (lex->last_integer <= INT16_MAX &&
                lex->last_integer >= INT16_MIN)
                lily_es_push_integer(parser->expr, (int16_t)
                        lex->last_integer);
            else {
                lily_literal *lit = lily_get_integer_literal(parser->symtab,
                        lex->last_integer);

                push_literal(parser, lit);
            }
        }
        else
            push_literal(parser, lex->last_literal);

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
    else if (token == tk_dollar_string) {
        lily_expr_state *es = parser->expr;
        lily_symtab *symtab = parser->symtab;
        lily_msgbuf *msgbuf = parser->msgbuf;
        lily_msgbuf_flush(msgbuf);
        lily_msgbuf_add(msgbuf, lex->label);
        char *scan_string = msgbuf->message;

        lily_es_enter_tree(es, tree_interp_top);
        do {
            int is_interp = lily_scan_interpolation_piece(lex, &scan_string);
            if (is_interp) {
                int pile_spot = es->pile_current;
                lily_sp_insert(parser->expr_strings, lex->label,
                        &es->pile_current);

                lily_es_push_text(es, tree_interp_block,
                        lex->expand_start_line, pile_spot);
            }
            else {
                lily_literal *lit = lily_get_string_literal(symtab, lex->label);
                push_literal(parser, lit);
            }
            lily_es_collect_arg(parser->expr);
        } while (*scan_string != '\0');

        lily_es_leave_tree(es);
        lily_msgbuf_flush(msgbuf);
        *state = ST_WANT_OPERATOR;
    }
    else if (lex->token == tk_integer) {
        if (lex->last_integer <= INT16_MAX &&
            lex->last_integer >= INT16_MIN)
            lily_es_push_integer(parser->expr, (int16_t)
                    lex->last_integer);
        else {
            lily_literal *lit = lily_get_integer_literal(parser->symtab,
                    lex->last_integer);
            push_literal(parser, lit);
        }

        *state = ST_WANT_OPERATOR;
    }
    else {
        push_literal(parser, lex->last_literal);
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

    if (parser->expr->active == NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                    "Expected a value, not ','.\n");

    lily_ast *last_tree = lily_es_get_saved_tree(parser->expr);
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

    lily_es_collect_arg(parser->expr);
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
            lily_es_push_unary_op(parser->expr, expr_unary_minus);
        else if (token == tk_not)
            lily_es_push_unary_op(parser->expr, expr_unary_not);

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
    if (lex->token == tk_word) {
        lily_expr_state *es = parser->expr;
        int spot = es->pile_current;
        lily_sp_insert(parser->expr_strings, lex->label, &es->pile_current);
        lily_es_push_text(es, tree_oo_access, 0, spot);
    }
    else if (lex->token == tk_typecast_parenth) {
        lily_lexer(lex);
        lily_type *bare_type = get_type(parser);

        /* Make sure Option exists, then wrap the resulting type in an Option of
           the specified type. This allows emitter to not worry about
           dynaloading when casting. */
        lily_symtab *symtab = parser->symtab;
        lily_class *option_cls = lily_find_class(symtab, symtab->builtin_module,
                "Option");
        if (option_cls == NULL)
            option_cls = find_run_class_dynaload(parser, symtab->builtin_module,
                    "Option");

        lily_tm_add(parser->tm, bare_type);
        lily_type *cast_type = lily_tm_make(parser->tm, 0, option_cls, 1);

        lily_es_enter_typecast(parser->expr, cast_type);
        lily_es_leave_tree(parser->expr);
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
                if (parser->expr->save_depth == 0)
                    state = ST_DONE;
                else
                    state = ST_BAD_TOKEN;
            else
                expression_word(parser, &state);
        }
        else if (expr_op != -1) {
            if (state == ST_WANT_OPERATOR) {
                lily_es_push_binary_op(parser->expr, (lily_expr_op)expr_op);
                state = ST_DEMAND_VALUE;
            }
            else if (lex->token == tk_minus)
                expression_unary(parser, &state);
            else
                state = ST_BAD_TOKEN;
        }
        else if (lex->token == tk_left_parenth) {
            if (state == ST_WANT_VALUE || state == ST_DEMAND_VALUE) {
                lily_es_enter_tree(parser->expr, tree_parenth);
                state = ST_DEMAND_VALUE;
            }
            else if (state == ST_WANT_OPERATOR) {
                lily_es_enter_tree(parser->expr, tree_call);
                state = ST_WANT_VALUE;
            }
        }
        else if (lex->token == tk_left_bracket) {
            if (state == ST_WANT_VALUE || state == ST_DEMAND_VALUE) {
                lily_es_enter_tree(parser->expr, tree_list);
                state = ST_WANT_VALUE;
            }
            else if (state == ST_WANT_OPERATOR) {
                lily_es_enter_tree(parser->expr, tree_subscript);
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
                lily_es_enter_tree(parser->expr, tree_tuple);
                state = ST_WANT_VALUE;
            }
        }
        else if (lex->token == tk_right_parenth ||
                 lex->token == tk_right_bracket ||
                 lex->token == tk_tuple_close) {
            if (state == ST_DEMAND_VALUE ||
                parser->expr->save_depth == 0) {
                state = ST_BAD_TOKEN;
            }
            else {
                check_valid_close_tok(parser);
                lily_es_leave_tree(parser->expr);
                if (maybe_end_on_parenth == 0 ||
                    lex->token != tk_right_parenth ||
                    parser->expr->save_depth != 0)
                    state = ST_WANT_OPERATOR;
                else {
                    state = ST_DONE;
                }
            }
        }
        else if (lex->token == tk_integer || lex->token == tk_double ||
                 lex->token == tk_double_quote || lex->token == tk_bytestring ||
                 lex->token == tk_dollar_string)
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
                lily_es_enter_tree(parser->expr, tree_call);

            lily_expr_state *es = parser->expr;
            int spot = es->pile_current;
            lily_sp_insert(parser->expr_strings, lex->label, &es->pile_current);
            lily_es_push_text(parser->expr, tree_lambda, lex->expand_start_line,
                    spot);

            if (state == ST_WANT_OPERATOR)
                lily_es_leave_tree(parser->expr);

            state = ST_WANT_OPERATOR;
        }
        /* Make sure this case stays lower down. If it doesn't, then certain
           expressions will exit before they really should. */
        else if (parser_tok_table[lex->token].val_or_end &&
                 parser->expr->save_depth == 0 &&
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
        else if (state & ST_FORWARD)
            state &= ~ST_FORWARD;
        else
            lily_lexer(lex);
    }
}

/* This calls expression_raw demanding a value. If you need that (and most
   callers do), then use this. If you don't, then call it raw. */
static void expression(lily_parse_state *parser)
{
    rewind_expr_state(parser->expr);
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

/* This runs through the body of a lambda, running any statements inside. The
   result of this function is the type of the last expression that was run.
   If the last thing was a block, or did not return a value, then NULL is
   returned. */
static lily_type *parse_lambda_body(lily_parse_state *parser,
        lily_type *expect_type)
{
    lily_lex_state *lex = parser->lex;
    int key_id = -1;
    lily_type *result_type = NULL;

    lily_lexer(parser->lex);
    while (1) {
        if (lex->token == tk_word)
            key_id = keyword_by_name(lex->label);

        if (key_id == -1) {
            expression(parser);
            if (lex->token != tk_right_curly)
                /* This expression isn't the last one, so it can do whatever it
                   wants to do. */
                lily_emit_eval_expr(parser->emit, parser->expr);
            else {
                /* The last expression is what will be returned, so give it the
                   inference information of the lambda. */
                lily_emit_eval_lambda_body(parser->emit, parser->expr,
                        expect_type);

                if (parser->expr->root->result)
                    result_type = parser->expr->root->result->type;

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

    return result_type;
}

/* This collects the arguments between the two '|' tokens of a lambda.
   'expect_type' is either a function type for inference to be pulled from, or
   NULL. Arguments may be just names, or names with types. */
static int collect_lambda_args(lily_parse_state *parser,
        lily_type *expect_type)
{
    int infer_count = (expect_type) ? expect_type->subtype_count : -1;
    int num_args = 0;
    lily_lex_state *lex = parser->lex;

    while (1) {
        NEED_NEXT_TOK(tk_word)
        lily_var *arg_var = get_named_var(parser, NULL);
        lily_type *arg_type;

        if (lex->token == tk_colon) {
            lily_lexer(lex);
            arg_type = get_type(parser);
            arg_var->type = arg_type;
        }
        else {
            arg_type = NULL;
            if (num_args < infer_count)
                arg_type = expect_type->subtypes[num_args + 1];

            if (arg_type == NULL || arg_type->flags & TYPE_IS_INCOMPLETE)
                lily_raise(parser->raiser, lily_SyntaxError,
                        "Cannot infer type of '%s'.\n", lex->label);

            arg_var->type = arg_type;
        }

        lily_tm_add(parser->tm, arg_type);
        num_args++;

        if (lex->token == tk_comma)
            continue;
        else if (lex->token == tk_bitwise_or)
            break;
        else
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Expected either ',' or '|', not '%s'.\n",
                    tokname(lex->token));
    }

    return num_args;
}

/* Interpolation gets handled here, because it's roughly the same thing as
   lambda handling. */
lily_sym *lily_parser_interp_eval(lily_parse_state *parser, int start_line,
        const char *text)
{
    lily_lex_state *lex = parser->lex;
    lily_load_copy_string(lex, lm_no_tags, text);
    lex->line_num = start_line;
    lily_lexer(parser->lex);

    if (lex->token == tk_eof)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Empty interpolation block.\n");

    lily_expr_state es;
    init_expr_state(parser, &es);
    expression(parser);

    lily_sym *result = lily_emit_eval_interp_expr(parser->emit,
            parser->expr);

    if (lex->token != tk_eof)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Interpolation block must be a single expression.\n",
                tokname(lex->token));

    if (result == NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Interpolation command does not return a value.\n");

    fini_expr_state(parser);
    lily_pop_lex_entry(lex);
    return result;
}

/* This is the main workhorse of lambda handling. It takes the lambda body and
   works through it. This is fairly complicated, because this happens during
   tree eval. As such, the current state has to be saved and a lambda has to be
   made too. When this is done, it has to build the resulting type of the lambda
   as well. */
lily_var *lily_parser_lambda_eval(lily_parse_state *parser,
        int lambda_start_line, const char *lambda_body, lily_type *expect_type)
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
    lily_load_copy_string(lex, lm_no_tags, lambda_body);
    lex->line_num = lambda_start_line;

    /* Block entry assumes that the most recent var added is the var to bind
       the function to. For the type of the lambda, use the default call
       type (a function with no args and no output) because expect_type may
       be NULL if the emitter doesn't know what it wants. */
    lily_var *lambda_var = lily_emit_new_define_var(parser->emit,
            parser->default_call_type, NULL, "(lambda)");

    /* From here on, vars created will be in the scope of the lambda. Also,
       this binds a function value to lambda_var. */
    lily_emit_enter_block(parser->emit, block_lambda);

    lily_lexer(lex);

    lily_tm_add(parser->tm, NULL);

    if (lex->token == tk_bitwise_or)
        args_collected = collect_lambda_args(parser, expect_type);
    else if (lex->token == tk_bitwise_or) {
        NEED_NEXT_TOK(tk_bitwise_or)
    }
    else if (lex->token != tk_logical_or)
        lily_raise(parser->raiser, lily_SyntaxError, "Unexpected token '%s'.\n",
                lex->token);

    /* Evaluate the lambda within a new expression state so the old one's stuff
       isn't trampled over. */
    lily_expr_state es;
    init_expr_state(parser, &es);
    root_result = parse_lambda_body(parser, expect_type);
    fini_expr_state(parser);

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

    lily_named_sym *sym = lily_find_member(current_class, name);
    if (sym) {
        if (sym->item_kind == ITEM_TYPE_VAR)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "A method in class '%s' already has the name '%s'.\n",
                    current_class->name, name);
        else
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Property %s already exists in class %s.\n", name,
                    current_class->name);
    }

    lily_prop_entry *prop;
    prop = lily_add_class_property(parser->symtab, current_class, prop_type,
            name, flags & ~SYM_NOT_INITIALIZED);

    lily_lexer(parser->lex);
    return prop;
}

/* This is called when @<name> is given outside of a class or <name> is given at
   the top of a class. */
static void bad_decl_token(lily_parse_state *parser)
{
    const char *message;

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
        rewind_expr_state(parser->expr);

        /* For this special case, give a useful error message. */
        if (lex->token == other_token)
            bad_decl_token(parser);

        NEED_CURRENT_TOK(want_token)

        if (lex->token == tk_word) {
            sym = (lily_sym *)get_named_var(parser, NULL);
            sym->flags |= SYM_NOT_INITIALIZED;
            if (sym->flags & VAR_IS_GLOBAL)
                lily_es_push_global_var(parser->expr, (lily_var *)sym);
            else
                lily_es_push_local_var(parser->expr, (lily_var *)sym);
        }
        else {
            sym = (lily_sym *)get_named_property(parser, NULL, flags);
            lily_es_push_property(parser->expr, (lily_prop_entry *)sym);
        }

        if (lex->token == tk_colon) {
            lily_lexer(lex);
            sym->type = get_type(parser);
        }

        if (lex->token != tk_equal) {
            lily_raise(parser->raiser, lily_SyntaxError,
                    "An initialization expression is required here.\n");
        }

        lily_es_push_binary_op(parser->expr, expr_assign);
        lily_lexer(lex);
        expression_raw(parser, ST_DEMAND_VALUE);
        lily_emit_eval_expr(parser->emit, parser->expr);

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
static void use_handler(lily_parse_state *, int);
static void var_handler(lily_parse_state *, int);
static void for_handler(lily_parse_state *, int);
static void try_handler(lily_parse_state *, int);
static void case_handler(lily_parse_state *, int);
static void else_handler(lily_parse_state *, int);
static void elif_handler(lily_parse_state *, int);
static void enum_handler(lily_parse_state *, int);
static void while_handler(lily_parse_state *, int);
static void raise_handler(lily_parse_state *, int);
static void match_handler(lily_parse_state *, int);
static void break_handler(lily_parse_state *, int);
static void class_handler(lily_parse_state *, int);
static void define_handler(lily_parse_state *, int);
static void return_handler(lily_parse_state *, int);
static void except_handler(lily_parse_state *, int);
static void import_handler(lily_parse_state *, int);
static void private_handler(lily_parse_state *, int);
static void protected_handler(lily_parse_state *, int);
static void continue_handler(lily_parse_state *, int);

typedef void (keyword_handler)(lily_parse_state *, int);

/* This is setup so that handlers[key_id] is the handler for that keyword. */
static keyword_handler *handlers[] = {
    if_handler,
    do_handler,
    use_handler,
    var_handler,
    for_handler,
    try_handler,
    case_handler,
    else_handler,
    elif_handler,
    enum_handler,
    while_handler,
    raise_handler,
    match_handler,
    break_handler,
    class_handler,
    define_handler,
    return_handler,
    except_handler,
    import_handler,
    private_handler,
    protected_handler,
    continue_handler,
};

static void var_handler(lily_parse_state *parser, int multi)
{
    parse_var(parser, 0);
}

static void ensure_unique_method_name(lily_parse_state *parser,
        const char *name)
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

    lily_class *parent;
    if (parser->class_self_type)
        parent = parser->class_self_type->cls;
    else
        parent = NULL;

    /* The type will be overwritten with the right thing later on. However, it's
       necessary to have some function-like entity there instead of, say, NULL.
       The reason is that a dynaload may be triggered, which may push a block.
       The emitter will attempt to restore the return type via the type of the
       define var here. */
    lily_var *define_var = lily_emit_new_define_var(parser->emit,
            parser->default_call_type, parent, lex->label);

    int i = 0;
    int arg_flags = 0;
    int result_pos = parser->tm->pos;
    int optarg_start = parser->optarg_stack->pos;

    /* This is the initial result. NULL means the function doesn't return
       anything. If it does, then this spot will be overwritten. */
    lily_tm_add(parser->tm, NULL);

    lily_lexer(lex);
    collect_generics_or(parser, parser->generic_count);
    lily_emit_enter_block(parser->emit, block_define);

    if (parser->class_self_type) {
        /* This is a method of a class. It should implicitly take 'self' as
           the first argument, and be registered to be within that class.
           It may also have a private/protected modifier, so add that too. */
        lily_tm_add(parser->tm, parser->class_self_type);
        i++;

        lily_var *self_var = lily_emit_new_local_var(parser->emit,
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
            define_var->type->subtypes[0]);

    if (arg_flags & TYPE_HAS_OPTARGS)
        lily_emit_write_optargs(parser->emit, parser->optarg_stack,
                optarg_start);
}

static lily_var *parse_for_range_value(lily_parse_state *parser,
        const char *name)
{
    lily_expr_state *es = parser->expr;
    expression(parser);

    /* Don't allow assigning expressions, since that just looks weird.
       ex: for i in a += 10..5
       Also, it makes no real sense to do that. */
    if (es->root->tree_type == tree_binary &&
        es->root->op >= expr_assign) {
        lily_raise(parser->raiser, lily_SyntaxError,
                   "For range value expression contains an assignment.");
    }

    lily_class *cls = parser->symtab->integer_class;

    /* For loop values are created as vars so there's a name in case of a
       problem. This name doesn't have to be unique, since it will never be
       found by the user. */
    lily_var *var = lily_emit_new_local_var(parser->emit, cls->type, name);

    lily_emit_eval_expr_to_var(parser->emit, es, var);

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
                lily_emit_eval_expr(parser->emit, parser->expr);
            }
        }
        else if (token == tk_integer || token == tk_double ||
                 token == tk_double_quote || token == tk_left_parenth ||
                 token == tk_left_bracket || token == tk_tuple_open ||
                 token == tk_prop_word || token == tk_bytestring ||
                 token == tk_dollar_string) {
            expression(parser);
            lily_emit_eval_expr(parser->emit, parser->expr);
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

static void do_elif(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_emit_change_block_to(parser->emit, block_if_elif);
    expression(parser);
    lily_emit_eval_condition(parser->emit, parser->expr);

    NEED_CURRENT_TOK(tk_colon)

    lily_lexer(lex);
}

static void do_else(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_change_block_to(parser->emit, block_if_else);

    NEED_CURRENT_TOK(tk_colon)
    lily_lexer(lex);
}

static void if_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, block_if);
    expression(parser);
    lily_emit_eval_condition(parser->emit, parser->expr);
    NEED_CURRENT_TOK(tk_colon)

    lily_lexer(lex);
    int multi_this_block = (lex->token == tk_left_curly);
    int have_else = 0;

    if (multi_this_block)
        lily_lexer(lex);

    while (1) {
        if (lex->token == tk_word) {
            int key = keyword_by_name(lex->label);
            if (key == -1) {
                expression(parser);
                lily_emit_eval_expr(parser->emit, parser->expr);
            }
            else if (key != KEY_ELIF && key != KEY_ELSE) {
                lily_lexer(lex);
                handlers[key](parser, multi_this_block);
            }
        }
        else if (lex->token != tk_right_curly) {
            expression(parser);
            lily_emit_eval_expr(parser->emit, parser->expr);
        }

        if (lex->token == tk_word && have_else == 0) {
            int key = keyword_by_name(lex->label);

            if (key == KEY_ELIF || key == KEY_ELSE) {
                lily_lexer(lex);
                if (key == KEY_ELIF)
                    do_elif(parser);
                else {
                    do_else(parser);
                    have_else = 1;
                }

                continue;
            }
            else if (multi_this_block == 0)
                break;
        }
        else if (lex->token == tk_right_curly || multi_this_block == 0)
            break;
    }

    if (multi_this_block == 1)
        lily_lexer(lex);

    lily_emit_leave_block(parser->emit);
}

static void elif_handler(lily_parse_state *parser, int multi)
{
    lily_raise(parser->raiser, lily_SyntaxError, "'elif' without 'if'.\n");
}

static void else_handler(lily_parse_state *parser, int multi)
{
    lily_raise(parser->raiser, lily_SyntaxError, "'else' without 'if'.\n");
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
    lily_block *block = parser->emit->function_block;
    if (block->block_type == block_class)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'return' not allowed in a class constructor.\n");
    else if (block->block_type == block_lambda)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'return' not allowed in a lambda.\n");
    else if (block->block_type == block_file || block->prev == NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'return' used outside of a function.\n");

    if (parser->emit->top_function_ret)
        expression(parser);

    lily_emit_eval_return(parser->emit, parser->expr);

    if (multi)
        ensure_no_code_after_exit(parser, "return");
}

static void while_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, block_while);

    expression(parser);
    lily_emit_eval_condition(parser->emit, parser->expr);

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

static void for_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;
    lily_var *loop_var;

    NEED_CURRENT_TOK(tk_word)

    lily_emit_enter_block(parser->emit, block_for_in);

    loop_var = lily_find_var(parser->symtab, NULL, lex->label);
    if (loop_var == NULL) {
        lily_class *cls = parser->symtab->integer_class;
        loop_var = lily_emit_new_local_var(parser->emit, cls->type,
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
    lily_emit_eval_condition(parser->emit, parser->expr);
    lily_emit_leave_block(parser->emit);
}

static void run_loaded_module(lily_parse_state *parser,
        lily_module_entry *module)
{
    if (module->handle == NULL) {
        lily_module_entry *save_active = parser->symtab->active_module;
        lily_lex_state *lex = parser->lex;

        parser->symtab->active_module = module;

        /* lily_emit_enter_block will write new code to this special var. */
        lily_var *import_var = lily_emit_new_define_var(parser->emit,
                parser->default_call_type, NULL, "__import__");

        lily_emit_enter_block(parser->emit, block_file);

        /* The whole of the file can be thought of as one large statement. */
        lily_lexer(lex);
        statement(parser, 1);

        /* Since this is processing an import, the lexer will raise an error if
           ?> is found. Because of that, multi-line statement can only end with
           either } or eof. Only one is right. */
        if (lex->token == tk_right_curly)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "'}' outside of a block.\n");

        if (parser->emit->block->block_type != block_file)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Unterminated block(s) at end of file.\n");

        lily_emit_leave_block(parser->emit);
        lily_pop_lex_entry(parser->lex);

        lily_emit_write_import_call(parser->emit, import_var);

        parser->symtab->active_module = save_active;
    }
}

static void import_handler(lily_parse_state *parser, int multi)
{
#ifdef WITH_EMSCRIPTEN
    lily_raise(parser->raiser, lily_SyntaxError,
            "'import' not allowed within the sandbox.\n");
#else
    lily_lex_state *lex = parser->lex;
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file &&
        block->prev != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Cannot import a file here.\n");

    lily_symtab *symtab = parser->symtab;
    lily_module_entry *active = symtab->active_module;
    lily_msgbuf *msgbuf = parser->msgbuf;
    lily_msgbuf_flush(msgbuf);

    while (1) {
        NEED_CURRENT_TOK(tk_word)
        /* The import path may include slashes. Use this to scan the path,
           because it won't allow spaces in between. */
        lily_scan_import_path(lex);

        char *search_start = lex->label;
        char *path_tail = strrchr(search_start, LILY_PATH_CHAR);
        /* Will the name that is going to be added conflict with something that
           has already been added? */
        if (path_tail != NULL)
            search_start = path_tail + 1;

        if (lily_find_module(symtab, active, search_start))
            lily_raise(parser->raiser, lily_SyntaxError,
                    "A module named '%s' has already been imported here.\n",
                    search_start);

        /* Has this path been imported before? */
        lily_msgbuf_flush(msgbuf);
        lily_msgbuf_add(msgbuf, active->dirname);
        lily_msgbuf_add(msgbuf, lex->label);

        lily_module_entry *module = lily_find_module_by_path(active->parent,
                msgbuf->message);

        lily_msgbuf_flush(msgbuf);

        if (module == NULL) {
            module = load_module(parser, active->dirname, lex->label);
            run_loaded_module(parser, module);
        }

        lily_lexer(parser->lex);
        if (lex->token == tk_word && strcmp(lex->label, "as") == 0) {
            NEED_NEXT_TOK(tk_word)
            /* This link must be done now, because the next token may be a word
               and lex->label would be modified. */
            link_module_to(active, module, lex->label);
            lily_lexer(lex);
        }
        else
            link_module_to(active, module, NULL);

        if (lex->token == tk_comma) {
            lily_lexer(parser->lex);
            continue;
        }
        else
            break;
    }
#endif
}

static void link_package_to(lily_package *target, lily_package *to_link)
{
    lily_package_link *link = lily_malloc(sizeof(lily_package_link));

    link->package = to_link;
    link->next = target->linked_packages;
    target->linked_packages = link;
}

typedef struct {
    const char *prefix;
    const char *suffix;
    lily_module_entry *(*load_fn)(lily_parse_state *, const char *);
} package_loader;

/* This is pretty similar to module loading, except there's a prefix to worry
   about this time. */
package_loader builtin_package_loaders[] =
{
    {LILY_PATH_SLASH, ".lly",          load_file},
    {LILY_PATH_SLASH, LILY_LIB_SUFFIX, load_library},
};

static lily_package *load_registered_package(lily_parse_state *parser,
        const char *name)
{
    lily_package *package_iter = parser->package_start;

    while (package_iter) {
        if (strcmp(package_iter->name, name) == 0)
            break;

        package_iter = package_iter->root_next;
    }

    return package_iter;
}

static lily_package *load_package(lily_parse_state *parser, const char *dirpath,
        const char *name)
{
    int i;
    lily_msgbuf *msgbuf = parser->msgbuf;
    lily_module_entry *module = NULL;

    for (i = 0;i < 2;i++) {
        package_loader l = builtin_package_loaders[i];
        lily_msgbuf_flush(msgbuf);

        lily_msgbuf_add_fmt(msgbuf, "%spackages" LILY_PATH_SLASH "%s%s%s%s",
                dirpath, name, l.prefix, name, l.suffix);

        module = l.load_fn(parser, msgbuf->message);
        if (module != NULL)
            break;
    }

    if (module == NULL) {
        lily_msgbuf_flush(msgbuf);
        lily_msgbuf_add_fmt(msgbuf, "Cannot load package '%s':\n", name);
        lily_msgbuf_add_fmt(msgbuf, "    no built-in package '%s'.\n", name);

        for (i = 0;i < 2;i++) {
            package_loader l = builtin_package_loaders[i];
            lily_msgbuf_add_fmt(msgbuf,
                    "    %spackages" LILY_PATH_SLASH "%s%s%s%s\n", dirpath,
                    name, l.prefix, name, l.suffix);
        }

        lily_raise(parser->raiser, lily_SyntaxError, msgbuf->message);
    }

    lily_package *new_package = new_empty_package(parser, name);
    new_package->first_module = module;
    module->parent = new_package;

    return new_package;
}

static void use_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file &&
        block->prev != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'use' not allowed inside of a block.\n");

    /* All packages that are 'use'-d by the root module of a package are
       available to non-root packages. Forbid use-ing throughout a package, so
       that uses are cleaner (they're just in the root).
       This seems like a good idea. */
    lily_module_entry *active = parser->symtab->active_module;
    if (active != active->parent->first_module)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'use' only allowed within the first module of a package.\n");

    lily_package_link *link_iter = active->parent->linked_packages;
    while (link_iter) {
        if (strcmp(link_iter->package->name, lex->label) == 0)
            lily_raise(parser->raiser, lily_SyntaxError,
                    "Attempt to use '%s' more than once.\n", lex->label);

        link_iter = link_iter->next;
    }

    NEED_CURRENT_TOK(tk_word)
    lily_package *package = load_registered_package(parser, lex->label);
    if (package == NULL) {
        package = load_package(parser, active->dirname, lex->label);
        run_loaded_module(parser, package->first_module);
    }

    link_package_to(active->parent, package);
    lily_lexer(lex);
}

static void process_except(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_type *except_type = get_type(parser);
    /* Exception is likely to always be the base exception class. */
    if (parser->exception_type == NULL) {
        lily_class *cls = lily_find_class(parser->symtab, NULL, "Exception");
        if (cls == NULL)
            cls = find_run_class_dynaload(parser,
                    parser->symtab->builtin_module, "Exception");

        parser->exception_type = cls->type;
    }

    lily_type *base_type = parser->exception_type;
    lily_block_type new_type = block_try_except;

    if (except_type == base_type)
        new_type = block_try_except_all;
    else if (lily_class_greater_eq(base_type->cls, except_type->cls) == 0)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'%s' is not a valid exception class.\n",
                except_type->cls->name);
    else if (except_type->subtypes != 0)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'except' type cannot have subtypes.\n");

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

        exception_var = lily_emit_new_local_var(parser->emit,
                except_type, lex->label);

        lily_lexer(lex);
    }

    NEED_CURRENT_TOK(tk_colon)
    lily_emit_except(parser->emit, except_type, exception_var,
            lex->line_num);

    lily_lexer(lex);
}

static void try_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, block_try);
    lily_emit_try(parser->emit, parser->lex->line_num);

    NEED_CURRENT_TOK(tk_colon)
    lily_lexer(lex);

    int multi_this_block = (lex->token == tk_left_curly);

    if (multi_this_block && multi == 0)
        lily_raise(parser->raiser, lily_SyntaxError,
                   "Multi-line block within single-line block.\n");

    if (multi_this_block)
        lily_lexer(lex);

    while (1) {
        if (lex->token == tk_word) {
            int key = keyword_by_name(lex->label);
            if (key == -1) {
                expression(parser);
                lily_emit_eval_expr(parser->emit, parser->expr);
            }
            else if (key != KEY_EXCEPT) {
                lily_lexer(lex);
                handlers[key](parser, multi_this_block);
            }
        }
        else if (lex->token != tk_right_curly)
            statement(parser, 0);

        if (lex->token == tk_word) {
            int key = keyword_by_name(lex->label);

            if (key == KEY_EXCEPT) {
                lily_lexer(lex);
                process_except(parser);
                continue;
            }
            else if (multi_this_block == 0)
                break;
        }
        else if (lex->token == tk_right_curly || multi_this_block == 0)
            break;
    }

    if (multi_this_block)
        lily_lexer(lex);

    lily_emit_leave_block(parser->emit);
}

static void except_handler(lily_parse_state *parser, int multi)
{
    lily_raise(parser->raiser, lily_SyntaxError, "'except' outside 'try'.\n");
}

static void raise_handler(lily_parse_state *parser, int multi)
{
    if (parser->emit->function_block->block_type == block_lambda)
        lily_raise(parser->raiser, lily_SyntaxError,
                "'raise' not allowed in a lambda.\n");

    expression(parser);
    lily_emit_raise(parser->emit, parser->expr);

    if (multi)
        ensure_no_code_after_exit(parser, "raise");
}

static void ensure_valid_class(lily_parse_state *parser, const char *name)
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

    lily_item *item = try_toplevel_dynaload(parser,
            parser->symtab->builtin_module, name);
    if (item && item->item_kind != ITEM_TYPE_VAR)
        lily_raise(parser->raiser, lily_SyntaxError,
                "A built-in class named '%s' already exists.\n", name);
}

/* This handles everything needed to create a class, up until figuring out
   inheritance (if that's indicated). */
static void parse_class_header(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    /* Use the default call type (function ()) in case one of the types listed
       triggers a dynaload. If a dynaload is triggered, emitter tries to
       restore the current return type from the last define's return type. */
    lily_var *call_var = lily_emit_new_define_var(parser->emit,
            parser->default_call_type, cls, "new");

    lily_lexer(lex);
    collect_generics_or(parser, 0);
    cls->generic_count = parser->generic_count;

    lily_emit_enter_block(parser->emit, block_class);

    parser->class_self_type = build_self_type(parser, cls,
            parser->generic_count);

    int i = 1;
    int flags = 0;
    int optarg_start = parser->optarg_stack->pos;
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
            call_var->type->subtypes[0]);

    if (flags & TYPE_HAS_OPTARGS)
        lily_emit_write_optargs(parser->emit, parser->optarg_stack,
                optarg_start);
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
    else if (super_class->flags &
             (CLS_IS_ENUM | CLS_IS_VARIANT | CLS_IS_BUILTIN))
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

    lily_expr_state *es = parser->expr;
    rewind_expr_state(es);
    lily_es_enter_tree(es, tree_call);
    lily_es_push_inherited_new(es, class_new);
    lily_es_collect_arg(es);

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
        lily_es_leave_tree(parser->expr);

    lily_emit_eval_expr(parser->emit, es);

    cls->parent = super_class;
    cls->prop_count = super_class->prop_count;
}

/* This is a helper function that scans 'target' to determine if it will require
   any gc information to hold. */
static int get_gc_flags_for(lily_class *top_class, lily_type *target)
{
    /* NULL is used as the return type of a Function that returns nothing. */
    if (target == NULL)
        return 0;

    int result_flag = 0;

    if (target->cls->id == SYM_CLASS_GENERIC) {
        /* If a class has generic types, then it can't be fetched from Dynamic.
           A generic type will always resolve to some bottom, but that bottom
           will not be equal to itself. Based on that assumption, the class does
           not need a tag (but it should be speculative). */
        if (top_class->generic_count)
            result_flag = CLS_GC_SPECULATIVE;
        else
            result_flag = CLS_GC_TAGGED;
    }
    else if (target->cls->flags & CLS_GC_TAGGED)
        result_flag = CLS_GC_TAGGED;
    else if (target->cls->flags & CLS_GC_SPECULATIVE)
        result_flag = CLS_GC_SPECULATIVE;
    else if (target->cls->flags & CLS_VISITED)
        result_flag = CLS_GC_TAGGED;
    else if (target->subtype_count) {
        int i;
        for (i = 0;i < target->subtype_count;i++)
            result_flag |= get_gc_flags_for(top_class, target->subtypes[i]);
    }

    return result_flag;
}

/* A user-defined class is about to close. This scans over 'target' to find out
   if any properties inside of the class may require gc information. If such
   information is needed, then the class will have the appropriate flags set. */
static void determine_class_gc_flag(lily_parse_state *parser,
        lily_class *target)
{
    lily_class *parent_iter = target->parent;
    int mark;

    if (parent_iter) {
        /* Start with this, just in case the child has no properties. */
        mark = parent_iter->flags & (CLS_GC_TAGGED | CLS_GC_SPECULATIVE);
        if (mark == CLS_GC_TAGGED) {
            target->flags |= CLS_GC_TAGGED;
            return;
        }

        while (parent_iter) {
            parent_iter->flags |= CLS_VISITED;
            parent_iter = parent_iter->next;
        }
    }
    else
        mark = 0;

    lily_named_sym *prop_iter = target->members;

    while (prop_iter) {
        lily_type *type = prop_iter->type;
        mark |= get_gc_flags_for(target, type);

        if (type->subtype_count) {
            int i;
            for (i = 0;i < type->subtype_count;i++)
                mark |= get_gc_flags_for(target, type->subtypes[i]);
        }

        prop_iter = prop_iter->next;
    }

    /* To eliminate confusion, make sure only one is set. */
    if (mark & CLS_GC_TAGGED)
        mark &= ~CLS_GC_SPECULATIVE;

    parent_iter = target->parent;
    while (parent_iter) {
        parent_iter->flags &= ~CLS_VISITED;
        parent_iter = parent_iter->next;
    }

    target->flags &= ~CLS_VISITED;
    target->flags |= mark;
}

static void parse_class_body(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    lily_type *save_class_self_type = parser->class_self_type;

    int save_generics = parser->generic_count;
    parse_class_header(parser, cls);

    if (lex->token == tk_lt)
        parse_inheritance(parser, cls);

    NEED_CURRENT_TOK(tk_left_curly)
    parse_multiline_block_body(parser, 1);

    determine_class_gc_flag(parser, parser->class_self_type->cls);

    parser->class_self_type = save_class_self_type;
    lily_emit_leave_block(parser->emit);
    lily_update_symtab_generics(parser->symtab, save_generics);
    parser->generic_count = save_generics;
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

    parse_class_body(parser, lily_new_class(parser->symtab, lex->label));
}

/* This is called when a variant takes arguments. It parses those arguments to
   spit out the 'variant_type' conversion type of the variant. These types are
   internally really going to make a tuple instead of being a call. */
static void parse_variant_header(lily_parse_state *parser,
        lily_variant_class *variant_cls)
{
    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);
    if (lex->token == tk_right_parenth)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Empty () not needed for a variant.\n");

    int i = 1;
    int flags = 0;

    /* The return of a variant is always written as NULL. Since variants are
       special cased (they emit as tuple-like things), the return of a variant
       is NEVER checked or used. */
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

    lily_type *build_type = lily_tm_make(parser->tm, flags,
            parser->symtab->function_class, i);

    variant_cls->build_type = build_type;
    variant_cls->flags &= ~CLS_EMPTY_VARIANT;
}

static lily_class *parse_enum(lily_parse_state *parser, int is_dynaload)
{
    lily_block *block = parser->emit->block;
    if (is_dynaload == 0 &&
        block->block_type != block_file &&
        block->prev != NULL)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Cannot define an enum here.\n");

    lily_lex_state *lex = parser->lex;

    NEED_CURRENT_TOK(tk_word)
    if (is_dynaload == 0)
        ensure_valid_class(parser, lex->label);

    lily_class *enum_cls = lily_new_enum(parser->symtab, lex->label);

    lily_lexer(lex);
    int save_generics = parser->generic_count;
    collect_generics_or(parser, 0);
    enum_cls->generic_count = parser->generic_count;

    lily_emit_enter_block(parser->emit, block_enum);

    lily_type *result_type = build_self_type(parser, enum_cls,
           parser->generic_count);
    lily_type *save_self_type = parser->class_self_type;
    parser->class_self_type = result_type;

    NEED_CURRENT_TOK(tk_left_curly)
    lily_lexer(lex);

    int variant_count = 0;
    int is_scoped = (lex->token == tk_dot);

    while (1) {
        if (is_scoped) {
            NEED_CURRENT_TOK(tk_dot)
            lily_lexer(lex);
        }

        NEED_CURRENT_TOK(tk_word)
        if (is_dynaload == 0) {
            lily_class *cls = lily_find_class(parser->symtab, NULL, lex->label);

            /* Plain enums dump their variants into the same area that classes
               go. So if there is a clash of any sort, reject this. But scoped
               enums keep what they have behind themselves as a qualifier. So
               for scoped enums, only consider it a problem if there are more
               than two in the same scope. */
            if (cls != NULL && (is_scoped == 0 || cls->parent == enum_cls)) {
                lily_raise(parser->raiser, lily_SyntaxError,
                        "A class with the name '%s' already exists.\n",
                        cls->name);
            }
        }

        lily_variant_class *variant_cls = lily_new_variant(parser->symtab,
                enum_cls, lex->label, variant_count);
        variant_count++;

        lily_lexer(lex);
        if (lex->token == tk_left_parenth)
            parse_variant_header(parser, variant_cls);

        if (lex->token == tk_right_curly)
            break;
        else if (lex->token == tk_word && lex->label[0] == 'd' &&
                 keyword_by_name(lex->label) == KEY_DEFINE)
            break;
    }

    if (variant_count < 2) {
        lily_raise(parser->raiser, lily_SyntaxError,
                "An enum must have at least two variants.\n");
    }

    /* This marks the enum as one (allowing match), and registers the variants
       as being within the enum. Because of that, it has to go before pulling
       the member functions. */
    lily_finish_enum(parser->symtab, enum_cls, is_scoped, result_type);

    if (is_dynaload == 0 && lex->token == tk_word) {
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

    lily_update_symtab_generics(parser->symtab, save_generics);
    parser->generic_count = save_generics;
    lily_lexer(lex);

    return enum_cls;
}

static void enum_handler(lily_parse_state *parser, int multi)
{
    parse_enum(parser, 0);
}

static void process_match_case(lily_parse_state *parser, lily_sym *match_sym)
{
    lily_type *match_input_type = match_sym->type;
    lily_class *match_class = match_input_type->cls;
    lily_lex_state *lex = parser->lex;
    lily_variant_class *variant_case = NULL;

    NEED_CURRENT_TOK(tk_word)

    int i;
    for (i = 0;i < match_class->variant_size;i++) {
        if (strcmp(lex->label, match_class->variant_members[i]->name) == 0) {
            variant_case = match_class->variant_members[i];
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

    if ((variant_case->flags & CLS_EMPTY_VARIANT) == 0) {
        lily_type *build_type = variant_case->build_type;
        lily_type_system *ts = parser->emit->ts;
        NEED_NEXT_TOK(tk_left_parenth)
        /* There should be as many identifiers as there are arguments to this
           variant's creation type.
           Also, start at 1 so that the return at [0] is skipped. */
        NEED_NEXT_TOK(tk_word)

        for (i = 1;i < build_type->subtype_count;i++) {
            lily_type *var_type = lily_ts_resolve_by_second(ts,
                    match_input_type, build_type->subtypes[i]);

            /* It doesn't matter what the var is, only that it's unique. The
               emitter will grab the vars it needs from the symtab when writing
               the decompose.
               This function also calls up the next token. */
            get_named_var(parser, var_type);
            if (i != build_type->subtype_count - 1) {
                NEED_CURRENT_TOK(tk_comma)
                NEED_NEXT_TOK(tk_word)
            }
        }
        NEED_CURRENT_TOK(tk_right_parenth)

        lily_emit_variant_decompose(parser->emit, match_sym->reg_spot,
                build_type);
    }
    /* else the variant does not take arguments, and cannot decompose because
       there is nothing inside to decompose. */

    NEED_NEXT_TOK(tk_colon)
    lily_lexer(lex);
}


/* This checks that the current match block (which is exiting) is exhaustive. IF
   it is not, then a SyntaxError is raised. */
static void ensure_proper_match_block(lily_parse_state *parser,
        lily_sym *match_sym)
{
    int error = 0;
    int match_case_start = parser->emit->block->match_case_start;
    int i;
    lily_msgbuf *msgbuf = parser->raiser->aux_msgbuf;
    lily_class *match_class = match_sym->type->cls;

    for (i = match_case_start;i < parser->emit->match_case_pos;i++) {
        if (parser->emit->match_cases[i] == 0) {
            if (error == 0) {
                lily_msgbuf_add(msgbuf,
                        "Match pattern not exhaustive. The following case(s) are missing:\n");
                error = 1;
            }

            lily_msgbuf_add_fmt(msgbuf, "* %s\n",
                    match_class->variant_members[i]->name);
        }
    }

    if (error)
        lily_raise(parser->raiser, lily_SyntaxError, msgbuf->message);
}

static void match_handler(lily_parse_state *parser, int multi)
{
    if (multi == 0)
        lily_raise(parser->raiser, lily_SyntaxError,
                "Match block cannot be in a single-line block.\n");

    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, block_match);

    expression(parser);
    lily_emit_eval_match_expr(parser->emit, parser->expr);

    lily_sym *match_sym = parser->expr->root->result;

    NEED_CURRENT_TOK(tk_colon)
    NEED_NEXT_TOK(tk_left_curly)
    lily_lexer(lex);

    while (1) {
        if (lex->token == tk_word) {
            int key = keyword_by_name(lex->label);
            if (key == KEY_CASE) {
                lily_lexer(lex);
                process_match_case(parser, match_sym);
            }
            else if (key != -1) {
                lily_lexer(lex);
                handlers[key](parser, multi);
            }
            else {
                expression(parser);
                lily_emit_eval_expr(parser->emit, parser->expr);
            }
        }
        else if (lex->token != tk_right_curly)
            statement(parser, 0);
        else
            break;
    }

    ensure_proper_match_block(parser, match_sym);

    lily_lexer(lex);
    lily_emit_leave_block(parser->emit);
}

static void case_handler(lily_parse_state *parser, int multi)
{
    lily_raise(parser->raiser, lily_SyntaxError,
            "'case' not allowed outside of 'match'.\n");
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
    int save_generics = parser->generic_count;
    parse_define_header(parser, modifiers);

    NEED_CURRENT_TOK(tk_left_curly)
    parse_multiline_block_body(parser, 1);
    lily_emit_leave_block(parser->emit);
    lily_update_symtab_generics(parser->symtab, save_generics);

    /* If the function defined is at the top level of a class, then immediately
       make that function a member of the class.
       This is safe because 'define' always exits with the top-most variable
       being what was just defined. */
    if (parser->emit->block->block_type == block_class ||
        parser->emit->block->block_type == block_enum) {
        lily_add_class_method(parser->symtab,
                parser->class_self_type->cls,
                parser->symtab->active_module->var_chain);
    }
}

static void define_handler(lily_parse_state *parser, int multi)
{
    parse_define(parser, 0);
}

static void parse_modifier(lily_parse_state *parser, const char *name,
        int modifier)
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

/* This is the entry point of the parser. It parses the thing that it was given
   and then runs the code. This shouldn't be called directly, but instead by
   one of the lily_parse_* functions that will set it up right. */
static void parser_loop(lily_parse_state *parser, const char *filename)
{
    /* The first pass of the interpreter starts with the current namespace being
       the builtin namespace. */
    if (parser->first_pass) {
        parser->main_module->const_path = filename;

        set_module_names_by_path(parser->main_module, filename);

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

            /* Parser's in the first block now. If __main__'s code_pos is 0,
               then there's literally nothing to do. Maybe the only thing that
               happened was some builtin dynaloading. Regardless, there's no
               point in revving up the vm to do nothing. */
            if (lily_u16_pos(parser->emit->code) != 0) {
                lily_register_classes(parser->symtab, parser->vm);
                lily_prepare_main(parser->emit);
                lily_vm_prep(parser->vm, parser->symtab,
                        parser->symtab->literals->data, parser->foreign_values);

                update_all_cid_tables(parser);

                parser->executing = 1;
                lily_vm_execute(parser->vm);
                parser->executing = 0;

                /* Clear __main__ for the next pass. */
                lily_reset_main(parser->emit);
            }

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
                 lex->token == tk_dollar_string ||
                 lex->token == tk_tuple_open) {
            expression(parser);
            lily_emit_eval_expr(parser->emit, parser->expr);
        }
        else
            lily_raise(parser->raiser, lily_SyntaxError, "Unexpected token %s.\n",
                       tokname(lex->token));
    }
}

int lily_parse_file(lily_parse_state *parser, lily_lex_mode mode,
        const char *filename)
{
    /* It is safe to do this, because the parser will always occupy the first
       jump. All others should use lily_jump_setup instead. */
    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        char *suffix = strrchr(filename, '.');
        if (suffix == NULL || strcmp(suffix, ".lly") != 0)
            lily_raise(parser->raiser, lily_Error,
                    "File name must end with '.lly'.\n");

        lily_load_file(parser->lex, mode, filename);
        parser_loop(parser, filename);
        lily_pop_lex_entry(parser->lex);

        return 1;
    }

    return 0;
}

int lily_parse_string(lily_parse_state *parser, const char *name,
        lily_lex_mode mode, char *str)
{
    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        lily_load_str(parser->lex, mode, str);
        parser_loop(parser, name);
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

    if (raiser->exception_cls) {
        /* If this exception came from another package, then print that package
           name for context. The only way the parent is NULL is if it's coming
           from the builtin module which isn't in a package. */
        lily_package *package = raiser->exception_cls->module->parent;
        if (package->name[0] != '\0')
            lily_msgbuf_add_fmt(msgbuf, "%s.", package->name);
    }

    lily_msgbuf_add(msgbuf, lily_name_for_error(raiser));
    if (raiser->msgbuf->message[0] != '\0')
        lily_msgbuf_add_fmt(msgbuf, ": %s", raiser->msgbuf->message);
    else
        lily_msgbuf_add_char(msgbuf, '\n');

    if (parser->executing == 0) {
        lily_lex_entry *iter = parser->lex->entry;
        if (iter) {
            int fixed_line_num = (raiser->line_adjust == 0 ?
                    parser->lex->line_num : raiser->line_adjust);

            lily_msgbuf_add_fmt(msgbuf, "    from %s:%d\n",
                    parser->symtab->active_module->path, fixed_line_num);
        }
    }
    else {
        lily_call_frame *frame = parser->vm->call_chain;

        lily_msgbuf_add(msgbuf, "Traceback:\n");

        while (frame) {
            lily_function_val *func = frame->function;
            const char *class_name = func->class_name;
            char *separator;
            if (class_name == NULL) {
                class_name = "";
                separator = "";
            }
            else
                separator = ".";

            if (frame->function->code == NULL)
                lily_msgbuf_add_fmt(msgbuf, "    from [C]: in %s%s%s\n",
                        class_name, separator, func->trace_name);
            else
                lily_msgbuf_add_fmt(msgbuf,
                        "    from %s:%d: in %s%s%s\n",
                        func->module->path, frame->line_num, class_name,
                        separator, func->trace_name);

            frame = frame->prev;
        }
    }

    return msgbuf->message;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "lily.h"

#include "lily_config.h"
#include "lily_library.h"
#include "lily_parser.h"
#include "lily_parser_tok_table.h"
#include "lily_keyword_table.h"
#include "lily_string_pile.h"
#include "lily_value_flags.h"
#include "lily_value_raw.h"
#include "lily_alloc.h"

#include "lily_int_opcode.h"

#define NEED_NEXT_TOK(expected) \
lily_next_token(lex); \
if (lex->token != expected) \
    lily_raise_syn(parser->raiser, "Expected '%s', not '%s'.", \
               tokname(expected), tokname(lex->token));

#define NEED_CURRENT_TOK(expected) \
if (lex->token != expected) \
    lily_raise_syn(parser->raiser, "Expected '%s', not '%s'.", \
               tokname(expected), tokname(lex->token));

#define NEED_COLON_AND_BRACE \
NEED_CURRENT_TOK(tk_colon) \
NEED_NEXT_TOK(tk_left_curly)

#define NEED_COLON_AND_NEXT \
NEED_CURRENT_TOK(tk_colon) \
lily_next_token(lex);

extern lily_type *lily_question_type;
extern lily_class *lily_scoop_class;
extern lily_class *lily_self_class;
extern lily_type *lily_unit_type;
extern void lily_prelude_register(lily_vm_state *);

/***
 *      ____       _
 *     / ___|  ___| |_ _   _ _ __
 *     \___ \ / _ \ __| | | | '_ \
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/
 *                          |_|
 */

/** Parser init and teardown is ugly right now, for how it reaches into
    different structures more than it should. It's currently broken down into
    different phases:

    * Create raiser (it won't be used here).
    * Create symtab to receive builtin classes/methods/etc.
    * Create the builtin module, to give to symtab.
    * Load the builtin module.
    * Initialize other parts of the interpreter.
    * Link different parts of the interpreter up (sorry).
    * Create the first module (parsed files/strings will become the root).
    * Create __main__ to receive toplevel code from the first module.

    What's perhaps more interesting is that different functions within the
    parser will either take or return the vm. The reason for this is that it
    allows embedders to use a single lily_state throughout (with the vm state
    being a typedef for that). That's easier than a parse state here, and a vm
    state over there.

    API functions can be found at the bottom of this file. **/
static lily_module_entry *new_module(lily_parse_state *);
static void create_main_func(lily_parse_state *);
static void mark_builtin_modules(lily_parse_state *);
void lily_module_register(lily_state *, const char *, const char **,
        lily_call_entry_func *);
void lily_default_import_func(lily_state *, const char *);
void lily_stdout_print(lily_vm_state *);
void lily_prelude_register(lily_vm_state *);

typedef struct {
    const char **table;
    const char *entry;
    lily_module_entry *m;
    lily_class *cls;
    lily_item *result;
    lily_module_entry *saved_active;
    int index;
    int saved_generics;
} lily_dyna_state;

typedef struct lily_rewind_state_
{
    lily_class *main_class_start;
    lily_var *main_var_start;
    lily_boxed_sym *main_boxed_start;
    lily_module_link *main_last_module_link;
    lily_module_entry *main_last_module;
    uint16_t line_num;
    uint16_t pad;
    uint32_t pending;
} lily_rewind_state;

/* The import state (ims) holds data relevant to the interpreter's import hook.
   Rewind does not need to adjust any fields because they are set before running
   the import hook. */
typedef struct lily_import_state_ {
    /* Buffer for constructing paths into. */
    lily_msgbuf *path_msgbuf;

    /* This is set to NULL before running the import hook. If an import function
       succeeds, this is non-NULL. */
    lily_module_entry *last_import;

    /* This module called for the import. */
    lily_module_entry *source_module;

    /* Strictly for the import hook (might be NULL/invalid outside of it). This
       is the name that the imported module will have. It is also the name used
       in symbol searches if using `lily_import_library`. */
    const char *pending_loadname;

    /* The full path that was handed to import. This provides a nicer means of
       getting lex->label. Used internally. */
    const char *fullname;

    /* The directory the user passed. Goes between the source's root and the
       target. */
    const char *dirname;

    /* 1 if fullname has slashes, 0 otherwise. Used internally. */
    uint16_t is_slashed_path;

    /* 1 if a package import, 0 otherwise. If 1, path building adds a package
       base directory after the dirname above. */
    uint16_t is_package_import;

    uint32_t pad;
} lily_import_state;

extern const char *lily_builtin_info_table[];
extern lily_call_entry_func lily_builtin_call_table[];

void lily_init_pkg_builtin(lily_symtab *);

void lily_config_init(lily_config *conf)
{
    conf->argc = 0;
    conf->argv = NULL;

    /* Starting gc options are completely arbitrary. */
    conf->gc_start = 100;
    conf->gc_multiplier = 4;

    char key[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};

    memcpy(conf->sipkey, key, sizeof(key));

    conf->import_func = lily_default_import_func;
    conf->render_func = (lily_render_func)fputs;
    conf->data = stdout;
}

/* This sets up the core of the interpreter. It's pretty rough around the edges,
   especially with how the parser is assigning into all sorts of various structs
   when it shouldn't. */
lily_state *lily_new_state(lily_config *config)
{
    lily_parse_state *parser = lily_malloc(sizeof(*parser));
    parser->module_top = NULL;
    parser->module_start = NULL;
    parser->config = config;

    lily_raiser *raiser = lily_new_raiser();

    parser->data_string_pos = 0;
    parser->current_class = NULL;
    parser->raiser = raiser;
    parser->msgbuf = lily_new_msgbuf(64);
    parser->expr = lily_new_expr_state();
    parser->generics = lily_new_generic_pool();
    parser->symtab = lily_new_symtab();
    parser->vm = lily_new_vm_state(raiser);
    parser->rs = lily_malloc(sizeof(*parser->rs));
    parser->rs->pending = 0;
    parser->ims = lily_malloc(sizeof(*parser->ims));
    parser->ims->path_msgbuf = lily_new_msgbuf(64);
    parser->flags = 0;

    parser->vm->gs->parser = parser;
    parser->vm->gs->gc_multiplier = config->gc_multiplier;
    parser->vm->gs->gc_threshold = config->gc_start;

    /* This needs a name to build the [builtin] path from that later traceback
       will use. Registered module search starts after builtin, so nothing
       should see the name to load this module. */
    lily_module_register(parser->vm, "builtin", lily_builtin_info_table,
            lily_builtin_call_table);
    lily_set_builtin(parser->symtab, parser->module_top);
    lily_init_pkg_builtin(parser->symtab);

    parser->emit = lily_new_emit_state(parser->symtab, raiser);
    parser->lex = lily_new_lex_state(raiser);
    parser->data_stack = lily_new_buffer_u16(4);

    /* Here's the awful part where parser digs in and links everything that different
       sections need. */
    parser->tm = parser->emit->tm;

    parser->expr->lex_linenum = &parser->lex->line_num;

    parser->emit->lex_linenum = &parser->lex->line_num;
    parser->emit->symtab = parser->symtab;
    parser->emit->parser = parser;

    parser->expr_strings = parser->emit->expr_strings;
    parser->data_strings = lily_new_string_pile();

    lily_module_entry *main_module = new_module(parser);

    parser->main_module = main_module;
    parser->symtab->active_module = parser->main_module;

    /* This creates the var representing __main__ and registers it in areas that
       need it. */
    create_main_func(parser);
    lily_prelude_register(parser->vm);
    mark_builtin_modules(parser);

    return parser->vm;
}

static void free_links_until(lily_module_link *link_iter,
        lily_module_link *stop)
{
    while (link_iter != stop) {
        lily_module_link *link_next = link_iter->next;
        lily_free(link_iter->as_name);
        lily_free(link_iter);
        link_iter = link_next;
    }
}

#define free_links(iter) free_links_until(iter, NULL)

void lily_free_state(lily_state *vm)
{
    lily_parse_state *parser = vm->gs->parser;

    /* The code for the toplevel function (really __main__) is a pointer to
       emitter's code that gets refreshed before every vm entry. Set it to NULL
       so that these teardown functions don't double free the code. */
    parser->toplevel_func->proto->code = NULL;

    /* `Unit` is readonly and is buried at the bottom of the symtab. This
       unlinks it so it isn't destroyed. */
    parser->symtab->integer_class->next = NULL;

    lily_free_raiser(parser->raiser);

    lily_free_expr_state(parser->expr);

    lily_free_vm(parser->vm);

    lily_free_lex_state(parser->lex);

    lily_free_emit_state(parser->emit);

    lily_free_buffer_u16(parser->data_stack);

    /* The path for the first module is always a shallow copy of the loadname
       that was sent. Make sure that doesn't get free'd. */
    parser->main_module->path = NULL;

    lily_module_entry *module_iter = parser->module_start;
    lily_module_entry *module_next = NULL;

    while (module_iter) {
        free_links(module_iter->module_chain);

        module_next = module_iter->next;

        if (module_iter->handle)
            lily_library_free(module_iter->handle);

        lily_free_module_symbols(parser->symtab, module_iter);
        lily_free(module_iter->path);
        lily_free(module_iter->dirname);
        lily_free(module_iter->loadname);
        lily_free(module_iter->cid_table);
        lily_free(module_iter);

        module_iter = module_next;
    }

    lily_free_string_pile(parser->data_strings);
    lily_free_symtab(parser->symtab);
    lily_free_generic_pool(parser->generics);
    lily_free_msgbuf(parser->msgbuf);
    lily_free_msgbuf(parser->ims->path_msgbuf);
    lily_free(parser->ims);
    lily_free(parser->rs);

    lily_free(parser);
}

static void rewind_parser(lily_parse_state *parser)
{
    lily_u16_set_pos(parser->data_stack, 0);
    parser->data_string_pos = 0;
    parser->current_class = NULL;

    /* Parser's flags are reset when the first content loads. */
    lily_module_entry *module_iter = parser->rs->main_last_module;

    while (module_iter) {
        /* Hide broken modules from being loaded in the next pass as though
           they weren't broken.
           todo: Dump module contents once rewind is tested out more. */
        if (module_iter->flags & MODULE_IN_EXECUTION) {
            module_iter->cmp_len = 0;
            module_iter->flags &= ~MODULE_IN_EXECUTION;
        }
        module_iter = module_iter->next;
    }
}

static void rewind_interpreter(lily_parse_state *parser)
{
    lily_rewind_state *rs = parser->rs;
    uint16_t executing = parser->flags & PARSER_IS_EXECUTING;

    rewind_parser(parser);
    lily_rewind_generic_pool(parser->generics);
    lily_rewind_expr_state(parser->expr);
    lily_rewind_emit_state(parser->emit);
    lily_rewind_type_system(parser->emit->ts);
    lily_rewind_lex_state(parser->lex, rs->line_num);
    lily_rewind_raiser(parser->raiser);
    lily_rewind_vm(parser->vm);

    /* Symtab will hide or delete symbols based on the execution state. Symbols
       that made it to execution might still be in use and are hidden. If they
       didn't make it that far, they'll be deleted. */
    lily_rewind_symtab(parser->symtab, parser->main_module,
            rs->main_class_start, rs->main_var_start, rs->main_boxed_start,
            executing);

    parser->rs->pending = 0;
}

static void initialize_rewind(lily_parse_state *parser)
{
    lily_rewind_state *rs = parser->rs;
    lily_module_entry *m = parser->main_module;

    rs->main_class_start = m->class_chain;
    rs->main_var_start = m->var_chain;
    rs->main_boxed_start = m->boxed_chain;
    rs->main_last_module_link = m->module_chain;
    rs->main_last_module = parser->module_top;
    rs->line_num = parser->lex->line_num;
}

static void mark_builtin_modules(lily_parse_state *parser)
{
    lily_module_entry *module_iter = parser->module_start;
    while (module_iter) {
        module_iter->flags |= MODULE_IS_PREDEFINED;
        module_iter = module_iter->next;
    }
}

static void add_data_string(lily_parse_state *parser, const char *to_add)
{
    lily_u16_write_1(parser->data_stack, parser->data_string_pos);
    lily_sp_insert(parser->data_strings, to_add, &parser->data_string_pos);
}

/***
 *      ____                      _
 *     / ___|  ___  __ _ _ __ ___| |__
 *     \___ \ / _ \/ _` | '__/ __| '_ \
 *      ___) |  __/ (_| | | | (__| | | |
 *     |____/ \___|\__,_|_|  \___|_| |_|
 *
 */

static lily_class *find_run_class_dynaload(lily_parse_state *,
        lily_module_entry *, const char *);

static lily_var *find_active_var(lily_parse_state *parser, const char *name)
{
    lily_module_entry *m = parser->symtab->active_module;
    lily_var *result = lily_find_var(m, name);

    return result;
}

static lily_class *find_active_class(lily_parse_state *parser, const char *name)
{
    lily_module_entry *m = parser->symtab->active_module;
    lily_class *result = lily_find_class(m, name);

    return result;
}

lily_class *find_or_dl_class(lily_parse_state *parser, lily_module_entry *m,
        const char *name)
{
    lily_symtab *symtab = parser->symtab;
    lily_class *result = NULL;

    if (m == symtab->active_module) {
        lily_module_entry *builtin = symtab->builtin_module;

        result = lily_find_class(builtin, name);

        if (result == NULL && name[1] == '\0')
            result = lily_gp_find(parser->generics, name);

        if (result == NULL)
            result = find_run_class_dynaload(parser, builtin, name);
    }

    if (result == NULL)
        result = lily_find_class(m, name);

    if (result == NULL && m->info_table)
        result = find_run_class_dynaload(parser, m, name);

    return result;
}

lily_sym *find_existing_sym(lily_module_entry *m, const char *name)
{
    lily_sym *result = NULL;

    result = (lily_sym *)lily_find_var(m, name);

    if (result == NULL)
        result = (lily_sym *)lily_find_class(m, name);

    if (result == NULL)
        result = (lily_sym *)lily_find_module(m, name);

    return result;
}

/***
 *      ___                            _
 *     |_ _|_ __ ___  _ __   ___  _ __| |_
 *      | || '_ ` _ \| '_ \ / _ \| '__| __|
 *      | || | | | | | |_) | (_) | |  | |_
 *     |___|_| |_| |_| .__/ \___/|_|   \__|
 *                   |_|
 */

/** Within the interpreter, code is broken down into different modules. Every
    module is a namespace that can contain classes, enums, variables, and
    possibly other modules. Every module is a 1-to-1 mapping between some
    symbol source (like sys), a dynamically-loaded library, a string source, or
    a file.

    The builtin module is the foundation of Lily, and the only module that is
    implicitly loaded. All other modules must be explicitly loaded through the
    import keyword. This is intentional. An important part of Lily is being able
    to know where symbols come from. The same reasoning is why `import *` is not
    present in the interpreter.

    What about grouping modules together? A package is a 1-to-many grouping of
    modules. `sys` is referred to as a package, for example, because even though
    it is one module, it is complete by itself. Inside the interpreter, there is
    no strict representation of packages apart from modules. Rather, the
    distinction comes in loading behavior.

    The first source (file or string) that is loaded into the interpreter
    automatically becomes a root module. Additionally, the first module of any
    package that is loaded becomes the root for that package. What's the
    difference? Internally, it's that non-root modules borrow the root_dirname
    field from the root module of their respective package.

    When the interpreter wants to run an import, that import is done relative to
    the root module. Consider the following example:

    ```
    farmer.lily
    vegetables/
        common.lily
        spinach.lily
        broccoli.lily
    ```

    If execution starts with `farmer.lily`, then it becomes the root module. If
    `spinach.lily` wants to import `common.lily`, it must use
    `import "vegetables/common"` even though it exists in the same directory as
    `common.lily`. This was done to lessen the amount of relative (`../`)
    imports and for packages.

    How does the interpreter know the difference between a regular module and
    the root module of a package? Packages in Lily have a specific organization.
    Following the above example:

    ```
    farmer.lily
    packages/
        potato/
            src/
                potato.lily
    vegetables/
        common.lily
        spinach.lily
        broccoli.lily
    ```

    The root module of `potato` is `packages/potato/src/potato.suffix`. For an
    example `apple` module, it would be `packages/apple/src/apple.suffix`. Since
    modules are relative from the root, `spinach.lily` can simply use
    `import potato` to open the potato package, rooted at the above-mentioned
    `potato.lily` file.

    Designing packages this way allows them to have subpackages and a module
    tree of their own. But there's one other catch. Lily is also supposed to be
    embeddable, and different embedders may have different needs.

    The import hook is the solution to this. The hook selects between a local
    directory and a package directory. The selection is also able to specify a
    directory to go between the root and the target provided. An embedder can
    make `tests/` able to access `src/`. A sandbox can choose to skip library
    loads. **/

static lily_module_entry *new_module(lily_parse_state *parser)
{
    lily_module_entry *module = lily_malloc(sizeof(*module));

    module->loadname = NULL;
    module->dirname = NULL;
    module->path = NULL;
    module->cmp_len = 0;
    module->info_table = NULL;
    module->cid_table = NULL;
    module->next = NULL;
    module->module_chain = NULL;
    module->class_chain = NULL;
    module->var_chain = NULL;
    module->handle = NULL;
    module->call_table = NULL;
    module->boxed_chain = NULL;
    module->item_kind = ITEM_MODULE;
    /* If the module has a foreign source, setting the data will drop this. */
    module->flags = MODULE_NOT_EXECUTED;
    module->root_dirname = NULL;

    if (parser->module_start) {
        parser->module_top->next = module;
        parser->module_top = module;
    }
    else {
        parser->module_start = module;
        parser->module_top = module;
    }

    parser->ims->last_import = module;

    return module;
}

static void add_data_to_module(lily_module_entry *module, void *handle,
        const char **table, lily_foreign_func *call_table)
{
    module->handle = handle;
    module->info_table = table;
    module->call_table = call_table;
    module->flags &= ~MODULE_NOT_EXECUTED;

    unsigned char cid_count = module->info_table[0][0];

    if (cid_count) {
        module->cid_table = lily_malloc(cid_count * sizeof(*module->cid_table));
        memset(module->cid_table, 0, cid_count * sizeof(*module->cid_table));
    }
}

/* Return a path without dot-slash characters at the front. Import path building
   adds those to prevent some platforms from using system load paths. */
static const char *simplified_path(const char *path)
{
    if (path[0] == '.' && path[1] == LILY_PATH_CHAR)
        path += 2;

    return path;
}

static void add_path_to_module(lily_module_entry *module,
            const char *loadname, const char *path)
{
    module->loadname = lily_malloc(
            (strlen(loadname) + 1) * sizeof(*module->loadname));
    strcpy(module->loadname, loadname);

    path = simplified_path(path);

    module->cmp_len = strlen(path);
    module->path = lily_malloc((strlen(path) + 1) * sizeof(*module->path));
    strcpy(module->path, path);
}

static char *dir_from_path(const char *path)
{
    const char *slash = strrchr(path, LILY_PATH_CHAR);
    char *out;

    if (slash == NULL) {
        out = lily_malloc(1 * sizeof(*out));
        out[0] = '\0';
    }
    else {
        int bare_len = slash - path;
        out = lily_malloc((bare_len + 1) * sizeof(*out));

        strncpy(out, path, bare_len);
        out[bare_len] = '\0';
    }

    return out;
}

static void set_dirs_on_module(lily_parse_state *parser,
        lily_module_entry *module)
{
    /* This only needs to be called on modules that will run an import. Foreign
       modules can skip this. */
    if (parser->ims->is_package_import) {
        module->dirname = dir_from_path(module->path);
        module->root_dirname = module->dirname;
    }
    else
        module->root_dirname = parser->ims->source_module->root_dirname;
}

static int import_check(lily_parse_state *parser, const char *path)
{
    lily_module_entry *m = parser->ims->last_import;
    int result = 1;

    if (m == NULL && path != NULL) {
        /* Import path building adds a dot-slash at the front that isn't stored
           in the module's path (it's useless). Use a fixed path or the module
           search will incorrectly turn up empty. */
        path = simplified_path(path);

        m = lily_find_module_by_path(parser->symtab, path);
        if (m != NULL)
            parser->ims->last_import = m;
        else
            result = 0;
    }

    return result;
}

static void add_fixslash_dir(lily_msgbuf *msgbuf, const char *input_str)
{
#ifdef _WIN32
    /* For Windows, add the directory but replacing '/' with '\\'. */
    int start = 0, stop = 0;
    const char *ch = &input_str[0];

    while (1) {
        if (*ch == '/') {
            stop = (ch - input_str);
            lily_mb_add_slice(msgbuf, input_str, start, stop);
            lily_mb_add_char(msgbuf, '\\');
            start = stop + 1;
        }
        else if (*ch == '\0')
            break;

        ch++;
    }

    if (start != 0) {
        stop = (ch - input_str);
        lily_mb_add_slice(msgbuf, input_str, start, stop);
    }
#else
    /* Platforms which already use '/' can simply add the string. */
    lily_mb_add(msgbuf, input_str);
#endif
    int len = strlen(input_str);

    if (input_str[len] != LILY_PATH_CHAR)
        lily_mb_add_char(msgbuf, LILY_PATH_CHAR);
}

static const char *build_import_path(lily_import_state *ims, const char *target,
        const char *suffix)
{
    /* Make sure the caller used a `lily_import_use_*` function first. */
    if (ims->dirname == NULL)
        return NULL;

    /* The packages directory is always flat, so slashed paths will always fail
       to match. Don't let them through. */
    if (ims->is_package_import &&
        strchr(target, LILY_PATH_CHAR))
        return NULL;

    lily_msgbuf *path_msgbuf = lily_mb_flush(ims->path_msgbuf);
    const char *root_dirname = ims->source_module->root_dirname;

    if (root_dirname == NULL || root_dirname[0] == '\0')
        lily_mb_add_char(path_msgbuf, '.');
    else
        lily_mb_add(path_msgbuf, root_dirname);

    lily_mb_add_char(path_msgbuf, LILY_PATH_CHAR);

    if (ims->dirname[0] != '\0')
        add_fixslash_dir(path_msgbuf, ims->dirname);

    if (ims->is_package_import == 1) {
        lily_mb_add_fmt(path_msgbuf,
                "packages" LILY_PATH_SLASH
                "%s" LILY_PATH_SLASH
                "src" LILY_PATH_SLASH, target);
    }

    lily_mb_add(path_msgbuf, target);
    lily_mb_add(path_msgbuf, suffix);

    return lily_mb_raw(path_msgbuf);
}

int lily_import_file(lily_state *s, const char *name)
{
    lily_parse_state *parser = s->gs->parser;
    const char *path = build_import_path(parser->ims, name, ".lily");

    if (import_check(parser, path))
        return path != NULL;

    FILE *source = fopen(path, "r");
    if (source == NULL) {
        add_data_string(parser, path);
        return 0;
    }

    lily_lexer_load(parser->lex, et_file, source);

    lily_module_entry *module = new_module(parser);

    add_path_to_module(module, parser->ims->pending_loadname, path);
    set_dirs_on_module(parser, module);
    return 1;
}

int lily_import_string(lily_state *s, const char *name, const char *source)
{
    lily_parse_state *parser = s->gs->parser;
    const char *path = build_import_path(parser->ims, name, ".lily");

    if (import_check(parser, path))
        return path != NULL;

    /* Always copy strings to be imported. The string being sent may have a
       lifetime that cannot be guaranteed to be as long as the interpreter's
       current parse/render cycle. */
    lily_lexer_load(parser->lex, et_copied_string, (char *)source);

    lily_module_entry *module = new_module(parser);

    add_path_to_module(module, parser->ims->pending_loadname, path);
    set_dirs_on_module(parser, module);
    return 1;
}

int lily_import_library(lily_state *s, const char *name)
{
    lily_parse_state *parser = s->gs->parser;
    const char *path = build_import_path(parser->ims, name,
            "." LILY_LIB_SUFFIX);

    if (import_check(parser, path))
        return path != NULL;

    void *handle = lily_library_load(path);
    if (handle == NULL) {
        add_data_string(parser, path);
        return 0;
    }

    lily_msgbuf *msgbuf = lily_mb_flush(parser->msgbuf);
    const char *loadname = parser->ims->pending_loadname;

    const char **info_table = (const char **)lily_library_get(handle,
            lily_mb_sprintf(msgbuf, "lily_%s_info_table", loadname));

    lily_foreign_func *call_table = lily_library_get(handle,
            lily_mb_sprintf(msgbuf, "lily_%s_call_table", loadname));

    if (info_table == NULL || call_table == NULL) {
        add_data_string(parser, path);
        lily_library_free(handle);
        return 0;
    }

    lily_module_entry *module = new_module(parser);

    add_path_to_module(module, parser->ims->pending_loadname, path);
    add_data_to_module(module, handle, info_table, call_table);
    return 1;
}

int lily_import_library_data(lily_state *s, const char *path,
        const char **info_table, lily_call_entry_func *call_table)
{
    lily_parse_state *parser = s->gs->parser;

    if (import_check(parser, path))
        return 1;

    lily_module_entry *module = new_module(parser);

    add_path_to_module(module, parser->ims->pending_loadname, path);
    add_data_to_module(module, NULL, info_table, call_table);
    return 1;
}

void lily_module_register(lily_state *s, const char *name,
        const char **info_table, lily_call_entry_func *call_table)
{
    lily_parse_state *parser = s->gs->parser;
    lily_module_entry *module = new_module(parser);

    /* This special "path" is for vm and parser traceback. */
    const char *module_path = lily_mb_sprintf(parser->msgbuf, "[%s]", name);

    add_path_to_module(module, name, module_path);
    add_data_to_module(module, NULL, info_table, call_table);
    module->cmp_len = 0;
    module->flags |= MODULE_IS_REGISTERED;
}

/* This adds 'to_link' as an entry within 'target' so that 'target' is able to
   reference it later on. If 'as_name' is not NULL, then 'to_link' will be
   available through that name. Otherwise, it will be available as the name it
   actually has. */
static void link_module_to(lily_module_entry *target, lily_module_entry *to_link,
        const char *as_name)
{
    lily_module_link *new_link = lily_malloc(sizeof(*new_link));
    char *link_name;
    if (as_name == NULL)
        link_name = NULL;
    else {
        link_name = lily_malloc((strlen(as_name) + 1) * sizeof(*link_name));
        strcpy(link_name, as_name);
    }

    new_link->module = to_link;
    new_link->next = target->module_chain;
    new_link->as_name = link_name;

    target->module_chain = new_link;
}

void lily_import_use_local_dir(lily_state *s, const char *dirname)
{
    lily_import_state *ims = s->gs->parser->ims;

    ims->dirname = dirname;
    ims->is_package_import = 0;
}

void lily_import_use_package_dir(lily_state *s, const char *dirname)
{
    lily_import_state *ims = s->gs->parser->ims;

    ims->dirname = dirname;
    ims->is_package_import = 1;
}

const char *lily_import_current_root_dir(lily_state *s)
{
    lily_parse_state *parser = s->gs->parser;

    const char *current_root = parser->ims->source_module->root_dirname;
    const char *first_root = parser->main_module->root_dirname;
    const char *result = current_root + strlen(first_root);

    return result;
}

void lily_default_import_func(lily_state *s, const char *target)
{
    /* Perform a local search from this directory. */
    lily_import_use_local_dir(s, "");
    if (lily_import_file(s, target) ||
        lily_import_library(s, target))
        return;

    /* Packages are in this directory as well. */
    lily_import_use_package_dir(s, "");
    if (lily_import_file(s, target) ||
        lily_import_library(s, target))
        return;
}

static lily_module_entry *load_module(lily_parse_state *parser,
        const char *name)
{
    uint16_t save_pos = lily_u16_pos(parser->data_stack);

    parser->config->import_func(parser->vm, name);

    if (parser->ims->last_import == NULL) {
        lily_msgbuf *msgbuf = lily_mb_flush(parser->msgbuf);

        lily_mb_add_fmt(msgbuf, "Cannot import '%s':", name);

        if (parser->ims->is_slashed_path == 0)
            lily_mb_add_fmt(msgbuf, "\n    no preloaded package '%s'", name);

        lily_buffer_u16 *b = parser->data_stack;
        uint16_t i;

        for (i = save_pos;i < lily_u16_pos(b);i++) {
            uint16_t check_pos = lily_u16_get(b, i);
            lily_mb_add_fmt(msgbuf, "\n    no file '%s'",
                    lily_sp_get(parser->data_strings, check_pos));
        }

        lily_raise_syn(parser->raiser, lily_mb_raw(msgbuf));
    }

    lily_u16_set_pos(parser->data_stack, save_pos);

    return parser->ims->last_import;
}

/***
 *     __     __
 *     \ \   / /_ _ _ __ ___
 *      \ \ / / _` | '__/ __|
 *       \ V / (_| | |  \__ \
 *        \_/ \__,_|_|  |___/
 *
 */

/** Parser is responsible for creating new vars and putting them where they need
    to go. Most callers want to create a scoped var which will use the emitter
    to determine if the var should be global or local.

    This is also where vars created by `define` have their backing
    lily_function_val struct stored. Native `define` vars will have their code
    field filled when the `define` reaches the end. **/

static void error_var_redeclaration(lily_parse_state *parser, lily_var *var)
{
    lily_raise_syn(parser->raiser, "%s has already been declared.", var->name);
}

static void make_new_function(lily_parse_state *parser, const char *class_name,
        lily_var *var, lily_foreign_func foreign_func)
{
    lily_function_val *f = lily_malloc(sizeof(*f));
    lily_module_entry *m = parser->symtab->active_module;
    lily_proto *proto = lily_emit_new_proto(parser->emit, m->path, class_name,
            var->name);

    /* This won't get a ref bump from being moved/assigned since all functions
       are marked as literals. Start at 1 ref, not 0. */
    f->refcount = 1;
    f->foreign_func = foreign_func;
    f->code = NULL;
    f->num_upvalues = 0;
    f->upvalues = NULL;
    f->gc_entry = NULL;
    f->cid_table = m->cid_table;
    f->proto = proto;

    lily_value *v = lily_malloc(sizeof(*v));
    v->flags = V_FUNCTION_FLAG | V_FUNCTION_BASE;
    v->value.function = f;

    lily_vs_push(parser->symtab->literals, v);
}

static void put_keyargs_in_target(lily_parse_state *parser, lily_item *target,
        uint16_t arg_start)
{
    char *source = lily_sp_get(parser->data_strings, arg_start);
    int len = parser->data_string_pos - arg_start + 1;
    char *buffer = lily_malloc(len * sizeof(*buffer));

    memcpy(buffer, source, len);

    if (target->item_kind == ITEM_VAR) {
        lily_var *var = (lily_var *)target;
        lily_proto *p = lily_emit_proto_for_var(parser->emit, var);

        p->arg_names = buffer;
    }
    else {
        lily_variant_class *c = (lily_variant_class *)target;

        c->arg_names = buffer;
    }
}

static void hide_block_vars(lily_parse_state *parser)
{
    int count = parser->emit->block->var_count;

    if (count == 0)
        return;

    lily_var *var_iter = parser->symtab->active_module->var_chain;
    lily_var *var_next;

    while (count) {
        var_next = var_iter->next;

        if (var_iter->flags & VAR_IS_READONLY) {
            var_iter->next = parser->symtab->hidden_function_chain;
            parser->symtab->hidden_function_chain = var_iter;
            count--;
        }
        else {
            /* todo: Store vars that are out of scope instead of destroying them. */
            lily_free(var_iter->name);
            lily_free(var_iter);
            count--;
        }

        var_iter = var_next;
    }

    parser->symtab->active_module->var_chain = var_iter;
    parser->emit->block->var_count = 0;
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

/* This handles the common parts of var initialization. Flags aren't set here
   because var builders have different hardcoded flags that they want. */
static lily_var *new_var(lily_type *type, const char *name, uint16_t line_num)
{
    lily_var *var = lily_malloc(sizeof(*var));

    var->item_kind = ITEM_VAR;
    var->name = lily_malloc((strlen(name) + 1) * sizeof(*var->name));
    strcpy(var->name, name);
    var->line_num = line_num;
    var->shorthash = shorthash_for_name(name);
    var->closure_spot = (uint16_t)-1;
    var->type = type;
    var->next = NULL;
    var->parent = NULL;

    return var;
}

/* Create a new var that must be local. Use this in cases where the target is
   going to have some data extracted into it. In situations such as the except
   clause of try, the target register has to be local. */
static lily_var *new_local_var(lily_parse_state *parser, lily_type *type,
        const char *name, uint16_t line_num)
{
    lily_var *var = new_var(type, name, line_num);
    lily_module_entry *m = parser->symtab->active_module;

    var->function_depth = parser->emit->function_depth;
    var->reg_spot = parser->emit->scope_block->next_reg_spot;
    var->flags = 0;
    parser->emit->scope_block->next_reg_spot++;
    parser->emit->block->var_count++;
    var->next = m->var_chain;
    m->var_chain = var;

    return var;
}

static lily_var *new_global_var(lily_parse_state *parser, lily_type *type,
        const char *name, uint16_t line_num)
{
    lily_var *var = new_var(type, name, line_num);
    lily_module_entry *m = parser->symtab->active_module;

    var->function_depth = 1;
    var->reg_spot = parser->symtab->next_global_id;
    var->flags = VAR_IS_GLOBAL;
    parser->symtab->next_global_id++;
    var->next = m->var_chain;
    m->var_chain = var;

    /* Each global occupies a spot in the vm. Dynaloaded vars will call a
       foreign function to write their value on the vm. To make sure native
       globals have the right spot, they need a placeholder. Native globals
       won't know their type yet, so use that for the check. */
    if (type == NULL) {
        lily_push_unit(parser->vm);
        parser->emit->block->var_count++;
    }

    return var;
}

static lily_var *new_define_var(lily_parse_state *parser, const char *name,
        uint16_t line_num)
{
    lily_var *var = new_var(NULL, name, line_num);
    lily_module_entry *m = parser->symtab->active_module;

    var->reg_spot = lily_vs_pos(parser->symtab->literals);
    var->function_depth = 1;
    var->flags = VAR_IS_READONLY;
    var->next = m->var_chain;
    m->var_chain = var;

    if (line_num)
        parser->emit->block->var_count++;

    make_new_function(parser, NULL, var, NULL);

    return var;
}

static lily_var *new_method_var(lily_parse_state *parser, lily_class *parent,
        const char *name, uint16_t modifiers, uint16_t line_num)
{
    lily_var *var = new_var(NULL, name, line_num);

    var->reg_spot = lily_vs_pos(parser->symtab->literals);
    var->function_depth = 1;
    var->flags = VAR_IS_READONLY | modifiers;
    var->parent = parent;
    var->next = (lily_var *)parent->members;
    parent->members = (lily_named_sym *)var;

    make_new_function(parser, parent->name, var, NULL);

    return var;
}

/* This creates a new local var using the current identifier as the name. */
static lily_var *declare_local_var(lily_parse_state *parser, lily_type *type)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var = find_active_var(parser, lex->label);

    if (var)
        error_var_redeclaration(parser, var);

    var = new_local_var(parser, type, lex->label, lex->line_num);
    lily_next_token(lex);
    return var;
}

/* This is used by the var keyword. */
static lily_var *declare_scoped_var(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var = find_active_var(parser, lex->label);

    if (var)
        error_var_redeclaration(parser, var);

    if (parser->emit->function_depth != 1)
        var = new_local_var(parser, NULL, lex->label, lex->line_num);
    else
        var = new_global_var(parser, NULL, lex->label, lex->line_num);

    lily_next_token(lex);
    return var;
}

static void create_main_func(lily_parse_state *parser)
{
    lily_type_maker *tm = parser->emit->tm;
    lily_lex_state *lex = parser->lex;
    uint16_t save_line = lex->line_num;

    lily_tm_add(tm, lily_unit_type);
    lily_type *main_type = lily_tm_make_call(tm, 0,
            parser->symtab->function_class, 1);

    /* __main__'s line number should be 1 since it's not a foreign function.
       Since lexer hasn't read in the first line (it might be broken and raiser
       isn't ready yet), the line number is at 0. */
    lex->line_num = 1;

    lily_var *main_var = new_define_var(parser, "__main__", lex->line_num);
    lily_value *v = lily_vs_nth(parser->symtab->literals, 0);
    lily_function_val *f = v->value.function;

    lex->line_num = save_line;
    main_var->type = main_type;
    main_var->module = parser->symtab->active_module;

    /* The vm carries a toplevel frame to hold globals, so that globals survive
       when __main__ is done. The toplevel frame needs a function value to hold
       module cid tables when dynaload executes. */
    parser->vm->call_chain->function = f;
    parser->toplevel_func = f;
    /* This is used by the magic constant __function__. */
    parser->emit->block->scope_var = main_var;
}

/***
 *      _____                     ____      _ _           _   _
 *     |_   _|   _ _ __   ___    / ___|___ | | | ___  ___| |_(_) ___  _ __
 *       | || | | | '_ \ / _ \  | |   / _ \| | |/ _ \/ __| __| |/ _ \| '_ \
 *       | || |_| | |_) |  __/  | |__| (_) | | |  __/ (__| |_| | (_) | | | |
 *       |_| \__, | .__/ \___|   \____\___/|_|_|\___|\___|\__|_|\___/|_| |_|
 *           |___/|_|
 */

static lily_type *get_type_raw(lily_parse_state *, int);
static lily_class *resolve_class_name(lily_parse_state *);
static int constant_by_name(const char *);
static lily_prop_entry *declare_property(lily_parse_state *, uint16_t);
static void simple_expression(lily_parse_state *);
static int keyword_by_name(const char *);

/** Type collection can be roughly dividied into two subparts. One half deals
    with general collection of types that either do or don't have a name. The
    other half deals with optional arguments (optargs) and optional argument
    value collection.
    There's a small bit that deals with making sure that the self_type of a
    class is properly set. For enums, self_type is used for solving variants, so
    it's important that self_type be right. **/

/* Collect the assignment for an optional argument. */
static void collect_optarg_for(lily_parse_state *parser, lily_sym *sym)
{
    lily_es_optarg_save(parser->expr);
    lily_es_push_assign_to(parser->expr, sym);
    lily_next_token(parser->lex);
    simple_expression(parser);
}

/* Return a type that is an optional of the type given. This is the only place
   where optarg types are created, and also should never be given an optarg type
   as input. */
static lily_type *make_optarg_of(lily_parse_state *parser, lily_type *type)
{
    lily_tm_add(parser->tm, type);
    lily_type *t = lily_tm_make(parser->tm, parser->symtab->optarg_class, 1);

    /* Since this function is the only place that optargs are created, this also
       marks optargs as what they are. This allows the optarg information to
       later bubble up. */
    t->flags |= TYPE_HAS_OPTARGS;

    return t;
}

/* This checks to see if 'type' got as many subtypes as it was supposed to. If
   it did not, then SyntaxError is raised.
   For now, this also includes an extra check. It attempts to ensure that the
   key of a hash is something that is hashable (or a generic type). */
static void ensure_valid_type(lily_parse_state *parser, lily_type *type)
{
    if (type->subtype_count != type->cls->generic_count &&
        type->cls->generic_count != -1)
        lily_raise_syn(parser->raiser,
                "Class %s expects %d type(s), but got %d type(s).",
                type->cls->name, type->cls->generic_count,
                type->subtype_count);

    /* Hack: This exists because Lily does not understand constraints. */
    if (type->cls == parser->symtab->hash_class) {
        lily_type *key_type = type->subtypes[0];
        uint16_t key_id = key_type->cls->id;

        if (key_id != LILY_ID_INTEGER &&
            key_id != LILY_ID_STRING &&
            key_id != LILY_ID_GENERIC)
            lily_raise_syn(parser->raiser, "'^T' is not a valid key for Hash.",
                    key_type);
    }
}

/* These are flags used by argument collection. They start high so that type and
   class flags don't collide with them. */
#define F_SCOOP_OK          0x040000
#define F_COLLECT_DEFINE    0x100000
#define F_COLLECT_DYNALOAD (0x200000 | F_SCOOP_OK)
#define F_COLLECT_CLASS     0x400000
#define F_COLLECT_VARIANT   0x800000
#define F_NO_COLLECT        0x00FFFF

/* Collect one argument. Unlike raw type collection, this supports optargs and
   varargs. If either is found, the relevant TYPE_ flag is set on the flags
   provided.

   Note: Caller is expected to handle expression collection for optargs. */
static lily_type *get_nameless_arg(lily_parse_state *parser, int *flags)
{
    lily_lex_state *lex = parser->lex;

    if (lex->token == tk_multiply) {
        *flags |= TYPE_HAS_OPTARGS;
        lily_next_token(lex);
    }
    else if (*flags & TYPE_HAS_OPTARGS)
        lily_raise_syn(parser->raiser,
                "Non-optional argument follows optional argument.");

    lily_type *type = get_type_raw(parser, *flags);

    /* get_type ends with a call to lily_lexer, so don't call that again. */

    if (type->flags & TYPE_HAS_SCOOP)
        *flags |= TYPE_HAS_SCOOP;

    if (lex->token == tk_three_dots) {
        lily_tm_add(parser->tm, type);
        type = lily_tm_make(parser->tm, parser->symtab->list_class, 1);

        lily_next_token(lex);
        if (lex->token != tk_arrow &&
            lex->token != tk_right_parenth &&
            lex->token != tk_equal)
            lily_raise_syn(parser->raiser,
                    "Expected either '=>' or ')' after varargs.");

        *flags |= TYPE_IS_VARARGS;
    }

    if (*flags & TYPE_HAS_OPTARGS)
        type = make_optarg_of(parser, type);

    return type;
}

static lily_type *get_variant_arg(lily_parse_state *parser, int *flags)
{
    lily_type *type = get_nameless_arg(parser, flags);

    if (*flags & TYPE_HAS_OPTARGS)
        lily_raise_syn(parser->raiser,
                "Variant types cannot have default values.");

    return type;
}

static lily_type *get_define_arg(lily_parse_state *parser, int *flags)
{
    lily_lex_state *lex = parser->lex;

    NEED_CURRENT_TOK(tk_word)

    lily_var *var = declare_local_var(parser, NULL);

    NEED_COLON_AND_NEXT;

    lily_type *type = get_nameless_arg(parser, flags);

    if (*flags & TYPE_HAS_OPTARGS) {
        /* Optional arguments are created by making an optarg type which holds
           a type that is supposed to be optional. For simplicity, give the
           var the concrete underlying type, and the caller the true optarg
           containing type. */
        var->type = type->subtypes[0];
        var->flags |= SYM_NOT_INITIALIZED;
        collect_optarg_for(parser, (lily_sym *)var);
    }
    else
        var->type = type;

    return type;
}

static lily_type *get_class_arg(lily_parse_state *parser, int *flags)
{
    lily_lex_state *lex = parser->lex;
    lily_prop_entry *prop = NULL;
    lily_var *var;
    int modifiers = 0;

    NEED_CURRENT_TOK(tk_word)

    if (lex->label[0] == 'p') {
        int keyword = keyword_by_name(lex->label);
        if (keyword == KEY_PRIVATE)
            modifiers = SYM_SCOPE_PRIVATE;
        else if (keyword == KEY_PROTECTED)
            modifiers = SYM_SCOPE_PROTECTED;
        else if (keyword == KEY_PUBLIC)
            modifiers = SYM_SCOPE_PUBLIC;
    }
    else if (lex->label[0] == 'v' &&
             strcmp(lex->label, "var") == 0) {
        lily_raise_syn(parser->raiser,
                "Constructor var declaration must start with a scope.");
    }

    if (modifiers) {
        lily_next_token(lex);
        if (lex->token != tk_word ||
            strcmp(lex->label, "var") != 0) {
            lily_raise_syn(parser->raiser,
                    "Expected 'var' after scope was given.");
        }

        NEED_NEXT_TOK(tk_prop_word)
        prop = declare_property(parser, modifiers);

        /* Properties aren't assigned until all optargs are done. */
        prop->flags |= SYM_NOT_INITIALIZED;
        var = new_local_var(parser, NULL, "", 0);
    }
    else {
        var = declare_local_var(parser, NULL);
        var->flags |= VAR_CANNOT_BE_UPVALUE;
    }

    NEED_COLON_AND_NEXT;

    lily_type *type = get_nameless_arg(parser, flags);

    if (*flags & TYPE_HAS_OPTARGS) {
        /* Optional arguments are created by making an optarg type which holds
           a type that is supposed to be optional. For simplicity, give the
           var the concrete underlying type, and the caller the true optarg
           containing type. */
        var->type = type->subtypes[0];
        var->flags |= SYM_NOT_INITIALIZED;
        collect_optarg_for(parser, (lily_sym *)var);
    }
    else
        var->type = type;

    if (prop)
        prop->type = var->type;

    return type;
}

/* Call this if you just need a type but no optional argument stuff to go along
   with it. If there is any resolution needed (ex: `a.b.c`), then that is done
   here. This is relied upon by get_named_arg and get_nameless_arg (which add
   optarg/vararg functionality).
   You probably don't want to call this directly, unless you just need a type
   and it cannot be optargs/varargs (ex: `: <type>` of a var decl). */
static lily_type *get_type_raw(lily_parse_state *parser, int flags)
{
    lily_lex_state *lex = parser->lex;
    lily_type *result;
    lily_class *cls = NULL;

    if (lex->token == tk_word)
        cls = resolve_class_name(parser);
    else if ((flags & F_SCOOP_OK) && lex->token == tk_scoop)
        cls = lily_scoop_class;
    else {
        NEED_CURRENT_TOK(tk_word)
    }

    if (cls->item_kind & ITEM_IS_VARIANT)
        lily_raise_syn(parser->raiser,
                "Variant types not allowed in a declaration.");

    if (cls->generic_count == 0)
        result = cls->self_type;
    else if (cls->id != LILY_ID_FUNCTION) {
        NEED_NEXT_TOK(tk_left_bracket)
        int i = 0;
        while (1) {
            lily_next_token(lex);
            lily_tm_add(parser->tm, get_type_raw(parser, flags));
            i++;

            if (lex->token == tk_comma)
                continue;
            else if (lex->token == tk_right_bracket)
                break;
            else
                lily_raise_syn(parser->raiser,
                        "Expected either ',' or ']', not '%s'.",
                        tokname(lex->token));
        }

        result = lily_tm_make(parser->tm, cls, i);
        ensure_valid_type(parser, result);
    }
    else {
        NEED_NEXT_TOK(tk_left_parenth)
        lily_next_token(lex);
        int arg_flags = flags & F_SCOOP_OK;
        int i = 0;
        int result_pos = parser->tm->pos;

        lily_tm_add(parser->tm, lily_unit_type);

        if (lex->token != tk_arrow && lex->token != tk_right_parenth) {
            while (1) {
                lily_tm_add(parser->tm, get_nameless_arg(parser, &arg_flags));
                i++;
                if (lex->token == tk_comma) {
                    lily_next_token(lex);
                    continue;
                }

                break;
            }
        }

        if (lex->token == tk_arrow) {
            lily_next_token(lex);
            lily_tm_insert(parser->tm, result_pos,
                    get_type_raw(parser, flags));
        }

        NEED_CURRENT_TOK(tk_right_parenth)

        result = lily_tm_make_call(parser->tm, arg_flags & F_NO_COLLECT, cls,
                i + 1);
    }

    lily_next_token(lex);
    return result;
}

/* Only function dynaload needs scoop types. Everything else can use this define
   that sends flags as 0. */
#define get_type(p) get_type_raw(p, 0)

/* This is called at the start of a define, class, or enum to collect the
   generics between square brackets. Collection begins at the current point so
   that class/enum methods don't need to specify generics again. */
static void collect_generics_for(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);

    if (lex->token != tk_left_bracket)
        return;

    lily_type_maker *tm = parser->tm;
    char ch = 'A' + lily_gp_num_in_scope(parser->generics);
    char name[] = {ch, '\0'};

    while (1) {
        NEED_NEXT_TOK(tk_word)

        if (lex->label[0] != ch || lex->label[1] != '\0') {
            if (ch == 'Z' + 1)
                lily_raise_syn(parser->raiser, "Too many generics.");
            else {
                lily_raise_syn(parser->raiser,
                        "Invalid generic name (wanted %s, got %s).",
                        name, lex->label);
            }
        }

        lily_type *g = lily_gp_push(parser->generics, name, ch - 'A');

        lily_next_token(lex);
        /* ch has to be updated before finishing, because 'seen' depends on
           it. Having the wrong # of generics seen (even if off by one)
           causes strange and major problems. */
        ch++;

        if (cls)
            lily_tm_add(tm, g);

        if (lex->token == tk_right_bracket) {
            lily_next_token(lex);
            break;
        }
        else if (lex->token != tk_comma)
            lily_raise_syn(parser->raiser,
                    "Expected either ',' or ']', not '%s'.",
                    tokname(lex->token));

        name[0] = ch;
    }

    uint16_t seen = ch - 'A';

    /* ts needs to know how many slots to reserve for solving generics. */
    lily_ts_generics_seen(parser->emit->ts, seen);

    if (cls) {
        cls->generic_count = seen;
        cls->self_type = lily_tm_make(tm, cls, seen);
    }
}

static lily_type *build_empty_variant_type(lily_parse_state *parser,
        lily_class *enum_cls)
{
    lily_type_maker *tm = parser->tm;
    lily_type *result;
    uint16_t i = 0;
    uint16_t count = (uint16_t)enum_cls->generic_count;

    if (count) {
        for (i = 0;i < count;i++)
            lily_tm_add(tm, lily_question_type);

        result = lily_tm_make(tm, enum_cls, count);
    }
    else
        result = enum_cls->self_type;

    return result;
}

typedef lily_type *(*collect_fn)(lily_parse_state *, int *);

static void error_forward_decl_type(lily_parse_state *parser, lily_var *var,
        lily_type *got)
{
    lily_raise_syn(parser->raiser,
            "Declaration does not match prior forward declaration at line %d.\n"
            "Expected: ^T\n"
            "Received: ^T", var->line_num, var->type, got);
}

static void collect_call_args(lily_parse_state *parser, void *target,
        int arg_flags)
{
    lily_lex_state *lex = parser->lex;
    /* -1 because Unit is injected at the front beforehand. */
    int result_pos = parser->tm->pos - 1;
    int i = 0;
    uint16_t last_keyarg_pos = 0;
    uint16_t keyarg_start = parser->data_string_pos;
    collect_fn arg_collect = NULL;

    if ((arg_flags & F_COLLECT_DEFINE)) {
        if (parser->emit->block->self) {
            i++;
            result_pos--;
        }

        lily_var *var = (lily_var *)target;

        if ((var->flags & VAR_IS_FORWARD) == 0)
            arg_collect = get_define_arg;
        else
            arg_collect = get_nameless_arg;
    }
    else if (arg_flags & F_COLLECT_DYNALOAD)
        arg_collect = get_nameless_arg;
    else if (arg_flags & F_COLLECT_CLASS)
        arg_collect = get_class_arg;
    else if (arg_flags & F_COLLECT_VARIANT)
        arg_collect = get_variant_arg;

    if (lex->token == tk_left_parenth) {
        lily_next_token(lex);

        if (lex->token == tk_right_parenth)
            lily_raise_syn(parser->raiser,
                    "Empty () found while reading input arguments. Omit instead.");

        while (1) {
            if (lex->token == tk_keyword_arg) {
                while (i != last_keyarg_pos) {
                    last_keyarg_pos++;
                    lily_sp_insert(parser->data_strings, " ",
                            &parser->data_string_pos);
                }

                lily_sp_insert(parser->data_strings, lex->label,
                        &parser->data_string_pos);

                last_keyarg_pos++;
                lily_next_token(lex);
            }

            lily_tm_add(parser->tm, arg_collect(parser, &arg_flags));
            i++;
            if (lex->token == tk_comma) {
                lily_next_token(lex);
                continue;
            }
            else if (lex->token == tk_right_parenth) {
                lily_next_token(lex);
                break;
            }
            else
                lily_raise_syn(parser->raiser,
                        "Expected either ',' or ')', not '%s'.",
                        tokname(lex->token));
        }
    }

    if (lex->token == tk_colon &&
        (arg_flags & (F_COLLECT_VARIANT | F_COLLECT_CLASS)) == 0) {
        lily_next_token(lex);
        if (arg_flags & F_COLLECT_DEFINE &&
            strcmp(lex->label, "self") == 0) {
            lily_var *v = (lily_var *)target;

            if (v->parent == NULL ||
                (v->parent->item_kind & ITEM_IS_CLASS) == 0)
                lily_raise_syn(parser->raiser,
                        "'self' return type only allowed on class methods.");

            if (v->flags & VAR_IS_STATIC)
                lily_raise_syn(parser->raiser,
                        "'self' return type not allowed on a static method.");

            lily_tm_insert(parser->tm, result_pos, lily_self_class->self_type);
            lily_next_token(lex);
        }
        else {
            lily_type *result_type = get_type_raw(parser, arg_flags);
            if (result_type == lily_unit_type) {
                lily_raise_syn(parser->raiser,
                        "Unit return type is automatic. Omit instead.");
            }

            /* Use the arg flags so that dynaload can use $1 in the return. */
            lily_tm_insert(parser->tm, result_pos, result_type);
        }
    }

    if (last_keyarg_pos) {
        lily_sym *sym = (lily_sym *)target;

        /* Allowing this would mean checking that the argument strings are the
           same. That's difficult, and forward declarations are really about
           allowing mutually recursive functions. */
        if (sym->flags & VAR_IS_FORWARD) {
            lily_raise_syn(parser->raiser,
                    "Forward declarations not allowed to have keyword arguments.");
        }

        while (last_keyarg_pos != i) {
            last_keyarg_pos++;
            lily_sp_insert(parser->data_strings, " ",
                    &parser->data_string_pos);
        }

        lily_sp_insert(parser->data_strings, "\t",
                &parser->data_string_pos);

        put_keyargs_in_target(parser, target, keyarg_start);
        parser->data_string_pos = keyarg_start;
    }

    lily_type *t = lily_tm_make_call(parser->tm, arg_flags & F_NO_COLLECT,
            parser->symtab->function_class, i + 1);

    if ((arg_flags & F_COLLECT_VARIANT) == 0) {
        lily_var *var = (lily_var *)target;
        if (var->type && var->type != t)
            error_forward_decl_type(parser, var, t);

        var->type = t;
    }
    else {
        lily_variant_class *cls = (lily_variant_class *)target;
        cls->build_type = t;
    }
}

/***
 *      ____                    _                 _
 *     |  _ \ _   _ _ __   __ _| | ___   __ _  __| |
 *     | | | | | | | '_ \ / _` | |/ _ \ / _` |/ _` |
 *     | |_| | |_| | | | | (_| | | (_) | (_| | (_| |
 *     |____/ \__, |_| |_|\__,_|_|\___/ \__,_|\__,_|
 *            |___/
 */

/** This is the system that the interpreter uses to load symbols from foreign
    modules. Foreign modules are usually shared libraries, but they can also be
    modules compiled with the interpreter or embedder. No matter where they come
    from, foreign modules will always bring two tables with them.

    Tables are generated by bindgen (not in this repository) using definitions
    that look like native Lily code.

    These tables work as parallel arrays. The first table is an array of strings
    that specify different exports. The second table is an array of functions.

    As an example, if the info table at 20 specifies a var, the call table at 20
    is the function to load that var. If the info table at 40 is, say, a variant
    or some other entity that doesn't need a loader, the info table is NULL.

    The first line of the info table is the table's header. The first byte is a
    count of how many classes/enums that the module exports. That's used to
    build the cid (class id) table, which the vm uses to send class ids back
    into the interpreter.

    Next are the dynaload records. Every dynaload record begins with a letter, a
    byte, a name, and (optionally) a type. The letter specifies what kind of
    record it is. The byte value specifies how far to move to get to the next
    record. The name is used for lookups and the type is applied to the new
    symbol. If a record has methods, the methods are always next, followed by
    variants/properties.

    Toplevel symbols link to each other so that toplevel search doesn't match to
    a symbol that wouldn't be visible. For enums, the offset is also used to
    implement scoping: Flat enums point to their variants while scoped enums
    point past them. The info table is always terminated with a "Z" record.

    When a module is first loaded (except for builtin), no symbols are loaded.
    Instead, the interpreter waits until a symbol is explicitly specified. When
    that happens, a lookup is done and this mechanism is used as a fallback.

    Notice how the above works without running native code. That's really
    important, because dynaload needs to work transparently whenever a symbol is
    requested.

    This mechanism was written with predefined modules in mind, notably the
    builtin module. At well over 100 records, this makes a considerable
    difference in startup memory cost. */

static void parse_variant_header(lily_parse_state *, lily_variant_class *);
static lily_type *build_empty_variant_type(lily_parse_state *, lily_class *);
static lily_item *try_dynaload_method(lily_parse_state *, lily_class *,
        const char *);
static lily_item *try_toplevel_dynaload(lily_parse_state *, lily_module_entry *,
        const char *);
typedef void (dyna_function)(lily_parse_state *, lily_dyna_state *);

/* [0] is the record letter and [1] is the offset. */
#define DYNA_NAME_OFFSET 2

/* This function scans through the first line in the dynaload table to find the
   cid entries listed. For each of those cid entries, the ones currently
   available are loaded into the appropriate place in the cid table. */
static void update_cid_table(lily_parse_state *parser, lily_module_entry *m)
{
    const char *cid_entry = m->info_table[0] + 1;
    int counter = 0;
    int stop = cid_entry[-1];
    uint16_t *cid_table = m->cid_table;
    lily_module_entry *builtin = parser->module_start;

    while (counter < stop) {
        if (cid_table[counter] == 0) {
            lily_class *cls = lily_find_class(builtin, cid_entry);

            if (cls == NULL)
                cls = lily_find_class(m, cid_entry);

            if (cls)
                cid_table[counter] = cls->id;
        }
        cid_entry += strlen(cid_entry) + 1;
        counter++;
    }
}

static void update_all_cid_tables(lily_parse_state *parser)
{
    lily_module_entry *entry_iter = parser->module_start;
    while (entry_iter) {
        if (entry_iter->cid_table)
            update_cid_table(parser, entry_iter);

        entry_iter = entry_iter->next;
    }
}

/* This is called when the current label is a module. This walks through the
   subsequent `.<name>` sequences until one of them finishes with a non-module.
   The result of this function is the last module given. That module is the
   namespace that should be used in place of the one given. */
static lily_module_entry *resolve_module(lily_parse_state *parser,
        lily_module_entry *m)
{
    lily_lex_state *lex = parser->lex;
    lily_module_entry *result = m;

    while (1) {
        NEED_NEXT_TOK(tk_dot)
        NEED_NEXT_TOK(tk_word)
        m = lily_find_module(m, lex->label);
        if (m == NULL)
            break;

        result = m;
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
    lily_module_entry *m = symtab->active_module;

    NEED_CURRENT_TOK(tk_word)

    lily_class *result = find_or_dl_class(parser, m, lex->label);

    if (result == NULL) {
        m = lily_find_module(m, lex->label);

        if (m) {
            m = resolve_module(parser, m);
            result = find_or_dl_class(parser, m, lex->label);
        }
    }

    if (result == NULL)
        lily_raise_syn(parser->raiser, "Class '%s' does not exist.",
                lex->label);

    return result;
}

static const char *dyna_get_name(lily_dyna_state *ds)
{
    return ds->entry + DYNA_NAME_OFFSET;
}

static const char *dyna_get_body(lily_dyna_state *ds)
{
    const char *name = ds->entry + DYNA_NAME_OFFSET;
    const char *body = name + strlen(name) + 1;

    return body;
}

static int dyna_name_is(lily_dyna_state *ds, const char *expect)
{
    const char *name = dyna_get_name(ds);
    int result = strcmp(name, expect) == 0;

    return result;
}

static char dyna_record_type(lily_dyna_state *ds)
{
    return ds->entry[0];
}

static char dyna_iter_next(lily_dyna_state *ds)
{
    ds->index += (unsigned char)(ds->entry[1] + 1);
    ds->entry = ds->table[ds->index];

    return ds->entry[0];
}

static char dyna_check_next(lily_dyna_state *ds)
{
    int index = ds->index;

    char ch = dyna_iter_next(ds);

    ds->index = index;
    ds->entry = ds->table[index];

    return ch;
}

static void dyna_iter_back_to_enum(lily_dyna_state *ds)
{
    do {
        ds->index--;
        ds->entry = ds->table[ds->index];
    } while (dyna_record_type(ds) != 'E');
}

static void dyna_iter_past_methods(lily_dyna_state *ds)
{
    do {
        ds->index++;
        ds->entry = ds->table[ds->index];
    } while (dyna_record_type(ds) == 'm');
}

static int dyna_find_class_method(lily_dyna_state *ds, lily_class *cls,
        const char *name)
{
    ds->result = NULL;

    if (cls->dyna_start == 0)
        return 0;

    ds->cls = cls;
    ds->m = cls->module;
    ds->table = ds->m->info_table;
    ds->index = cls->dyna_start + 1;
    ds->entry = ds->table[ds->index];

    int result = 0;

    while (dyna_record_type(ds) == 'm') {
        if (dyna_name_is(ds, name)) {
            result = 1;
            break;
        }

        dyna_iter_next(ds);
    }

    return result;
}

static int dyna_find_toplevel_item(lily_dyna_state *ds,
        lily_module_entry *m, const char *name)
{
    ds->result = NULL;

    if (m->info_table == NULL)
        return 0;

    ds->cls = NULL;
    ds->m = m;
    ds->table = m->info_table;
    ds->index = 1;
    ds->entry = ds->table[ds->index];

    int result = 0;

    do {
        if (dyna_name_is(ds, name)) {
            result = 1;
            break;
        }

        dyna_iter_next(ds);
    } while (dyna_record_type(ds) != 'Z');

    return result;
}

static void dyna_save(lily_parse_state *parser, lily_dyna_state *ds)
{
    ds->saved_active = parser->symtab->active_module;
    ds->saved_generics = lily_gp_save_and_hide(parser->generics);
    parser->symtab->active_module = ds->m;
    lily_lexer_load(parser->lex, et_shallow_string, dyna_get_body(ds));
}

static void dyna_restore(lily_parse_state *parser, lily_dyna_state *ds)
{
    lily_pop_lex_entry(parser->lex);
    parser->symtab->active_module = ds->saved_active;
    lily_gp_restore_and_unhide(parser->generics, ds->saved_generics);
}

static void dynaload_foreign(lily_parse_state *parser, lily_dyna_state *ds)
{
    lily_module_entry *save_active = parser->symtab->active_module;

    parser->symtab->active_module = ds->m;

    lily_class *cls = lily_new_class(parser->symtab, dyna_get_name(ds), 0);

    cls->item_kind = ITEM_CLASS_FOREIGN;
    cls->dyna_start = ds->index;
    ds->result = (lily_item *)cls;
    parser->symtab->active_module = save_active;
}

static void dynaload_var(lily_parse_state *parser, lily_dyna_state *ds)
{
    dyna_save(parser, ds);
    lily_next_token(parser->lex);

    lily_module_entry *m = ds->m;
    lily_type *type = get_type_raw(parser, 0);
    lily_var *var = new_global_var(parser, type, dyna_get_name(ds), 0);

    /* The initial vm has a function above main for storing globals that's never
       truly exited. Var loaders work by pushing a new value in toplevel space.
       Make sure the identity table is up-to-date before running the loader in
       case the loader needs it. */

    /* These two allow var loaders to use ID_ macros. */
    update_cid_table(parser, m);
    parser->toplevel_func->cid_table = m->cid_table;

    lily_foreign_func var_loader = m->call_table[ds->index];

    /* This should push exactly one extra value onto the stack. Since
       global vars have placeholder values inserted, the var ends up
       exactly where it should be. */
    var_loader(parser->vm);
    dyna_restore(parser, ds);
    ds->result = (lily_item *)var;
}

/* The vm expects certain predefined classes to have specific ids. This is
   called when dynaload sees a predefined class or enum. */
static void fix_predefined_class_id(lily_parse_state *parser, lily_class *cls)
{
    char *name = cls->name;
    uint16_t adjust = 1;
    uint16_t new_id = 12345;

    if (strcmp(name, "DivisionByZeroError") == 0)
        new_id = LILY_ID_DBZERROR;
    else if (strcmp(name, "Exception") == 0)
        new_id = LILY_ID_EXCEPTION;
    else if (strcmp(name, "IndexError") == 0)
        new_id = LILY_ID_INDEXERROR;
    else if (strcmp(name, "IOError") == 0)
        new_id = LILY_ID_IOERROR;
    else if (strcmp(name, "KeyError") == 0)
        new_id = LILY_ID_KEYERROR;
    else if (strcmp(name, "RuntimeError") == 0)
        new_id = LILY_ID_RUNTIMEERROR;
    else if (strcmp(name, "ValueError") == 0)
        new_id = LILY_ID_VALUEERROR;
    else if (cls->item_kind & ITEM_IS_ENUM) {
        /* Has to be Option or Result, both of which have two members. */

        if (strcmp(name, "Option") == 0)
            new_id = LILY_ID_OPTION;
        else if (strcmp(name, "Result") == 0)
            new_id = LILY_ID_RESULT;

        /* No ids were given out. Fix the two variants manually. */
        cls->members->id = new_id + 2;
        cls->members->next->id = new_id + 1;
        adjust = 0;
    }

    parser->symtab->next_class_id -= adjust;
    cls->id = new_id;
}

static void dynaload_enum(lily_parse_state *parser, lily_dyna_state *ds)
{
    dyna_save(parser, ds);

    lily_lex_state *lex = parser->lex;
    lily_class *enum_cls = lily_new_enum_class(parser->symtab,
            dyna_get_name(ds), 0);

    enum_cls->dyna_start = ds->index;
    collect_generics_for(parser, enum_cls);

    /* Enums are followed by methods, then variants. Flat enums will write an
       offset that goes to the first enum, whereas scoped enums skip to the next
       toplevel entry. */
    if (dyna_check_next(ds) != 'V')
        enum_cls->item_kind = ITEM_ENUM_SCOPED;

    dyna_iter_past_methods(ds);

    lily_type *empty_type = build_empty_variant_type(parser, enum_cls);

    do {
        lily_variant_class *variant = lily_new_variant_class(enum_cls,
                dyna_get_name(ds), 0);

        lily_lexer_load(lex, et_shallow_string, dyna_get_body(ds));
        lily_next_token(lex);

        if (lex->token == tk_left_parenth)
            parse_variant_header(parser, variant);
        else
            variant->build_type = empty_type;

        lily_pop_lex_entry(lex);
        dyna_iter_next(ds);
    } while (dyna_record_type(ds) == 'V');

    if (ds->m == parser->module_start)
        fix_predefined_class_id(parser, enum_cls);
    else
        lily_fix_enum_variant_ids(parser->symtab, enum_cls);

    dyna_restore(parser, ds);
    ds->result = (lily_item *)enum_cls;
}

static void dynaload_variant(lily_parse_state *parser, lily_dyna_state *ds)
{
    const char *name = dyna_get_name(ds);

    dyna_iter_back_to_enum(ds);
    dynaload_enum(parser, ds);
    ds->result = (lily_item *)lily_find_class(ds->m, name);
}

static void dynaload_native(lily_parse_state *parser, lily_dyna_state *ds)
{
    dyna_save(parser, ds);

    lily_lex_state *lex = parser->lex;
    lily_class *cls = lily_new_class(parser->symtab, dyna_get_name(ds), 0);
    uint16_t source_mods[] = {SYM_SCOPE_PRIVATE, SYM_SCOPE_PROTECTED,
            SYM_SCOPE_PUBLIC};

    cls->dyna_start = ds->index;
    collect_generics_for(parser, cls);

    if (lex->token == tk_lt) {
        lily_next_token(lex);

        const char *name = lex->label;
        lily_class *parent = lily_find_class(ds->m, name);

        if (parent == NULL)
            parent = (lily_class *)try_toplevel_dynaload(parser, ds->m, name);

        cls->parent = parent;
        cls->prop_count = parent->prop_count;
    }

    if (ds->m == parser->module_start)
        fix_predefined_class_id(parser, cls);

    dyna_iter_past_methods(ds);

    do {
        char rec = dyna_record_type(ds);

        if (rec != '1' && rec != '2' && rec != '3')
            break;

        lily_lexer_load(lex, et_shallow_string, dyna_get_body(ds));
        lily_next_token(lex);

        uint16_t modifiers = source_mods[rec - '1'];
        lily_type *type = get_type_raw(parser, 0);

        lily_add_class_property(cls, type, dyna_get_name(ds), modifiers);
        lily_pop_lex_entry(lex);
        dyna_iter_next(ds);
    } while (1);

    /* Make sure the constructor loads too. Parts like inheritance will call for
       the class to dynaload, but (reasonably) expect <new> to be visible. */
    try_dynaload_method(parser, cls, "<new>");
    dyna_restore(parser, ds);
    ds->result = (lily_item *)cls;
}

static void dynaload_function(lily_parse_state *parser, lily_dyna_state *ds)
{
    const char *name = dyna_get_name(ds);
    lily_module_entry *m = ds->m;
    lily_var *var;

    dyna_save(parser, ds);

    if (ds->cls)
        var = new_method_var(parser, ds->cls, name, SYM_SCOPE_PUBLIC, 0);
    else
        var = new_define_var(parser, name, 0);

    var->flags |= VAR_IS_FOREIGN_FUNC;
    collect_generics_for(parser, NULL);
    lily_tm_add(parser->tm, lily_unit_type);
    collect_call_args(parser, var, F_COLLECT_DYNALOAD);

    lily_value *v = lily_vs_nth(parser->symtab->literals, var->reg_spot);
    lily_function_val *f = v->value.function;

    f->foreign_func = m->call_table[ds->index];
    f->reg_count = var->type->subtype_count;
    dyna_restore(parser, ds);
    ds->result = (lily_item *)var;
}

static lily_item *run_dynaload(lily_parse_state *parser,
        lily_dyna_state *ds)
{
    char rec = dyna_record_type(ds);
    char records[] = {'C', 'E', 'F', 'N', 'R', 'V'};
    int i = 0;

    if (rec == 'Z')
        return NULL;

    dyna_function *functions[] = {
        dynaload_foreign,
        dynaload_enum,
        dynaload_function,
        dynaload_native,
        dynaload_var,
        dynaload_variant,
    };

    while (records[i] != rec)
        i++;

    functions[i](parser, ds);

    return ds->result;
}

static lily_item *try_toplevel_dynaload(lily_parse_state *parser,
        lily_module_entry *m, const char *name)
{
    lily_dyna_state ds;
    lily_item *result = NULL;

    if (dyna_find_toplevel_item(&ds, m, name))
        result = run_dynaload(parser, &ds);

    return result;
}

static lily_item *try_dynaload_method(lily_parse_state *parser, lily_class *cls,
        const char *name)
{
    lily_dyna_state ds;

    if (dyna_find_class_method(&ds, cls, name))
        dynaload_function(parser, &ds);

    return ds.result;
}

static lily_class *find_run_class_dynaload(lily_parse_state *parser,
        lily_module_entry *m, const char *name)
{
    lily_item *result = try_toplevel_dynaload(parser, m, name);

    if (result && result->item_kind == ITEM_VAR)
        result = NULL;

    return (lily_class *)result;
}

lily_class *lily_dynaload_exception(lily_parse_state *parser, const char *name)
{
    lily_module_entry *m = parser->module_start;
    return (lily_class *)try_toplevel_dynaload(parser, m, name);
}

lily_item *lily_find_or_dl_member(lily_parse_state *parser, lily_class *cls,
        const char *name)
{
    lily_item *result = (lily_item *)lily_find_member(cls, name);

    while (result == NULL && cls != NULL) {
        /* Has to be a method, because properties and variants are loaded with
           their classes and enums. */
        result = try_dynaload_method(parser, cls, name);
        cls = cls->parent;
    }

    return result;
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

/* Expression dispatch functions will never see these two. */
#define ST_BAD_TOKEN            0
#define ST_DONE                 1

/* These are the values that dispatch functions will see. */
/* I need a value to work with. */
#define ST_DEMAND_VALUE         2
/* A binary op or an operation (dot call, call, subscript), or a close. */
#define ST_WANT_OPERATOR        3
/* A value is nice, but not required (ex: call arguments). */
#define ST_WANT_VALUE           4

/* Normally, the next token is pulled up after an expression_* helper has been
   called. If this is or'd onto the state, then it's assumed that the next token
   has already been pulled up.
   Dispatch functions will never see this. */
#define ST_FORWARD              0x8

/* This is a wrapper function that handles pushing the given literal into the
   parser's ast pool. */
static void push_literal(lily_parse_state *parser, lily_literal *lit)
{
    lily_class *literal_cls;
    int base = FLAGS_TO_BASE(lit);

    if (base == V_INTEGER_BASE)
        literal_cls = parser->symtab->integer_class;
    else if (base == V_DOUBLE_BASE)
        literal_cls = parser->symtab->double_class;
    else if (base == V_STRING_BASE)
        literal_cls = parser->symtab->string_class;
    else if (base == V_BYTESTRING_BASE)
        literal_cls = parser->symtab->bytestring_class;
    else if (base == V_UNIT_BASE)
        literal_cls = lily_unit_type->cls;
    else
        /* Impossible, but keeps the compiler from complaining. */
        literal_cls = lily_question_type->cls;

    lily_es_push_literal(parser->expr, literal_cls->self_type, lit->reg_spot);
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

static void error_self_usage(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    const char *what;

    if (lex->token == tk_prop_word)
        what = "a class property";
    else if (strcmp(lex->label, "self") == 0)
        what = "self";
    else
        what = "an instance method";

    lily_raise_syn(parser->raiser, "Cannot use %s here.", what);
}

/* This takes an id that corresponds to some id in the table of magic constants.
   From that, it determines that value of the magic constant, and then adds that
   value to the current ast pool. */
static int expression_word_try_constant(lily_parse_state *parser)
{
    lily_expr_state *es = parser->expr;
    lily_symtab *symtab = parser->symtab;
    lily_lex_state *lex = parser->lex;
    int key_id = constant_by_name(lex->label);
    lily_literal *lit;

    /* These literal fetching routines are guaranteed to return a literal with
       the given value. */
    if (key_id == CONST__LINE__) {
        int num = lex->line_num;

        if ((int16_t)num <= INT16_MAX)
            lily_es_push_integer(es, (int16_t)num);
        else {
            lit = lily_get_integer_literal(symtab, lex->line_num);
            push_literal(parser, lit);
        }
    }
    else if (key_id == CONST__FILE__) {
        lit = lily_get_string_literal(symtab, symtab->active_module->path);
        push_literal(parser, lit);
    }
    else if (key_id == CONST__FUNCTION__) {
        lit = lily_get_string_literal(symtab,
                parser->emit->scope_block->scope_var->name);
        push_literal(parser, lit);
    }
    else if (key_id == CONST_TRUE)
        lily_es_push_boolean(es, 1);
    else if (key_id == CONST_FALSE)
        lily_es_push_boolean(es, 0);
    else if (key_id == CONST_SELF) {
        if (lily_emit_can_use_self_keyword(parser->emit) == 0)
            error_self_usage(parser);

        lily_es_push_self(es);
    }
    else if (key_id == CONST_UNIT) {
        lit = lily_get_unit_literal(symtab);
        push_literal(parser, lit);
    }

    return key_id != -1;
}

static int expression_word_try_use_self(lily_parse_state *parser)
{
    lily_item *item = NULL;

    if (parser->current_class) {
        lily_class *self_cls = parser->current_class;
        const char *name = parser->lex->label;

        item = lily_find_or_dl_member(parser, self_cls, name);

        if (item) {
            if (item->item_kind == ITEM_VAR) {
                /* Pushing the item as a method tells emitter to add an implicit
                   self to the mix. */
                if ((item->flags & VAR_IS_STATIC) == 0) {
                    if (lily_emit_can_use_self_method(parser->emit) == 0)
                        error_self_usage(parser);

                    lily_es_push_method(parser->expr, (lily_var *)item);
                }
                else
                    lily_es_push_static_func(parser->expr, (lily_var *)item);
            }
            else if (item->item_kind == ITEM_PROPERTY)
                lily_raise_syn(parser->raiser,
                        "%s is a property, and must be referenced as @%s.",
                        name, name);
            /* If 'self' is a flat enum, then one of the variants would have
               been found in the flat scope search. So this is a variant of a
               scoped enum. Do not count this so that the variant names have to
               be scoped at all times. */
            else if (item->item_kind & ITEM_IS_VARIANT)
                item = NULL;
        }
    }

    return item != NULL;
}

static void expression_word_ctor(lily_parse_state *parser, lily_class *cls)
{
    if (cls->item_kind & ITEM_IS_ENUM)
        lily_raise_syn(parser->raiser,
                "Cannot implicitly use the constructor of an enum.");

    lily_var *target = (lily_var *)lily_find_member_in_class(cls, "<new>");

    if (target == NULL)
        target = (lily_var *)try_dynaload_method(parser, cls, "<new>");

    if (target == NULL)
        lily_raise_syn(parser->raiser,
                "Class %s does not have a constructor.", cls->name);

    /* This happens when an optional argument of a constructor tries to use
       that same constructor. */
    if (target->flags & SYM_NOT_INITIALIZED)
        lily_raise_syn(parser->raiser,
                "Constructor for class %s is not initialized.", cls->name);

    lily_es_push_static_func(parser->expr, target);
}

/* This handles when a class is seen within an expression. Any import qualifier
   has already been scanned and is unimportant. The key here is to figure out if
   this is `<class>.member` or `<class>()`. The first is a static access, while
   the latter is an implicit `<class>.new()`. */
static void expression_word_as_class(lily_parse_state *parser, lily_class *cls,
        int *state)
{
    lily_lex_state *lex = parser->lex;
    lily_next_token(lex);

    if (lex->token != tk_dot) {
        expression_word_ctor(parser, cls);
        *state = ST_FORWARD;
        return;
    }

    NEED_NEXT_TOK(tk_word)

    lily_item *item = (lily_item *)lily_find_member_in_class(cls, lex->label);

    if (item == NULL)
        item = try_dynaload_method(parser, cls, lex->label);

    if (item == NULL) {
        lily_raise_syn(parser->raiser,
                "Class %s does not have a member named %s.", cls->name,
                lex->label);
    }

    if (item->item_kind == ITEM_VAR)
        lily_es_push_static_func(parser->expr, (lily_var *)item);
    else if (item->item_kind & ITEM_IS_VARIANT)
        lily_es_push_variant(parser->expr, (lily_variant_class *)item);
    else if (item->item_kind == ITEM_PROPERTY)
        lily_raise_syn(parser->raiser,
                "Cannot use a class property without a class instance.");
}

/* This function takes a var and determines what kind of tree to put it into.
   The tree type is used by emitter to group vars into different types as a
   small optimization. */
static void expression_word_as_var(lily_parse_state *parser, lily_var *var,
        int *state)
{
    if (var->flags & SYM_NOT_INITIALIZED)
        lily_raise_syn(parser->raiser,
                "Attempt to use uninitialized value '%s'.",
                var->name);

    /* Defined functions have a depth of one, so they have to be first. */
    else if (var->flags & VAR_IS_READONLY)
        lily_es_push_defined_func(parser->expr, var);
    else if (var->flags & VAR_IS_GLOBAL)
        lily_es_push_global_var(parser->expr, var);
    else if (var->function_depth == parser->emit->function_depth)
        lily_es_push_local_var(parser->expr, var);
    else
        lily_es_push_upvalue(parser->expr, var);
}

/* This is called by expression when there is a word. This is complicated,
   because a word could be a lot of things.  */
static void expression_word(lily_parse_state *parser, int *state)
{
    lily_symtab *symtab = parser->symtab;
    lily_lex_state *lex = parser->lex;
    lily_module_entry *m = symtab->active_module;
    const char *name = lex->label;

    lily_sym *sym = find_existing_sym(m, name);

    if (sym) {
        /* If the name given is a module, there could be more submodules after
           it to look through. Walk the modules, then do another search in the
           final module. */
        if (sym->item_kind == ITEM_MODULE) {
            m = resolve_module(parser, (lily_module_entry *)sym);
            sym = find_existing_sym(m, name);
        }
    }
    else if (expression_word_try_constant(parser) ||
             expression_word_try_use_self(parser))
        return;
    else {
        /* Since no module was explicitly provided, look through the builtins.
           This intentionally sets 'm' so that the dynaload check targets the
           builtin module. */
        m = symtab->builtin_module;
        sym = find_existing_sym(m, name);
    }

    /* As a last resort, try running a dynaload. This will check either the
       module explicitly provided, or the builtin module.
       In most other situations, the active module should be checked as well
       since it could be a foreign module. Since expressions are limited to
       native modules, it is impossible for the active module to have a dynaload
       as a last resort. */
    if (sym == NULL)
        sym = (lily_sym *)try_toplevel_dynaload(parser, m, name);

    if (sym) {
        if (sym->item_kind == ITEM_VAR)
            expression_word_as_var(parser, (lily_var *)sym, state);
        else if (sym->item_kind & ITEM_IS_VARIANT)
	        lily_es_push_variant(parser->expr, (lily_variant_class *)sym);
        else
            expression_word_as_class(parser, (lily_class *)sym, state);
    }
    else
        lily_raise_syn(parser->raiser, "%s has not been declared.", name);
}

/* This is called to handle `@<prop>` accesses. */
static void expression_property(lily_parse_state *parser, int *state)
{
    if (lily_emit_can_use_self_property(parser->emit) == 0)
        error_self_usage(parser);

    lily_class *current_class = parser->current_class;
    char *name = parser->lex->label;
    lily_named_sym *sym = lily_find_member(current_class, name);

    if (sym == NULL) {
        const char *extra = "";

        if (parser->emit->block->block_type == block_class)
            extra = " ('var' keyword missing?)";

        lily_raise_syn(parser->raiser, "Property %s is not in class %s.%s",
                name, current_class->name, extra);
    }
    else if (sym->item_kind == ITEM_VAR) {
        lily_raise_syn(parser->raiser,
                "Cannot access a method as a property (use %s instead of @%s).",
                name, name);
    }

    if (sym->flags & SYM_NOT_INITIALIZED)
        lily_raise_syn(parser->raiser,
                "Invalid use of uninitialized property '@%s'.",
                sym->name);

    lily_es_push_property(parser->expr, (lily_prop_entry *)sym);
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

    if (tt == tree_call || tt == tree_parenth || tt == tree_typecast ||
        tt == tree_named_call)
        expect = tk_right_parenth;
    else if (tt == tree_tuple)
        expect = tk_tuple_close;
    else
        expect = tk_right_bracket;

    if (token != expect)
        lily_raise_syn(parser->raiser, "Expected closing token '%s', not '%s'.",
                tokname(expect), tokname(token));
}

/* There's this annoying problem where 1-1 can be 1 - 1 or 1 -1. This is called
   if an operator is wanted but a digit is given instead. It checks to see if
   the numeric token can be broken up into an operator and a value, instead of
   just an operator. */
static int maybe_digit_fixup(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    int result = 0;

    if (lex->token == tk_integer || lex->token == tk_double) {
        int is_positive = lex->n.integer_val >= 0;

        if (lily_lexer_digit_rescan(lex)) {
            if (is_positive)
                lily_es_push_binary_op(parser->expr, tk_plus);
            else
                lily_es_push_binary_op(parser->expr, tk_minus);

            result = 1;
        }
    }

    return result;
}

static void expression_right_parenth(lily_parse_state *parser, int *state)
{
    if (*state == ST_DEMAND_VALUE) {
        *state = ST_BAD_TOKEN;
        return;
    }
    else if (*state == ST_WANT_OPERATOR &&
             parser->expr->save_depth == 0) {
        *state = ST_DONE;
        return;
    }

    check_valid_close_tok(parser);
    lily_es_leave_tree(parser->expr);
    *state = ST_WANT_OPERATOR;
}

static void expression_bracket_tuple(lily_parse_state *parser, int *state)
{
    if (*state == ST_DEMAND_VALUE) {
        *state = ST_BAD_TOKEN;
        return;
    }
    else if (parser->expr->save_depth == 0) {
        *state = (*state == ST_WANT_OPERATOR);
        return;
    }

    check_valid_close_tok(parser);
    lily_es_leave_tree(parser->expr);
    *state = ST_WANT_OPERATOR;
}

/* This handles literals, and does that fixup thing if that's necessary. */
static void expression_literal(lily_parse_state *parser, int *state)
{
    if (*state == ST_WANT_OPERATOR &&
        maybe_digit_fixup(parser) == 0) {
        *state = (parser->expr->save_depth == 0);
        return;
    }

    lily_lex_state *lex = parser->lex;

    if (lex->token == tk_integer) {
        if (lex->n.integer_val <= INT16_MAX &&
            lex->n.integer_val >= INT16_MIN)
            lily_es_push_integer(parser->expr, (int16_t)
                    lex->n.integer_val);
        else {
            lily_literal *lit = lily_get_integer_literal(parser->symtab,
                    lex->n.integer_val);
            push_literal(parser, lit);
        }
    }
    else if (lex->token == tk_byte)
        lily_es_push_byte(parser->expr, (uint8_t) lex->n.integer_val);
    else if (lex->token == tk_double_quote) {
        lily_literal *lit = lily_get_string_literal(parser->symtab, lex->label);
        push_literal(parser, lit);
    }
    else if (lex->token == tk_bytestring) {
        lily_literal *lit = lily_get_bytestring_literal(parser->symtab,
                lex->label, lex->string_length);
        push_literal(parser, lit);
    }
    else if (lex->token == tk_double) {
        lily_literal *lit = lily_get_double_literal(parser->symtab,
                lex->n.double_val);
        push_literal(parser, lit);
    }

    *state = ST_WANT_OPERATOR;
}

static void expression_comma(lily_parse_state *parser, int *state)
{
    if (parser->expr->active == NULL)
        lily_raise_syn(parser->raiser, "Expected a value, not ','.");

    lily_ast *last_tree = lily_es_get_saved_tree(parser->expr);
    if (last_tree == NULL) {
        *state = ST_BAD_TOKEN;
        return;
    }

    lily_tree_type last_tt = last_tree->tree_type;

    /* Hash literals are linked as key, value, key, value. Commas get the
       values, so an even argument count is wrong here.  */
    if (last_tt == tree_hash &&
        (last_tree->args_collected & 0x1) == 0)
        lily_raise_syn(parser->raiser,
                "Expected a key => value pair before ','.");
    else if (last_tt == tree_subscript)
        lily_raise_syn(parser->raiser, "Subscripts cannot contain ','.");
    else if (last_tt == tree_parenth)
        lily_raise_syn(parser->raiser, "() expression cannot contain ','.");

    lily_es_collect_arg(parser->expr);

    if (last_tt == tree_call || last_tt == tree_named_call)
        *state = ST_DEMAND_VALUE;
    /* Allow a trailing comma for lists, hashes, and tuples. */
    else
        *state = ST_WANT_VALUE;
}

static void expression_arrow(lily_parse_state *parser, int *state)
{
    if (parser->expr->active == NULL)
        lily_raise_syn(parser->raiser, "Expected a value, not ','.");

    lily_ast *last_tree = lily_es_get_saved_tree(parser->expr);
    if (last_tree == NULL) {
        *state = ST_BAD_TOKEN;
        return;
    }

    lily_tree_type last_tt = last_tree->tree_type;

    if (last_tt == tree_list &&
        last_tree->args_collected == 0)
        last_tree->tree_type = tree_hash;
    /* Hashes are linked as key, value, key, value. Arrows get the keys, so they
       should see an even argument count. */
    else if (last_tt == tree_hash &&
             (last_tree->args_collected & 0x1) == 0)
        ;
    else {
        /* No special error message because arrows are really rare. */
        *state = ST_BAD_TOKEN;
        return;
    }

    lily_es_collect_arg(parser->expr);
    *state = ST_DEMAND_VALUE;
}

/* Unary expressions! These are easy, because tokens are ops. */
static void expression_unary(lily_parse_state *parser, int *state)
{
    if (*state == ST_WANT_OPERATOR)
        *state = ST_BAD_TOKEN;
    else {
        lily_es_push_unary_op(parser->expr, parser->lex->token);
        *state = ST_DEMAND_VALUE;
    }
}

static void expression_lambda(lily_parse_state *parser, int *state)
{
    if (parser->flags & PARSER_SIMPLE_EXPR)
        lily_raise_syn(parser->raiser, "Not allowed to use a lambda here.");

    /* Checking for an operator allows this
        
       `x.some_call(|x| ... )`

       to act like

       `x.some_call((|x| ... ))`

       which cuts a level of parentheses. */
    if (*state == ST_WANT_OPERATOR)
        lily_es_enter_tree(parser->expr, tree_call);

    lily_expr_state *es = parser->expr;
    int spot = es->pile_current;
    lily_lex_state *lex = parser->lex;

    lily_sp_insert(parser->expr_strings, lex->label, &es->pile_current);
    lily_es_push_text(parser->expr, tree_lambda, lex->expand_start_line, spot);

    if (*state == ST_WANT_OPERATOR)
        lily_es_leave_tree(parser->expr);

    *state = ST_WANT_OPERATOR;
}

/* This handles two rather different things. It could be an `x.y` access, OR
   `x.@(<type>)`. The emitter will have type information, so don't bother
   checking if either of them is correct. */
static void expression_dot(lily_parse_state *parser, int *state)
{
    if (*state != ST_WANT_OPERATOR) {
        *state = ST_BAD_TOKEN;
        return;
    }

    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);

    if (lex->token == tk_word) {
        lily_expr_state *es = parser->expr;
        int spot = es->pile_current;
        lily_sp_insert(parser->expr_strings, lex->label, &es->pile_current);
        lily_es_push_text(es, tree_oo_access, lex->line_num, spot);
    }
    else if (lex->token == tk_typecast_parenth) {
        lily_next_token(lex);

        lily_type *cast_type = get_type(parser);

        lily_es_enter_typecast(parser->expr, cast_type);
        lily_es_leave_tree(parser->expr);
        NEED_CURRENT_TOK(tk_right_parenth)
    }
    else
        lily_raise_syn(parser->raiser,
                "Expected either '%s' or '%s', not '%s'.",
                tokname(tk_word), tokname(tk_typecast_parenth),
                tokname(lex->token));
}

static void expression_named_arg(lily_parse_state *parser, int *state)
{
    lily_expr_state *es = parser->expr;

    if (es->root) {
        *state = ST_BAD_TOKEN;
        return;
    }

    lily_ast *last_tree = lily_es_get_saved_tree(parser->expr);
    if (last_tree == NULL) {
        *state = ST_BAD_TOKEN;
        return;
    }

    if (last_tree->tree_type != tree_call &&
        last_tree->tree_type != tree_named_call) {
        *state = ST_BAD_TOKEN;
        return;
    }

    last_tree->tree_type = tree_named_call;

    int spot = es->pile_current;
    lily_sp_insert(parser->expr_strings, parser->lex->label, &es->pile_current);
    lily_es_push_text(es, tree_oo_access, 0, spot);
    lily_es_push_binary_op(es, tk_keyword_arg);
    *state = ST_DEMAND_VALUE;
}

/* This is the magic function that handles expressions. The states it uses are
   defined above. Most callers will use expression instead of this. */
static void expression_raw(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    int state;

    if (parser->expr->root || parser->expr->save_depth)
        state = ST_DEMAND_VALUE;
    else
        state = ST_WANT_VALUE;

    while (1) {
        int expr_op = parser_tok_table[lex->token].expr_op;
        if (lex->token == tk_word) {
            if (state == ST_WANT_OPERATOR)
                state = (parser->expr->save_depth == 0);
            else {
                expression_word(parser, &state);
                /* Words never start with forward, but might finish with it.
                   Every word finishes with wanting an operator, but one of them
                   wants forward as well. This fixes the state so that each case
                   doesn't need an individual state fix. */
                state = (state & ST_FORWARD) + ST_WANT_OPERATOR;
            }
        }
        else if (expr_op != -1) {
            if (state == ST_WANT_OPERATOR) {
                lily_es_push_binary_op(parser->expr, lex->token);
                state = ST_DEMAND_VALUE;
            }
            else if (lex->token == tk_minus)
                expression_unary(parser, &state);
            else
                state = ST_BAD_TOKEN;
        }
        else if (lex->token == tk_left_parenth) {
            if (state != ST_WANT_OPERATOR) {
                lily_es_enter_tree(parser->expr, tree_parenth);
                state = ST_DEMAND_VALUE;
            }
            else {
                lily_es_enter_tree(parser->expr, tree_call);
                state = ST_WANT_VALUE;
            }
        }
        else if (lex->token == tk_left_bracket) {
            if (state != ST_WANT_OPERATOR) {
                lily_es_enter_tree(parser->expr, tree_list);
                state = ST_WANT_VALUE;
            }
            else {
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
        else if (lex->token == tk_right_parenth)
            expression_right_parenth(parser, &state);
        else if (lex->token == tk_right_bracket ||
                 lex->token == tk_tuple_close)
            expression_bracket_tuple(parser, &state);
        else if (lex->token == tk_integer || lex->token == tk_double ||
                 lex->token == tk_double_quote || lex->token == tk_bytestring ||
                 lex->token == tk_byte)
            expression_literal(parser, &state);
        else if (lex->token == tk_dot)
            expression_dot(parser, &state);
        else if (lex->token == tk_minus ||
                 lex->token == tk_not ||
                 lex->token == tk_tilde)
            expression_unary(parser, &state);
        else if (lex->token == tk_lambda)
            expression_lambda(parser, &state);
        /* Make sure this case stays lower down. If it doesn't, then certain
           expressions will exit before they really should. */
        else if (parser_tok_table[lex->token].val_or_end &&
                 parser->expr->save_depth == 0 &&
                 state == ST_WANT_OPERATOR)
            state = ST_DONE;
        else if (lex->token == tk_comma)
            expression_comma(parser, &state);
        else if (lex->token == tk_arrow)
            expression_arrow(parser, &state);
        else if (lex->token == tk_keyword_arg)
            expression_named_arg(parser, &state);
        else
            state = ST_BAD_TOKEN;

        if (state == ST_DONE)
            break;
        else if (state == ST_BAD_TOKEN)
            lily_raise_syn(parser->raiser, "Unexpected token '%s'.",
                    tokname(lex->token));
        else if (state & ST_FORWARD)
            state &= ~ST_FORWARD;
        else
            lily_next_token(lex);
    }
}

/* This calls expression_raw demanding a value. If you need that (and most
   callers do), then use this. If you don't, then call it raw. */
static void expression(lily_parse_state *parser)
{
    lily_es_flush(parser->expr);
    expression_raw(parser);
}

static void simple_expression(lily_parse_state *parser)
{
    parser->flags |= PARSER_SIMPLE_EXPR;
    expression_raw(parser);
    parser->flags &= ~PARSER_SIMPLE_EXPR;
}

/***
 *      _                    _         _
 *     | |    __ _ _ __ ___ | |__   __| | __ _ ___
 *     | |   / _` | '_ ` _ \| '_ \ / _` |/ _` / __|
 *     | |__| (_| | | | | | | |_) | (_| | (_| \__ \
 *     |_____\__,_|_| |_| |_|_.__/ \__,_|\__,_|___/
 *
 */

/** Lambdas are neat, but they present some interesting challenges. To make sure
    they have types for their arguments, lexer scoops up the lambdas as a single
    token. Emitter will later enter that token and provide it with whatever
    inference it can.

    The return of a lambda is whatever runs last, unless it's a block or some
    keyword. In that case, a lambda will send back `Unit`. **/

static inline void handle_multiline(lily_parse_state *, int);

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

    lily_next_token(parser->lex);
    while (1) {
        if (lex->token == tk_word)
            key_id = keyword_by_name(lex->label);

        if (key_id == -1) {
            expression(parser);
            if (lex->token != tk_end_lambda)
                /* This expression isn't the last one, so it can do whatever it
                   wants to do. */
                lily_eval_expr(parser->emit, parser->expr);
            else {
                /* The last expression is what will be returned, so give it the
                   inference information of the lambda. */
                lily_eval_lambda_body(parser->emit, parser->expr,
                        expect_type);

                if (parser->expr->root->result)
                    result_type = parser->expr->root->result->type;

                break;
            }
        }
        else {
            /* Don't call statement(1), or control is taken away from the
               lambda (and the final expression doesn't get inference). Instead,
               dispatch directly. */
            lily_next_token(lex);
            handle_multiline(parser, key_id);

            key_id = -1;
            if (lex->token == tk_end_lambda)
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
        lily_var *arg_var = declare_local_var(parser, NULL);
        lily_type *arg_type;

        if (lex->token == tk_colon) {
            lily_next_token(lex);
            arg_type = get_type(parser);
            arg_var->type = arg_type;
        }
        else {
            arg_type = NULL;
            if (num_args < infer_count)
                arg_type = expect_type->subtypes[num_args + 1];

            if (arg_type == NULL || arg_type->flags & TYPE_TO_BLOCK)
                lily_raise_syn(parser->raiser, "Cannot infer type of '%s'.",
                        lex->label);

            arg_var->type = arg_type;
        }

        lily_tm_add(parser->tm, arg_type);
        num_args++;

        if (lex->token == tk_comma)
            continue;
        else if (lex->token == tk_bitwise_or)
            break;
        else
            lily_raise_syn(parser->raiser,
                    "Expected either ',' or '|', not '%s'.",
                    tokname(lex->token));
    }

    return num_args;
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

    lily_lexer_load(lex, et_lambda, lambda_body);
    lex->line_num = lambda_start_line;

    lily_var *lambda_var = new_define_var(parser, "(lambda)",
            lambda_start_line);

    lily_emit_enter_lambda_block(parser->emit, lambda_var);

    /* Placeholder for the lambda's return type, unless one isn't found.*/
    lily_tm_add(parser->tm, lily_unit_type);
    lily_next_token(lex);

    if (lex->token == tk_bitwise_or)
        args_collected = collect_lambda_args(parser, expect_type);
    /* Otherwise the token is ||, meaning the lambda does not have args. */

    /* The current expression may not be done. This makes sure that the pool
       won't use the same trees again. */
    lily_es_checkpoint_save(parser->expr);
    root_result = parse_lambda_body(parser, expect_type);
    lily_es_checkpoint_restore(parser->expr);

    if (root_result != NULL)
        lily_tm_insert(parser->tm, tm_return, root_result);

    int flags = 0;
    if (expect_type && expect_type->cls->id == LILY_ID_FUNCTION &&
        expect_type->flags & TYPE_IS_VARARGS)
        flags = TYPE_IS_VARARGS;

    lambda_var->type = lily_tm_make_call(parser->tm, flags,
            parser->symtab->function_class, args_collected + 1);

    hide_block_vars(parser);
    lily_emit_leave_lambda_block(parser->emit, lex->line_num);
    lily_pop_lex_entry(lex);

    return lambda_var;
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

static void keyword_if(lily_parse_state *);
static void keyword_do(lily_parse_state *);
static void keyword_var(lily_parse_state *);
static void keyword_for(lily_parse_state *);
static void keyword_try(lily_parse_state *);
static void keyword_case(lily_parse_state *);
static void keyword_else(lily_parse_state *);
static void keyword_elif(lily_parse_state *);
static void keyword_enum(lily_parse_state *);
static void keyword_while(lily_parse_state *);
static void keyword_raise(lily_parse_state *);
static void keyword_match(lily_parse_state *);
static void keyword_break(lily_parse_state *);
static void keyword_class(lily_parse_state *);
static void keyword_public(lily_parse_state *);
static void keyword_static(lily_parse_state *);
static void keyword_scoped(lily_parse_state *);
static void keyword_define(lily_parse_state *);
static void keyword_return(lily_parse_state *);
static void keyword_except(lily_parse_state *);
static void keyword_import(lily_parse_state *);
static void keyword_forward(lily_parse_state *);
static void keyword_private(lily_parse_state *);
static void keyword_protected(lily_parse_state *);
static void keyword_continue(lily_parse_state *);

typedef void (keyword_handler)(lily_parse_state *);

/* This is setup so that handlers[key_id] is the handler for that keyword. */
static keyword_handler *handlers[] = {
    keyword_if,
    keyword_do,
    keyword_var,
    keyword_for,
    keyword_try,
    keyword_case,
    keyword_else,
    keyword_elif,
    keyword_enum,
    keyword_while,
    keyword_raise,
    keyword_match,
    keyword_break,
    keyword_class,
    keyword_public,
    keyword_static,
    keyword_scoped,
    keyword_define,
    keyword_return,
    keyword_except,
    keyword_import,
    keyword_forward,
    keyword_private,
    keyword_protected,
    keyword_continue,
};

/* This is used by lambda handling so that statements (and the handler
   declarations) can come after lambdas. */
static inline void handle_multiline(lily_parse_state *parser, int key_id)
{
    handlers[key_id](parser);
}

static void error_member_redeclaration(lily_parse_state *parser,
        lily_class *cls, lily_named_sym *sym)
{
    if (sym->item_kind == ITEM_VAR)
        lily_raise_syn(parser->raiser,
                "A method in class '%s' already has the name '%s'.",
                cls->name, sym->name);
    else
        lily_raise_syn(parser->raiser,
                "A property in class '%s' already has the name @%s.",
                cls->name, sym->name);
}

static int sym_visible_from(lily_class *cls, lily_named_sym *sym)
{
    int result = 1;

    if (sym->flags & SYM_SCOPE_PRIVATE) {
        lily_class *parent;

        if (sym->item_kind == ITEM_VAR)
            parent = ((lily_var *)sym)->parent;
        else
            parent = ((lily_prop_entry *)sym)->parent;

        /* Private members aren't really private if inheriting classes need
           to avoid their names. So don't count them. */
        if (parent != cls)
            result = 0;
    }

    return result;
}

static lily_prop_entry *declare_property(lily_parse_state *parser,
        uint16_t flags)
{
    char *name = parser->lex->label;
    lily_class *cls = parser->current_class;
    lily_named_sym *sym = lily_find_member(cls, name);

    if (sym && sym_visible_from(cls, sym))
        error_member_redeclaration(parser, cls, sym);

    lily_prop_entry *prop = lily_add_class_property(cls, NULL, name, flags);

    lily_next_token(parser->lex);
    return prop;
}

/* This is called when @<name> is given outside of a class or <name> is given at
   the top of a class. */
static void bad_decl_token(lily_parse_state *parser)
{
    const char *message;

    if (parser->lex->token == tk_word)
        message = "Class properties must start with @.";
    else
        message = "Cannot use a class property outside of a constructor.";

    lily_raise_syn(parser->raiser, message);
}

static void add_unresolved_defines_to_msgbuf(lily_parse_state *parser,
        lily_msgbuf *msgbuf)
{
    int count = parser->emit->block->forward_count;
    lily_module_entry *m = parser->symtab->active_module;
    lily_var *var_iter;

    if (parser->emit->block->block_type == block_file)
        var_iter = m->var_chain;
    else
        var_iter = (lily_var *)m->class_chain->members;

    while (var_iter) {
        if (var_iter->flags & VAR_IS_FORWARD) {
            lily_proto *p = lily_emit_proto_for_var(parser->emit, var_iter);

            lily_mb_add_fmt(msgbuf, "\n* %s at line %d", p->name,
                    var_iter->line_num);

            if (count == 1)
                break;
            else
                count--;
        }

        var_iter = var_iter->next;
    }
}

static void error_forward_decl_keyword(lily_parse_state *parser, int key)
{
    lily_msgbuf *msgbuf = lily_mb_flush(parser->msgbuf);
    const char *action = "";

    if (key == KEY_VAR) {
        if (parser->emit->block->block_type == block_class)
            action = "declare a class property";
        else
            action = "declare a global var";
    }
    else
        action = "use 'import'";

    lily_mb_add_fmt(msgbuf, "Cannot %s when there are unresolved forward(s):",
            action);

    add_unresolved_defines_to_msgbuf(parser, msgbuf);
    lily_raise_syn(parser->raiser, lily_mb_raw(msgbuf));
}

static void parse_var(lily_parse_state *parser, int modifiers)
{
    lily_lex_state *lex = parser->lex;
    lily_sym *sym = NULL;
    lily_block *block = parser->emit->block;

    lily_token want_token, other_token;
    if (block->block_type == block_class) {
        if (modifiers == 0)
            lily_raise_syn(parser->raiser,
                    "Class var declaration must start with a scope.");

        want_token = tk_prop_word;
        other_token = tk_word;
    }
    else {
        want_token = tk_word;
        other_token = tk_prop_word;
    }

    if (block->forward_count)
        error_forward_decl_keyword(parser, KEY_VAR);

    while (1) {
        lily_es_flush(parser->expr);

        /* For this special case, give a useful error message. */
        if (lex->token == other_token)
            bad_decl_token(parser);

        NEED_CURRENT_TOK(want_token)

        if (lex->token == tk_word)
            sym = (lily_sym *)declare_scoped_var(parser);
        else
            sym = (lily_sym *)declare_property(parser, modifiers);

        sym->flags |= SYM_NOT_INITIALIZED;

        if (lex->token == tk_colon) {
            lily_next_token(lex);
            sym->type = get_type(parser);
        }

        if (lex->token != tk_equal) {
            lily_raise_syn(parser->raiser,
                    "An initialization expression is required here.");
        }

        lily_es_push_assign_to(parser->expr, sym);
        lily_next_token(lex);
        expression_raw(parser);
        lily_eval_expr(parser->emit, parser->expr);

        if (lex->token != tk_comma)
            break;

        lily_next_token(lex);
    }
}

static void keyword_var(lily_parse_state *parser)
{
    parse_var(parser, 0);
}

static void finish_define_init(lily_parse_state *parser, lily_var *var)
{
    uint16_t count = parser->expr->optarg_count;

    if (count) {
        uint16_t i;

        /* This reverses optargs to allow for a pop+eval loop. */
        lily_es_optarg_finish(parser->expr);

        for (i = 0;i < count;i++) {
            lily_es_checkpoint_restore(parser->expr);
            lily_eval_optarg(parser->emit, parser->expr->root);
        }

        lily_es_checkpoint_restore(parser->expr);
    }

    var->flags &= ~SYM_NOT_INITIALIZED;
}

static void error_forward_decl_pending(lily_parse_state *parser)
{
    lily_msgbuf *msgbuf = lily_mb_flush(parser->msgbuf);
    const char *what = "";

    /* Don't say 'file', because the close tag `?>` checks for this. */
    if (parser->emit->block->block_type == block_file)
        what = "module";
    else
        what = "class";

    lily_mb_add_fmt(msgbuf,
            "Reached end of %s with unresolved forward(s):", what);
    add_unresolved_defines_to_msgbuf(parser, msgbuf);
    lily_raise_syn(parser->raiser, lily_mb_raw(msgbuf));
}

static const char *scope_to_str(uint16_t flags)
{
    const char *result;

    if (flags & SYM_SCOPE_PRIVATE)
        result = "private";
    else if (flags & SYM_SCOPE_PROTECTED)
        result = "protected";
    else
        result = "public";

    return result;
}

static const char *static_to_str(uint16_t flags)
{
    const char *result;

    if (flags & VAR_IS_STATIC)
        result = " static";
    else
        result = "";

    return result;
}

static void error_forward_decl_modifiers(lily_parse_state *parser,
        lily_var *define_var)
{
    lily_proto *p = lily_emit_proto_for_var(parser->emit, define_var);
    uint16_t modifiers = define_var->flags;
    const char *scope_str = scope_to_str(modifiers);
    const char *static_str = static_to_str(modifiers);

    lily_raise_syn(parser->raiser,
            "Wrong qualifiers in resolution of %s (expected: %s%s).",
            p->name, scope_str, static_str);
}

static void process_docblock(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_next_token(lex);

    int key_id;
    if (lex->token == tk_word)
        key_id = keyword_by_name(lex->label);
    else
        key_id = -1;

    if (key_id == KEY_PRIVATE ||
        key_id == KEY_PROTECTED ||
        key_id == KEY_PUBLIC ||
        key_id == KEY_DEFINE ||
        key_id == KEY_CLASS) {
        lily_next_token(lex);
        handlers[key_id](parser);
    }
    else
        lily_raise_syn(parser->raiser,
                "Docblock must be followed by a function or class definition.");
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
                lily_next_token(lex);
                handlers[key_id](parser);
            }
            else {
                expression(parser);
                lily_eval_expr(parser->emit, parser->expr);
            }
        }
        else if (token == tk_integer || token == tk_double ||
                 token == tk_double_quote || token == tk_left_parenth ||
                 token == tk_left_bracket || token == tk_tuple_open ||
                 token == tk_prop_word || token == tk_bytestring ||
                 token == tk_byte) {
            expression(parser);
            lily_eval_expr(parser->emit, parser->expr);
        }
        else if (token == tk_docblock)
            process_docblock(parser);
        /* The caller will be expecting '}' or maybe ?> / EOF if it's the main
           parse loop. */
        else if (multi)
            break;
        /* Single-line expressions need a value to prevent things like
           'if 1: }' and 'if 1: ?>'. */
        else
            lily_raise_syn(parser->raiser, "Expected a value, not '%s'.",
                    tokname(token));
    } while (multi);
}

/* This handles the '{' ... '}' part for blocks. */
static void parse_block_body(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);
    /* statement expects the token to be ready. */
    if (lex->token != tk_right_curly)
        statement(parser, 1);
    NEED_CURRENT_TOK(tk_right_curly)
    lily_next_token(lex);
}

static void do_elif(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    hide_block_vars(parser);
    lily_emit_branch_switch(parser->emit);
    expression(parser);
    lily_eval_entry_condition(parser->emit, parser->expr);
    NEED_COLON_AND_NEXT;
}

static void do_else(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    hide_block_vars(parser);
    lily_emit_branch_finalize(parser->emit);
    NEED_COLON_AND_NEXT;
}

static void keyword_if(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_if_block(parser->emit);
    expression(parser);
    lily_eval_entry_condition(parser->emit, parser->expr);
    NEED_COLON_AND_BRACE;
    lily_next_token(lex);

    int have_else = 0;

    while (1) {
        if (lex->token == tk_word) {
            int key = keyword_by_name(lex->label);
            if (key == -1) {
                expression(parser);
                lily_eval_expr(parser->emit, parser->expr);
            }
            else if (key != KEY_ELIF && key != KEY_ELSE) {
                lily_next_token(lex);
                handlers[key](parser);
            }
            else if (have_else == 1) {
                const char *what = "else";

                if (key == KEY_ELIF)
                    what = "elif";

                lily_raise_syn(parser->raiser,
                        "%s after else in multi-line if block.", what);
            }
        }
        else if (lex->token != tk_right_curly) {
            expression(parser);
            lily_eval_expr(parser->emit, parser->expr);
        }

        if (lex->token == tk_word && have_else == 0) {
            int key = keyword_by_name(lex->label);

            if (key == KEY_ELIF || key == KEY_ELSE) {
                lily_next_token(lex);
                if (key == KEY_ELIF)
                    do_elif(parser);
                else {
                    do_else(parser);
                    have_else = 1;
                }

                continue;
            }
        }
        else if (lex->token == tk_right_curly)
            break;
    }

    lily_next_token(lex);
    hide_block_vars(parser);
    lily_emit_leave_block(parser->emit);
}

static void keyword_elif(lily_parse_state *parser)
{
    lily_raise_syn(parser->raiser, "'elif' without 'if'.");
}

static void keyword_else(lily_parse_state *parser)
{
    lily_raise_syn(parser->raiser, "'else' without 'if'.");
    }

static int code_is_after_exit(lily_parse_state *parser)
{
    lily_token token = parser->lex->token;

    /* It's not dead if this block is being exited. */
    if (token == tk_right_curly ||
        token == tk_eof ||
        token == tk_end_tag)
        return 0;

    if (token == tk_word) {
        int key_id = keyword_by_name(parser->lex->label);

        /* A different branch is starting, and that's okay. */
        if (key_id == KEY_ELIF ||
            key_id == KEY_ELSE ||
            key_id == KEY_EXCEPT ||
            key_id == KEY_CASE)
            return 0;
    }

    return 1;
}

static void keyword_return(lily_parse_state *parser)
{
    lily_block *block = parser->emit->scope_block;
    lily_type *return_type = NULL;

    if (block->block_type == block_class)
        lily_raise_syn(parser->raiser,
                "'return' not allowed in a class constructor.");
    else if (block->block_type == block_lambda)
        lily_raise_syn(parser->raiser, "'return' not allowed in a lambda.");
    else if (block->block_type == block_file)
        lily_raise_syn(parser->raiser, "'return' used outside of a function.");
    else
        return_type = block->scope_var->type->subtypes[0];

    if (return_type != lily_unit_type)
        expression(parser);

    lily_eval_return(parser->emit, parser->expr, return_type);

    if (code_is_after_exit(parser)) {
        const char *extra = ".";
        if (return_type == lily_unit_type)
            extra = " (no return type given).";

        lily_raise_syn(parser->raiser,
                "Statement(s) after 'return' will not execute%s", extra);
    }
}

static void keyword_while(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_while_block(parser->emit);

    expression(parser);
    lily_eval_entry_condition(parser->emit, parser->expr);
    NEED_COLON_AND_BRACE;
    parse_block_body(parser);

    hide_block_vars(parser);
    lily_emit_leave_block(parser->emit);
}

static void keyword_continue(lily_parse_state *parser)
{
    if (lily_emit_try_write_continue(parser->emit) == 0)
        lily_raise_syn(parser->raiser, "'continue' used outside of a loop.");

    if (code_is_after_exit(parser))
        lily_raise_syn(parser->raiser,
                "Statement(s) after 'continue' will not execute.");
}

static void keyword_break(lily_parse_state *parser)
{
    if (lily_emit_try_write_break(parser->emit) == 0)
        lily_raise_syn(parser->raiser, "'break' used outside of a loop.");

    if (code_is_after_exit(parser))
        lily_raise_syn(parser->raiser,
                "Statement(s) after 'break' will not execute.");
}

static void parse_for_expr(lily_parse_state *parser, lily_var *var)
{
    lily_expr_state *es = parser->expr;

    lily_next_token(parser->lex);
    expression(parser);

    /* Don't allow assigning expressions, since that just looks weird.
       ex: for i in a += 10..5
       Also, it makes no real sense to do that. */
    if (es->root->tree_type == tree_binary &&
        IS_ASSIGN_TOKEN(es->root->op)) {
        lily_raise_syn(parser->raiser,
                   "For range value expression contains an assignment.");
    }

    lily_eval_to_loop_var(parser->emit, parser->expr, var);
}

static void expect_word(lily_parse_state *parser, const char *what)
{
    lily_lex_state *lex = parser->lex;

    NEED_CURRENT_TOK(tk_word)

    if (strcmp(lex->label, what) != 0)
        lily_raise_syn(parser->raiser, "Expected '%s', not '%s'.", what,
                lex->label);
}

static int extra_if_word(lily_parse_state *parser, const char *what)
{
    lily_lex_state *lex = parser->lex;
    int result = 0;

    if (lex->token == tk_word) {
        if (strcmp(lex->label, what) != 0)
            lily_raise_syn(parser->raiser, "Expected '%s', not '%s'.", what,
                    lex->label);

        result = 1;
    }

    return result;
}

static void keyword_for(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_type *integer_type = parser->symtab->integer_class->self_type;

    NEED_CURRENT_TOK(tk_word)
    lily_emit_enter_for_in_block(parser->emit);

    lily_var *loop_var = find_active_var(parser, lex->label);

    if (loop_var == NULL) {
        lily_class *cls = parser->symtab->integer_class;
        loop_var = new_local_var(parser, cls->self_type, lex->label,
                lex->line_num);
    }
    else if (loop_var->type->cls->id != LILY_ID_INTEGER) {
        lily_raise_syn(parser->raiser,
                   "Loop var must be type Integer, not type '^T'.",
                   loop_var->type);
    }

    lily_var *for_start = new_local_var(parser, integer_type, "", 0);
    lily_var *for_end = new_local_var(parser, integer_type, "", 0);
    lily_var *for_step = new_local_var(parser, integer_type, "", 0);

    lily_next_token(lex);
    expect_word(parser, "in");
    parse_for_expr(parser, for_start);
    NEED_CURRENT_TOK(tk_three_dots)
    parse_for_expr(parser, for_end);

    if (extra_if_word(parser, "by"))
        parse_for_expr(parser, for_step);
    else {
        lily_es_flush(parser->expr);
        lily_es_push_assign_to(parser->expr, (lily_sym *)for_step);
        lily_es_push_integer(parser->expr, 1);
        lily_eval_expr(parser->emit, parser->expr);
    }

    lily_emit_write_for_header(parser->emit, loop_var, for_start, for_end,
                               for_step, lex->line_num);
    NEED_COLON_AND_BRACE;
    parse_block_body(parser);
    hide_block_vars(parser);
    lily_emit_leave_block(parser->emit);
}

static void keyword_do(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_do_while_block(parser->emit);
    NEED_COLON_AND_BRACE;
    parse_block_body(parser);
    expect_word(parser, "while");

    /* Hide vars before running the expression because inner vars may have had
       their initialization skipped over. */
    hide_block_vars(parser);
    lily_next_token(lex);
    expression(parser);
    lily_eval_exit_condition(parser->emit, parser->expr);
    lily_emit_leave_block(parser->emit);
}

static void run_loaded_module(lily_parse_state *parser,
        lily_module_entry *module)
{
    /* The flag is changed so that rewind can identify modules that didn't
       fully load and hide them from a subsequent pass. */
    module->flags &= ~MODULE_NOT_EXECUTED;
    module->flags |= MODULE_IN_EXECUTION;

    lily_lex_state *lex = parser->lex;
    lily_module_entry *save_active = parser->symtab->active_module;

    parser->symtab->active_module = module;

    /* This is either `__main__` or another `__module__`. */
    lily_type *module_type = parser->emit->scope_block->scope_var->type;
    lily_var *module_var = new_define_var(parser, "__module__", lex->line_num);

    module_var->type = module_type;
    module_var->module = module;
    lily_emit_enter_file_block(parser->emit, module_var);

    /* The whole of the file can be thought of as one large statement. */
    lily_next_token(lex);
    statement(parser, 1);

    /* Since this is processing an import, the lexer will raise an error if
       ?> is found. Because of that, multi-line statement can only end with
       either } or eof. Only one is right. */
    if (lex->token == tk_right_curly)
        lily_raise_syn(parser->raiser, "'}' outside of a block.");

    if (lex->token == tk_end_tag)
        lily_raise_syn(parser->raiser, "Unexpected token '?>'.");

    if (lex->token == tk_invalid)
        lily_raise_syn(parser->raiser, "Unexpected token '%s'.",
                tokname(lex->token));

    if (parser->emit->block->forward_count)
        error_forward_decl_pending(parser);

    uint16_t last_line = lex->line_num;

    /* Vars and functions in this block are globals, so don't hide them. */
    lily_pop_lex_entry(lex);

    /* Since this writes the call to the `__module__` function in the importer,
       it has to come after the lex entry is gone. */
    lily_emit_leave_import_block(parser->emit, lex->line_num, last_line);
    parser->symtab->active_module = save_active;
    module->flags &= ~MODULE_IN_EXECUTION;
}

/* This links 'count' symbols from 'source' into the active module. The symbol
   names come from popping names inserted by collect_import_refs. */
static void link_import_syms(lily_parse_state *parser,
        lily_module_entry *source, uint16_t count)
{
    lily_symtab *symtab = parser->symtab;
    lily_module_entry *active = symtab->active_module;
    lily_buffer_u16 *buffer = parser->data_stack;
    uint16_t start = lily_u16_pos(buffer) - count;
    uint16_t iter = start;

    lily_u16_set_pos(parser->data_stack, start);
    parser->data_string_pos = lily_u16_get(buffer, iter);

    do {
        uint16_t search_pos = lily_u16_get(buffer, iter);
        char *name = lily_sp_get(parser->data_strings, search_pos);
        lily_sym *sym = find_existing_sym(active, name);

        if (sym)
            lily_raise_syn(parser->raiser, "'%s' has already been declared.",
                    name);

        sym = find_existing_sym(source, name);

        if (sym == NULL)
            sym = (lily_sym *)try_toplevel_dynaload(parser, source, name);

        if (sym == NULL)
            lily_raise_syn(parser->raiser,
                    "Cannot find symbol '%s' inside of module '%s'.",
                    name, source->loadname);
        else if (sym->item_kind == ITEM_MODULE)
            lily_raise_syn(parser->raiser,
                    "Not allowed to directly import modules ('%s').", name);

        lily_add_symbol_ref(active, sym);
        iter++;
        count--;
    } while (count);
}

/* This function collects symbol names within parentheses for import. The result
   is how many names were collected, and is never zero. */
static uint16_t collect_import_refs(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    uint16_t count = 0;

    while (1) {
        NEED_NEXT_TOK(tk_word)
        add_data_string(parser, lex->label);
        count++;

        lily_next_token(lex);

        if (lex->token == tk_right_parenth)
            break;
        else if (lex->token != tk_comma)
            lily_raise_syn(parser->raiser,
                    "Expected either ',' or ')', not '%s'.",
                    tokname(lex->token));
    }

    lily_next_token(lex);
    return count;
}

static void parse_import_path_into_ims(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_import_state *ims = parser->ims;

    ims->is_slashed_path = 0;

    if (lex->token == tk_double_quote) {
        lily_lexer_verify_path_string(lex);
        ims->fullname = lex->label;

        const char *pending_loadname = strrchr(ims->fullname, LILY_PATH_CHAR);

        if (pending_loadname == NULL)
            pending_loadname = ims->fullname;
        else {
            pending_loadname += 1;
            ims->is_slashed_path = 1;
        }

        ims->pending_loadname = pending_loadname;
    }
    else if (lex->token == tk_word) {
        ims->fullname = lex->label;
        ims->pending_loadname = ims->fullname;
    }
    else
        lily_raise_syn(parser->raiser,
                "'import' expected a path (identifier or string), not %s.",
                tokname(lex->token));

    ims->source_module = parser->symtab->active_module;
    ims->last_import = NULL;
    ims->dirname = NULL;
}

static lily_module_entry *find_registered_module(lily_parse_state *parser,
        const char *target)
{
    /* Registered modules are allowed to have non-identifier paths, but it's
       really not a good idea. Block them to discourage that. */
    if (parser->ims->is_slashed_path)
        return NULL;

    lily_symtab *symtab = parser->symtab;
    lily_module_entry *module = lily_find_registered_module(symtab, target);

    if (module &&
        (module->flags & MODULE_IS_PREDEFINED) == 0) {
        /* Prevent registered modules from being loaded outside of the initial
           package. This allows a different embedder to simulate the module by
           only needing to supply a single file at the initial root. It also
           prevents potential issues from name conflicts in subpackages. */
        const char *active_root = symtab->active_module->root_dirname;
        const char *main_root = parser->main_module->root_dirname;

        if (strcmp(active_root, main_root) != 0)
            module = NULL;
    }

    return module;
}

static void keyword_import(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file)
        lily_raise_syn(parser->raiser, "Cannot import a file here.");

    if (block->forward_count)
        error_forward_decl_keyword(parser, KEY_IMPORT);

    lily_symtab *symtab = parser->symtab;
    lily_module_entry *active = symtab->active_module;

    while (1) {
        uint16_t import_sym_count = 0;

        if (lex->token == tk_left_parenth) {
            lily_u16_write_1(parser->data_stack, parser->data_string_pos);
            import_sym_count = collect_import_refs(parser);
        }

        parse_import_path_into_ims(parser);

        lily_import_state *ims = parser->ims;
        lily_module_entry *module = NULL;

        /* Will the name that is going to be added conflict with something that
           has already been added? */
        if (lily_find_module(active, ims->pending_loadname))
            lily_raise_syn(parser->raiser,
                    "A module named '%s' has already been imported here.",
                    ims->pending_loadname);

        module = find_registered_module(parser, ims->pending_loadname);

        /* Is there a cached version that was loaded somewhere else? */
        if (module == NULL) {
            module = load_module(parser, ims->fullname);
            /* module is never NULL: load_module raises on error. */

            /* This is only set on modules that are new and native. */
            if (module->flags & MODULE_NOT_EXECUTED)
                run_loaded_module(parser, module);
        }

        lily_next_token(parser->lex);
        if (lex->token == tk_word && strcmp(lex->label, "as") == 0) {
            if (import_sym_count)
                lily_raise_syn(parser->raiser,
                        "Cannot use 'as' when only specific items are being imported.");

            NEED_NEXT_TOK(tk_word)
            /* This link must be done now, because the next token may be a word
               and lex->label would be modified. */
            link_module_to(active, module, lex->label);
            lily_next_token(lex);
        }
        else if (import_sym_count) {
            link_import_syms(parser, module, import_sym_count);
            parser->data_string_pos = lily_u16_pop(parser->data_stack);
        }
        else
            link_module_to(active, module, NULL);

        if (lex->token == tk_comma) {
            lily_next_token(parser->lex);
            continue;
        }
        else
            break;
    }
}

static void process_except(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_class *except_cls = get_type(parser)->cls;
    lily_emit_state *emit = parser->emit;
    lily_var *exception_var = NULL;

    /* If it's 'except Exception', then all possible cases have been handled. */
    if (lily_class_greater_eq_id(LILY_ID_EXCEPTION, except_cls) == 0)
        lily_raise_syn(parser->raiser, "'%s' is not a valid exception class.",
                except_cls->name);
    else if (except_cls->generic_count != 0)
        lily_raise_syn(parser->raiser, "'except' type cannot have subtypes.");

    if (emit->block->flags & BLOCK_FINAL_BRANCH)
        lily_raise_syn(parser->raiser, "'except' clause is unreachable.");

    hide_block_vars(parser);

    if (extra_if_word(parser, "as")) {
        NEED_NEXT_TOK(tk_word)
        exception_var = declare_local_var(parser, except_cls->self_type);
    }

    lily_emit_except_switch(emit, except_cls, exception_var, lex->line_num);
    NEED_COLON_AND_NEXT;
}

static void keyword_try(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_try_block(parser->emit, lex->line_num);
    NEED_COLON_AND_BRACE;
    lily_next_token(lex);

    while (1) {
        if (lex->token == tk_word) {
            int key = keyword_by_name(lex->label);
            if (key == -1) {
                expression(parser);
                lily_eval_expr(parser->emit, parser->expr);
            }
            else if (key != KEY_EXCEPT) {
                lily_next_token(lex);
                handlers[key](parser);
            }
        }
        else if (lex->token != tk_right_curly)
            statement(parser, 0);

        if (lex->token == tk_word) {
            int key = keyword_by_name(lex->label);

            if (key == KEY_EXCEPT) {
                lily_next_token(lex);
                process_except(parser);
                continue;
            }
        }
        else if (lex->token == tk_right_curly)
            break;
    }

    lily_next_token(lex);
    hide_block_vars(parser);
    lily_emit_leave_block(parser->emit);
}

static void keyword_except(lily_parse_state *parser)
{
    lily_raise_syn(parser->raiser, "'except' outside 'try'.");
}

static void keyword_raise(lily_parse_state *parser)
{
    if (parser->emit->scope_block->block_type == block_lambda)
        lily_raise_syn(parser->raiser, "'raise' not allowed in a lambda.");

    expression(parser);
    lily_eval_raise(parser->emit, parser->expr);

    if (code_is_after_exit(parser))
        lily_raise_syn(parser->raiser,
                "Statement(s) after 'raise' will not execute.");
}

static void ensure_valid_class(lily_parse_state *parser, const char *name)
{
    if (name[1] == '\0')
        lily_raise_syn(parser->raiser,
                "'%s' is not a valid class name (too short).", name);

    lily_module_entry *m = parser->symtab->active_module;
    lily_item *item = (lily_item *)find_or_dl_class(parser, m, name);

    if (item && item->item_kind != ITEM_VAR) {
        const char *prefix;
        const char *suffix;
        const char *what = "";
        lily_class *cls = NULL;

        /* Only classes, enums, and variants will reach here. Find out which one
           and report accordingly. */
        if (item->item_kind & (ITEM_IS_CLASS | ITEM_IS_ENUM))
            cls = (lily_class *)item;
        else if (item->item_kind & ITEM_IS_VARIANT)
            cls = ((lily_variant_class *)item)->parent;

        if (cls->module == parser->symtab->builtin_module) {
            prefix = "A built-in";
            suffix = "already exists.";
        }
        else {
            prefix = "A";
            suffix = "has already been declared.";
        }

        if (item->item_kind & ITEM_IS_CLASS)
            what = " class";
        else if (item->item_kind & ITEM_IS_ENUM) {
            if (cls->line_num == 0)
                what = " enum";
            else
                what = "n enum";
        }
        else if (item->item_kind & ITEM_IS_VARIANT)
            what = " variant";

        lily_raise_syn(parser->raiser, "%s%s named '%s' %s", prefix, what, name,
                suffix);
    }
}

static void parse_super(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    NEED_NEXT_TOK(tk_word)

    lily_class *super_class = resolve_class_name(parser);

    if (super_class == cls)
        lily_raise_syn(parser->raiser, "A class cannot inherit from itself!");
    else if (super_class->item_kind != ITEM_CLASS_NATIVE)
        lily_raise_syn(parser->raiser, "'%s' cannot be inherited from.",
                lex->label);

    int adjust = super_class->prop_count;

    /* Lineage must be fixed before running the inherited constructor, as the
       constructor may use 'self'. */
    cls->parent = super_class;
    cls->prop_count += super_class->prop_count;
    cls->inherit_depth = super_class->inherit_depth + 1;

    if (cls->members) {
        lily_named_sym *sym = cls->members;

        while (sym) {
            if (sym->item_kind == ITEM_PROPERTY) {
                /* Shorthand properties have already been checked for uniqueness
                   against each other. Now that a parent class is known, check
                   for uniqueness there too. */
                lily_named_sym *search_sym = lily_find_member(super_class,
                        sym->name);

                if (search_sym && sym_visible_from(cls, search_sym))
                    error_member_redeclaration(parser, super_class, search_sym);

                sym->reg_spot += adjust;
            }

            sym = sym->next;
        }
    }
}

/* There's a class to inherit from, so run the constructor. */
static void super_expression(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    lily_class *super_cls = cls->parent;
    lily_var *class_new = (lily_var *)lily_find_member_in_class(super_cls,
            "<new>");
    lily_expr_state *es = parser->expr;

    /* This prevents the class from using uninitialized data from itself or from
       the class it's trying to inherit. */
    parser->current_class = NULL;
    lily_es_flush(es);

    /* This is a special tree that tells emitter to nail generics in place
       instead of allowing solving. that makes ts far far simpler, because the
       A of a class is the A of the parent for any number of levels. */
    lily_es_push_inherited_new(es, class_new);
    lily_es_enter_tree(es, tree_call);

    /* This makes expression bail on ',' and ')'. That prevents expression from
       trying to be "helpful" and allowing subscripts/dot access/etc. against
       the constructor. */
    es->save_depth = 0;

    lily_next_token(parser->lex);

    if (lex->token == tk_left_parenth) {
        /* Since the call was already entered, skip the first '(' or the parser
           will attempt to enter it again. */
        lily_next_token(lex);
        if (lex->token == tk_right_parenth)
            lily_raise_syn(parser->raiser,
                    "Empty () not needed here for inherited new.");

        while (1) {
            expression_raw(parser);
            lily_es_collect_arg(parser->expr);
            if (lex->token == tk_comma) {
                lily_next_token(lex);
                continue;
            }
            else if (lex->token == tk_right_parenth) {
                lily_next_token(lex);
                break;
            }
            else
                lily_raise_syn(parser->raiser,
                        "Expected either ',' or ')', not '%s'.",
                        tokname(lex->token));
        }
    }

    /* Gotta fix the depth before the drop, or it'll roll into negative. */
    parser->expr->save_depth = 1;
    lily_es_leave_tree(parser->expr);
    lily_eval_expr(parser->emit, es);
    parser->current_class = cls;
}

/* This handles everything needed to create a class, including the inheritance
   if that turns out to be necessary. */
static void parse_class_header(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    lily_var *call_var = new_method_var(parser, cls, "<new>",
            SYM_SCOPE_PUBLIC | SYM_NOT_INITIALIZED, lex->line_num);

    lily_emit_enter_class_block(parser->emit, call_var);
    parser->current_class = cls;
    collect_generics_for(parser, cls);
    lily_tm_add(parser->tm, cls->self_type);
    collect_call_args(parser, call_var, F_COLLECT_CLASS);

    if (lex->token == tk_lt)
        parse_super(parser, cls);

    /* Don't make 'self' available until the class is fully constructed. */
    lily_emit_create_block_self(parser->emit, cls->self_type);
    lily_emit_write_class_init(parser->emit, cls, lex->line_num);
    finish_define_init(parser, call_var);

    if (cls->members->item_kind == ITEM_PROPERTY)
        lily_emit_write_shorthand_ctor(parser->emit, cls,
                parser->symtab->active_module->var_chain, lex->line_num);

    if (cls->parent)
        super_expression(parser, cls);

    lily_emit_activate_block_self(parser->emit);
}

/* This is a helper function that scans 'target' to determine if it will require
   any gc information to hold. */
static int get_gc_flags_for(lily_type *target)
{
    int result_flag = 0;

    if (target->cls->flags & (CLS_GC_TAGGED | CLS_VISITED))
        result_flag = CLS_GC_TAGGED;
    else {
        result_flag = target->cls->flags & (CLS_GC_TAGGED | CLS_GC_SPECULATIVE);

        if (target->subtype_count) {
            int i;
            for (i = 0;i < target->subtype_count;i++)
                result_flag |= get_gc_flags_for(target->subtypes[i]);
        }
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
    int mark = 0;

    if (parent_iter) {
        /* Start with this, just in case the child has no properties. */
        mark = parent_iter->flags & (CLS_GC_TAGGED | CLS_GC_SPECULATIVE);
        if (mark == CLS_GC_TAGGED) {
            target->flags |= CLS_GC_TAGGED;
            return;
        }

        while (parent_iter) {
            parent_iter->flags |= CLS_VISITED;
            parent_iter = parent_iter->parent;
        }
    }

    /* If this class sees itself, it absolutely needs a tag. */
    target->flags |= CLS_VISITED;

    lily_named_sym *member_iter = target->members;

    while (member_iter) {
        if (member_iter->item_kind == ITEM_PROPERTY)
            mark |= get_gc_flags_for(member_iter->type);

        member_iter = member_iter->next;
    }

    /* To eliminate confusion, make sure only one is set. */
    if (mark & CLS_GC_TAGGED)
        mark &= ~CLS_GC_SPECULATIVE;

    parent_iter = target->parent;
    while (parent_iter) {
        parent_iter->flags &= ~CLS_VISITED;
        parent_iter = parent_iter->parent;
    }

    target->flags &= ~CLS_VISITED;
    target->flags |= mark;
}

static void keyword_class(lily_parse_state *parser)
{
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file)
        lily_raise_syn(parser->raiser, "Cannot define a class here.");

    lily_lex_state *lex = parser->lex;
    NEED_CURRENT_TOK(tk_word);
    ensure_valid_class(parser, lex->label);

    lily_class *cls = lily_new_class(parser->symtab, lex->label, lex->line_num);

    parse_class_header(parser, cls);
    NEED_CURRENT_TOK(tk_left_curly)
    parse_block_body(parser);

    if (parser->emit->block->forward_count)
        error_forward_decl_pending(parser);

    determine_class_gc_flag(parser, cls);
    parser->current_class = NULL;
    lily_gp_restore(parser->generics, 0);
    hide_block_vars(parser);
    lily_emit_leave_class_block(parser->emit, lex->line_num);
}

/* This is called when a variant takes arguments. It parses those arguments to
   spit out the 'variant_type' conversion type of the variant. These types are
   internally really going to make a tuple instead of being a call. */
static void parse_variant_header(lily_parse_state *parser,
        lily_variant_class *variant_cls)
{
    /* For consistency with `Function`, the result of a variant is the
       all-generic type of the parent enum. */
    lily_tm_add(parser->tm, variant_cls->parent->self_type);
    collect_call_args(parser, variant_cls, F_COLLECT_VARIANT);

    variant_cls->item_kind = ITEM_VARIANT_FILLED;
}

static void ensure_valid_enum(lily_parse_state *parser, int is_scoped)
{
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file)
        lily_raise_syn(parser->raiser, "Cannot define an enum here.");

    lily_lex_state *lex = parser->lex;
    NEED_CURRENT_TOK(tk_word)

    if (is_scoped == 1) {
        if (strcmp(lex->label, "enum") != 0)
            lily_raise_syn(parser->raiser, "Expected 'enum' after 'scoped'.");

        NEED_NEXT_TOK(tk_word)
    }

    ensure_valid_class(parser, lex->label);
}

static void parse_enum(lily_parse_state *parser, int is_scoped)
{
    ensure_valid_enum(parser, is_scoped);

    lily_lex_state *lex = parser->lex;
    lily_class *enum_cls = lily_new_enum_class(parser->symtab, lex->label,
            lex->line_num);

    if (is_scoped)
        enum_cls->item_kind = ITEM_ENUM_SCOPED;

    lily_emit_enter_enum_block(parser->emit, enum_cls);
    parser->current_class = enum_cls;
    collect_generics_for(parser, enum_cls);

    lily_type *empty_type = build_empty_variant_type(parser, enum_cls);

    NEED_CURRENT_TOK(tk_left_curly)
    lily_next_token(lex);

    while (1) {
        NEED_CURRENT_TOK(tk_word)

        lily_variant_class *variant_cls = lily_find_variant(enum_cls,
                lex->label);

        if (variant_cls == NULL && is_scoped == 0)
            variant_cls = (lily_variant_class *)find_active_class(parser,
                    lex->label);

        if (variant_cls)
            lily_raise_syn(parser->raiser,
                    "A class with the name '%s' already exists.",
                    lex->label);

        variant_cls = lily_new_variant_class(enum_cls, lex->label,
                lex->line_num);

        lily_next_token(lex);
        if (lex->token == tk_left_parenth)
            parse_variant_header(parser, variant_cls);
        else
            variant_cls->build_type = empty_type;

        if (lex->token == tk_right_curly)
            break;
        else if (lex->token == tk_word && lex->label[0] == 'd' &&
                 keyword_by_name(lex->label) == KEY_DEFINE)
            break;
        else {
            NEED_CURRENT_TOK(tk_comma)
            lily_next_token(lex);
        }
    }

    if (enum_cls->variant_size < 2) {
        lily_raise_syn(parser->raiser,
                "An enum must have at least two variants.");
    }

    lily_fix_enum_variant_ids(parser->symtab, enum_cls);

    if (lex->token == tk_word) {
        while (1) {
            lily_next_token(lex);
            keyword_define(parser);
            if (lex->token == tk_right_curly)
                break;
            else if (lex->token != tk_word ||
                keyword_by_name(lex->label) != KEY_DEFINE)
                lily_raise_syn(parser->raiser,
                        "Expected '}' or 'define', not '%s'.",
                        tokname(lex->token));
        }
    }

    /* Enums don't allow code or have a constructor, so don't write final code
       or bother hiding block vars. */
    lily_emit_leave_enum_block(parser->emit);
    parser->current_class = NULL;

    lily_gp_restore(parser->generics, 0);
    lily_next_token(lex);
}

static void keyword_enum(lily_parse_state *parser)
{
    parse_enum(parser, 0);
}

static void keyword_scoped(lily_parse_state *parser)
{
    parse_enum(parser, 1);
}

/* This is called when a match against a class or enum is not considered
   complete. Matches for a class need to have an else, and matches against enums
   need all variants covered. */
static void error_incomplete_match(lily_parse_state *parser)
{
    lily_block *block = parser->emit->block;
    lily_class *match_class = block->match_type->cls;

    if ((match_class->item_kind & ITEM_IS_CLASS))
        lily_raise_syn(parser->raiser,
                "Match against a class must have an 'else' case.");

    lily_msgbuf *msgbuf = parser->raiser->aux_msgbuf;
    lily_named_sym *sym_iter = match_class->members;
    int match_case_start = block->match_case_start;
    int *match_cases = parser->emit->match_cases + match_case_start;
    int i;

    lily_mb_add(msgbuf,
            "Match pattern not exhaustive. The following case(s) are missing:");

    while (sym_iter) {
        if (sym_iter->item_kind & ITEM_IS_VARIANT) {
            for (i = match_case_start;i < parser->emit->match_case_pos;i++) {
                if (sym_iter->id == match_cases[i])
                    break;
            }

            if (i == parser->emit->match_case_pos)
                lily_mb_add_fmt(msgbuf, "\n* %s", sym_iter->name);
        }

        sym_iter = sym_iter->next;
    }

    lily_raise_syn(parser->raiser, lily_mb_raw(msgbuf));
}

static lily_var *parse_match_target(lily_parse_state *parser, lily_type *type)
{
    lily_lex_state *lex = parser->lex;
    lily_var *result;

    NEED_NEXT_TOK(tk_word)

    if (strcmp(lex->label, "_") != 0)
        result = declare_local_var(parser, type);
    else {
        lily_next_token(lex);
        result = NULL;
    }

    return result;
}

static lily_class *parse_match_class(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_class *match_cls = parser->emit->block->match_type->cls;

    NEED_CURRENT_TOK(tk_word)

    lily_class *cls = resolve_class_name(parser);

    if (lily_class_greater_eq(match_cls, cls) == 0) {
        lily_raise_syn(parser->raiser,
                "Class %s does not inherit from matching class %s.", cls->name,
                match_cls->name);
    }

    if (lily_emit_try_match_switch(parser->emit, cls) == 0)
        lily_raise_syn(parser->raiser, "Already have a case for class %s.",
                lex->label);

    /* Forbid non-monomorphic types to avoid the question of what to do
       if the match class has more generics. */
    if (cls->generic_count != 0) {
        lily_raise_syn(parser->raiser,
                "Class matching only works for types without generics.",
                cls->name);
    }

    return cls;
}

static lily_class *parse_match_variant(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_block *block = parser->emit->block;
    lily_class *match_cls = block->match_type->cls;

    NEED_CURRENT_TOK(tk_word)

    if (match_cls->item_kind == ITEM_ENUM_SCOPED) {
        if (strcmp(match_cls->name, lex->label) != 0)
            lily_raise_syn(parser->raiser,
                    "Expected '%s.<variant>', not '%s' because '%s' is a scoped enum.",
                    match_cls->name, lex->label, match_cls->name);

        NEED_NEXT_TOK(tk_dot)
        NEED_NEXT_TOK(tk_word)
    }

    lily_variant_class *variant = lily_find_variant(match_cls, lex->label);

    if (variant == NULL)
        lily_raise_syn(parser->raiser, "%s is not a member of enum %s.",
                lex->label, match_cls->name);

    if (lily_emit_try_match_switch(parser->emit, (lily_class *)variant) == 0)
        lily_raise_syn(parser->raiser, "Already have a case for variant %s.",
                lex->label);

    return (lily_class *)variant;
}

static void parse_match_case(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_class *match_cls = parser->emit->block->match_type->cls;
    lily_class *cls;

    lily_next_token(lex);

    if (match_cls->item_kind & ITEM_IS_ENUM)
        cls = parse_match_variant(parser);
    else
        cls = parse_match_class(parser);

    hide_block_vars(parser);

    /* Empty variants don't have anything to unpack. */
    if (cls->item_kind == ITEM_VARIANT_EMPTY)
        return;

    NEED_NEXT_TOK(tk_left_parenth)

    if (cls->item_kind == ITEM_VARIANT_FILLED) {
        lily_variant_class *variant = (lily_variant_class *)cls;
        lily_type *t = lily_emit_type_for_variant(parser->emit, variant);

        /* Skip [0] since it's the return and not one of the arguments. */
        lily_type **var_types = t->subtypes + 1;
        uint16_t stop = t->subtype_count - 1;
        uint16_t i;

        for (i = 0;i < stop;i++) {
            lily_var *var = parse_match_target(parser, var_types[i]);

            if (var)
                lily_emit_write_variant_case(parser->emit, var, i);

            if (i == stop - 1)
                break;

            NEED_CURRENT_TOK(tk_comma)
        }
    }
    else if (cls->item_kind & ITEM_IS_CLASS) {
        lily_var *var = parse_match_target(parser, cls->self_type);

        if (var)
            lily_emit_write_class_case(parser->emit, var);
    }

    NEED_CURRENT_TOK(tk_right_parenth)
}

static void keyword_match(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_match_block(parser->emit);
    expression(parser);
    lily_eval_match(parser->emit, parser->expr);
    NEED_COLON_AND_BRACE;
    NEED_NEXT_TOK(tk_word)

    if (keyword_by_name(lex->label) != KEY_CASE)
        lily_raise_syn(parser->raiser, "'match' must start with a case.");

    lily_block *block = parser->emit->block;

    while (1) {
        if (lex->token == tk_word) {
            int key = keyword_by_name(lex->label);
            if (key == KEY_CASE) {
                if (block->flags & BLOCK_FINAL_BRANCH)
                    lily_raise_syn(parser->raiser,
                            "'case' in exhaustive match.");

                parse_match_case(parser);
                lily_next_token(lex);
                NEED_COLON_AND_NEXT;
            }
            else if (key == KEY_ELSE) {
                if (lily_emit_try_match_finalize(parser->emit) == 0)
                    lily_raise_syn(parser->raiser,
                            "'else' in exhaustive match.");

                lily_next_token(lex);
                NEED_COLON_AND_NEXT;
            }
            else if (key != -1) {
                lily_next_token(lex);
                handlers[key](parser);
            }
            else {
                expression(parser);
                lily_eval_expr(parser->emit, parser->expr);
            }
        }
        else if (lex->token != tk_right_curly)
            statement(parser, 0);
        else
            break;
    }

    if ((block->flags & BLOCK_FINAL_BRANCH) == 0)
        error_incomplete_match(parser);

    hide_block_vars(parser);
    lily_next_token(lex);
    lily_emit_leave_block(parser->emit);
}

static void keyword_case(lily_parse_state *parser)
{
    lily_raise_syn(parser->raiser, "'case' not allowed outside of 'match'.");
}

#define ALL_MODIFIERS (ANY_SCOPE | VAR_IS_STATIC)

static void verify_resolve_define_var(lily_parse_state *parser,
        lily_var *define_var, int modifiers)
{
    if ((define_var->flags & VAR_IS_FORWARD) == 0)
        error_var_redeclaration(parser, define_var);
    else if (modifiers & VAR_IS_FORWARD)
        lily_raise_syn(parser->raiser,
                "A forward declaration for %s already exists.",
                define_var->name);
    else if ((define_var->flags & ALL_MODIFIERS) !=
             (modifiers & ALL_MODIFIERS))
        error_forward_decl_modifiers(parser, define_var);

    /* This forward definition is no longer forward. */
    define_var->flags &= ~VAR_IS_FORWARD;
    parser->emit->block->forward_count--;
}

#undef ALL_MODIFIERS

static lily_var *parse_new_define(lily_parse_state *parser, lily_class *parent,
        int modifiers)
{
    lily_lex_state *lex = parser->lex;
    const char *name = lex->label;
    lily_var *define_var = find_active_var(parser, name);

    if (define_var == NULL && parent) {
        lily_named_sym *named_sym = lily_find_member(parent, name);

        if (named_sym == NULL)
            ;
        else if (named_sym->flags & VAR_IS_FORWARD)
            define_var = (lily_var *)named_sym;
        else if (sym_visible_from(parent, named_sym))
            error_member_redeclaration(parser, parent, named_sym);
    }

    if (define_var)
        verify_resolve_define_var(parser, define_var, modifiers);
    else if (parent)
        define_var = new_method_var(parser, parent, name, modifiers,
                lex->line_num);
    else
        define_var = new_define_var(parser, name, lex->line_num);

    /* Make flags consistent no matter which above case was selected. */
    define_var->flags |= modifiers | SYM_NOT_INITIALIZED;

    return define_var;
}

#define ALLOW_DEFINE (SCOPE_CLASS | SCOPE_DEFINE | SCOPE_ENUM | SCOPE_FILE)

static void parse_define(lily_parse_state *parser, int modifiers)
{
    lily_block *block = parser->emit->block;
    lily_lex_state *lex = parser->lex;
    lily_class *parent = NULL;
    lily_block_type block_type = block->block_type;
    uint16_t save_generic_start = lily_gp_save(parser->generics);

    if ((block->block_type & ALLOW_DEFINE) == 0)
        lily_raise_syn(parser->raiser, "Cannot define a function here.");

    if (block->block_type == block_class &&
        (modifiers & ANY_SCOPE) == 0)
        lily_raise_syn(parser->raiser,
                "Class method declaration must start with a scope.");

    if (block_type & (SCOPE_CLASS | SCOPE_ENUM))
        parent = parser->current_class;

    NEED_CURRENT_TOK(tk_word)

    lily_var *define_var = parse_new_define(parser, parent, modifiers);

    lily_emit_enter_define_block(parser->emit, define_var);
    collect_generics_for(parser, NULL);
    lily_tm_add(parser->tm, lily_unit_type);

    if (define_var->parent && (define_var->flags & VAR_IS_STATIC) == 0)
        /* Toplevel non-static class methods have 'self' as an implicit first
           argument. */
        lily_tm_add(parser->tm, define_var->parent->self_type);

    collect_call_args(parser, define_var, F_COLLECT_DEFINE);
    finish_define_init(parser, define_var);
    NEED_CURRENT_TOK(tk_left_curly)

    if ((modifiers & VAR_IS_FORWARD) == 0)
        parse_block_body(parser);
    else {
        NEED_NEXT_TOK(tk_three_dots)
        NEED_NEXT_TOK(tk_right_curly)
        lily_next_token(lex);
    }

    hide_block_vars(parser);
    lily_emit_leave_define_block(parser->emit, lex->line_num);
    lily_gp_restore(parser->generics, save_generic_start);
}

#undef ALLOW_DEFINE

static void keyword_define(lily_parse_state *parser)
{
    parse_define(parser, 0);
}

static void parse_modifier(lily_parse_state *parser, int key)
{
    lily_lex_state *lex = parser->lex;
    int modifiers = 0;

    if (key == KEY_FORWARD) {
        lily_block_type block_type = parser->emit->block->block_type;
        if ((block_type & (SCOPE_CLASS | SCOPE_FILE)) == 0)
            lily_raise_syn(parser->raiser,
                    "'forward' qualifier is only for toplevel functions and methods.");

        modifiers |= VAR_IS_FORWARD;
        NEED_CURRENT_TOK(tk_word)
        key = keyword_by_name(lex->label);
    }

    if (key == KEY_PUBLIC ||
        key == KEY_PROTECTED ||
        key == KEY_PRIVATE) {
        if (key == KEY_PUBLIC)
            modifiers |= SYM_SCOPE_PUBLIC;
        else if (key == KEY_PROTECTED)
            modifiers |= SYM_SCOPE_PROTECTED;
        else
            modifiers |= SYM_SCOPE_PRIVATE;

        if (parser->emit->block->block_type != block_class)
            lily_raise_syn(parser->raiser, "'%s' is not allowed here.",
                    scope_to_str(modifiers));

        if (modifiers & VAR_IS_FORWARD)
            lily_next_token(lex);

        NEED_CURRENT_TOK(tk_word)
        key = keyword_by_name(lex->label);
    }
    else if (modifiers & VAR_IS_FORWARD &&
             parser->emit->block->block_type == block_class) {
        lily_raise_syn(parser->raiser,
                "'forward' must be followed by a class scope here.");
    }

    if (key == KEY_STATIC) {
        modifiers |= VAR_IS_STATIC;
        lily_next_token(lex);
        NEED_CURRENT_TOK(tk_word)
        key = keyword_by_name(lex->label);

        if (key != KEY_DEFINE)
            lily_raise_syn(parser->raiser,
                    "'static' must be followed by 'define', not '%s'.",
                    lex->label);
    }

    if (key == KEY_VAR) {
        if (modifiers & VAR_IS_FORWARD)
            lily_raise_syn(parser->raiser, "Cannot use 'forward' with 'var'.");

        lily_next_token(lex);
        parse_var(parser, modifiers);
    }
    else if (key == KEY_DEFINE) {
        lily_next_token(lex);
        parse_define(parser, modifiers);
    }
    else {
        const char *what = "either 'var' or 'define'";

        if (modifiers & VAR_IS_FORWARD)
            what = "'define'";

        lily_raise_syn(parser->raiser, "Expected %s, but got '%s'.", what,
                lex->label);
    }
}

static void keyword_public(lily_parse_state *parser)
{
    parse_modifier(parser, KEY_PUBLIC);
}

static void keyword_static(lily_parse_state *parser)
{
    lily_raise_syn(parser->raiser,
            "'static' must follow a scope (public, protected, or private).");
}

static void keyword_forward(lily_parse_state *parser)
{
    parse_modifier(parser, KEY_FORWARD);
}

static void keyword_private(lily_parse_state *parser)
{
    parse_modifier(parser, KEY_PRIVATE);
}

static void keyword_protected(lily_parse_state *parser)
{
    parse_modifier(parser, KEY_PROTECTED);
}

static void maybe_fix_print(lily_parse_state *parser)
{
    lily_symtab *symtab = parser->symtab;
    lily_module_entry *builtin = symtab->builtin_module;
    lily_var *stdout_var = lily_find_var(builtin, "stdout");
    lily_vm_state *vm = parser->vm;

    if (stdout_var) {
        lily_var *print_var = lily_find_var(builtin, "print");
        if (print_var) {
            /* Swap out the default implementation of print for one that will
               check if stdin is closed first. */
            lily_value *print_value = vm->gs->readonly_table[print_var->reg_spot];
            lily_function_val *print_func = print_value->value.function;

            print_func->foreign_func = lily_stdout_print;
            print_func->cid_table = &stdout_var->reg_spot;
        }
    }
}

static void template_read_loop(lily_parse_state *parser, lily_lex_state *lex)
{
    lily_config *config = parser->config;
    int has_more = 0;

    while (1) {
        char *buffer = lily_read_template_content(lex, &has_more);

        if (has_more == 0)
            break;

        config->render_func(buffer, config->data);
    }
}

static void main_func_setup(lily_parse_state *parser)
{
    /* todo: Find a way to do some of this as-needed, instead of always. */
    lily_register_classes(parser->symtab, parser->vm);
    lily_prepare_main(parser->emit, parser->toplevel_func);

    parser->vm->gs->readonly_table = parser->symtab->literals->data;

    maybe_fix_print(parser);
    update_all_cid_tables(parser);

    parser->flags |= PARSER_IS_EXECUTING;
    lily_call_prepare(parser->vm, parser->toplevel_func);
    /* The above function pushes a Unit value to act as a sink for lily_call to
       put a value into. __main__ won't return a value so get rid of it. */
    lily_stack_drop_top(parser->vm);
}

static void main_func_teardown(lily_parse_state *parser)
{
    parser->vm->call_chain = parser->vm->call_chain->prev;
    parser->vm->call_depth--;
    parser->flags &= ~PARSER_IS_EXECUTING;
}

/* This is the entry point into parsing regardless of the starting mode. This
   should only be called by the content handling functions that do the proper
   initialization beforehand.
   This does not execute code. If it returns, it was successful (an error is
   raised otherwise). */
static void parser_loop(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);

    while (1) {
        if (lex->token == tk_word)
            statement(parser, 1);
        else if (lex->token == tk_right_curly)
            /* This brace is mismatched so the call to leave will raise. */
            lily_emit_leave_block(parser->emit);
        else if (lex->token == tk_end_tag || lex->token == tk_eof) {
            /* Block handling is recursive, so this can't be reached if there
               are unterminated blocks. */
            if ((parser->flags & PARSER_IS_RENDERING) == 0 &&
                lex->token == tk_end_tag)
                lily_raise_syn(parser->raiser, "Unexpected token '%s'.",
                        tokname(lex->token));

            if (parser->emit->block->forward_count)
                error_forward_decl_pending(parser);

            break;
        }
        else if (lex->token == tk_docblock) {
            process_docblock(parser);
        }
        else {
            expression(parser);
            lily_eval_expr(parser->emit, parser->expr);
        }
    }
}

static void update_main_name(lily_parse_state *parser,
        const char *filename)
{
    lily_module_entry *module = parser->main_module;

    if (module->path &&
        strcmp(module->path, filename) == 0)
        return;

    /* The first module isn't likely to have several names. Instead of creating
       a special stack to store all of them, throw it into a literal. It'll
       survive until the interpreter is done and won't leak. */
    lily_literal *lit = lily_get_string_literal(parser->symtab, filename);
    char *path = lily_as_string_raw((lily_value *)lit);

    lily_free(module->dirname);

    module->path = path;
    module->dirname = dir_from_path(path);
    module->cmp_len = strlen(path);
    module->root_dirname = module->dirname;
    /* The loadname isn't set because the first module isn't importable. */

    parser->emit->protos->data[0]->module_path = path;
}

static void error_add_header(lily_parse_state *parser)
{
    lily_raiser *raiser = parser->raiser;
    lily_msgbuf *msgbuf = parser->msgbuf;
    const char *name;

    switch (raiser->source) {
        case err_from_vm:
            name = raiser->error_class->name;
            break;
        case err_from_parse:
        case err_from_emit:
            name = "SyntaxError";
            break;
        default:
            name = "Error";
            break;
    }

    lily_mb_add(msgbuf, name);

    const char *message = lily_mb_raw(raiser->msgbuf);

    if (message[0] != '\0')
        lily_mb_add_fmt(msgbuf, ": %s\n", message);
    else
        lily_mb_add_char(msgbuf, '\n');
}

static void error_add_frontend_trace(lily_parse_state *parser)
{
    lily_msgbuf *msgbuf = parser->msgbuf;
    uint16_t line_num = parser->lex->line_num;

    if (parser->raiser->source == err_from_emit)
        line_num = parser->raiser->error_ast->line_num;

    lily_mb_add_fmt(msgbuf, "    from %s:%d:\n",
            parser->symtab->active_module->path, line_num);
}

static void error_add_vm_trace(lily_parse_state *parser)
{
    lily_msgbuf *msgbuf = parser->msgbuf;
    lily_call_frame *frame = parser->vm->call_chain;

    lily_mb_add(msgbuf, "Traceback:\n");

    while (frame->prev) {
        lily_proto *proto = frame->function->proto;

        if (frame->function->code == NULL)
            lily_mb_add_fmt(msgbuf, "    from %s: in %s\n", proto->module_path,
                    proto->name);
        else
            lily_mb_add_fmt(msgbuf, "    from %s:%d: in %s\n",
                    proto->module_path, frame->code[-1], proto->name);

        frame = frame->prev;
    }
}

/* This is called when the interpreter encounters an error. This builds an
   error message that is stored within parser's msgbuf. A runner can later fetch
   this error with `lily_error_message`. */
static void build_error(lily_parse_state *parser)
{
    lily_raiser *raiser = parser->raiser;

    lily_mb_flush(parser->msgbuf);

    if (raiser->source == err_from_none)
        return;

    error_add_header(parser);

    switch (raiser->source) {
        case err_from_emit:
        case err_from_parse:
            error_add_frontend_trace(parser);
            break;
        case err_from_vm:
            error_add_vm_trace(parser);
            break;
        default:
            break;
    }
}

static FILE *load_file_to_parse(lily_parse_state *parser, const char *path)
{
    FILE *load_file = fopen(path, "r");
    if (load_file == NULL) {
        /* Assume that the message is of a reasonable sort of size. */
        char buffer[128];
#ifdef _WIN32
        strerror_s(buffer, sizeof(buffer), errno);
#else
        strerror_r(errno, buffer, sizeof(buffer));
#endif
        lily_raise_raw(parser->raiser, "Failed to open %s: (%s).", path,
                buffer);
    }

    return load_file;
}

static int open_first_content(lily_state *s, const char *filename,
        char *content)
{
    lily_parse_state *parser = s->gs->parser;

    if (parser->flags & PARSER_HAS_CONTENT)
        return 0;

    /* Loading initial content should only be done outside of execution, so
       using the parser's base jump is okay. */
    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        lily_lex_entry_type load_type;
        void *load_content;

        if (content == NULL) {
            char *suffix = strrchr(filename, '.');
            if (suffix == NULL || strcmp(suffix, ".lily") != 0)
                lily_raise_raw(parser->raiser,
                        "File name must end with '.lily'.");

            load_type = et_file;
            load_content = load_file_to_parse(parser, filename);
        }
        else {
            /* Strings sent to be parsed/rendered are expected to be on a
               caller's stack somewhere. There shouldn't be a need to copy this
               string. */
            load_type = et_shallow_string;
            load_content = content;
        }

        /* Rewind before loading content so it starts with a fresh slate. */
        if (parser->rs->pending)
            rewind_interpreter(parser);

        initialize_rewind(parser);
        lily_lexer_load(parser->lex, load_type, load_content);
        /* The first module is now rooted based on the name given. */
        update_main_name(parser, filename);

        parser->flags = PARSER_HAS_CONTENT;
        return 1;
    }

    return 0;
}

int lily_load_file(lily_state *s, const char *filename)
{
    return open_first_content(s, filename, NULL);
}

int lily_load_string(lily_state *s, const char *context,
        const char *str)
{
    return open_first_content(s, context, (char *)str);
}

int lily_parse_content(lily_state *s)
{
    lily_parse_state *parser = s->gs->parser;

    if ((parser->flags & PARSER_HAS_CONTENT) == 0)
        return 0;

    parser->flags = 0;

    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        parser_loop(parser);

        main_func_setup(parser);
        lily_call(parser->vm, 0);
        main_func_teardown(parser);

        lily_pop_lex_entry(parser->lex);
        lily_mb_flush(parser->msgbuf);

        return 1;
    }
    else
        parser->rs->pending = 1;

    return 0;
}

int lily_validate_content(lily_state *s)
{
    lily_parse_state *parser = s->gs->parser;

    if ((parser->flags & PARSER_HAS_CONTENT) == 0)
        return 0;

    parser->flags = 0;

    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        parser_loop(parser);

        lily_pop_lex_entry(parser->lex);
        lily_mb_flush(parser->msgbuf);
        /* Clear __main__ so the code doesn't run on the next pass. This allows
           running introspection after validation. */
        lily_clear_main(parser->emit);

        return 1;
    }
    else
        parser->rs->pending = 1;

    return 0;
}

int lily_render_content(lily_state *s)
{
    lily_parse_state *parser = s->gs->parser;

    if ((parser->flags & PARSER_HAS_CONTENT) == 0)
        return 0;

    parser->flags = PARSER_IS_RENDERING;

    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        lily_lex_state *lex = parser->lex;
        /* Templates have to start with `<?lily` to prevent "rendering" files
           that are intended to be run in code mode. It has the nice bonus that
           execution always starts in code mode. */
        if (lily_read_template_header(lex) == 0) {
            lily_raise_syn(lex->raiser,
                    "Files in template mode must start with '<?lily'.");
        }

        while (1) {
            /* Parse and execute the code block. */
            parser_loop(parser);

            main_func_setup(parser);
            lily_call(parser->vm, 0);
            main_func_teardown(parser);

            /* Read through what needs to be rendered. */
            if (lex->token == tk_end_tag)
                template_read_loop(parser, lex);

            if (lex->token == tk_eof)
                break;
        }

        lily_pop_lex_entry(parser->lex);
        lily_mb_flush(parser->msgbuf);
        return 1;
    }
    else
        parser->rs->pending = 1;

    return 0;
}

int lily_parse_expr(lily_state *s, const char **text)
{
    if (text)
        *text = NULL;

    lily_parse_state *parser = s->gs->parser;

    if ((parser->flags & PARSER_HAS_CONTENT) == 0)
        return 0;

    parser->flags = 0;

    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        lily_lex_state *lex = parser->lex;

        lily_next_token(lex);
        expression(parser);
        lily_eval_expr(parser->emit, parser->expr);
        NEED_CURRENT_TOK(tk_eof);

        lily_sym *sym = parser->expr->root->result;

        main_func_setup(parser);
        lily_call(parser->vm, 0);
        main_func_teardown(parser);

        lily_pop_lex_entry(parser->lex);

        if (sym && text) {
            /* This grabs the symbol from __main__. */
            lily_value *reg = s->call_chain->next->start[sym->reg_spot];
            lily_msgbuf *msgbuf = lily_mb_flush(parser->msgbuf);

            lily_mb_add_fmt(msgbuf, "(^T): ", sym->type);

            /* Add value doesn't quote String values, because most callers do
               not want that. This one does, so bypass that. */
            if (reg->flags & V_STRING_FLAG)
                lily_mb_add_fmt(msgbuf, "\"%s\"", reg->value.string->string);
            else
                lily_mb_add_value(msgbuf, s, reg);

            *text = lily_mb_raw(msgbuf);
        }

        return 1;
    }
    else {
        parser->rs->pending = 1;
    }

    return 0;
}

lily_function_val *lily_find_function(lily_vm_state *vm, const char *name)
{
    /* todo: Handle scope access, class methods, and so forth. Ideally, it can
       be done without loading any fake files (like dynaloading does), as this
       may be the base of a preloader. */
    lily_var *v = find_active_var(vm->gs->parser, name);
    lily_function_val *result;

    if (v)
        result = vm->gs->readonly_table[v->reg_spot]->value.function;
    else
        result = NULL;

    return result;
}

/* Return a string describing the last error encountered by the interpreter.
   This string is guaranteed to be valid until the next execution of the
   interpreter. */
const char *lily_error_message(lily_state *s)
{
    build_error(s->gs->parser);
    return lily_mb_raw(s->gs->parser->msgbuf);
}

/* Return the message of the last error encountered by the interpreter. */
const char *lily_error_message_no_trace(lily_state *s)
{
    return lily_mb_raw(s->raiser->msgbuf);
}

lily_config *lily_config_get(lily_state *s)
{
    return s->gs->parser->config;
}

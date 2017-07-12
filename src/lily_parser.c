#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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

#include "lily_api_value.h"

#define NEED_NEXT_TOK(expected) \
lily_lexer(lex); \
if (lex->token != expected) \
    lily_raise_syn(parser->raiser, "Expected '%s', not '%s'.", \
               tokname(expected), tokname(lex->token));

#define NEED_CURRENT_TOK(expected) \
if (lex->token != expected) \
    lily_raise_syn(parser->raiser, "Expected '%s', not '%s'.", \
               tokname(expected), tokname(lex->token));

extern lily_class *lily_self_class;
extern lily_type *lily_unit_type;

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
static void statement(lily_parse_state *, int);
static lily_type *type_by_name(lily_parse_state *, const char *);
static lily_module_entry *new_module(lily_parse_state *);
static void create_main_func(lily_parse_state *);
void lily_register_package(lily_state *, const char *, const char **, void *);
void lily_default_import_func(lily_state *, const char *, const char *,
        const char *);
void lily_stdout_print(lily_vm_state *);

typedef struct lily_rewind_state_
{
    lily_class *main_class_start;
    lily_var *main_var_start;
    lily_module_link *main_last_module_link;
    lily_module_entry *main_last_module;
    uint32_t line_num;
    uint32_t pending;
} lily_rewind_state;

extern const char *lily_builtin_table[];
void *lily_builtin_loader(lily_state *s, int);

extern const char *lily_sys_table[];
void *lily_sys_loader(lily_state *s, int);

extern const char *lily_random_table[];
void *lily_random_loader(lily_state *s, int);

extern const char *lily_time_table[];
void *lily_time_loader(lily_state *s, int);

void lily_init_pkg_builtin(lily_symtab *);

void lily_init_config(lily_config *conf)
{
    conf->argc = 0;
    conf->argv = NULL;

    /* Starting gc options are completely arbitrary. */
    conf->gc_start = 100;
    conf->gc_multiplier = 4;

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

    parser->first_pass = 1;
    parser->class_self_type = NULL;
    parser->raiser = raiser;
    parser->expr = lily_new_expr_state();
    parser->generics = lily_new_generic_pool();
    parser->symtab = lily_new_symtab(parser->generics);
    parser->vm = lily_new_vm_state(raiser);
    parser->rs = lily_malloc(sizeof(*parser->rs));
    parser->rs->pending = 0;

    parser->vm->parser = parser;
    parser->vm->gc_multiplier = config->gc_multiplier;
    parser->vm->gc_threshold = config->gc_start;

    lily_register_package(parser->vm, "", lily_builtin_table, lily_builtin_loader);
    lily_set_builtin(parser->symtab, parser->module_top);
    lily_init_pkg_builtin(parser->symtab);

    parser->emit = lily_new_emit_state(parser->symtab, raiser);
    parser->lex = lily_new_lex_state(raiser);
    parser->msgbuf = lily_new_msgbuf(64);
    parser->data_stack = lily_new_buffer_u16(4);

    /* Here's the awful part where parser digs in and links everything that different
       sections need. */
    parser->tm = parser->emit->tm;

    parser->vm->vm_buffer = parser->raiser->msgbuf;

    parser->expr->lex_linenum = &parser->lex->line_num;

    parser->emit->lex_linenum = &parser->lex->line_num;
    parser->emit->symtab = parser->symtab;
    parser->emit->parser = parser;

    parser->lex->symtab = parser->symtab;

    parser->expr_strings = parser->emit->expr_strings;

    lily_module_entry *main_package = new_module(parser);

    parser->main_module = main_package;
    parser->symtab->active_module = parser->main_module;

    /* This creates the var representing __main__ and registers it in areas that
       need it. */
    create_main_func(parser);

    lily_register_package(parser->vm, "sys", lily_sys_table, lily_sys_loader);
    lily_register_package(parser->vm, "random", lily_random_table, lily_random_loader);
    lily_register_package(parser->vm, "time", lily_time_table, lily_time_loader);

    parser->executing = 0;

    return parser->vm;
}

static void free_links_until(lily_module_link *link_iter,
        lily_module_link *stop)
{
    while (link_iter != stop) {
        lily_module_link *link_next = link_iter->next_module;
        lily_free(link_iter->as_name);
        lily_free(link_iter);
        link_iter = link_next;
    }
}

#define free_links(iter) free_links_until(iter, NULL)

void lily_free_state(lily_state *vm)
{
    lily_parse_state *parser = vm->parser;

    /* The code for the toplevel function (really __main__) is a pointer to
       emitter's code that gets refreshed before every vm entry. Set it to NULL
       so that these teardown functions don't double free the code. */
    parser->toplevel_func->code = NULL;

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

        module_next = module_iter->root_next;

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

    lily_free_symtab(parser->symtab);
    lily_free_generic_pool(parser->generics);
    lily_free_msgbuf(parser->msgbuf);
    lily_free(parser->rs);

    lily_free(parser);
}

static void rewind_parser(lily_parse_state *parser, lily_rewind_state *rs)
{
    lily_u16_set_pos(parser->data_stack, 0);

    /* Rewind generics */
    lily_generic_pool *gp = parser->generics;
    gp->scope_start = 0;
    gp->scope_end = 0;

    /* Rewind expression state */
    lily_expr_state *es = parser->expr;
    es->root = NULL;
    es->active = NULL;

    if (es->checkpoint_pos) {
        es->first_tree = es->checkpoints[0]->first_tree;
        es->checkpoint_pos = 0;
    }

    es->next_available = es->first_tree;

    lily_ast_save_entry *save_iter = es->save_chain;
    while (1) {
        save_iter->entered_tree = NULL;
        if (save_iter->prev == NULL)
            break;
        save_iter = save_iter->prev;
    }
    es->save_chain = save_iter;
    es->save_depth = 0;
    es->pile_start = 0;
    es->pile_current = 0;

    /* Rewind emit state */
    lily_emit_state *emit = parser->emit;
    lily_u16_set_pos(emit->patches, 0);
    lily_u16_set_pos(emit->code, 0);
    if (emit->closure_aux_code)
        lily_u16_set_pos(emit->closure_aux_code, 0);

    emit->closed_pos = 0;
    emit->match_case_pos = 0;

    lily_block *block_stop = emit->block->next;
    lily_block *block_iter = emit->main_block->next;
    while (block_iter != block_stop) {
        if (block_iter->block_type >= block_define) {
            emit->storages->scope_end = block_iter->storage_start;
            break;
        }
        block_iter = block_iter->next;
    }

    emit->block = emit->main_block;
    emit->function_block = emit->main_block;
    emit->function_depth = 1;

    /* Rewind ts */
    lily_type_system *ts = parser->emit->ts;
    /* ts blasts types on entry (instead of cleaning up on exit), so there's no
       need to alter existing ts state. */
    ts->num_used = 0;
    ts->pos = 0;

    /* Rewind lex state */
    lily_rewind_lex_state(parser->lex);
    parser->lex->line_num = rs->line_num;

    /* Rewind raiser */
    lily_raiser *raiser = parser->raiser;
    lily_mb_flush(raiser->msgbuf);
    lily_mb_flush(raiser->aux_msgbuf);
    raiser->line_adjust = 0;
    raiser->exception_cls = NULL;

    /* Rewind the parts of the vm that can be rewound. */
    lily_vm_state *vm = parser->vm;

    lily_vm_catch_entry *catch_iter = vm->catch_chain;
    while (catch_iter->prev)
        catch_iter = catch_iter->prev;

    vm->catch_chain = catch_iter;
    vm->exception_value = NULL;

    lily_call_frame *call_iter = vm->call_chain;
    while (call_iter->prev)
        call_iter = call_iter->prev;

    vm->call_chain = call_iter;
    vm->call_depth = 0;

    /* Symtab will choose to hide new classes (if executing) or destroy them (if
       not executing). New vars are destroyed, and the main module is made
       active again. */
    lily_rewind_symtab(parser->symtab, parser->main_module,
            rs->main_class_start, rs->main_var_start, parser->executing);
}

static void handle_rewind(lily_parse_state *parser)
{
    lily_rewind_state *rs = parser->rs;

    if (parser->rs->pending) {
        rewind_parser(parser, rs);
        parser->rs->pending = 0;
    }

    lily_module_entry *main_module = parser->main_module;
    rs->main_class_start = main_module->class_chain;
    rs->main_var_start = main_module->var_chain;

    rs->main_last_module_link = main_module->module_chain;
    rs->main_last_module = parser->module_top;
    rs->line_num = parser->lex->line_num;
}

/***
 *      ___                            _
 *     |_ _|_ __ ___  _ __   ___  _ __| |_
 *      | || '_ ` _ \| '_ \ / _ \| '__| __|
 *      | || | | | | | |_) | (_) | |  | |_
 *     |___|_| |_| |_| .__/ \___/|_|   \__|
 *                   |_|
 */

/** Within Lily, code is broken down into modules, with each module representing
    a single file. The first time a module is loaded, code inside that is not
    within a function or class is executed (a function called __import__ is
    created to hold and execute that code). The items of a loaded module are
    then made available using the module's name as a namespace.

    When determining where to load a module from, Lily first tries a relative
    to the current module. If that fails, it will try a package directory. The
    package directory paths are written to allow taking a directory of code from
    someone and dropping it in. You can then import it as you would any other
    kind of module (instead of using a verbose path).

    Lily is designed to be embedded, which is why `import` does not use system
    library paths or the environment to determine where to load from. Lily does,
    however, allow embedders to register packages with Lily. A module that has
    been registered with Lily is globally available, and has top priority in
    loading.

    Lily itself will registers builtin module, and an optional sys module. The
    builtin module is the only module that is implicitly loaded: All others must
    be explicitly loaded. This design prevents a script from assuming a certain
    module exists, and breaking when run under a different embedder.

    There are two modes that Lily executes within: Standalone, and template. In
    standalone mode, all text is processed as code. Template mode, on the other
    hand, sees Lily code as being between `<?lily ... ?>` and the rest as text.
    When a module is imported, it is always imported in standalone mode,
    regardless of what the importer's state was. This design forces a separation
    between files meant for template layout, and those meant to hold shared
    code. **/

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

static char *loadname_from_path(const char *path)
{
    const char *slash = strrchr(path, LILY_PATH_CHAR);

    if (slash == NULL)
        slash = path;
    else
        slash += 1;

    char *dot = strrchr(slash, '.');
    int load_len;

    if (dot)
        load_len = dot - slash;
    else
        load_len = strlen(path);

    char *out = lily_malloc((load_len + 1) * sizeof(*out));

    strncpy(out, slash, load_len);
    out[load_len] = '\0';

    return out;
}

static lily_module_entry *new_module(lily_parse_state *parser)
{
    lily_module_entry *module = lily_malloc(sizeof(*module));

    module->loadname = NULL;
    module->dirname = NULL;
    module->path = NULL;
    module->cmp_len = 0;
    module->dynaload_table = NULL;
    module->cid_table = NULL;
    module->root_next = NULL;
    module->module_chain = NULL;
    module->class_chain = NULL;
    module->var_chain = NULL;
    module->handle = NULL;
    module->loader = NULL;
    module->item_kind = ITEM_TYPE_MODULE;
    module->flags = 0;

    if (parser->module_start) {
        parser->module_top->root_next = module;
        parser->module_top = module;
    }
    else {
        parser->module_start = module;
        parser->module_top = module;
    }

    parser->last_import = module;

    return module;
}

static void add_data_to_module(lily_module_entry *module, void *handle,
        const char **table, lily_loader loader)
{
    module->handle = handle;
    module->dynaload_table = table;
    module->loader = loader;

    unsigned char cid_count = module->dynaload_table[0][0];

    if (cid_count) {
        module->cid_table = lily_malloc(cid_count * sizeof(*module->cid_table));
        memset(module->cid_table, 0, cid_count * sizeof(*module->cid_table));
    }
}

static void add_path_to_module(lily_module_entry *module, const char *path)
{
    module->loadname = loadname_from_path(path);
    module->dirname = dir_from_path(path);
    module->path = lily_malloc((strlen(path) + 1) * sizeof(*module->path));
    module->cmp_len = strlen(path);
    strcpy(module->path, path);
}

static lily_module_entry *find_existing_module(lily_parse_state *parser,
        const char *path)
{
    size_t len = strlen(path);
    lily_module_entry *module_iter = parser->module_start;
    while (module_iter) {
        if (module_iter->cmp_len == len &&
            strcmp(module_iter->path, path) == 0) {
            break;
        }

        module_iter = module_iter->root_next;
    }

    return module_iter;
}

static int import_check(lily_parse_state *parser, const char *path, int *out)
{
    if (parser->last_import) {
        *out = 0;
        return 1;
    }

    lily_module_entry *m = find_existing_module(parser, path);

    if (m) {
        parser->last_import = m;
        *out = 1;
        return 1;
    }

    /* 'import' isn't allowed inside of an expression, so expr_strings should
       not be holding anything important. Use it to store paths that have been
       tried so the interpreter can deliver a better error message. */
    lily_buffer_u16 *b = parser->data_stack;
    uint16_t pos = lily_u16_get(b, lily_u16_pos(b) - 1);
    lily_sp_insert(parser->expr_strings, path, &pos);
    lily_u16_write_1(b, pos);

    return 0;
}

int lily_load_file(lily_state *s, const char *path)
{
    int out;
    lily_parse_state *parser = s->parser;

    if (import_check(parser, path, &out))
        return out;

    FILE *source = fopen(path, "r");
    if (source == NULL)
        return 0;

    lily_lexer_load(parser->lex, et_file, source);

    lily_module_entry *module = new_module(parser);

    add_path_to_module(module, path);
    module->flags |= MODULE_NOT_EXECUTED;
    return 1;
}

int lily_load_string(lily_state *s, const char *path, const char *source)
{
    int out;
    lily_parse_state *parser = s->parser;

    if (import_check(parser, path, &out))
        return out;

    lily_lexer_load(parser->lex, et_shallow_string, (char *)source);

    lily_module_entry *module = new_module(parser);

    add_path_to_module(module, path);
    module->flags |= MODULE_NOT_EXECUTED;
    return 1;
}

int lily_load_library(lily_state *s, const char *path)
{
    int out;
    lily_parse_state *parser = s->parser;

    if (import_check(parser, path, &out))
        return out;

    void *handle = lily_library_load(path);
    if (handle == NULL)
        return 0;

    lily_msgbuf *msgbuf = parser->msgbuf;
    lily_mb_flush(msgbuf);

    char *loadname = loadname_from_path(path);

    char *path_copy = lily_malloc((strlen(path) + 1) * sizeof(*path_copy));
    strcpy(path_copy, path);

    const char **table = (const char **)lily_library_get(handle,
            lily_mb_sprintf(msgbuf, "lily_%s_table", loadname));

    void *loader = lily_library_get(handle,
            lily_mb_sprintf(msgbuf, "lily_%s_loader", loadname));

    if (table == NULL || loader == NULL) {
        lily_free(loadname);
        lily_free(path_copy);
        lily_library_free(handle);
        return 0;
    }

    lily_module_entry *module = new_module(parser);

    module->loadname = loadname;
    module->dirname = dir_from_path(path);
    module->path = path_copy;
    module->cmp_len = strlen(path);
    add_data_to_module(module, handle, table, (lily_loader)loader);
    return 1;
}

int lily_load_library_data(lily_state *s, const char *path, const char **table,
        void *loader)
{
    int out;
    lily_parse_state *parser = s->parser;

    if (import_check(parser, path, &out))
        return out;

    lily_module_entry *module = new_module(parser);

    add_path_to_module(module, path);
    add_data_to_module(module, NULL, table, (lily_loader)loader);
    return 1;
}

void lily_register_package(lily_state *s, const char *name, const char **table,
        void *loader)
{
    lily_parse_state *parser = s->parser;
    lily_module_entry *module = new_module(parser);

    module->loadname = lily_malloc(
            (strlen(name) + 1) * sizeof(*module->loadname));
    strcpy(module->loadname, name);
    add_data_to_module(module, NULL, table, (lily_loader)loader);
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
    new_link->next_module = target->module_chain;
    new_link->as_name = link_name;

    target->module_chain = new_link;
}

#define PACKAGE_DIR LILY_PATH_SLASH "packages" LILY_PATH_SLASH

#define FIRST_PATH "%s" LILY_PATH_SLASH "%s.lily"
#define SECOND_PATH "%s" LILY_PATH_SLASH "%s." LILY_LIB_SUFFIX
#define THIRD_PATH "%s" PACKAGE_DIR "%s" LILY_PATH_SLASH "%s.lily"
#define FOURTH_PATH "%s" PACKAGE_DIR "%s" LILY_PATH_SLASH "%s." LILY_LIB_SUFFIX

void lily_default_import_func(lily_state *s, const char *root,
        const char *source, const char *name)
{
    lily_msgbuf *msgbuf = lily_get_clean_msgbuf(s);
    const char *path;

    path = lily_mb_sprintf(msgbuf, FIRST_PATH, source, name);
    if (lily_load_file(s, path))
        return;

    path = lily_mb_sprintf(msgbuf, SECOND_PATH, source, name);
    if (lily_load_library(s, path))
        return;

    path = lily_mb_sprintf(msgbuf, THIRD_PATH, root, name, name);
    if (lily_load_file(s, path))
        return;

    path = lily_mb_sprintf(msgbuf, FOURTH_PATH, root, name, name);
    if (lily_load_library(s, path))
        return;
}

#undef PACKAGE_DIR
#undef FIRST_PATH
#undef SECOND_PATH
#undef THIRD_PATH
#undef FOURTH_PATH

static lily_module_entry *load_module(lily_parse_state *parser,
        const char *name)
{
    char *current = parser->symtab->active_module->dirname;
    char *root = parser->main_module->dirname;

    /* Using . provides context and prevents Linux from searching system
       library paths. */

    if (root[0] == '\0')
        root = ".";
    if (current[0] == '\0')
        current = ".";

    /* 'import' can't execute during an expression, so the data stack and the
       string pool are used to store paths that have been tried. */
    lily_u16_write_1(parser->data_stack, 0);
    parser->last_import = NULL;

    parser->config->import_func(parser->vm, root, current, name);

    if (parser->last_import == NULL) {
        lily_msgbuf *msgbuf = parser->msgbuf;
        lily_mb_flush(parser->msgbuf);
        lily_mb_add_fmt(msgbuf, "Cannot import '%s':\n", name);
        lily_mb_add_fmt(msgbuf, "    no preloaded package '%s'", name);

        lily_buffer_u16 *b = parser->data_stack;
        int i;

        for (i = 0;i < lily_u16_pos(b) - 1;i++) {
            uint16_t check_pos = lily_u16_get(b, i);
            lily_mb_add_fmt(msgbuf, "\n    no file '%s'",
                    lily_sp_get(parser->expr_strings, check_pos));
        }

        lily_raise_syn(parser->raiser, lily_mb_get(msgbuf));
    }
    else
        /* Nothing needs to be done for the string pool, because the pool
           itself doesn't hold a position. */
        lily_u16_set_pos(parser->data_stack, 0);

    return parser->last_import;
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

static void make_new_function(lily_parse_state *parser, const char *class_name,
        lily_var *var, lily_foreign_func foreign_func)
{
    lily_function_val *f = lily_malloc(sizeof(*f));

    /* This won't get a ref bump from being moved/assigned since all functions
       are marked as literals. Start at 1 ref, not 0. */
    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = var->name;
    f->foreign_func = foreign_func;
    f->code = NULL;
    /* Closures can have zero upvalues, so use -1 to mean no upvalues at all. */
    f->num_upvalues = (uint16_t) -1;
    f->upvalues = NULL;
    f->gc_entry = NULL;
    f->locals = NULL;
    f->line_num = var->line_num;
    f->module = parser->symtab->active_module;

    if (var->type)
        f->reg_count = var->type->subtype_count;
    else
        f->reg_count = -1;

    lily_value *v = lily_malloc(sizeof(*v));
    v->flags = LILY_FUNCTION_ID;
    v->value.function = f;

    lily_vs_push(parser->symtab->literals, v);
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

static lily_var *make_new_var(lily_type *type, const char *name,
        uint16_t line_num)
{
    lily_var *var = lily_malloc(sizeof(*var));

    var->name = lily_malloc((strlen(name) + 1) * sizeof(*var->name));
    var->item_kind = ITEM_TYPE_VAR;
    var->flags = 0;
    strcpy(var->name, name);
    var->line_num = line_num;
    var->shorthash = shorthash_for_name(name);
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
    lily_var *var = make_new_var(type, name, line_num);

    /* This is always a local var, so it gets an id from the current block. */
    var->function_depth = parser->emit->function_depth;
    var->reg_spot = parser->emit->function_block->next_reg_spot;
    parser->emit->function_block->next_reg_spot++;
    var->next = parser->symtab->active_module->var_chain;
    parser->symtab->active_module->var_chain = var;

    return var;
}

static lily_var *new_scoped_var(lily_parse_state *parser, lily_type *type,
        const char *name, uint16_t line_num)
{
    lily_var *var = make_new_var(type, name, line_num);

    var->next = parser->symtab->active_module->var_chain;
    parser->symtab->active_module->var_chain = var;
    var->function_depth = parser->emit->function_depth;

    /* Depth is 1 if in __main__ or only __import__ functions. */
    if (var->function_depth == 1) {
        /* This effectively reserves the current slot for this global in vm's
           toplevel area. */
        lily_push_unit(parser->vm);
        var->reg_spot = parser->symtab->next_global_id;
        parser->symtab->next_global_id++;
        var->flags |= VAR_IS_GLOBAL;
    }
    else {
        var->reg_spot = parser->emit->function_block->next_reg_spot;
        parser->emit->function_block->next_reg_spot++;
    }

    return var;
}

/* This is used when dynaloading a var. To make sure the dynaloaded var is
   reachable anywhere later on, it's made into a global. */
static lily_var *new_global_var(lily_parse_state *parser, lily_type *type,
        const char *name)
{
    /* line_num is 0 because the source is always a dynaload. */
    lily_var *var = make_new_var(type, name, 0);

    var->next = parser->symtab->active_module->var_chain;
    parser->symtab->active_module->var_chain = var;
    var->function_depth = 1;
    var->flags |= VAR_IS_GLOBAL;
    var->reg_spot = parser->symtab->next_global_id;
    parser->symtab->next_global_id++;

    return var;
}

static lily_var *new_native_define_var(lily_parse_state *parser,
        lily_class *parent, const char *name, uint16_t line_num)
{
    lily_var *var = make_new_var(NULL, name, line_num);

    var->reg_spot = lily_vs_pos(parser->symtab->literals);
    var->function_depth = 1;
    var->flags |= VAR_IS_READONLY;
    var->next = parser->symtab->active_module->var_chain;
    parser->symtab->active_module->var_chain = var;

    char *class_name;
    if (parent)
        class_name = parent->name;
    else
        class_name = NULL;

    make_new_function(parser, class_name, var, NULL);

    return var;
}

static lily_var *new_foreign_define_var(lily_parse_state *parser,
        lily_class *parent, lily_type *type, const char *name, int dyna_index)
{
    lily_module_entry *m = parser->symtab->active_module;
    lily_var *var = make_new_var(type, name, 0);

    var->reg_spot = lily_vs_pos(parser->symtab->literals);
    var->function_depth = 1;
    var->flags |= VAR_IS_READONLY | VAR_IS_FOREIGN_FUNC;

    if (parent) {
        var->next = (lily_var *)parent->members;
        parent->members = (lily_named_sym *)var;
        var->parent = parent;
    }
    else {
        var->next = m->var_chain;
        m->var_chain = var;
    }

    char *class_name;
    if (parent)
        class_name = parent->name;
    else
        class_name = NULL;

    lily_foreign_func func = (lily_foreign_func)m->loader(parser->vm,
            dyna_index);

    make_new_function(parser, class_name, var, func);

    return var;
}

static void create_main_func(lily_parse_state *parser)
{
    lily_type_maker *tm = parser->emit->tm;

    lily_tm_add(tm, lily_unit_type);
    lily_type *main_type = lily_tm_make(tm, 0, parser->symtab->function_class,
            1);

    lily_var *main_var = new_native_define_var(parser, NULL, "__main__", 0);
    lily_value *v = lily_vs_nth(parser->symtab->literals, 0);
    lily_function_val *f = v->value.function;

    main_var->type = main_type;

    /* The vm carries a toplevel frame to hold globals, so that globals survive
       when __main__ is done. The toplevel frame needs a function value to hold
       module cid tables when dynaload executes. */
    parser->vm->call_chain->function = f;
    parser->toplevel_func = f;
    parser->default_call_type = main_type;
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
static lily_prop_entry *get_named_property(lily_parse_state *, int);
static void expression_raw(lily_parse_state *);

/** Type collection can be roughly dividied into two subparts. One half deals
    with general collection of types that either do or don't have a name. The
    other half deals with optional arguments (optargs) and optional argument
    value collection.
    There's a small bit that deals with making sure that the self_type of a
    class is properly set. For enums, self_type is used for solving variants, so
    it's important that self_type be right. **/

/* Given a var, collect the optional argument that goes with it. This will push
   information to parser's data_stack to link the value to the var. The token
   is expected to start on the '=', and will be set at the token after the
   optional value. */
static void collect_optarg_for(lily_parse_state *parser, lily_var *var)
{
    /* This saves either the expression before optargs runs, or the previous
       optarg expression. The last expression will be saved by whatever function
       ends up writing optargs. */
    lily_es_checkpoint_save(parser->expr);

    lily_es_push_local_var(parser->expr, var);
    lily_es_push_binary_op(parser->expr, expr_assign);
    lily_lexer(parser->lex);
    expression_raw(parser);
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
        lily_raise_syn(parser->raiser,
                "Class %s expects %d type(s), but got %d type(s).",
                type->cls->name, type->cls->generic_count,
                type->subtype_count);

    /* Hack: This exists because Lily does not understand constraints. */
    if (type->cls == parser->symtab->hash_class) {
        lily_type *check_type = type->subtypes[0];
        if ((check_type->cls->flags & CLS_VALID_HASH_KEY) == 0 &&
            check_type->cls->id != LILY_GENERIC_ID)
            lily_raise_syn(parser->raiser, "'^T' is not a valid hash key.",
                    check_type);
    }
}

static lily_class *get_scoop_class(lily_parse_state *parser, int which)
{
    if (which > 2 || which == 0)
        lily_raise_syn(parser->raiser,
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
        lily_raise_syn(parser->raiser,
                "Non-optional argument follows optional argument.");

    lily_type *type = get_type_raw(parser, *flags);

    /* get_type ends with a call to lily_lexer, so don't call that again. */

    if (type->flags & TYPE_HAS_SCOOP) {
        if ((*flags & F_SCOOP_OK) == 0)
            lily_raise_syn(parser->raiser,
                    "Numeric scooping types only available to the backend.");

        *flags |= TYPE_HAS_SCOOP;
    }

    if (lex->token == tk_three_dots) {
        type = make_type_of_class(parser, parser->symtab->list_class, type);

        lily_lexer(lex);
        if (lex->token != tk_arrow &&
            lex->token != tk_right_parenth &&
            lex->token != tk_equal)
            lily_raise_syn(parser->raiser,
                    "Expected either '=>' or ')' after varargs.");

        *flags |= TYPE_IS_VARARGS;
    }

    if (*flags & TYPE_HAS_OPTARGS)
        type = make_type_of_class(parser, parser->symtab->optarg_class, type);

    return type;
}

static lily_type *get_prop_arg(lily_parse_state *parser, int *flags)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var = new_scoped_var(parser, NULL, "", lex->line_num);

    NEED_NEXT_TOK(tk_prop_word)
    lily_prop_entry *prop = get_named_property(parser, 0);

    NEED_CURRENT_TOK(tk_colon)
    lily_lexer(lex);
    lily_type *type = get_nameless_arg(parser, flags);

    if (*flags & TYPE_HAS_OPTARGS) {
        /* Optional arguments are created by making an optarg type which holds
           a type that is supposed to be optional. For simplicity, give the
           var the concrete underlying type, and the caller the true optarg
           containing type. */
        prop->type = type->subtypes[0];
        var->type = prop->type;
        collect_optarg_for(parser, var);
    }
    else
        prop->type = type;

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
        lily_raise_syn(parser->raiser, "%s has already been declared.",
                lex->label);

    var = new_scoped_var(parser, NULL, lex->label, lex->line_num);
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
static lily_type *get_type_raw(lily_parse_state *parser, int flags)
{
    lily_lex_state *lex = parser->lex;
    lily_type *result;
    lily_class *cls = NULL;

    if (lex->token == tk_word)
        cls = resolve_class_name(parser);
    else if (lex->token == tk_integer && (flags & F_SCOOP_OK))
        cls = get_scoop_class(parser, lex->last_integer);
    else {
        NEED_CURRENT_TOK(tk_word)
    }

    if (cls->item_kind == ITEM_TYPE_VARIANT)
        lily_raise_syn(parser->raiser,
                "Variant types not allowed in a declaration.");

    if (cls->generic_count == 0)
        result = cls->self_type;
    else if (cls->id != LILY_FUNCTION_ID) {
        NEED_NEXT_TOK(tk_left_bracket)
        int i = 0;
        while (1) {
            lily_lexer(lex);
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

        result = lily_tm_make(parser->tm, 0, cls, i);
        ensure_valid_type(parser, result);
    }
    else if (cls->id == LILY_FUNCTION_ID) {
        NEED_NEXT_TOK(tk_left_parenth)
        lily_lexer(lex);
        int arg_flags = flags & F_SCOOP_OK;
        int i = 0;
        int result_pos = parser->tm->pos;

        lily_tm_add(parser->tm, lily_unit_type);

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
            lily_tm_insert(parser->tm, result_pos,
                    get_type_raw(parser, flags));
        }

        NEED_CURRENT_TOK(tk_right_parenth)

        result = lily_tm_make(parser->tm, arg_flags & ~F_SCOOP_OK, cls, i + 1);
    }
    else
        result = NULL;

    lily_lexer(lex);
    return result;
}

/* Only function dynaload needs scoop types. Everything else can use this define
   that sends flags as 0. */
#define get_type(p) get_type_raw(p, 0)

/* Get a type represented by the name given. Largely used by dynaload. */
static lily_type *type_by_name(lily_parse_state *parser, const char *name)
{
    lily_lexer_load(parser->lex, et_copied_string, name);
    lily_lexer(parser->lex);
    lily_type *result = get_type(parser);
    lily_pop_lex_entry(parser->lex);

    return result;
}

/* This is called at the start of a class or define. If '[' is present, then
   generics are collected. Any collection starts from the current scope's
   generics (so that one does not need to re-specify A if a caller does). */
static void collect_generics(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    if (lex->token == tk_left_bracket) {
        char ch = 'A' + lily_gp_num_in_scope(parser->generics);
        char name[] = {ch, '\0'};

        while (1) {
            NEED_NEXT_TOK(tk_word)
            if (lex->label[0] != ch || lex->label[1] != '\0') {
                if (ch == 'Z' + 1) {
                    lily_raise_syn(parser->raiser, "Too many generics.");
                }
                else {
                    lily_raise_syn(parser->raiser,
                            "Invalid generic name (wanted %s, got %s).",
                            name, lex->label);
                }
            }

            lily_gp_push(parser->generics, name, ch - 'A');
            lily_lexer(lex);

            /* ch has to be updated before finishing, because 'seen' depends on
               it. Having the wrong # of generics seen (even if off by one)
               causes strange and major problems. */
            ch++;

            if (lex->token == tk_right_bracket) {
                lily_lexer(lex);
                break;
            }
            else if (lex->token != tk_comma)
                lily_raise_syn(parser->raiser,
                        "Expected either ',' or ']', not '%s'.",
                        tokname(lex->token));

            name[0] = ch;
        }
        int seen = ch - 'A';
        lily_ts_generics_seen(parser->emit->ts, seen);
    }
}

/* This is called when creating a class and after any generics have been
   collected.
   If the class has generics, then the self type will be a type of the class
   which has all of those generics:
   `class Box[A]` == `Box[A]`
   `enum Result[A, B]` == `Result[A, B]`.
   If the class doesn't have generics, the self type is set and there's nothing
   to do. */
static lily_type *build_self_type(lily_parse_state *parser, lily_class *cls)
{
    int generics_used = lily_gp_num_in_scope(parser->generics);
    lily_type *result;
    if (generics_used) {
        char name[] = {'A', '\0'};
        while (generics_used) {
            lily_class *lookup_cls = lily_find_class(parser->symtab, NULL, name);
            lily_tm_add(parser->tm, lookup_cls->self_type);
            name[0]++;
            generics_used--;
        }

        result = lily_tm_make(parser->tm, 0, cls, (name[0] - 'A'));
        cls->self_type = result;
    }
    else
        result = cls->self_type;

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
static lily_class *parse_enum(lily_parse_state *, int, int);
static lily_item *try_toplevel_dynaload(lily_parse_state *, lily_module_entry *,
        const char *);

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
    lily_module_entry *builtin = parser->module_start;

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
    lily_module_entry *entry_iter = parser->module_start;
    while (entry_iter) {
        if (entry_iter->cid_table)
            update_cid_table(parser, entry_iter);

        entry_iter = entry_iter->root_next;
    }
}

/* This function is called when the current label could potentially be a module.
   If it is, then this function will continue digging until all of the modules
   have been seen.
   The result of this is the context from which to continue looking up. */
static lily_module_entry *resolve_module(lily_parse_state *parser)
{
    lily_module_entry *result = NULL;
    lily_symtab *symtab = parser->symtab;
    lily_lex_state *lex = parser->lex;
    lily_module_entry *search_entry = lily_find_module(symtab, NULL,
            lex->label);

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
            lily_raise_syn(parser->raiser, "Class '%s' does not exist.",
                    lex->label);
    }

    return result;
}

static lily_type *dynaload_function(lily_parse_state *parser,
        lily_module_entry *m, int dyna_index)
{
    lily_lex_state *lex = parser->lex;

    const char *entry = m->dynaload_table[dyna_index];
    const char *name = entry + DYNA_NAME_OFFSET;
    const char *body = name + strlen(name) + 1;

    lily_lexer_load(lex, et_shallow_string, body);
    lily_lexer(lex);

    int save_generic_start;
    lily_gp_save_and_hide(parser->generics, &save_generic_start);
    collect_generics(parser);

    int result_pos = parser->tm->pos;
    int i = 1;
    int flags = 0 | F_SCOOP_OK;

    lily_tm_add(parser->tm, lily_unit_type);

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
                lily_raise_syn(parser->raiser,
                        "Expected either ',' or ')', not '%s'.",
                        tokname(lex->token));
        }
    }

    if (lex->token == tk_colon) {
        lily_lexer(lex);
        lily_tm_insert(parser->tm, result_pos,
                get_type_raw(parser, F_SCOOP_OK));
    }

    flags &= ~F_SCOOP_OK;

    lily_type *result = lily_tm_make(parser->tm, flags,
            parser->symtab->function_class, i);

    lily_gp_restore_and_unhide(parser->generics, save_generic_start);
    lily_pop_lex_entry(lex);

    return result;
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
    lily_mb_flush(msgbuf);
    lily_mb_add(msgbuf, name);
    lily_mb_add(msgbuf, name + strlen(name) + 1);
    lily_mb_add_char(msgbuf, '{');

    do {
        entry_index++;
        entry = table[entry_index];
    } while (entry[0] != 'V');

    int is_scoped = entry[0] != 'V';

    while (entry[0] == 'V') {
        name = entry + DYNA_NAME_OFFSET;
        lily_mb_add(msgbuf, name);
        lily_mb_add(msgbuf, name + strlen(name) + 1);
        lily_mb_add_char(msgbuf, ' ');

        entry_index++;
        entry = table[entry_index];
        if (entry[0] == 'V')
            lily_mb_add_char(msgbuf, ',');
    }

    lily_mb_add_char(msgbuf, '}');

    lily_lexer_load(parser->lex, et_copied_string, lily_mb_get(msgbuf));
    lily_lexer(parser->lex);

    int save_next_class_id;
    /* Option and Result have specific ids set aside for them so they don't need
       to be included in cid tables.
       The id must be set -before- parsing the enum, because variant default
       values rely on the id of an enum. If it's fixed later, they'll have the
       wrong id, and possibly crash. */
    if (m == parser->module_start) {
        save_next_class_id = parser->symtab->next_class_id;

        name = table[dyna_index] + DYNA_NAME_OFFSET;
        if (name[0] == 'O')
            parser->symtab->next_class_id = LILY_OPTION_ID;
        else
            parser->symtab->next_class_id = LILY_RESULT_ID;
    }
    else
        save_next_class_id = 0;

    int save_generics;
    lily_gp_save_and_hide(parser->generics, &save_generics);

    lily_class *result = parse_enum(parser, 1, is_scoped);

    lily_gp_restore_and_unhide(parser->generics, save_generics);

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

    return cls;
}

lily_item *try_method_dynaload(lily_parse_state *parser, lily_class *cls,
        const char *name)
{
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

    lily_item *result;

    if (entry[0] == 'm') {
        lily_module_entry *save_active = parser->symtab->active_module;
        parser->symtab->active_module = cls->module;

        const char *name = entry + DYNA_NAME_OFFSET;
        lily_type *dyna_type = dynaload_function(parser, cls->module, index);
        lily_var *dyna_var = new_foreign_define_var(parser, cls, dyna_type,
                name, index);

        parser->symtab->active_module = save_active;
        result = (lily_item *)dyna_var;
    }
    else
        result = NULL;

    return result;
}

static lily_class *dynaload_native(lily_parse_state *parser,
        lily_module_entry *m, int dyna_index)
{
    const char **table = m->dynaload_table;
    const char *entry = m->dynaload_table[dyna_index];
    const char *name = entry + DYNA_NAME_OFFSET;

    const char *body = name + strlen(name) + 1;
    int entry_index = dyna_index;
    lily_lex_state *lex = parser->lex;

    lily_lexer_load(lex, et_shallow_string, body);
    lily_lexer(lex);

    lily_class *cls = lily_new_class(parser->symtab, name);

    int save_generic_start;
    lily_gp_save_and_hide(parser->generics, &save_generic_start);
    collect_generics(parser);
    cls->generic_count = lily_gp_num_in_scope(parser->generics);

    if (lex->token == tk_lt) {
        lily_lexer(lex);
        lily_class *parent = lily_find_class(parser->symtab, m, lex->label);

        if (parent == NULL)
            parent = (lily_class *)try_toplevel_dynaload(parser, m, lex->label);

        cls->parent = parent;
        cls->prop_count = parent->prop_count;
    }

    lily_pop_lex_entry(parser->lex);

    cls->dyna_start = dyna_index + 1;
    if (m == parser->module_start) {
        parser->symtab->next_class_id--;

        if (strcmp(cls->name, "DivisionByZeroError") == 0)
            cls->id = LILY_DBZERROR_ID;
        else if (strcmp(cls->name, "Exception") == 0)
            cls->id = LILY_EXCEPTION_ID;
        else if (strcmp(cls->name, "IndexError") == 0)
            cls->id = LILY_INDEXERROR_ID;
        else if (strcmp(cls->name, "IOError") == 0)
            cls->id = LILY_IOERROR_ID;
        else if (strcmp(cls->name, "KeyError") == 0)
            cls->id = LILY_KEYERROR_ID;
        else if (strcmp(cls->name, "RuntimeError") == 0)
            cls->id = LILY_RUNTIMEERROR_ID;
        else if (strcmp(cls->name, "ValueError") == 0)
            cls->id = LILY_VALUEERROR_ID;
        else
            /* Shouldn't happen, but use an impossible id to make it stand out. */
            cls->id = 12345;
    }

    do {
        entry_index++;
        entry = table[entry_index];
    } while (entry[0] == 'm');

    do {
        int flags;
        char ch = entry[0];

        if (ch == '1')
            flags = SYM_SCOPE_PRIVATE;
        else if (ch == '2')
            flags = SYM_SCOPE_PROTECTED;
        else if (ch == '3')
            flags = 0;
        else
            break;

        const char *prop_name = entry + DYNA_NAME_OFFSET;
        const char *prop_body = prop_name + strlen(prop_name) + 1;

        lily_lexer_load(lex, et_shallow_string, prop_body);
        lily_lexer(lex);
        lily_add_class_property(parser->symtab, cls, get_type(parser),
                prop_name, flags);
        lily_pop_lex_entry(lex);

        entry_index++;
        entry = table[entry_index];
    } while (1);

    /* Properties may use generics, so this must be after them. */
    lily_gp_restore_and_unhide(parser->generics, save_generic_start);

    /* Make sure the constructor loads too. Parts like inheritance will call for
       the class to dynaload, but (reasonably) expect <new> to be visible. */
    try_method_dynaload(parser, cls, "<new>");

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
        lily_var *new_var = new_global_var(parser, var_type, name);

        /* Vars should not be uncommon, and they may need cid information.
           Make sure that cid information is up-to-date. */
        update_cid_table(parser, m);

        /* This fixes the cid table so the callee can use ID_ macros to get
           the ids they need. */
        parser->toplevel_func->cid_table = m->cid_table;

        /* This should push exactly one extra value onto the stack. Since
           global vars have placeholder values inserted, the var ends up
           exactly where it should be. */
        m->loader(parser->vm, dyna_pos);

        result = (lily_item *)new_var;
    }
    else if (letter == 'F') {
        const char *name = m->dynaload_table[dyna_pos] + DYNA_NAME_OFFSET;
        lily_type * dyna_type = dynaload_function(parser, m, dyna_pos);
        lily_var *dyna_var = new_foreign_define_var(parser, NULL, dyna_type,
                name, dyna_pos);
        result = (lily_item *)dyna_var;
    }
    else if (letter == 'C') {
        lily_class *new_cls = dynaload_class(parser, m, dyna_pos);
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
    else if (letter == 'N') {
        lily_class *new_cls = dynaload_native(parser, m, dyna_pos);
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
    lily_module_entry *m = parser->module_start;
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

    if (cls->dyna_start)
        return try_method_dynaload(parser, cls, name);

    return NULL;
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
#define ST_DONE                 4
#define ST_BAD_TOKEN            5
/* Normally, the next token is pulled up after an expression_* helper has been
   called. If this is or'd onto the state, then it's assumed that the next token
   has already been pulled up. */
#define ST_FORWARD              0x8

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
            lily_raise_syn(parser->raiser,
                    "Cannot implicitly use the constructor of an enum.");

        lily_item *target = lily_find_or_dl_member(parser, cls, "<new>");
        if (target == NULL)
            lily_raise_syn(parser->raiser,
                    "Class %s does not have a constructor.", cls->name);

        /* This happens when an optional argument of a constructor tries to use
           that same constructor. */
        if (target->flags & SYM_NOT_INITIALIZED)
            lily_raise_syn(parser->raiser,
                    "Constructor for class %s is not initialized.", cls->name);

        lily_es_push_static_func(parser->expr, (lily_var *)target);
        *state = ST_FORWARD | ST_WANT_OPERATOR;
        return;
    }

    *state = ST_WANT_OPERATOR;

    NEED_NEXT_TOK(tk_word)
    lily_item *item = lily_find_or_dl_member(parser, cls, lex->label);

    /* Allow only static methods that are exactly in this class. */
    if (item &&
        item->item_kind == ITEM_TYPE_VAR &&
        ((lily_var *)item)->parent == cls) {
        lily_es_push_static_func(parser->expr, (lily_var *)item);
        return;
    }

    /* Enums allow scoped variants through `<enum>.<variant>`. */
    if (cls->flags & CLS_IS_ENUM) {
        lily_variant_class *variant = lily_find_variant(cls, lex->label);
        if (variant) {
            lily_es_push_variant(parser->expr, variant);
            return;
        }
    }

    lily_raise_syn(parser->raiser, "%s.%s does not exist.", cls->name,
            lex->label);
}

/* This is a wrapper function that handles pushing the given literal into the
   parser's ast pool. This function exists because literals may, in the future,
   not have a type associated with them (and be just a lily_value). */
static void push_literal(lily_parse_state *parser, lily_literal *lit)
{
    lily_class *literal_cls;

    if (lit->class_id == LILY_INTEGER_ID)
        literal_cls = parser->symtab->integer_class;
    else if (lit->class_id == LILY_DOUBLE_ID)
        literal_cls = parser->symtab->double_class;
    else if (lit->class_id == LILY_STRING_ID)
        literal_cls = parser->symtab->string_class;
    else if (lit->class_id == LILY_BYTESTRING_ID)
        literal_cls = parser->symtab->bytestring_class;
    else
        /* Impossible, but keeps the compiler from complaining. */
        literal_cls = parser->symtab->question_class;

    lily_es_push_literal(parser->expr, literal_cls->self_type, lit->reg_spot);
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
        lit = lily_get_string_literal(symtab,
                parser->emit->function_block->function_var->name);
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
    if (cls->item_kind == ITEM_TYPE_VARIANT) {
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
        lily_raise_syn(parser->raiser,
                "Attempt to use uninitialized value '%s'.",
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
                lily_raise_syn(parser->raiser,
                        "'self' must be used within a class.");

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

    if (search_module->dynaload_table) {
        lily_item *dl_result = try_toplevel_dynaload(parser,
                search_module, lex->label);
        if (dl_result) {
            dispatch_dynaload(parser, dl_result, state);
            return;
        }
    }

    lily_raise_syn(parser->raiser, "%s has not been declared.", lex->label);
}

/* This is called to handle `@<prop>` accesses. */
static void expression_property(lily_parse_state *parser, int *state)
{
    if (parser->class_self_type == NULL)
        lily_raise_syn(parser->raiser,
                "Properties cannot be used outside of a class constructor.");

    char *name = parser->lex->label;
    lily_class *current_class = parser->class_self_type->cls;

    lily_prop_entry *prop = lily_find_property(current_class, name);
    if (prop == NULL) {
        const char *extra = "";
        if (parser->emit->block->block_type == block_class)
            extra = " ('var' keyword missing?)";

        lily_raise_syn(parser->raiser, "Property %s is not in class %s.%s",
                name, current_class->name, extra);
    }

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
        lily_raise_syn(parser->raiser, "Expected closing token '%s', not '%s'.",
                tokname(expect), tokname(token));
}

/* There's this annoying problem where 1-1 can be 1 - 1 or 1 -1. This is called
   if an operator is wanted but a digit is given instead. It checks to see if
   the numeric token can be broken up into an operator and a value, instead of
   just an operator. */
static int maybe_digit_fixup(lily_parse_state *parser)
{
    int fixed = 0;
    int is_positive = parser->lex->last_integer >= 0;

    if (lily_lexer_digit_rescan(parser->lex)) {
        if (is_positive)
            lily_es_push_binary_op(parser->expr, expr_plus);
        else
            lily_es_push_binary_op(parser->expr, expr_minus);

        fixed = 1;
    }

    return fixed;
}

/* This handles literals, and does that fixup thing if that's necessary. */
static void expression_literal(lily_parse_state *parser, int *state)
{
    lily_lex_state *lex = parser->lex;
    lily_token token = parser->lex->token;

    if (*state == ST_WANT_OPERATOR) {
        if ((token == tk_integer || token == tk_double) &&
            maybe_digit_fixup(parser))
            goto integer_case;
        else if (parser->expr->save_depth == 0)
            *state = ST_DONE;
        else
            *state = ST_BAD_TOKEN;
    }
    else if (lex->token == tk_integer) {
integer_case: ;
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
    else if (lex->token == tk_byte) {
        lily_es_push_byte(parser->expr, (uint8_t) lex->last_integer);
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
        lily_raise_syn(parser->raiser, "Expected a value, not ','.");

    lily_ast *last_tree = lily_es_get_saved_tree(parser->expr);
    if (last_tree == NULL) {
        *state = ST_BAD_TOKEN;
        return;
    }

    if (lex->token == tk_comma) {
        if (last_tree->tree_type == tree_hash &&
            (last_tree->args_collected & 0x1) == 0)
            lily_raise_syn(parser->raiser,
                    "Expected a key => value pair before ','.");
        if (last_tree->tree_type == tree_subscript)
            lily_raise_syn(parser->raiser,
                    "Subscripts cannot contain ','.");
    }
    else if (lex->token == tk_arrow) {
        if (last_tree->tree_type == tree_list) {
            if (last_tree->args_collected == 0)
                last_tree->tree_type = tree_hash;
            else
                lily_raise_syn(parser->raiser, "Unexpected token '%s'.",
                        tokname(tk_arrow));
        }
        else if (last_tree->tree_type != tree_hash ||
                 (last_tree->args_collected & 0x1) == 1)
                lily_raise_syn(parser->raiser, "Unexpected token '%s'.",
                        tokname(tk_arrow));
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
    else
        lily_raise_syn(parser->raiser,
                "Expected either '%s' or '%s', not '%s'.",
                tokname(tk_word), tokname(tk_typecast_parenth),
                tokname(lex->token));

    *state = ST_WANT_OPERATOR;
}

/* This is the magic function that handles expressions. The states it uses are
   defined above. Most callers will use expression instead of this. */
static void expression_raw(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    int state = ST_DEMAND_VALUE;

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
            if (state == ST_DEMAND_VALUE)
                state = ST_BAD_TOKEN;
            else if (state == ST_WANT_OPERATOR &&
                     parser->expr->save_depth == 0)
                state = ST_DONE;
            else {
                check_valid_close_tok(parser);
                lily_es_leave_tree(parser->expr);
                state = ST_WANT_OPERATOR;
            }
        }
        else if (lex->token == tk_integer || lex->token == tk_double ||
                 lex->token == tk_double_quote || lex->token == tk_bytestring ||
                 lex->token == tk_byte)
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
            lily_raise_syn(parser->raiser, "Unexpected token '%s'.",
                    tokname(lex->token));
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
    lily_es_flush(parser->expr);
    expression_raw(parser);
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

    lily_lexer(parser->lex);
    while (1) {
        if (lex->token == tk_word)
            key_id = keyword_by_name(lex->label);

        if (key_id == -1) {
            expression(parser);
            if (lex->token != tk_end_lambda)
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
            /* Don't call statement(1), or control is taken away from the
               lambda (and the final expression doesn't get inference). Instead,
               dispatch directly. */
            lily_lexer(lex);
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

/* Make sure that this lambda isn't the default value for an optional argument.
   This prevents 'creative' uses of optional arguments. */
static void ensure_not_in_optargs(lily_parse_state *parser, int line)
{
    lily_block *block = parser->emit->block;

    if (block->block_type != block_define &&
        block->block_type != block_class)
        return;

    if (block->patch_start != lily_u16_pos(parser->emit->patches)) {
        parser->lex->line_num = line;
        lily_raise_syn(parser->raiser,
                "Optional arguments are not allowed to use lambdas.");
    }
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

    ensure_not_in_optargs(parser, lambda_start_line);

    /* Process the lambda as if it were a file with a slightly adjusted
       starting line number. The line number is patched so that multi-line
       lambdas show the right line number for errors.
       Additionally, lambda_body is a shallow copy of data within the ast's
       string pool. A deep copy MUST be made because expressions within this
       lambda may cause the ast's string pool to be resized. */
    lily_lexer_load(lex, et_lambda, lambda_body);
    lex->line_num = lambda_start_line;

    /* Block entry assumes that the most recent var added is the var to bind
       the function to. For the type of the lambda, use the default call
       type (a function with no args and no output) because expect_type may
       be NULL if the emitter doesn't know what it wants. */
    lily_var *lambda_var = new_native_define_var(parser, NULL, "(lambda)",
            lex->line_num);

    /* From here on, vars created will be in the scope of the lambda. Also,
       this binds a function value to lambda_var. */
    lily_emit_enter_block(parser->emit, block_lambda);

    lily_lexer(lex);

    lily_tm_add(parser->tm, lily_unit_type);

    if (lex->token == tk_bitwise_or)
        args_collected = collect_lambda_args(parser, expect_type);
    else if (lex->token == tk_bitwise_or) {
        NEED_NEXT_TOK(tk_bitwise_or)
    }
    else if (lex->token != tk_logical_or)
        lily_raise_syn(parser->raiser, "Unexpected token '%s'.", lex->token);

    /* The current expression may not be done. This makes sure that the pool
       won't use the same trees again. */
    lily_es_checkpoint_save(parser->expr);
    root_result = parse_lambda_body(parser, expect_type);
    lily_es_checkpoint_restore(parser->expr);

    if (root_result != NULL)
        lily_tm_insert(parser->tm, tm_return, root_result);

    int flags = 0;
    if (expect_type && expect_type->cls->id == LILY_FUNCTION_ID &&
        expect_type->flags & TYPE_IS_VARARGS)
        flags = TYPE_IS_VARARGS;

    lambda_var->type = lily_tm_make(parser->tm, flags,
            parser->symtab->function_class, args_collected + 1);

    lily_emit_function_end(parser->emit, lambda_var->type, lex->line_num);
    lily_emit_leave_block(parser->emit);
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

static void if_handler(lily_parse_state *, int);
static void do_handler(lily_parse_state *, int);
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
static void scoped_handler(lily_parse_state *, int);
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
    scoped_handler,
    define_handler,
    return_handler,
    except_handler,
    import_handler,
    private_handler,
    protected_handler,
    continue_handler,
};

/* This is used by lambda handling so that statements (and the handler
   declarations) can come after lambdas. */
static inline void handle_multiline(lily_parse_state *parser, int key_id)
{
    handlers[key_id](parser, 1);
}

/* This tries to make a var with the given type, but won't if a var with that
   name already exists. */
static lily_var *get_named_var(lily_parse_state *parser, lily_type *var_type)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var;

    var = lily_find_var(parser->symtab, NULL, lex->label);
    if (var != NULL)
        lily_raise_syn(parser->raiser, "%s has already been declared.",
                lex->label);

    var = new_scoped_var(parser, var_type, lex->label, lex->line_num);

    lily_lexer(lex);
    return var;
}

/* Same as get_named_var, except this creates a var that's always local. */
static lily_var *get_local_var(lily_parse_state *parser, lily_type *var_type)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var;

    var = lily_find_var(parser->symtab, NULL, lex->label);
    if (var != NULL)
        lily_raise_syn(parser->raiser, "%s has already been declared.",
                lex->label);

    var = new_local_var(parser, var_type, lex->label, lex->line_num);

    lily_lexer(lex);
    return var;
}

/* The same thing as get_named_var, but with a property instead. */
static lily_prop_entry *get_named_property(lily_parse_state *parser, int flags)
{
    char *name = parser->lex->label;
    lily_class *current_class = parser->class_self_type->cls;

    lily_named_sym *sym = lily_find_member(current_class, name);
    if (sym) {
        if (sym->item_kind == ITEM_TYPE_VAR)
            lily_raise_syn(parser->raiser,
                    "A method in class '%s' already has the name '%s'.",
                    current_class->name, name);
        else
            lily_raise_syn(parser->raiser,
                    "Property %s already exists in class %s.", name,
                    current_class->name);
    }

    lily_prop_entry *prop;
    prop = lily_add_class_property(parser->symtab, current_class, NULL, name,
            flags);

    lily_lexer(parser->lex);
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

    lily_token want_token, other_token;
    if (parser->emit->block->block_type == block_class) {
        want_token = tk_prop_word;
        other_token = tk_word;
    }
    else {
        want_token = tk_word;
        other_token = tk_prop_word;
    }

    while (1) {
        lily_es_flush(parser->expr);

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
            sym = (lily_sym *)get_named_property(parser, flags);
            lily_es_push_property(parser->expr, (lily_prop_entry *)sym);
        }

        if (lex->token == tk_colon) {
            lily_lexer(lex);
            sym->type = get_type(parser);
        }

        if (lex->token != tk_equal) {
            lily_raise_syn(parser->raiser,
                    "An initialization expression is required here.");
        }

        lily_es_push_binary_op(parser->expr, expr_assign);
        lily_lexer(lex);
        expression_raw(parser);
        lily_emit_eval_expr(parser->emit, parser->expr);

        if (lex->token != tk_comma)
            break;

        lily_lexer(lex);
    }
}

static void var_handler(lily_parse_state *parser, int multi)
{
    parse_var(parser, 0);
}

static void ensure_unique_method_name(lily_parse_state *parser,
        const char *name)
{
    if (lily_find_var(parser->symtab, NULL, name) != NULL)
        lily_raise_syn(parser->raiser, "%s has already been declared.", name);

    if (parser->class_self_type) {
        lily_class *current_class = parser->class_self_type->cls;

        if (lily_find_property(current_class, name)) {
            lily_raise_syn(parser->raiser,
                "A property in class '%s' already has the name '%s'.",
                current_class->name, name);
        }
    }
}

static void send_optargs_for(lily_parse_state *parser, lily_type *type)
{
    int count, i;

    lily_emit_write_optarg_header(parser->emit, type, &count);
    lily_es_checkpoint_save(parser->expr);

    /* This reorders optarg expressions to be last to first, so they can be
       popped. */
    lily_es_checkpoint_reverse_n(parser->expr, count);

    for (i = 0;i < count;i++) {
        lily_es_checkpoint_restore(parser->expr);
        lily_emit_eval_optarg(parser->emit, parser->expr->root);
    }

    /* Restore the original expression. */
    lily_es_checkpoint_restore(parser->expr);
}

static lily_var *parse_define_header(lily_parse_state *parser, int modifiers)
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
    lily_var *define_var = new_native_define_var(parser, parent, lex->label,
            lex->line_num);

    /* This prevents optargs from using function they're declared in. */
    define_var->flags |= SYM_NOT_INITIALIZED;

    int i = 0;
    int arg_flags = 0;
    int result_pos = parser->tm->pos;

    /* This is the initial result. NULL means the function doesn't return
       anything. If it does, then this spot will be overwritten. */
    lily_tm_add(parser->tm, lily_unit_type);

    lily_lexer(lex);
    collect_generics(parser);
    lily_emit_enter_block(parser->emit, block_define);

    if (parser->class_self_type) {
        /* This is a method of a class. It should implicitly take 'self' as
           the first argument, and be registered to be within that class.
           It may also have a private/protected modifier, so add that too. */
        lily_tm_add(parser->tm, parser->class_self_type);
        i++;

        lily_var *self_var = new_local_var(parser, parser->class_self_type,
                "(self)", lex->line_num);
        define_var->parent = parser->class_self_type->cls;
        define_var->flags |= modifiers;

        parser->emit->block->self = (lily_storage *)self_var;
    }

    if (lex->token == tk_left_parenth) {
        lily_lexer(lex);

        /* If () is omitted, then it's assumed that the function will not take
           any arguments (unless it implicitly takes self). */
        if (lex->token == tk_right_parenth)
            lily_raise_syn(parser->raiser, "Empty () not needed for a define.");

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
                lily_raise_syn(parser->raiser,
                        "Expected either ',' or ')', not '%s'.",
                        tokname(lex->token));
        }
    }

    if (lex->token == tk_colon) {
        lily_lexer(lex);
        if (strcmp(lex->label, "self") != 0)
            lily_tm_insert(parser->tm, result_pos, get_type(parser));
        else {
            lily_block *block = parser->emit->block->prev;
            if (block == NULL || block->block_type != block_class)
                lily_raise_syn(parser->raiser,
                        "'self' return type only allowed on class methods.");

            lily_tm_insert(parser->tm, result_pos, lily_self_class->self_type);
            lily_lexer(lex);
        }
    }

    NEED_CURRENT_TOK(tk_left_curly)

    define_var->type = lily_tm_make(parser->tm, arg_flags,
            parser->symtab->function_class, i + 1);

    if (define_var->type->flags & TYPE_HAS_OPTARGS)
        send_optargs_for(parser, define_var->type);

    define_var->flags &= ~SYM_NOT_INITIALIZED;

    return define_var;
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
        lily_raise_syn(parser->raiser,
                   "For range value expression contains an assignment.");
    }

    lily_class *cls = parser->symtab->integer_class;

    /* For loop values are created as vars so there's a name in case of a
       problem. This name doesn't have to be unique, since it will never be
       found by the user. */
    lily_var *var = new_local_var(parser, cls->self_type, name,
            parser->lex->line_num);

    lily_emit_eval_expr_to_var(parser->emit, es, var);

    return var;
}

static void process_docstring(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);

    int key_id;
    if (lex->token == tk_word)
        key_id = keyword_by_name(lex->label);
    else
        key_id = -1;

    if (key_id == KEY_PRIVATE ||
        key_id == KEY_PROTECTED ||
        key_id == KEY_DEFINE ||
        key_id == KEY_CLASS) {
        lily_lexer(lex);
        handlers[key_id](parser, 1);
    }
    else
        lily_raise_syn(parser->raiser,
                "Docstring must be followed by a function or class definition.");
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
                 token == tk_byte) {
            expression(parser);
            lily_emit_eval_expr(parser->emit, parser->expr);
        }
        else if (token == tk_docstring)
            process_docstring(parser);
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

/* This handles the '{' ... '}' part for blocks that are multi-lined. */
static void parse_multiline_block_body(lily_parse_state *parser,
        int multi)
{
    lily_lex_state *lex = parser->lex;

    if (multi == 0)
        lily_raise_syn(parser->raiser,
                   "Multi-line block within single-line block.");

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
    lily_raise_syn(parser->raiser, "'elif' without 'if'.");
}

static void else_handler(lily_parse_state *parser, int multi)
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

static void return_handler(lily_parse_state *parser, int multi)
{
    lily_block *block = parser->emit->function_block;
    lily_type *return_type = NULL;

    if (block->block_type == block_class)
        lily_raise_syn(parser->raiser,
                "'return' not allowed in a class constructor.");
    else if (block->block_type == block_lambda)
        lily_raise_syn(parser->raiser, "'return' not allowed in a lambda.");
    else if (block->block_type == block_file)
        lily_raise_syn(parser->raiser, "'return' used outside of a function.");
    else
        return_type = block->function_var->type->subtypes[0];

    if (return_type != lily_unit_type)
        expression(parser);

    lily_emit_eval_return(parser->emit, parser->expr, return_type);

    if (multi && code_is_after_exit(parser)) {
        const char *extra = ".";
        if (return_type == lily_unit_type)
            extra = " (no return type given).";

        lily_raise_syn(parser->raiser,
                "Statement(s) after 'return' will not execute%s", extra);
    }
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

    if (multi && code_is_after_exit(parser))
        lily_raise_syn(parser->raiser,
                "Statement(s) after 'continue' will not execute.");
}

static void break_handler(lily_parse_state *parser, int multi)
{
    lily_emit_break(parser->emit);

    if (multi && code_is_after_exit(parser))
        lily_raise_syn(parser->raiser,
                "Statement(s) after 'break' will not execute.");
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
        loop_var = new_local_var(parser, cls->self_type, lex->label,
                lex->line_num);
    }
    else if (loop_var->type->cls->id != LILY_INTEGER_ID) {
        lily_raise_syn(parser->raiser,
                   "Loop var must be type integer, not type '^T'.",
                   loop_var->type);
    }

    NEED_NEXT_TOK(tk_word)
    if (strcmp(lex->label, "in") != 0)
        lily_raise_syn(parser->raiser, "Expected 'in', not '%s'.", lex->label);

    lily_lexer(lex);

    lily_var *for_start, *for_end;
    lily_sym *for_step;

    for_start = parse_for_range_value(parser, "(for start)");

    NEED_CURRENT_TOK(tk_three_dots)
    lily_lexer(lex);

    for_end = parse_for_range_value(parser, "(for end)");

    if (lex->token == tk_word) {
        if (strcmp(lex->label, "by") != 0)
            lily_raise_syn(parser->raiser, "Expected 'by', not '%s'.",
                    lex->label);

        lily_lexer(lex);
        for_step = (lily_sym *)parse_for_range_value(parser, "(for step)");
    }
    else {
        lily_var *step_var = new_local_var(parser,
                parser->symtab->integer_class->self_type, "(for step)",
                lex->line_num);
        /* Must flush manually since expression isn't being called. */
        lily_es_flush(parser->expr);
        lily_es_push_integer(parser->expr, 1);
        lily_emit_eval_expr_to_var(parser->emit, parser->expr, step_var);
        for_step = (lily_sym *)step_var;
    }

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
        lily_raise_syn(parser->raiser, "Expected 'while', not '%s'.",
                lex->label);

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
    lily_module_entry *save_active = parser->symtab->active_module;
    lily_lex_state *lex = parser->lex;

    parser->symtab->active_module = module;

    /* lily_emit_enter_block will write new code to this special var. */
    lily_var *import_var = new_native_define_var(parser, NULL, "__import__",
            lex->line_num);

    import_var->type = parser->default_call_type;

    lily_emit_enter_block(parser->emit, block_file);

    /* The whole of the file can be thought of as one large statement. */
    lily_lexer(lex);
    statement(parser, 1);

    /* Since this is processing an import, the lexer will raise an error if
       ?> is found. Because of that, multi-line statement can only end with
       either } or eof. Only one is right. */
    if (lex->token == tk_right_curly)
        lily_raise_syn(parser->raiser, "'}' outside of a block.");

    if (parser->emit->block->block_type != block_file)
        lily_raise_syn(parser->raiser, "Unterminated block(s) at end of file.");

    lily_emit_function_end(parser->emit, parser->default_call_type,
            lex->line_num);
    lily_emit_leave_block(parser->emit);
    lily_pop_lex_entry(parser->lex);

    lily_emit_write_import_call(parser->emit, import_var);

    parser->symtab->active_module = save_active;
}

static void import_handler(lily_parse_state *parser, int multi)
{
    lily_lex_state *lex = parser->lex;
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file &&
        block->prev != NULL)
        lily_raise_syn(parser->raiser, "Cannot import a file here.");

    lily_symtab *symtab = parser->symtab;
    lily_module_entry *active = symtab->active_module;
    lily_msgbuf *msgbuf = parser->msgbuf;
    lily_mb_flush(msgbuf);

    while (1) {
        NEED_CURRENT_TOK(tk_word)
        /* The import path may include slashes. Use this to scan the path,
           because it won't allow spaces in between. */
        lily_scan_import_path(lex);

        lily_module_entry *module = NULL;
        char *search_start = lex->label;
        char *path_tail = strrchr(search_start, LILY_PATH_CHAR);
        /* Will the name that is going to be added conflict with something that
           has already been added? */
        if (path_tail != NULL)
            search_start = path_tail + 1;

        if (lily_find_module(symtab, active, search_start))
            lily_raise_syn(parser->raiser,
                    "A module named '%s' has already been imported here.",
                    search_start);

        if (path_tail == NULL)
            module = lily_find_registered_module(symtab, lex->label);

        /* Is there a cached version that was loaded somewhere else? */
        if (module == NULL) {
            module = load_module(parser, lex->label);
            /* module is never NULL: load_module raises on error. */
            if (module->flags & MODULE_NOT_EXECUTED) {
                module->flags &= ~MODULE_NOT_EXECUTED;
                run_loaded_module(parser, module);
            }
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
}

static void process_except(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_class *except_cls = get_type(parser)->cls;
    lily_block_type new_type = block_try_except;

    /* If it's 'except Exception', then all possible cases have been handled. */
    if (except_cls->id == LILY_EXCEPTION_ID)
        new_type = block_try_except_all;
    else if (lily_class_greater_eq_id(LILY_EXCEPTION_ID, except_cls) == 0)
        lily_raise_syn(parser->raiser, "'%s' is not a valid exception class.",
                except_cls->name);
    else if (except_cls->generic_count != 0)
        lily_raise_syn(parser->raiser, "'except' type cannot have subtypes.");

    /* The block change has to come before the var is made, or the var will be
       made in the wrong scope. */
    lily_emit_change_block_to(parser->emit, new_type);

    lily_var *exception_var = NULL;
    if (lex->token == tk_word) {
        if (strcmp(parser->lex->label, "as") != 0)
            lily_raise_syn(parser->raiser, "Expected 'as', not '%s'.",
                    lex->label);

        NEED_NEXT_TOK(tk_word)
        exception_var = lily_find_var(parser->symtab, NULL, lex->label);
        if (exception_var != NULL)
            lily_raise_syn(parser->raiser, "%s has already been declared.",
                    exception_var->name);

        exception_var = new_local_var(parser, except_cls->self_type,
                lex->label, lex->line_num);

        lily_lexer(lex);
    }

    NEED_CURRENT_TOK(tk_colon)
    lily_emit_except(parser->emit, except_cls->self_type, exception_var,
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
        lily_raise_syn(parser->raiser,
                   "Multi-line block within single-line block.");

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
    lily_raise_syn(parser->raiser, "'except' outside 'try'.");
}

static void raise_handler(lily_parse_state *parser, int multi)
{
    if (parser->emit->function_block->block_type == block_lambda)
        lily_raise_syn(parser->raiser, "'raise' not allowed in a lambda.");

    expression(parser);
    lily_emit_raise(parser->emit, parser->expr);

    if (multi && code_is_after_exit(parser))
        lily_raise_syn(parser->raiser,
                "Statement(s) after 'raise' will not execute.");
}

static void ensure_valid_class(lily_parse_state *parser, const char *name)
{
    if (name[1] == '\0')
        lily_raise_syn(parser->raiser,
                "'%s' is not a valid class name (too short).", name);

    lily_block *block = parser->emit->block;

    if (block->block_type != block_file && block->prev != NULL) {
        lily_raise_syn(parser->raiser, "Cannot declare a class here.");
    }

    lily_class *lookup_class = lily_find_class(parser->symtab, NULL, name);
    if (lookup_class != NULL) {
        lily_raise_syn(parser->raiser, "Class '%s' has already been declared.",
                name);
    }

    lily_item *item = try_toplevel_dynaload(parser,
            parser->symtab->builtin_module, name);
    if (item && item->item_kind != ITEM_TYPE_VAR)
        lily_raise_syn(parser->raiser,
                "A built-in class named '%s' already exists.", name);
}

static lily_class *parse_and_verify_super(lily_parse_state *parser,
        lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    NEED_NEXT_TOK(tk_word)

    lily_class *super_class = resolve_class_name(parser);

    if (super_class == NULL)
        lily_raise_syn(parser->raiser, "Class '%s' does not exist.",
                lex->label);
    else if (super_class == cls)
        lily_raise_syn(parser->raiser, "A class cannot inherit from itself!");
    else if (super_class->item_kind == ITEM_TYPE_VARIANT ||
             super_class->flags & (CLS_IS_ENUM | CLS_IS_BUILTIN))
        lily_raise_syn(parser->raiser, "'%s' cannot be inherited from.",
                lex->label);

    int adjust = super_class->prop_count;

    /* Lineage must be fixed before running the inherited constructor, as the
       constructor may use 'self'. */
    cls->parent = super_class;
    cls->prop_count += super_class->prop_count;
    cls->inherit_depth = super_class->inherit_depth + 1;

    if (cls->prop_count && cls->members) {
        /* Properties created through the `var @<name> = ...` shorthand have the
           wrong index. Fix their register spots before the assigns are written
           by emitter's function setup. */
        lily_named_sym *sym = cls->members;
        while (sym) {
            if (sym->item_kind == ITEM_TYPE_PROPERTY)
                sym->reg_spot += adjust;

            sym = sym->next;
        }
    }

    return super_class;
}

/* There's a class to inherit from, so run the constructor. */
static void run_super_ctor(lily_parse_state *parser, lily_class *cls,
        lily_class *super_class)
{
    lily_lex_state *lex = parser->lex;
    lily_var *class_new = lily_find_method(super_class, "<new>");

    /* It's time to process the constructor to be sent. The constructor function
       is inserted as a special 'inherited_new' tree. That will let emitter know
       that generics have to solve as themselves. Such a guarantee ensures that
       the A of one class is not the B of an upward class.

       Normally it's fine for expressions to go on after a call. But in this
       case, no expressions should occur after the parentheses.

       ```
       class Two(value: Integer) > One(value)[0]
       ```

       Because of the above two requirements, expressions need to be handled
       directly instead of using a simpler call to expression_raw. */

    lily_expr_state *es = parser->expr;
    lily_es_flush(es);
    lily_es_enter_tree(es, tree_call);
    lily_es_push_inherited_new(es, class_new);
    lily_es_collect_arg(es);
    /* This causes expression to stop on ',' and ')'. It's safe to do this
       because dynaload doesn't come through here. */
    es->save_depth = 0;

    lily_lexer(parser->lex);

    if (lex->token == tk_left_parenth) {
        /* Since the call was already entered, skip the first '(' or the parser
           will attempt to enter it again. */
        lily_lexer(lex);
        if (lex->token == tk_right_parenth)
            lily_raise_syn(parser->raiser,
                    "Empty () not needed here for inherited new.");

        while (1) {
            expression_raw(parser);
            lily_es_collect_arg(parser->expr);
            if (lex->token == tk_comma) {
                lily_lexer(lex);
                continue;
            }
            else if (lex->token == tk_right_parenth) {
                lily_lexer(lex);
                break;
            }
            else
                lily_raise_syn(parser->raiser,
                        "Expected either ',' or ')', not '%s'.\n",
                        tokname(lex->token));
        }
    }

    /* Tree exit will drop the depth down by 1, so fix it first. */
    parser->expr->save_depth = 1;
    lily_es_leave_tree(parser->expr);
    lily_emit_eval_expr(parser->emit, es);
}

/* This handles everything needed to create a class, including the inheritance
   if that turns out to be necessary. */
static lily_var *parse_class_header(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    /* Use the default call type (function ()) in case one of the types listed
       triggers a dynaload. If a dynaload is triggered, emitter tries to
       restore the current return type from the last define's return type. */
    lily_var *call_var = new_native_define_var(parser, cls, "<new>",
            lex->line_num);

    /* Prevent optargs from using this function. */
    call_var->flags |= SYM_NOT_INITIALIZED;

    lily_lexer(lex);
    collect_generics(parser);
    cls->generic_count = lily_gp_num_in_scope(parser->generics);

    lily_emit_enter_block(parser->emit, block_class);

    parser->class_self_type = build_self_type(parser, cls);

    int i = 1;
    int flags = 0;
    lily_tm_add(parser->tm, parser->class_self_type);

    if (lex->token == tk_left_parenth) {
        lily_lexer(lex);
        if (lex->token == tk_right_parenth)
            lily_raise_syn(parser->raiser, "Empty () not needed for a class.");

        while (1) {
            NEED_CURRENT_TOK(tk_word)
            char ch = lex->label[0];
            if (ch != 'v' || strcmp(lex->label, "var") != 0)
                lily_tm_add(parser->tm, get_named_arg(parser, &flags));
            else
                lily_tm_add(parser->tm, get_prop_arg(parser, &flags));

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
                lily_raise_syn(parser->raiser,
                        "Expected either ',' or ')', not '%s'.",
                        tokname(lex->token));
        }
    }

    call_var->type = lily_tm_make(parser->tm, flags,
            parser->symtab->function_class, i);

    lily_class *super_cls = NULL;

    if (lex->token == tk_lt)
        super_cls = parse_and_verify_super(parser, cls);

    lily_emit_write_class_header(parser->emit, parser->class_self_type,
            lex->line_num);

    if (call_var->type->flags & TYPE_HAS_OPTARGS)
        send_optargs_for(parser, call_var->type);

    call_var->flags &= ~SYM_NOT_INITIALIZED;

    if (cls->members)
        lily_emit_write_shorthand_ctor(parser->emit, cls,
                parser->symtab->active_module->var_chain, lex->line_num);

    if (super_cls)
        run_super_ctor(parser, cls, super_cls);

    return call_var;
}

/* This is a helper function that scans 'target' to determine if it will require
   any gc information to hold. */
static int get_gc_flags_for(lily_class *top_class, lily_type *target)
{
    /* NULL is used as the return type of a Function that returns nothing. */
    if (target == NULL)
        return 0;

    int result_flag = 0;

    if (target->cls->id == LILY_GENERIC_ID) {
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

    lily_named_sym *member_iter = target->members;

    while (member_iter) {
        /* Class/Enum methods do not count toward circularity. */
        if (member_iter->item_kind != ITEM_TYPE_VAR) {
            lily_type *type = member_iter->type;
            mark |= get_gc_flags_for(target, type);

            if (type->subtype_count) {
                int i;
                for (i = 0;i < type->subtype_count;i++)
                    mark |= get_gc_flags_for(target, type->subtypes[i]);
            }
        }

        member_iter = member_iter->next;
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

    int save_generic_start;
    lily_gp_save_and_hide(parser->generics, &save_generic_start);

    lily_var *ctor_var = parse_class_header(parser, cls);

    NEED_CURRENT_TOK(tk_left_curly)
    parse_multiline_block_body(parser, 1);

    determine_class_gc_flag(parser, parser->class_self_type->cls);

    parser->class_self_type = save_class_self_type;
    lily_emit_function_end(parser->emit, ctor_var->type, lex->line_num);
    lily_emit_leave_block(parser->emit);

    lily_gp_restore_and_unhide(parser->generics, save_generic_start);
}

static void class_handler(lily_parse_state *parser, int multi)
{
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file &&
        block->prev != NULL)
        lily_raise_syn(parser->raiser, "Cannot define a class here.");

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
        lily_raise_syn(parser->raiser, "Empty () not needed for a variant.");

    int i = 1;
    int flags = 0;

    /* For consistency with `Function`, the result of a variant is the
       all-generic type of the parent enum. */
    lily_tm_add(parser->tm, variant_cls->parent->self_type);

    while (1) {
        lily_tm_add(parser->tm, get_nameless_arg(parser, &flags));

        if (flags & TYPE_HAS_OPTARGS)
            lily_raise_syn(parser->raiser,
                    "Variant types cannot have default values.");

        i++;
        if (lex->token == tk_comma) {
            lily_lexer(lex);
            continue;
        }
        else if (lex->token == tk_right_parenth)
            break;
        else
            lily_raise_syn(parser->raiser,
                    "Expected either ',' or ')', not '%s'.",
                    tokname(lex->token));
    }

    lily_lexer(lex);

    lily_type *build_type = lily_tm_make(parser->tm, flags,
            parser->symtab->function_class, i);

    variant_cls->build_type = build_type;
    variant_cls->flags &= ~CLS_EMPTY_VARIANT;
}

static lily_class *parse_enum(lily_parse_state *parser, int is_dynaload,
        int is_scoped)
{
    lily_block *block = parser->emit->block;
    if (is_dynaload == 0 &&
        block->block_type != block_file &&
        block->prev != NULL)
        lily_raise_syn(parser->raiser, "Cannot define an enum here.");

    lily_lex_state *lex = parser->lex;
    NEED_CURRENT_TOK(tk_word)

    if (is_scoped == 1 && is_dynaload == 0) {
        if (strcmp(lex->label, "enum") != 0)
            lily_raise_syn(parser->raiser, "Expected 'enum' after flat.");

        NEED_NEXT_TOK(tk_word)
    }

    if (is_dynaload == 0)
        ensure_valid_class(parser, lex->label);

    lily_class *enum_cls = lily_new_enum_class(parser->symtab, lex->label);

    if (is_scoped)
        enum_cls->flags |= CLS_ENUM_IS_SCOPED;

    lily_lexer(lex);

    int save_generic_start;
    lily_gp_save_and_hide(parser->generics, &save_generic_start);
    collect_generics(parser);

    enum_cls->generic_count = lily_gp_num_in_scope(parser->generics);

    lily_emit_enter_block(parser->emit, block_enum);

    lily_type *result_type = build_self_type(parser, enum_cls);
    lily_type *save_self_type = parser->class_self_type;
    parser->class_self_type = result_type;

    NEED_CURRENT_TOK(tk_left_curly)
    lily_lexer(lex);

    int variant_count = 0;

    while (1) {
        NEED_CURRENT_TOK(tk_word)
        if (is_dynaload == 0 && variant_count) {
            lily_class *cls = (lily_class *)lily_find_variant(enum_cls,
                    lex->label);

            if (cls == NULL && is_scoped == 0)
                cls = lily_find_class(parser->symtab, NULL, lex->label);

            if (cls) {
                lily_raise_syn(parser->raiser,
                        "A class with the name '%s' already exists.",
                        lex->label);
            }
        }

        lily_variant_class *variant_cls = lily_new_variant_class(parser->symtab,
                enum_cls, lex->label);
        variant_count++;

        lily_lexer(lex);
        if (lex->token == tk_left_parenth)
            parse_variant_header(parser, variant_cls);

        if (lex->token == tk_right_curly)
            break;
        else if (lex->token == tk_word && lex->label[0] == 'd' &&
                 keyword_by_name(lex->label) == KEY_DEFINE)
            break;
        else {
            NEED_CURRENT_TOK(tk_comma)
            lily_lexer(lex);
        }
    }

    if (variant_count < 2) {
        lily_raise_syn(parser->raiser,
                "An enum must have at least two variants.");
    }

    /* Emitter uses this later to determine how many cases are allowed. */
    enum_cls->variant_size = variant_count;

    if (is_dynaload == 0 && lex->token == tk_word) {
        while (1) {
            lily_lexer(lex);
            define_handler(parser, 1);
            if (lex->token == tk_right_curly)
                break;
            else if (lex->token != tk_word ||
                keyword_by_name(lex->label) != KEY_DEFINE)
                lily_raise_syn(parser->raiser,
                        "Expected '}' or 'define', not '%s'.",
                        tokname(lex->token));
        }
    }

    lily_emit_leave_block(parser->emit);
    parser->class_self_type = save_self_type;

    lily_gp_restore_and_unhide(parser->generics, save_generic_start);
    lily_lexer(lex);

    return enum_cls;
}

static void enum_handler(lily_parse_state *parser, int multi)
{
    parse_enum(parser, 0, 0);
}

static void scoped_handler(lily_parse_state *parser, int multi)
{
    parse_enum(parser, 0, 1);
}

static void match_case_enum(lily_parse_state *parser, lily_sym *match_sym)
{
    lily_type *match_input_type = match_sym->type;
    lily_class *match_class = match_input_type->cls;
    lily_lex_state *lex = parser->lex;

    NEED_CURRENT_TOK(tk_word)
    if (match_class->flags & CLS_ENUM_IS_SCOPED) {
        if (strcmp(match_class->name, lex->label) != 0)
            lily_raise_syn(parser->raiser,
                    "Expected '%s.<variant>', not '%s' because '%s' is a scoped enum.",
                    match_class->name, lex->label, match_class->name);

        NEED_NEXT_TOK(tk_dot)
        NEED_NEXT_TOK(tk_word)
    }

    lily_variant_class *variant_case = lily_find_variant(match_class,
            lex->label);

    if (variant_case == NULL)
        lily_raise_syn(parser->raiser, "%s is not a member of enum %s.",
                lex->label, match_class->name);

    if (lily_emit_is_duplicate_case(parser->emit, (lily_class *)variant_case))
        lily_raise_syn(parser->raiser, "Already have a case for variant %s.",
                lex->label);

    lily_emit_change_match_branch(parser->emit);
    lily_emit_write_match_case(parser->emit, match_sym,
            (lily_class *)variant_case);

    if ((variant_case->flags & CLS_EMPTY_VARIANT) == 0) {
        lily_type *build_type = variant_case->build_type;
        lily_type_system *ts = parser->emit->ts;

        NEED_NEXT_TOK(tk_left_parenth)
        /* There should be as many identifiers as there are arguments to this
           variant's creation type.
           Also, start at 1 so that the return at [0] is skipped. */
        NEED_NEXT_TOK(tk_word)

        int i;
        for (i = 1;i < build_type->subtype_count;i++) {
            lily_type *var_type = lily_ts_resolve_by_second(ts,
                    match_input_type, build_type->subtypes[i]);

            if (strcmp(lex->label, "_") == 0)
                lily_lexer(lex);
            else {
                lily_var *var = get_local_var(parser, var_type);
                lily_emit_decompose(parser->emit, match_sym, i - 1,
                        var->reg_spot);
            }

            if (i != build_type->subtype_count - 1) {
                NEED_CURRENT_TOK(tk_comma)
                NEED_NEXT_TOK(tk_word)
            }
        }
        NEED_CURRENT_TOK(tk_right_parenth)
    }
    /* else the variant does not take arguments, and cannot decompose because
       there is nothing inside to decompose. */

    NEED_NEXT_TOK(tk_colon)
    lily_lexer(lex);
}

/* This is called when an enum match block is missing one or more cases. An
   error message as well as the classes that are missing are written to a
   msgbuf. This finishes by raising a SyntaxError. */
static void error_incomplete_match(lily_parse_state *parser,
        lily_sym *match_sym)
{
    lily_class *match_class = match_sym->type->cls;
    int match_case_start = parser->emit->block->match_case_start;

    int i;
    lily_msgbuf *msgbuf = parser->raiser->aux_msgbuf;
    lily_named_sym *sym_iter = match_class->members;
    int *match_cases = parser->emit->match_cases + match_case_start;

    lily_mb_add(msgbuf,
            "Match pattern not exhaustive. The following case(s) are missing:");

    while (sym_iter) {
        if (sym_iter->item_kind == ITEM_TYPE_VARIANT) {
            for (i = match_case_start;i < parser->emit->match_case_pos;i++) {
                if (sym_iter->id == match_cases[i])
                    break;
            }

            if (i == parser->emit->match_case_pos)
                lily_mb_add_fmt(msgbuf, "\n* %s", sym_iter->name);
        }

        sym_iter = sym_iter->next;
    }

    lily_raise_syn(parser->raiser, lily_mb_get(msgbuf));
}

static void match_case_class(lily_parse_state *parser,
        lily_sym *match_sym)
{
    lily_lex_state *lex = parser->lex;
    NEED_CURRENT_TOK(tk_word)
    lily_class *cls = resolve_class_name(parser);

    /* The second case only happens if the source is a Dynamic. */
    if (lily_class_greater_eq(match_sym->type->cls, cls) == 0 &&
        match_sym->type->cls->id != LILY_QUESTION_ID) {
        lily_raise_syn(parser->raiser,
                "Class %s does not inherit from matching class %s.", cls->name,
                match_sym->type->cls->name);
    }

    if (lily_emit_is_duplicate_case(parser->emit, cls))
        lily_raise_syn(parser->raiser, "Already have a case for class %s.",
                cls->name);

    lily_emit_change_match_branch(parser->emit);
    lily_emit_write_match_case(parser->emit, match_sym, cls);

    /* Forbid non-monomorphic types to avoid the question of what to do
       if the match class has more generics. */
    if (cls->generic_count != 0) {
        lily_raise_syn(parser->raiser,
                "Class matching only works for types without generics.",
                cls->name);
    }

    NEED_NEXT_TOK(tk_left_parenth)
    NEED_NEXT_TOK(tk_word)

    if (strcmp(lex->label, "_") == 0)
        lily_lexer(lex);
    else {
        lily_var *var = get_local_var(parser, cls->self_type);
        lily_emit_decompose(parser->emit, match_sym, 0, var->reg_spot);
    }

    NEED_CURRENT_TOK(tk_right_parenth)
    NEED_NEXT_TOK(tk_colon)
    lily_lexer(lex);
}

static void match_handler(lily_parse_state *parser, int multi)
{
    if (multi == 0)
        lily_raise_syn(parser->raiser,
                "Match block cannot be in a single-line block.");

    lily_lex_state *lex = parser->lex;

    lily_emit_enter_block(parser->emit, block_match);

    expression(parser);
    lily_emit_eval_match_expr(parser->emit, parser->expr);

    NEED_CURRENT_TOK(tk_colon)
    NEED_NEXT_TOK(tk_left_curly)
    NEED_NEXT_TOK(tk_word)
    if (keyword_by_name(lex->label) != KEY_CASE)
        lily_raise_syn(parser->raiser, "'match' must start with a case.");

    lily_sym *match_sym = parser->expr->root->result;
    int is_enum = match_sym->type->cls->flags & CLS_IS_ENUM;
    int have_else = 0, case_count = 0;

    while (1) {
        if (lex->token == tk_word) {
            int key = keyword_by_name(lex->label);
            if (key == KEY_CASE) {
                if (have_else)
                    lily_raise_syn(parser->raiser,
                            "'case' in exhaustive match.");

                lily_lexer(lex);
                if (is_enum)
                    match_case_enum(parser, match_sym);
                else
                    match_case_class(parser, match_sym);

                case_count++;
            }
            else if (key == KEY_ELSE) {
                if (have_else)
                    lily_raise_syn(parser->raiser,
                            "'else' in exhaustive match.");

                NEED_NEXT_TOK(tk_colon)
                lily_emit_change_match_branch(parser->emit);
                lily_lexer(lex);
                have_else = 1;
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

    if (have_else == 0) {
        if (is_enum == 0)
            lily_raise_syn(parser->raiser,
                    "Match against a class must have an 'else' case.");
        else if (case_count != match_sym->type->cls->variant_size)
            error_incomplete_match(parser, match_sym);
    }

    lily_lexer(lex);
    lily_emit_leave_block(parser->emit);
}

static void case_handler(lily_parse_state *parser, int multi)
{
    lily_raise_syn(parser->raiser, "'case' not allowed outside of 'match'.");
}

static void parse_define(lily_parse_state *parser, int modifiers)
{
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file &&
        block->block_type != block_define &&
        block->block_type != block_class &&
        block->block_type != block_enum &&
        block->prev != NULL)
        lily_raise_syn(parser->raiser, "Cannot define a function here.");

    lily_lex_state *lex = parser->lex;
    int save_generic_start;
    lily_gp_save(parser->generics, &save_generic_start);

    lily_var *define_var = parse_define_header(parser, modifiers);

    NEED_CURRENT_TOK(tk_left_curly)
    parse_multiline_block_body(parser, 1);
    lily_emit_function_end(parser->emit, define_var->type, lex->line_num);
    lily_emit_leave_block(parser->emit);
    lily_gp_restore(parser->generics, save_generic_start);

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
        lily_raise_syn(parser->raiser, "'%s' is not allowed here.", name);

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
        lily_raise_syn(parser->raiser,
                "Expected either 'var' or 'define', but got '%s'.",
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

static void maybe_fix_print(lily_parse_state *parser)
{
    lily_symtab *symtab = parser->symtab;
    lily_module_entry *builtin = symtab->builtin_module;
    lily_var *stdout_var = lily_find_var(symtab, builtin, "stdout");
    lily_vm_state *vm = parser->vm;

    if (stdout_var) {
        lily_var *print_var = lily_find_var(symtab, builtin, "print");
        if (print_var) {
            /* Swap out the default implementation of print for one that will
               check if stdin is closed first. */
            lily_value *print_value = vm->readonly_table[print_var->reg_spot];
            lily_function_val *print_func = print_value->value.function;

            print_func->foreign_func = lily_stdout_print;
            print_func->cid_table = &stdout_var->reg_spot;
        }
    }
}

static void setup_and_exec_vm(lily_parse_state *parser)
{
    /* todo: Find a way to do some of this as-needed, instead of always. */
    lily_register_classes(parser->symtab, parser->vm);
    lily_prepare_main(parser->emit, parser->toplevel_func);

    parser->vm->readonly_table = parser->symtab->literals->data;

    maybe_fix_print(parser);
    update_all_cid_tables(parser);

    parser->executing = 1;
    lily_call_prepare(parser->vm, parser->toplevel_func);
    /* The above function pushes a Unit value to act as a sink for lily_call to
       put a value into. __main__ won't return a value so get rid of it. */
    lily_stack_delete_top(parser->vm);
    lily_call(parser->vm, 0);
    parser->executing = 0;

    /* Clear __main__ for the next pass. */
    lily_reset_main(parser->emit);
}

static void template_read_loop(lily_parse_state *parser, lily_lex_state *lex)
{
    lily_config *config = parser->config;
    int result = 0;

    do {
        char *buffer;
        result = lily_lexer_read_content(lex, &buffer);
        if (buffer[0])
            config->render_func(buffer, config->data);
    } while (result);
}

/* This is the entry point of the parser. It parses the thing that it was given
   and then runs the code. This shouldn't be called directly, but instead by
   one of the lily_parse_* functions that will set it up right. */
static void parser_loop(lily_parse_state *parser, const char *filename,
        int in_template)
{
    lily_lex_state *lex = parser->lex;

    if (in_template)
        /* Force template files to start with <?lily at the very top.
           This prevents accidentally viewing a code file as content. */
        lily_verify_template(lex);

    lily_lexer(lex);

    while (1) {
        if (lex->token == tk_word)
            statement(parser, 1);
        else if (lex->token == tk_right_curly) {
            lily_emit_leave_block(parser->emit);
            lily_lexer(lex);
        }
        else if (lex->token == tk_end_tag || lex->token == tk_eof) {
            if (in_template == 0 && lex->token == tk_end_tag)
                lily_raise_syn(parser->raiser, "Unexpected token '%s'.",
                        tokname(lex->token));

            if (parser->emit->block->prev != NULL) {
                lily_raise_syn(parser->raiser,
                           "Unterminated block(s) at end of parsing.");
            }

            setup_and_exec_vm(parser);

            if (lex->token == tk_end_tag)
                template_read_loop(parser, lex);

            if (lex->token == tk_eof)
                break;
        }
        else if (lex->token == tk_docstring) {
            process_docstring(parser);
        }
        /* This makes it possible to have expressions that don't start with a
           var. This may be useful later for building a repl. */
        else if (lex->token == tk_integer || lex->token == tk_double ||
                 lex->token == tk_double_quote ||
                 lex->token == tk_left_parenth ||
                 lex->token == tk_left_bracket ||
                 lex->token == tk_bytestring ||
                 lex->token == tk_lambda ||
                 lex->token == tk_byte) {
            expression(parser);
            lily_emit_eval_expr(parser->emit, parser->expr);
        }
        else
            lily_raise_syn(parser->raiser, "Unexpected token '%s'.",
                       tokname(lex->token));
    }
}

static void fix_first_file_name(lily_parse_state *parser,
        const char *filename)
{
    lily_module_entry *module = parser->main_module;

    module->const_path = filename;
    module->dirname = dir_from_path(filename);
    module->loadname = loadname_from_path(filename);
    module->cmp_len = strlen(filename);

    parser->first_pass = 0;
}

/* This is called when the interpreter encounters an error. This builds an
   error message that is stored within parser's msgbuf. A runner can later fetch
   this error with lily_get_error. */
static void build_error(lily_parse_state *parser)
{
    lily_raiser *raiser = parser->raiser;
    lily_msgbuf *msgbuf = parser->msgbuf;

    lily_mb_flush(parser->msgbuf);

    if (raiser->exception_cls) {
        lily_module_entry *m = raiser->exception_cls->module;
        /* If this doesn't come from the first package (or the builtin one),
           then add the plain name of the module for clarity. */
        if (m != parser->module_start &&
            m != parser->module_start->root_next)
            lily_mb_add_fmt(msgbuf, "%s.", m->loadname);
    }

    const char *msg = lily_mb_get(raiser->msgbuf);
    lily_mb_add(msgbuf, lily_name_for_error(raiser));
    if (msg[0] != '\0')
        lily_mb_add_fmt(msgbuf, ": %s\n", msg);
    else
        lily_mb_add_char(msgbuf, '\n');

    if (parser->executing == 0) {
        lily_lex_entry *iter = parser->lex->entry;
        if (iter) {
            int fixed_line_num = (raiser->line_adjust == 0 ?
                    parser->lex->line_num : raiser->line_adjust);

            lily_mb_add_fmt(msgbuf, "    from %s:%d:\n",
                    parser->symtab->active_module->path, fixed_line_num);
        }
    }
    else {
        lily_call_frame *frame = parser->vm->call_chain;

        lily_mb_add(msgbuf, "Traceback:\n");

        while (frame->prev) {
            lily_function_val *func = frame->function;
            const char *class_name = func->class_name;
            const char *func_name = func->trace_name;
            char *separator = ".";
            if (class_name == NULL) {
                class_name = "";
                separator = "";
            }
            else if (strcmp(func_name, "<new>") == 0) {
                func_name = "";
                separator = "";
            }

            if (frame->function->code == NULL)
                lily_mb_add_fmt(msgbuf, "    from [C]: in %s%s%s\n",
                        class_name, separator, func_name);
            else
                lily_mb_add_fmt(msgbuf,
                        "    from %s:%d: in %s%s%s\n",
                        func->module->path, frame->code[-1], class_name,
                        separator, func_name);

            frame = frame->prev;
        }
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
        lily_raise_err(parser->raiser, "Failed to open %s: (%s).", path,
                buffer);
    }

    return load_file;
}

static int parse_file(lily_parse_state *parser, const char *filename,
        int in_template)
{
    if (parser->first_pass)
        fix_first_file_name(parser, filename);

    handle_rewind(parser);

    /* It is safe to do this, because the parser will always occupy the first
       jump. All others should use lily_jump_setup instead. */
    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        char *suffix = strrchr(filename, '.');
        if (suffix == NULL || strcmp(suffix, ".lily") != 0)
            lily_raise_err(parser->raiser, "File name must end with '.lily'.");

        FILE *f = load_file_to_parse(parser, filename);

        lily_lexer_load(parser->lex, et_file, f);
        parser_loop(parser, filename, in_template);
        lily_pop_lex_entry(parser->lex);
        lily_mb_flush(parser->msgbuf);

        return 1;
    }
    else
        parser->rs->pending = 1;

    return 0;
}

static int parse_string(lily_parse_state *parser, const char *name, char *str,
        int in_template)
{
    if (parser->first_pass)
        fix_first_file_name(parser, name);

    handle_rewind(parser);

    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        lily_lexer_load(parser->lex, et_shallow_string, str);
        parser_loop(parser, name, in_template);
        lily_pop_lex_entry(parser->lex);
        lily_mb_flush(parser->msgbuf);
        return 1;
    }
    else
        parser->rs->pending = 1;

    return 0;
}

int lily_parse_file(lily_state *s, const char *path)
{
    return parse_file(s->parser, path, 0);
}

int lily_parse_string(lily_state *s, const char *name, const char *str)
{
    return parse_string(s->parser, name, (char *)str, 0);
}

int lily_parse_expr(lily_state *s, const char *name, char *str,
        const char **text)
{
    if (text)
        *text = NULL;

    lily_parse_state *parser = s->parser;
    if (parser->first_pass)
        fix_first_file_name(parser, name);

    handle_rewind(parser);

    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        lily_lex_state *lex = parser->lex;
        lily_lexer_load(lex, et_shallow_string, str);
        lily_lexer(lex);

        expression(parser);
        lily_emit_eval_expr(parser->emit, parser->expr);
        NEED_CURRENT_TOK(tk_eof);

        lily_sym *sym = parser->expr->root->result;

        setup_and_exec_vm(parser);
        lily_pop_lex_entry(parser->lex);

        if (sym && text) {
            /* This grabs the symbol from __main__. */
            lily_value *reg = s->call_chain->next->start[sym->reg_spot];
            lily_msgbuf *msgbuf = parser->msgbuf;

            lily_mb_flush(msgbuf);
            lily_mb_add_fmt(msgbuf, "(^T): ", sym->type);

            /* Add value doesn't quote String values, because most callers do
               not want that. This one does, so bypass that. */
            if (reg->class_id == LILY_STRING_ID)
                lily_mb_add_fmt(msgbuf, "\"%s\"", reg->value.string->string);
            else
                lily_mb_add_value(msgbuf, s, reg);

            *text = lily_mb_get(msgbuf);
        }

        return 1;
    }
    else
        parser->rs->pending = 1;

    return 0;
}

int lily_render_string(lily_state *s, const char *name, const char *str)
{
    return parse_string(s->parser, name, (char *)str, 1);
}

int lily_render_file(lily_state *s, const char *filename)
{
    return parse_file(s->parser, filename, 1);
}

lily_function_val *lily_get_func(lily_vm_state *vm, const char *name)
{
    /* todo: Handle scope access, class methods, and so forth. Ideally, it can
       be done without loading any fake files (like dynaloading does), as this
       may be the base of a preloader. */
    lily_var *v = lily_find_var(vm->parser->symtab, NULL, name);
    lily_function_val *result;

    if (v)
        result = vm->readonly_table[v->reg_spot]->value.function;
    else
        result = NULL;

    return result;
}

/* Return the message of the last error encountered by the interpreter. */
const char *lily_get_error_message(lily_state *s)
{
    return lily_mb_get(s->raiser->msgbuf);
}

void *lily_get_data(lily_vm_state *vm)
{
    return vm->data;
}

/* Return a string describing the last error encountered by the interpreter.
   This string is guaranteed to be valid until the next execution of the
   interpreter. */
const char *lily_get_error(lily_state *s)
{
    build_error(s->parser);
    return lily_mb_get(s->parser->msgbuf);
}

lily_config *lily_get_config(lily_state *s)
{
    return s->parser->config;
}

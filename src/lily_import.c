#include <string.h>

#include "lily.h"
#include "lily_alloc.h"
#include "lily_core_types.h"
#include "lily_import.h"
#include "lily_library.h"
#include "lily_parser.h"
#include "lily_platform.h"

#ifdef _WIN32
/* Need this to process system directories. */
# include <windows.h>
#endif

/** The import system (ims) handles building modules and dispatching imports to
    the right location. Modules hold symbols: Classes, enums, vars, and more.
    Each module is backed by a single source (library, file, string, etc).

    When parser wants to find a module, it uses the import hook defined in the
    config struct. That function (`lily_default_import_hook`) implements the
    default search strategy. Embedders are, however, free to define their own
    functions with their own strategies.

    The following explains how the default strategy works, as well as why.

    ```
    crops/
        corn.lily
        potato.lily
    packages/
        tractor/
            src/
                tractor.lily
                controls/
                    movement.lily
    farm.lily
    start.lily <--- Begin here.
    ```

    The module for `start.lily` is a root module. Imports are first done
    relative to the root module. If that doesn't work, the next step is to try a
    package directory in the root. The module for `tractor.lily` is a root
    module. Modules inside of it are relative to that directory.

    This strategy allows `crops/corn.lily` to import the tractor. A new package
    can be added by simply dropping it into the `packages` directory, so long as
    it follows the structure (<name>/src/<name>.suffix). */

static void add_data_to_module(lily_module_entry *module, void *handle,
        const char **table, lily_foreign_func *call_table)
{
    module->handle = handle;
    module->info_table = table;
    module->call_table = call_table;
    module->flags &= ~MODULE_NOT_EXECUTED;

    /* Here, 'cid' is short for 'class id'. Suppose a foreign library wants to
       create a class instance. What ID does it have? This table is given the
       IDs. The macros that bindgen creates use this. */

    unsigned char cid_count = module->info_table[0][0];

    if (cid_count) {
        module->cid_table = lily_malloc(cid_count * sizeof(*module->cid_table));
        memset(module->cid_table, 0, cid_count * sizeof(*module->cid_table));
    }
}

/* Parser prefixes paths with ./ or .\\ to prevent absolute paths. Before saving
   the path (which traceback will use), trim that off. */
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
    module->cmp_len = (uint16_t)strlen(path);
    module->path = lily_malloc((strlen(path) + 1) * sizeof(*module->path));
    strcpy(module->path, path);
}

static void set_dirs_on_module(lily_parse_state *parser,
        lily_module_entry *module)
{
    /* Fix the directory of a module that might run an import. */
    if (parser->ims->import_type != imp_local) {
        /* The first module of a package or system import is the root. */
        module->dirname = lily_ims_dir_from_path(module->path);
        module->root_dirname = module->dirname;
    }
    else
        module->root_dirname = parser->ims->source_module->root_dirname;
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
    size_t len = strlen(input_str);

    if (input_str[len] != LILY_PATH_CHAR)
        lily_mb_add_char(msgbuf, LILY_PATH_CHAR);
}


/* Functions used by parser and here, or just parser. */


const char *lily_ims_build_path(lily_import_state *ims, const char *target,
        const char *suffix)
{
    /* Make sure the caller used a `lily_import_use_*` function first. */
    if (ims->dirname == NULL)
        return NULL;

    /* Keep package and system paths simple. */
    if (ims->import_type != imp_local && ims->is_slashed_path)
        return NULL;

    lily_msgbuf *path_msgbuf = lily_mb_flush(ims->path_msgbuf);
    const char *root_dirname = ims->source_module->root_dirname;

    if (ims->import_type != imp_system) {
        if (root_dirname == NULL || root_dirname[0] == '\0')
            lily_mb_add_char(path_msgbuf, '.');
        else
            lily_mb_add(path_msgbuf, root_dirname);

        lily_mb_add_char(path_msgbuf, LILY_PATH_CHAR);

        if (ims->dirname[0] != '\0')
            add_fixslash_dir(path_msgbuf, ims->dirname);

        if (ims->import_type == imp_package) {
            lily_mb_add_fmt(path_msgbuf,
                    "packages" LILY_PATH_SLASH
                    "%s" LILY_PATH_SLASH
                    "src" LILY_PATH_SLASH, target);
        }
    }
    else {
        lily_mb_add_fmt(path_msgbuf,
                "%s%s" LILY_PATH_SLASH
                "src" LILY_PATH_SLASH, ims->dirname, target);
    }

    lily_mb_add(path_msgbuf, target);
    lily_mb_add_char(path_msgbuf, '.');
    lily_mb_add(path_msgbuf, suffix);

    return lily_mb_raw(path_msgbuf);
}

char *lily_ims_dir_from_path(const char *path)
{
    const char *slash = strrchr(path, LILY_PATH_CHAR);
    char *out;

    if (slash == NULL) {
        out = lily_malloc(1 * sizeof(*out));
        out[0] = '\0';
    }
    else {
        size_t bare_len = slash - path;
        out = lily_malloc((bare_len + 1) * sizeof(*out));

        strncpy(out, path, bare_len);
        out[bare_len] = '\0';
    }

    return out;
}

/* This adds 'to_link' as an entry within 'target' so that 'target' is able to
   reference it later on. If 'as_name' is not NULL, then 'to_link' will be
   available through that name. Otherwise, it will be available as the name it
   actually has. */
void lily_ims_link_module_to(lily_module_entry *target,
        lily_module_entry *to_link, const char *as_name)
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

lily_module_entry *lily_ims_new_module(lily_parse_state *parser)
{
    lily_module_entry *module = lily_malloc(sizeof(*module));

    module->loadname = NULL;
    module->dirname = NULL;
    module->path = NULL;
    module->doc_id = UINT16_MAX;
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

        /* Children of a module share a pointer to their parent's root_dirname,
           so a strcmp isn't necessary here. */
        if (active_root != main_root)
            module = NULL;
    }

    return module;
}

lily_module_entry *lily_ims_open_module(lily_parse_state *parser)
{
    lily_import_state *ims = parser->ims;
    lily_module_entry *module = find_registered_module(parser,
            ims->pending_loadname);

    if (module)
        return module;

    uint16_t save_pos = lily_u16_pos(parser->data_stack);
    const char *name = ims->fullname;

    parser->config->import_func(parser->vm, name);

    if (ims->last_import == NULL) {
        lily_msgbuf *msgbuf = lily_mb_flush(parser->msgbuf);

        lily_mb_add_fmt(msgbuf, "Cannot import '%s':", name);

        if (ims->is_slashed_path == 0)
            lily_mb_add_fmt(msgbuf, "\n    no builtin module '%s'", name);

        lily_buffer_u16 *b = parser->data_stack;
        uint16_t i;

        for (i = save_pos;i < lily_u16_pos(b);i++) {
            uint16_t check_pos = lily_u16_get(b, i);
            lily_mb_add_fmt(msgbuf, "\n    no file '%s'",
                    lily_sp_get(parser->data_strings, check_pos));
        }

        /* Don't send the buffer as the only argument, because the path may have
           format characters. */
        lily_raise_syn(parser->raiser, "%s", lily_mb_raw(msgbuf));
    }

    module = ims->last_import;

    if (module->flags & MODULE_IN_EXECUTION)
        lily_raise_syn(parser->raiser,
                "This module is already being imported.");

    lily_u16_set_pos(parser->data_stack, save_pos);
    return module;
}

#ifdef _WIN32
# define pattern LILY_DIR_SEPARATOR LILY_DIR_PROCESS_STR
# define search_fn strpbrk
#else
# define pattern LILY_DIR_SEPARATOR
# define search_fn strstr
#endif

/* Process directories specified by config into a \0 divided string for the
   import system. LILY_PATH_CHAR is written at the end to act as a sentinel.
   Since Windows does not have a standard location for libraries, this also
   transforms LILY_DIR_PROCESS_CHAR (default `|`) into the path of the current
   process, allowing paths relative to that. */
void lily_ims_process_sys_dirs(lily_parse_state *parser, lily_config *config)
{
    lily_import_state *ims = parser->ims;

    if (config->use_sys_dirs == 0 || ims->sys_dirs) {
        return;
    }

#ifdef _WIN32
    char buffer[MAX_PATH + 1];
    DWORD buffer_size = sizeof(buffer) / sizeof(*buffer);
    DWORD n = GetModuleFileNameA(NULL, buffer, buffer_size);

    if (n == 0 || n == buffer_size)
        /* Assume this will never happen and be brief. */
        lily_raise_raw(parser->raiser, "GetModuleFileNameA failed.");

    char *end = strrchr(buffer, '\\');

    *end = '\0';
#endif

    lily_msgbuf *msgbuf = lily_mb_flush(ims->path_msgbuf);
    char *start = config->sys_dirs;
    char *iter = search_fn(start, pattern);

    while (iter) {
#if _WIN32
        if (*iter == LILY_DIR_PROCESS_CHAR)
            lily_mb_add(msgbuf, buffer);
        else {
            lily_mb_add_slice(msgbuf, start, 0, iter - start);
            lily_mb_add_char(msgbuf, '\0');
        }
#else
        lily_mb_add_slice(msgbuf, start, 0, iter - start);
        lily_mb_add_char(msgbuf, '\0');
#endif

        start = iter + 1;
        iter = search_fn(iter + 1, pattern);
    }

    lily_mb_add(msgbuf, start);

    /* Add a sentinel so path looping knows where to stop. */
    lily_mb_add_sized(msgbuf, "\0" LILY_DIR_SEPARATOR, 2);

    uint32_t size = lily_mb_pos(msgbuf);
    char *dirs = lily_malloc((size + 1) * sizeof(*dirs));

    /* The message is always \0 terminated, so size + 1 is safe. */
    memcpy(dirs, lily_mb_raw(msgbuf), size + 1);

    ims->sys_dirs = dirs;
}

#undef pattern
#undef search_fn


/* External (lily.h) import api (except module registration) */


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

int lily_import_file(lily_state *s, const char *name)
{
    lily_parse_state *parser = s->gs->parser;
    const char *path = lily_ims_build_path(parser->ims, name, "lily");

    if (import_check(parser, path))
        return path != NULL;

    FILE *source = fopen(path, "r");

    if (source == NULL) {
        lily_pa_add_data_string(parser, path);
        return 0;
    }

    lily_lexer_load(parser->lex, et_file, source);

    lily_module_entry *module = lily_ims_new_module(parser);

    add_path_to_module(module, parser->ims->pending_loadname, path);
    set_dirs_on_module(parser, module);
    return 1;
}

int lily_import_file_or_library(lily_state *s, const char *name)
{
    return lily_import_file(s, name) || lily_import_library(s, name);
}

int lily_import_string(lily_state *s, const char *name, const char *source)
{
    lily_parse_state *parser = s->gs->parser;
    const char *path = lily_ims_build_path(parser->ims, name, "lily");

    if (import_check(parser, path))
        return path != NULL;

    /* Technically not necessary if the source is readonly. Inconsistent bugs
       happen if the caller says it's readonly but it actually isn't. Therefore,
       all strings get copied. */
    lily_lexer_load(parser->lex, et_copied_string, (char *)source);

    lily_module_entry *module = lily_ims_new_module(parser);

    add_path_to_module(module, parser->ims->pending_loadname, path);
    set_dirs_on_module(parser, module);
    return 1;
}

int lily_import_library(lily_state *s, const char *name)
{
    lily_parse_state *parser = s->gs->parser;
    const char *suffix_table[] = LILY_LIB_SUFFIXES;
    const char **suffix = suffix_table;
    int result = 0;

    /* Libraries provide a mechanism for escaping the sandbox. */
    if (parser->config->sandbox)
        return 0;

    while (*suffix != NULL) {
        const char *path = lily_ims_build_path(parser->ims, name, *suffix);

        suffix++;

        if (import_check(parser, path)) {
            result = (path != NULL);
            break;
        }

        void *handle = lily_library_load(path);

        if (handle == NULL) {
            lily_pa_add_data_string(parser, path);
            continue;
        }

        lily_msgbuf *msgbuf = lily_mb_flush(parser->msgbuf);
        const char *loadname = parser->ims->pending_loadname;

        const char **info_table = (const char **)lily_library_get(handle,
                lily_mb_sprintf(msgbuf, "lily_%s_info_table", loadname));

        lily_foreign_func *call_table = lily_library_get(handle,
                lily_mb_sprintf(msgbuf, "lily_%s_call_table", loadname));

        if (info_table == NULL || call_table == NULL) {
            lily_pa_add_data_string(parser, path);
            lily_library_free(handle);
            continue;
        }

        lily_module_entry *module = lily_ims_new_module(parser);

        add_path_to_module(module, parser->ims->pending_loadname, path);
        add_data_to_module(module, handle, info_table, call_table);
        result = 1;
        break;
    }

    return result;
}

int lily_import_library_data(lily_state *s, const char *path,
        const char **info_table, lily_call_entry_func *call_table)
{
    lily_parse_state *parser = s->gs->parser;

    if (import_check(parser, path))
        return 1;

    lily_module_entry *module = lily_ims_new_module(parser);

    add_path_to_module(module, parser->ims->pending_loadname, path);
    add_data_to_module(module, NULL, info_table, call_table);
    return 1;
}

void lily_module_register(lily_state *s, const char *name,
        const char **info_table, lily_call_entry_func *call_table)
{
    lily_parse_state *parser = s->gs->parser;
    lily_module_entry *module = lily_ims_new_module(parser);

    /* This special "path" is for vm and parser traceback. */
    const char *module_path = lily_mb_sprintf(parser->msgbuf, "[%s]", name);

    add_path_to_module(module, name, module_path);
    add_data_to_module(module, NULL, info_table, call_table);
    module->cmp_len = 0;
    module->flags |= MODULE_IS_REGISTERED;
}

/* This is not in lily.h, but is similar to the above so they are together.
   This takes a parser because only lily_pkg_core.c should be using it. */
void lily_predefined_module_register(lily_parse_state *parser, const char *name,
        const char **info_table, lily_call_entry_func *call_table)
{
    lily_module_entry *module = lily_ims_new_module(parser);
    const char *module_path = lily_mb_sprintf(parser->msgbuf, "[%s]", name);

    add_path_to_module(module, name, module_path);
    add_data_to_module(module, NULL, info_table, call_table);
    module->cmp_len = 0;
    module->flags |= MODULE_IS_REGISTERED | MODULE_IS_PREDEFINED;
}

const char *lily_import_current_root_dir(lily_state *s)
{
    lily_parse_state *parser = s->gs->parser;

    const char *current_root = parser->ims->source_module->root_dirname;
    const char *first_root = parser->main_module->root_dirname;
    const char *result = current_root + strlen(first_root);

    return result;
}

int lily_import_foreach_sys_dir(lily_state *s, const char *target,
        lily_sys_dir_func func)
{
    lily_import_state *ims = s->gs->parser->ims;

    if (ims->sys_dirs == NULL || ims->is_slashed_path)
        return 0;

    ims->import_type = imp_system;

    const char *iter = ims->sys_dirs;
    int result = 0;

    while (*iter != LILY_DIR_CHAR && result == 0) {
        ims->dirname = iter;
        result = func(s, target);
        iter = iter + strlen(iter) + 1;
    }

    /* Reset this in case the embedder wants to try more imports. */
    ims->dirname = NULL;
    return result;
}

void lily_import_use_local_dir(lily_state *s, const char *dirname)
{
    lily_import_state *ims = s->gs->parser->ims;

    ims->dirname = dirname;
    ims->import_type = imp_local;
}

void lily_import_use_package_dir(lily_state *s, const char *dirname)
{
    lily_import_state *ims = s->gs->parser->ims;

    ims->dirname = dirname;
    ims->import_type = imp_package;
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

    /* As a last resort, try system directories. */
    lily_import_foreach_sys_dir(s, target, lily_import_file_or_library);
}

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lily.h"
#include "lily_alloc.h"
#include "lily_import.h"
#include "lily_library.h"
#include "lily_opcode.h"
#include "lily_parser.h"
#include "lily_parser_data.h"
#include "lily_platform.h"
#include "lily_string_pile.h"
#include "lily_value.h"

#define NEED_IDENT(message) \
if (lex->token != tk_word) \
    lily_raise_syn(parser->raiser, message);

#define NEED_NEXT_IDENT(message) \
lily_next_token(lex); \
if (lex->token != tk_word) \
    lily_raise_syn(parser->raiser, message);

#define NEED_NEXT_TOK(expected) \
lily_next_token(lex); \
if (lex->token != expected) \
    lily_raise_syn(parser->raiser, "Expected '%s' here.", tokname(expected));

#define NEED_CURRENT_TOK(expected) \
if (lex->token != expected) \
    lily_raise_syn(parser->raiser, "Expected '%s' here.", tokname(expected));

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
extern lily_type *lily_unset_type;

/***
 *      ____       _
 *     / ___|  ___| |_ _   _ _ __
 *     \___ \ / _ \ __| | | | '_ \
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/
 *                          |_|
 */

/** This is the entry point to starting the interpreter and rewinding it. The
    following are done here.

    * Create raiser (it won't be used here).
    * Create symtab to receive prelude symbols.
    * Create the prelude module, to give to symtab.
    * Load the prelude module.
    * Initialize other parts of the interpreter.
    * Create the first module (parsed files/strings will become the root).
    * Create __main__ to receive toplevel code from the first module.

    All api functions use the initial vm so that the api does not need to know
    about the parser.
 **/
static void create_main_func(lily_parse_state *);
void lily_stdout_print(lily_vm_state *);
void lily_open_prelude_library(lily_parse_state *);

typedef struct {
    const char **table;
    const char *entry;
    lily_module_entry *m;
    lily_class *cls;
    lily_item *result;
    lily_module_entry *saved_active;
    uint16_t saved_generics;
    uint16_t index;
    uint32_t pad;
} lily_dyna_state;

typedef struct lily_rewind_state_
{
    lily_class *main_class_start;
    lily_var *main_var_start;
    lily_boxed_sym *main_boxed_start;
    lily_module_link *main_last_module_link;
    lily_module_entry *main_last_module;
    uint16_t line_num;
    uint16_t pending;
    uint8_t exit_status;
    uint8_t has_exited;
    uint16_t pad;
} lily_rewind_state;

void lily_init_pkg_prelude(lily_symtab *);

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
    conf->data = NULL;
    conf->extra_info = 0;
    conf->sandbox = 0;
    conf->use_sys_dirs = 0;
    conf->sys_dirs = LILY_CONFIG_SYS_DIRS_INIT;
}

/* This sets up the core of the interpreter. It's pretty rough around the edges,
   especially with how the parser is assigning into all sorts of various structs
   when it shouldn't. */
lily_state *lily_new_state(lily_config *config)
{
    lily_parse_state *parser = lily_malloc(sizeof(*parser));

    /* Start with the simple parts of parser. */
    parser->config = config;
    parser->current_class = NULL;
    parser->data_string_pos = 0;
    parser->doc = NULL;
    parser->flags = 0;
    parser->modifiers = 0;
    parser->module_start = NULL;
    parser->module_top = NULL;
    parser->spare_vars = NULL;

    /* These two are used for handling keyword arguments and paths that have
       been tried. The strings are stored next to each other in the pile, with
       the stack storing starting indexes. */
    parser->data_stack = lily_new_buffer_u16(4);
    parser->data_strings = lily_new_string_pile();

    /* Parser's msgbuf is used to build strings for import and errors. This is
       not shared anywhere. */
    parser->msgbuf = lily_new_msgbuf(64);

    /* These two hold import data and rewind state. */
    parser->ims = lily_malloc(sizeof(*parser->ims));
    parser->ims->path_msgbuf = lily_new_msgbuf(64);
    parser->ims->sys_dirs = NULL;

    parser->rs = lily_malloc(sizeof(*parser->rs));
    parser->rs->pending = 0;
    parser->rs->has_exited = 0;

    /* These two are simple and don't depend on other parts. */
    parser->expr = lily_new_expr_state();
    parser->generics = lily_new_generic_pool();

    /* The raiser is used by the remaining parts to launch errors. The parser
       shares this raiser with the first vm. Coroutine vms will get their own
       raiser which stops at their origin point. */
    parser->raiser = lily_new_raiser();

    /* The global state (gs) maps from any vm (origin or Coroutine) back to the
       parser. It's for api functions. */
    parser->vm = lily_new_vm_state(parser->raiser);
    parser->vm->gs->parser = parser;
    parser->vm->gs->gc_multiplier = config->gc_multiplier;
    parser->vm->gs->gc_threshold = config->gc_start;

    /* Make just the prelude module available. */
    lily_open_prelude_library(parser);

    if (parser->config->sandbox == 0)
        /* Now the other predefined modules. */
        lily_open_all_libraries(parser->vm);

    /* Make the symtab and load it. */
    parser->symtab = lily_new_symtab();
    lily_set_prelude(parser->symtab, parser->module_start);
    lily_init_pkg_prelude(parser->symtab);

    parser->lex = lily_new_lex_state(parser->raiser);

    /* Emitter is launched last since it shares the most with parser. */
    parser->emit = lily_new_emit_state(parser->symtab, parser->raiser);
    parser->tm = parser->emit->tm;
    parser->expr_strings = parser->emit->expr_strings;

    /* Emitter's parser is used for dynaloads and lambda parsing. */
    parser->emit->parser = parser;

    /* Cache the parts of lexer that are used frequently. */
    parser->expr->lex_linenum = &parser->lex->line_num;
    parser->expr->lex_tokstart = &parser->lex->token_start;
    parser->emit->lex_linenum = &parser->lex->line_num;

    /* Build the module that will hold `__main__`. */
    lily_module_entry *main_module = lily_ims_new_module(parser);

    parser->main_module = main_module;
    parser->symtab->active_module = parser->main_module;

    /* Create the `__main__` var and underlying function. */
    create_main_func(parser);

    return parser->vm;
}

static void free_docs(lily_doc_stack *d)
{
    if (d == NULL)
        return;

    char ***data = d->data;
    uint16_t i;

    for (i = 0;i < d->pos;i++) {
        char **c = data[i];

        lily_free(c[0]);
        lily_free(c);
    }

    lily_free(d->data);
    lily_free(d);
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

    /* This function's code is a pointer to emitter's code. NULL this to prevent
       a double free. */
    parser->toplevel_func->proto->code = NULL;

    /* The first module's path is a shallow copy, so NULL it too. */
    parser->main_module->path = NULL;

    /* Each of these deletes different parts, so order does not matter here. */
    lily_free_emit_state(parser->emit);
    lily_free_expr_state(parser->expr);
    lily_free_generic_pool(parser->generics);
    lily_free_lex_state(parser->lex);
    lily_free_raiser(parser->raiser);
    lily_free_symtab(parser->symtab);
    lily_free_vm(parser->vm);

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

    lily_var *var_iter = parser->spare_vars;

    while (var_iter) {
        lily_var *var_next = var_iter->next;

        lily_free(var_iter);
        var_iter = var_next;
    }

    lily_free_buffer_u16(parser->data_stack);
    lily_free_msgbuf(parser->ims->path_msgbuf);
    lily_free_msgbuf(parser->msgbuf);
    lily_free(parser->ims->sys_dirs);
    lily_free(parser->ims);
    lily_free(parser->rs);
    lily_free_string_pile(parser->data_strings);
    free_docs(parser->doc);
    lily_free(parser);
}

static void rewind_parser(lily_parse_state *parser)
{
    lily_u16_set_pos(parser->data_stack, 0);
    parser->data_string_pos = 0;
    parser->modifiers = 0;
    parser->current_class = NULL;

    /* Parser's flags are reset when the first content loads. */
    lily_module_entry *module_iter = parser->rs->main_last_module;

    while (module_iter) {
        /* Hide broken modules from being loaded in the next pass as though
           they weren't broken. */
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

/***
 *      ____
 *     |  _ \  ___   ___ ___
 *     | | | |/ _ \ / __/ __|
 *     | |_| | (_) | (__\__ \
 *     |____/ \___/ \___|___/
 *
 */

static void grow_docs(lily_doc_stack *d)
{
    uint16_t new_size = d->size * 2;
    char ***new_data = lily_realloc(d->data,
            sizeof(*new_data) * d->size * 2);

    d->data = new_data;
    d->size = new_size;
}

static void init_doc(lily_parse_state *parser)
{
    parser->flags |= PARSER_EXTRA_INFO;

    if (parser->doc)
        return;

    lily_doc_stack *d = lily_malloc(sizeof(*d));

    d->data = lily_malloc(4 * sizeof(*d->data));
    d->pos = 0;
    d->size = 4;
    parser->doc = d;
}

/* This takes pairs from parser's data stack and builds a string array. Unlike a
   typical string array where each element is a small string, this function
   builds a single backing string. This is done to make deletion easier (element
   zero is always the backing string), and to reduce the number of allocation
   calls.
   The array built is terminated by NULL at the end to allow safe looping.
   Parser's data stack and strings are fixed by this call at the end. */
static char **build_strings_by_data(lily_parse_state *parser,
        uint16_t arg_count, uint16_t key_start)
{
    lily_buffer_u16 *ds = parser->data_stack;

    /* If the first argument doesn't have a keyword, insert a zero at the start
       and push the string by 1. */
    uint16_t no_first_key = !!lily_u16_get(ds, key_start);
    uint16_t offset = lily_u16_get(ds, key_start + 1);
    uint16_t range = parser->data_string_pos - offset;
    char **keys = lily_malloc((arg_count + 1) * sizeof(*keys));

    /* There's no extra +1 because range includes the terminating zero. */
    char *block = lily_malloc((range + no_first_key) * sizeof(*block));
    char *source = parser->data_strings->buffer;

    /* Deletion is easier if [0] is always the backing string. If the first
       argument doesn't have a keyword, then the block starts with "\0". */
    block[0] = '\0';
    memcpy(block + no_first_key, source + offset, range * sizeof(*block));

    /* Use the "\0" at the end of the block for empty keys. */
    char *empty = block + range + no_first_key - 1;
    uint16_t key_end = lily_u16_pos(parser->data_stack);
    uint16_t i;

    for (i = 1;i < arg_count;i++)
        keys[i] = empty;

    keys[0] = block;
    keys[arg_count] = NULL;

    for (i = key_start;i < key_end;i += 2) {
        uint16_t arg_pos = lily_u16_get(ds, i);
        uint16_t string_pos = lily_u16_get(ds, i + 1);

        /* Translate data string position to block position. */
        uint16_t target_pos = string_pos - offset + no_first_key;

        keys[arg_pos] = block + target_pos;
    }

    parser->data_string_pos = lily_u16_get(ds, key_start + 1);
    lily_u16_set_pos(ds, key_start);
    return keys;
}

static uint16_t build_doc_data(lily_parse_state *parser, uint16_t arg_count)
{
    lily_doc_stack *d = parser->doc;

    if (d->pos == d->size)
        grow_docs(d);

    uint16_t start = lily_u16_pos(parser->data_stack) - (arg_count * 2);
    char **text = build_strings_by_data(parser, arg_count, start);
    uint16_t result = d->pos;

    d->data[d->pos] = text;
    d->pos++;

    return result;
}

static uint16_t store_docblock(lily_parse_state *parser)
{
    parser->flags &= ~PARSER_HAS_DOCBLOCK;
    return build_doc_data(parser, 1);
}

void lily_pa_add_data_string(lily_parse_state *parser, const char *to_add)
{
    lily_u16_write_1(parser->data_stack, parser->data_string_pos);
    lily_sp_insert(parser->data_strings, to_add, &parser->data_string_pos);
}

static void save_docblock(lily_parse_state *parser)
{
    if (parser->flags & PARSER_EXTRA_INFO) {
        lily_u16_write_1(parser->data_stack, 0);
        lily_pa_add_data_string(parser, parser->lex->label);
        parser->flags |= PARSER_HAS_DOCBLOCK;
    }
}

static void write_generics(lily_parse_state *parser, uint16_t where)
{
    lily_generic_pool *gp = parser->generics;
    lily_msgbuf *msgbuf = lily_mb_flush(parser->msgbuf);

    /* Since generics are required to be in letter order, introspect only needs
       the total available. It can walk the generic pool later on. */
    char range = (char)(gp->scope_end - gp->scope_start);

    lily_mb_add_char(msgbuf, range);
    lily_u16_write_1(parser->data_stack, where);
    lily_pa_add_data_string(parser, lily_mb_raw(msgbuf));
}

static uint16_t store_enum_docblock(lily_parse_state *parser)
{
    if ((parser->flags & PARSER_HAS_DOCBLOCK) == 0) {
        lily_u16_write_1(parser->data_stack, 0);
        lily_pa_add_data_string(parser, "");
    }
    else
        parser->flags &= ~PARSER_HAS_DOCBLOCK;

    write_generics(parser, 1);
    return build_doc_data(parser, 2);
}

static void set_definition_doc(lily_parse_state *parser)
{
    lily_var *define_var = parser->emit->scope_block->scope_var;
    lily_var *var_iter = parser->symtab->active_module->var_chain;
    uint16_t count = parser->emit->scope_block->var_count;
    uint16_t offset = 0;
    int is_ctor = 0;
    uint16_t i;

    if ((parser->flags & PARSER_HAS_DOCBLOCK) == 0) {
        lily_u16_write_1(parser->data_stack, 0);
        lily_pa_add_data_string(parser, "");
    }
    else
        parser->flags &= ~PARSER_HAS_DOCBLOCK;

    if (define_var->parent &&
        (define_var->flags & VAR_IS_STATIC) == 0) {
        if (define_var->name[0] != '<') {
            /* This is a non-static class method. The self of class methods is
               held in a storage instead of a var, but is in the type. An extra
               space is added later so that parameters and types line up. */
            offset = 1;
            count++;
        }
        else
            is_ctor = 1;
    }

    for (i = count;i > offset;i--) {
        lily_u16_write_1(parser->data_stack, i);
        lily_pa_add_data_string(parser, var_iter->name);
        var_iter = var_iter->next;
    }

    if (offset) {
        lily_u16_write_1(parser->data_stack, i);
        lily_pa_add_data_string(parser, "");
    }

    write_generics(parser, count + 1);
    define_var->doc_id = build_doc_data(parser, count + 2);

    if (is_ctor)
        /* Give the info to the class too since it has the docblock. */
        define_var->parent->doc_id = define_var->doc_id;
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
static lily_module_entry *find_run_module_dynaload(lily_parse_state *,
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
        lily_module_entry *prelude = symtab->prelude_module;

        result = lily_find_class(prelude, name);

        if (result == NULL)
            result = find_run_class_dynaload(parser, prelude, name);
    }

    if (result == NULL)
        result = lily_find_class(m, name);

    if (result == NULL && m->info_table)
        result = find_run_class_dynaload(parser, m, name);

    return result;
}

lily_class *find_dl_class_in(lily_parse_state *parser, lily_module_entry *m,
        const char *name)
{
    lily_class *result = lily_find_class(m, name);

    if (result == NULL && m->info_table)
        result = find_run_class_dynaload(parser, m, name);

    return result;
}

lily_module_entry *find_dl_module_in(lily_parse_state *parser,
        lily_module_entry *m, const char *name)
{
    lily_module_entry *result = lily_find_module(m, name);

    if (result == NULL && m->info_table)
        result = find_run_module_dynaload(parser, m, name);

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

static lily_function_val *make_new_function(lily_parse_state *parser,
        lily_var *var)
{
    lily_function_val *f = lily_malloc(sizeof(*f));
    lily_module_entry *m = parser->symtab->active_module;
    lily_proto *proto = lily_emit_new_proto(parser->emit, m->path, var);

    /* This won't get a ref bump from being moved/assigned since all functions
       are marked as literals. Start at 1 ref, not 0. */
    f->refcount = 1;
    f->foreign_func = NULL;
    f->code = NULL;
    f->num_upvalues = 0;
    f->upvalues = NULL;
    f->gc_entry = NULL;
    f->cid_table = m->cid_table;
    f->proto = proto;

    lily_value *v = lily_malloc(sizeof(*v));
    v->flags = V_FUNCTION_FLAG | V_FUNCTION_BASE;
    v->value.function = f;

    lily_new_function_literal(parser->symtab, var, v);
    return f;
}

static void put_keywords_in_target(lily_parse_state *parser, lily_item *target,
        char **keys)
{
    if (target->item_kind == ITEM_DEFINE) {
        lily_var *var = (lily_var *)target;
        lily_proto *p = lily_emit_proto_for_var(parser->emit, var);

        p->keywords = keys;
    }
    else {
        lily_variant_class *c = (lily_variant_class *)target;

        c->keywords = keys;
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
        }
        else {
            lily_free(var_iter->name);
            var_iter->next = parser->spare_vars;
            parser->spare_vars = var_iter;
        }

        count--;
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
static lily_var *new_var(lily_parse_state *parser, const char *name,
        uint16_t line_num)
{
    lily_var *var = parser->spare_vars;

    if (var == NULL)
        var = lily_malloc(sizeof(*var));
    else
        parser->spare_vars = var->next;

    var->item_kind = ITEM_VAR;
    var->name = lily_malloc((strlen(name) + 1) * sizeof(*var->name));
    strcpy(var->name, name);
    var->line_num = line_num;
    var->shorthash = shorthash_for_name(name);
    var->closure_spot = UINT16_MAX;
    var->doc_id = UINT16_MAX;
    var->type = lily_question_type;
    var->next = NULL;
    var->parent = NULL;

    return var;
}

static lily_var *new_constant_var(lily_parse_state *parser, const char *name,
        uint16_t line_num)
{
    lily_var *var = new_var(parser, name, line_num);
    lily_module_entry *m = parser->symtab->active_module;

    /* Constants get their id from the literal they're assigned to. */
    var->item_kind = ITEM_CONSTANT;
    var->function_depth = 1;
    var->reg_spot = 0;
    var->flags = VAR_IS_READONLY;
    var->next = m->var_chain;
    m->var_chain = var;

    return var;
}

/* Create a new var that must be local. Use this in cases where the target is
   going to have some data extracted into it. In situations such as the except
   clause of try, the target register has to be local. */
static lily_var *new_local_var(lily_parse_state *parser, const char *name,
        uint16_t line_num)
{
    lily_var *var = new_var(parser, name, line_num);
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

static inline lily_var *new_typed_local_var(lily_parse_state *parser,
        lily_type *type, const char *name, uint16_t line_num)
{
    lily_var *var = new_local_var(parser, name, line_num);

    var->type = type;
    return var;
}

static lily_var *new_global_var(lily_parse_state *parser, const char *name,
        uint16_t line_num)
{
    lily_var *var = new_var(parser, name, line_num);
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
    if (line_num != 0) {
        lily_push_unit(parser->vm);
        parser->emit->block->var_count++;
    }

    return var;
}

static lily_var *new_define_var(lily_parse_state *parser, const char *name,
        uint16_t line_num)
{
    lily_var *var = new_var(parser, name, line_num);
    lily_module_entry *m = parser->symtab->active_module;

    /* Symtab sets reg_spot when the function is made. */
    var->item_kind = ITEM_DEFINE;
    var->function_depth = 1;
    var->flags = VAR_IS_READONLY;
    var->next = m->var_chain;
    m->var_chain = var;

    if (line_num)
        parser->emit->block->var_count++;

    return var;
}

static lily_var *new_method_var(lily_parse_state *parser, lily_class *parent,
        const char *name, uint16_t modifiers, uint16_t line_num)
{
    lily_var *var = new_var(parser, name, line_num);

    /* Symtab sets reg_spot when the function is made. */
    var->item_kind = ITEM_DEFINE;
    var->function_depth = 1;
    var->flags = VAR_IS_READONLY | modifiers;
    var->parent = parent;
    var->next = (lily_var *)parent->members;
    parent->members = (lily_named_sym *)var;

    return var;
}

static lily_var *declare_constant(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var = find_active_var(parser, lex->label);

    if (var)
        error_var_redeclaration(parser, var);

    var = new_constant_var(parser, lex->label, lex->line_num);

    if (parser->flags & PARSER_HAS_DOCBLOCK)
        var->doc_id = store_docblock(parser);

    lily_next_token(lex);
    return var;
}

/* This creates a new local var using the current identifier as the name. */
static lily_var *declare_local_var(lily_parse_state *parser, lily_type *type)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var = find_active_var(parser, lex->label);

    if (var)
        error_var_redeclaration(parser, var);

    var = new_typed_local_var(parser, type, lex->label, lex->line_num);
    lily_next_token(lex);
    return var;
}

static lily_var *declare_match_var(lily_parse_state *parser, const char *name,
        uint16_t line_num)
{
    lily_var *var = find_active_var(parser, name);

    /* Vars with a NULL type here are match vars that have gone out of scope. */
    if (var && var->type != NULL)
        error_var_redeclaration(parser, var);
    else if (strcmp(name, "_") == 0)
        /* Prevent parser from trying to use this skipped var name. */
        name = "\0";

    var = new_local_var(parser, name, line_num);
    var->type = lily_question_type;
    var->item_kind = ITEM_MATCH_TEMP;
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
        var = new_local_var(parser, lex->label, lex->line_num);
    else
        var = new_global_var(parser, lex->label, lex->line_num);

    if (parser->flags & PARSER_HAS_DOCBLOCK)
        var->doc_id = store_docblock(parser);

    lily_next_token(lex);
    return var;
}

static void create_main_func(lily_parse_state *parser)
{
    lily_type_maker *tm = parser->emit->tm;
    lily_lex_state *lex = parser->lex;

    lily_tm_add(tm, lily_unit_type);
    lily_type *main_type = lily_tm_make_call(tm, 0,
            parser->symtab->function_class, 1);

    /* __main__'s line number should be 1 since it's not a foreign function.
       Since lexer hasn't read in the first line (it might be broken and raiser
       isn't ready yet), the line number is at 0. */
    lex->line_num = 1;

    lily_var *main_var = new_define_var(parser, "__main__", lex->line_num);
    lily_function_val *f = make_new_function(parser, main_var);

    lex->line_num = 0;
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
static int constant_by_name(const char *);
static lily_prop_entry *declare_property(lily_parse_state *, uint16_t);
static void simple_expression(lily_parse_state *);
static int keyword_by_name(const char *);
static void collect_keyarg(lily_parse_state *, uint16_t, int *);

/* Type collection uses this to find the class. It could be a simple predefined
   class or a generic. If there are module(s), walk them. If there's a dynaload
   of a class (or a module!), walk those two.
   Caller must ensure the token is on the first identifier. */
static lily_class *resolve_class_name(lily_parse_state *parser)
{
    lily_symtab *symtab = parser->symtab;
    lily_lex_state *lex = parser->lex;
    lily_module_entry *m = symtab->active_module;
    char *name = lex->label;

    /* Try the prelude and generics first. */
    lily_class *result = find_dl_class_in(parser, symtab->prelude_module, name);

    if (result == NULL) {
        if (name[1] == '\0')
            result = (lily_class *)lily_gp_find(parser->generics, name);

        if (result == NULL)
            result = find_dl_class_in(parser, m, name);
    }

    if (result)
        return result;

    m = find_dl_module_in(parser, m, name);

    while (m) {
        NEED_NEXT_TOK(tk_dot)
        NEED_NEXT_IDENT("Expected a symbol name (module, class, etc.) here.")
        name = lex->label;

        result = find_dl_class_in(parser, m, name);

        if (result)
            return result;

        m = find_dl_module_in(parser, m, name);
    }

    if (result == NULL)
        lily_raise_syn(parser->raiser, "Class '%s' does not exist.",
                lex->label);

    return result;
}

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
    lily_lex_state *lex = parser->lex;

    lily_es_optarg_save(parser->expr);
    lily_es_push_assign_to(parser->expr, sym);

    if (lex->token == tk_equal) {
        lily_next_token(lex);
        simple_expression(parser);
        return;
    }

    if (parser->flags & PARSER_IN_MANIFEST) {
        /* Allow manifest mode to omit a value. This is useful in rare cases
           where the default value is difficult or impossible to express in a
           simple Lily expression. Optarg parsing expects an assignment, so give
           it one that will never fail. */
        sym->flags &= ~SYM_NOT_INITIALIZED;
        lily_es_push_local_var(parser->expr, (lily_var *)sym);
    }
    else
        lily_raise_syn(parser->raiser,
                "Expected '=', then an optional argument value.");
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
        uint16_t key_id = key_type->cls_id;

        /* Parser allows generic keys so that Hash methods work, but also allows
           users to have definitions with generic keys. Emitter will make sure
           when building a Hash that the key is always one of the first two
           types mentioned here. */
        if (key_id != LILY_ID_INTEGER &&
            key_id != LILY_ID_STRING &&
            key_id != LILY_ID_GENERIC)
            lily_raise_syn(parser->raiser, "'^T' is not a valid key for Hash.",
                    key_type);
    }
}

/* These are flags used by argument collection. They start high so that type and
   class flags don't collide with them. */
#define F_AUTO_KEYARG       0x020000
#define F_SCOOP_OK          0x040000
#define F_IS_FORWARD        0x080000
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

    NEED_IDENT("Expected a definition name here.")

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
    uint16_t modifiers = 0;

    NEED_IDENT("Expected an argument name here.")

    if (lex->label[0] == 'p') {
        int keyword = keyword_by_name(lex->label);

        if (keyword == KEY_PRIVATE)
            modifiers = SYM_SCOPE_PRIVATE;
        else if (keyword == KEY_PROTECTED)
            modifiers = SYM_SCOPE_PROTECTED;
        else if (keyword == KEY_PUBLIC)
            modifiers = SYM_SCOPE_PUBLIC;

        if (modifiers) {
            lily_next_token(lex);

            if (lex->token != tk_word ||
                strcmp(lex->label, "var") != 0) {
                lily_raise_syn(parser->raiser,
                        "Expected 'var' after scope was given.");
            }

            NEED_NEXT_TOK(tk_prop_word)
        }
    }
    else if (lex->label[0] == 'v' &&
             strcmp(lex->label, "var") == 0) {
        lily_raise_syn(parser->raiser,
                "Constructor var declaration must start with a scope.");
    }

    if (*flags & F_AUTO_KEYARG) {
        uint16_t start = lily_u16_pop(parser->data_stack);

        collect_keyarg(parser, start, flags);
        *flags &= ~F_AUTO_KEYARG;
    }

    if (modifiers) {
        prop = declare_property(parser, modifiers);

        /* Properties aren't assigned until all optargs are done. */
        prop->flags |= SYM_NOT_INITIALIZED;
        var = new_local_var(parser, "", 0);
    }
    else
        var = declare_local_var(parser, NULL);

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
        NEED_IDENT("Expected a class name here.")
    }

    if (cls->item_kind & ITEM_IS_VARIANT)
        lily_raise_syn(parser->raiser,
                "Variant types not allowed in a declaration.");

    if (cls->generic_count == 0)
        result = cls->self_type;
    else if (cls->id != LILY_ID_FUNCTION) {
        NEED_NEXT_TOK(tk_left_bracket)
        uint16_t i = 0;
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
                        "Expected either ',' <type> or ']' here.");
        }

        result = lily_tm_make(parser->tm, cls, i);
        ensure_valid_type(parser, result);
    }
    else {
        NEED_NEXT_TOK(tk_left_parenth)
        lily_next_token(lex);
        int arg_flags = flags & F_SCOOP_OK;
        uint16_t i = 0;
        uint16_t result_pos = parser->tm->pos;

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

        uint16_t call_flags = (uint16_t)(arg_flags & F_NO_COLLECT);

        result = lily_tm_make_call(parser->tm, call_flags, cls, i + 1);
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
    char ch = 'A' + (char)lily_gp_num_in_scope(parser->generics);
    char name[] = {ch, '\0'};

    while (1) {
        NEED_NEXT_IDENT("Expected a name for a generic here.")

        if (lex->label[0] != ch || lex->label[1] != '\0') {
            if (ch == 'Z' + 1)
                lily_raise_syn(parser->raiser, "Too many generics.");
            else {
                lily_raise_syn(parser->raiser,
                        "Generics must be in order (expected %s here).",
                        name);
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
                    "Expected either ',' <type> or ']' here.");

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

typedef lily_type *(*collect_fn)(lily_parse_state *, int *);

static void error_forward_decl_type(lily_parse_state *parser, lily_var *var,
        lily_type *got)
{
    lily_raise_syn(parser->raiser,
            "Declaration does not match prior forward declaration at line %d.\n"
            "Expected: ^T\n"
            "Received: ^T", var->line_num, var->type, got);
}

static void collect_keyarg(lily_parse_state *parser, uint16_t pos,
        int *arg_flags)
{
    lily_buffer_u16 *ds = parser->data_stack;
    uint16_t stop = lily_u16_pos(ds) - 1;
    lily_lex_state *lex = parser->lex;
    char *name = lex->label;
    int load_next = (lex->token == tk_keyword_arg);

    if (*arg_flags & F_IS_FORWARD)
        /* Not worth the trouble. */
        lily_raise_syn(parser->raiser,
                "Forward declarations cannot have keyword arguments.");

    if (name[0] == '_' && name[1] == '\0' &&
        lex->token == tk_keyword_arg) {
        /* `:_` means use the parameter name as the keyword name. The third
           check prevents `:_ public var @_` from entering here. */

        if (*arg_flags & F_COLLECT_VARIANT)
            lily_raise_syn(parser->raiser,
                    "Variants cannot use keyword argument shorthand.");

        lily_next_token(lex);

        if (lex->token != tk_word)
            lily_raise_syn(parser->raiser, "Expected a name here.");

        /* This could be `:_ public var @<name>`, or just `:_ <name>`. Class
           argument collection will call this function again when it has the
           name. Save the start for it to peel off. */
        if (*arg_flags & F_COLLECT_CLASS) {
            *arg_flags |= F_AUTO_KEYARG;
            lily_u16_write_1(ds, pos);
            return;
        }

        /* (Unlikely) next token may have moved lex->label, so must resync. */
        name = lex->label;
        load_next = 0;
    }

    for (;pos != stop;pos += 2) {
        uint16_t key_pos = lily_u16_get(ds, pos + 1);
        char *keyarg = lily_sp_get(parser->data_strings, key_pos);

        if (strcmp(keyarg, name) == 0)
            lily_raise_syn(parser->raiser,
                    "A keyword named :%s has already been declared.", name);
    }

    lily_pa_add_data_string(parser, name);

    if (load_next)
        lily_next_token(lex);
}

static void collect_call_args(lily_parse_state *parser, void *target,
        int arg_flags)
{
    lily_lex_state *lex = parser->lex;
    /* -1 because Unit is injected at the front beforehand. */
    uint16_t result_pos = parser->tm->pos - 1;
    uint16_t i = 0;
    uint16_t keyarg_start = lily_u16_pos(parser->data_stack);
    collect_fn arg_collect = NULL;

    if ((arg_flags & F_COLLECT_DEFINE)) {
        if (parser->emit->block->self) {
            i++;
            result_pos--;
        }

        lily_var *var = (lily_var *)target;

        if (parser->flags & PARSER_IN_MANIFEST)
            arg_flags |= F_SCOOP_OK;

        if ((var->flags & SYM_IS_FORWARD) == 0)
            arg_collect = get_define_arg;
        else {
            arg_collect = get_nameless_arg;
            arg_flags |= F_IS_FORWARD;
        }
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
                    "() must be removed if there are no arguments inside.");

        while (1) {
            if (lex->token == tk_keyword_arg) {
                lily_u16_write_1(parser->data_stack, i);
                collect_keyarg(parser, keyarg_start, &arg_flags);
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
                        "Expected either ',' or ')' here.");
        }
    }

    if (lex->token == tk_colon &&
        (arg_flags & (F_COLLECT_VARIANT | F_COLLECT_CLASS)) == 0) {
        lily_next_token(lex);
        if (lex->token == tk_word && strcmp(lex->label, "self") == 0) {
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
                        "Unit return type does not need to be explicitly stated.");
            }

            /* Use the arg flags so that dynaload can use $1 in the return. */
            lily_tm_insert(parser->tm, result_pos, result_type);
        }
    }

    if (keyarg_start != lily_u16_pos(parser->data_stack)) {
        char **keys = build_strings_by_data(parser, i, keyarg_start);

        put_keywords_in_target(parser, target, keys);
    }

    lily_type *t = lily_tm_make_call(parser->tm, arg_flags & F_NO_COLLECT,
            parser->symtab->function_class, i + 1);

    if ((arg_flags & F_COLLECT_VARIANT) == 0) {
        lily_var *var = (lily_var *)target;
        if (var->type != t &&
            var->type != lily_question_type)
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

    When a module is first loaded (except for prelude), no symbols are loaded.
    Instead, the interpreter waits until a symbol is explicitly specified. When
    that happens, a lookup is done and this mechanism is used as a fallback.

    Notice how the above works without running native code. That's really
    important, because dynaload needs to work transparently whenever a symbol is
    requested.

    This mechanism was written with predefined modules in mind, notably the
    prelude module. At well over 100 records, this makes a considerable
    difference in startup memory cost. */

static void parse_value_variant(lily_parse_state *, lily_variant_class *);
static void parse_variant_header(lily_parse_state *, lily_variant_class *);
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
    lily_module_entry *prelude = parser->module_start;

    while (counter < stop) {
        if (cid_table[counter] == 0) {
            lily_class *cls = lily_find_class(prelude, cid_entry);

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
    uint16_t index = ds->index;

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
    dyna_save(parser, ds);

    lily_class *cls = lily_new_class(parser->symtab, dyna_get_name(ds), 0);

    /* These can have closures, so anything holding them needs a tag. */
    if (ds->m->flags & MODULE_IS_PREDEFINED &&
        strcmp(dyna_get_name(ds), "Coroutine") == 0) {
        cls->flags |= CLS_GC_TAGGED;

        /* Resumption may need this. Doing this here allows exception dynaload
           to stay in the prelude. */
        (void) find_dl_class_in(parser, ds->m, "CoError");
    }

    cls->item_kind = ITEM_CLASS_FOREIGN;
    cls->dyna_start = ds->index;
    collect_generics_for(parser, cls);
    dyna_restore(parser, ds);
    ds->result = (lily_item *)cls;
}

static void dynaload_var(lily_parse_state *parser, lily_dyna_state *ds)
{
    dyna_save(parser, ds);
    lily_next_token(parser->lex);

    lily_module_entry *m = ds->m;
    lily_type *type = get_type_raw(parser, 0);
    lily_var *var = new_global_var(parser, dyna_get_name(ds), 0);

    /* The initial vm has a function above main for storing globals that's never
       truly exited. Var loaders work by pushing a new value in toplevel space.
       Make sure the identity table is up-to-date before running the loader in
       case the loader needs it. */

    var->type = type;

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

static void make_boolean_constant(lily_parse_state *parser, lily_var *var,
        int val)
{
    var->constant_value = (int16_t)val;
    var->flags |= VAR_INLINE_CONSTANT;
    var->type = parser->symtab->boolean_class->self_type;
}

#define CAN_INLINE_INTEGER(v_) (v_ >= INT16_MIN && v_ <= INT16_MAX)

static void make_integer_constant(lily_parse_state *parser, lily_var *var,
        int64_t val)
{
    if (CAN_INLINE_INTEGER(val)) {
        var->constant_value = (int16_t)val;
        var->flags |= VAR_INLINE_CONSTANT;
        var->type = parser->symtab->integer_class->self_type;
    }
    else {
        lily_type *t;
        lily_literal *lit = lily_get_integer_literal(parser->symtab, &t, val);

        var->reg_spot = lit->reg_spot;
        var->type = t;
    }
}

static void dynaload_constant(lily_parse_state *parser, lily_dyna_state *ds)
{
    dyna_save(parser, ds);
    lily_next_token(parser->lex);

    lily_type *type = get_type_raw(parser, 0);
    lily_var *var = new_constant_var(parser, dyna_get_name(ds), 0);

    /* Constants just push a simple builtin value, so leave the tables alone. */
    var->type = type;

    lily_foreign_func constant_loader = ds->m->call_table[ds->index];

    constant_loader(parser->vm);

    lily_value *v = lily_stack_get_top(parser->vm);
    uint16_t cls_id = type->cls_id;
    lily_symtab *symtab = parser->symtab;
    lily_literal *lit = NULL;

    if (cls_id == LILY_ID_INTEGER) {
        make_integer_constant(parser, var, lily_as_integer(v));
        lit = NULL;
    }
    else if (cls_id == LILY_ID_DOUBLE)
        lit = lily_get_double_literal(symtab, &type, lily_as_double(v));
    else if (cls_id == LILY_ID_STRING)
        lit = lily_get_string_literal(symtab, &type, lily_as_string_raw(v));
    else if (cls_id == LILY_ID_BOOLEAN) {
        make_boolean_constant(parser, var, lily_as_boolean(v));
        lit = NULL;
    }
    else if (cls_id == LILY_ID_BYTESTRING) {
        lily_bytestring_val *b = lily_as_bytestring(v);

        lit = lily_get_bytestring_literal(parser->symtab, &type, b->string,
                b->size);
    }

    if (lit)
        var->reg_spot = lit->reg_spot;

    lily_stack_drop_top(parser->vm);
    dyna_restore(parser, ds);
    ds->result = (lily_item *)var;
}

/* The vm expects certain predefined classes to have specific ids. This is
   called when dynaload sees a predefined class. */
static void fix_predefined_class_id(lily_parse_state *parser, lily_class *cls)
{
    char *name = cls->name;
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

    parser->symtab->next_class_id--;
    cls->id = new_id;
}

static void fix_option_result_class_ids(lily_class *enum_cls)
{
    lily_named_sym *first = enum_cls->members;
    lily_named_sym *second = first->next;
    uint16_t id;

    /* It's going to be one of these two. */
    if (enum_cls->name[0] == 'O')
        id = LILY_ID_OPTION;
    else
        id = LILY_ID_RESULT;

    /* Result's variants are in order, but Option's variants are reversed. A
       little math is used to get the right ids. */
    enum_cls->id = id;
    first->id = id + (id == LILY_ID_RESULT) + 1;
    second->id = id + (id == LILY_ID_OPTION) + 1;
}

static void dynaload_enum(lily_parse_state *parser, lily_dyna_state *ds)
{
    dyna_save(parser, ds);

    lily_lex_state *lex = parser->lex;
    lily_class *enum_cls = lily_new_enum_class(parser->symtab,
            dyna_get_name(ds), 0);

    enum_cls->dyna_start = ds->index;
    collect_generics_for(parser, enum_cls);

    int is_value_enum = (lex->token == INHERITANCE_TOKEN);

    if (is_value_enum) {
        /* Integer is the only valid option, so assume it's that. */
        enum_cls->parent = parser->symtab->integer_class;
        enum_cls->flags |= CLS_IS_BASIC_NUMBER | CLS_IS_HAS_VALUE;
    }

    /* Enums are followed by methods, then variants. Flat enums will write an
       offset that goes to the first enum, whereas scoped enums skip to the next
       toplevel entry. */
    if (dyna_check_next(ds) != 'V')
        enum_cls->item_kind = ITEM_ENUM_SCOPED;

    dyna_iter_past_methods(ds);

    lily_type *empty_type = lily_tm_build_empty_variant_type(parser->tm,
            enum_cls);

    do {
        lily_variant_class *variant = lily_new_variant_class(enum_cls,
                dyna_get_name(ds), 0);

        lily_lexer_load(lex, et_shallow_string, dyna_get_body(ds));
        lily_next_token(lex);

        if (is_value_enum == 0) {
            if (lex->token == tk_left_parenth)
                parse_variant_header(parser, variant);
            else
                variant->build_type = empty_type;
        }
        else
            parse_value_variant(parser, variant);

        lily_pop_lex_entry(lex);
        dyna_iter_next(ds);
    } while (dyna_record_type(ds) == 'V');

    if (ds->m == parser->module_start)
        fix_option_result_class_ids(enum_cls);
    else
        lily_fix_enum_variant_ids(parser->symtab, enum_cls);

    lily_fix_enum_type_ids(enum_cls);
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

    if (lex->token == INHERITANCE_TOKEN) {
        lily_next_token(lex);

        lily_class *parent = find_or_dl_class(parser, ds->m, lex->label);

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

        lily_add_class_property(cls, type, dyna_get_name(ds), 0, modifiers);
        lily_pop_lex_entry(lex);
        dyna_iter_next(ds);
    } while (1);

    /* Make sure the constructor loads too. Parts like inheritance will call for
       the class to dynaload, but (reasonably) expect <new> to be visible. */
    try_dynaload_method(parser, cls, "<new>");
    dyna_restore(parser, ds);
    ds->result = (lily_item *)cls;
}

static void dynaload_module(lily_parse_state *parser, lily_dyna_state *ds)
{
    lily_module_entry *m = ds->m;
    lily_foreign_func module_loader = m->call_table[ds->index];

    /* Clear off prior import data, or the last module will be returned. */
    parser->ims->last_import = NULL;

    /* This will call lily_import_library_data with the necessary tables. */
    module_loader(parser->vm);
    ds->result = (lily_item *)parser->ims->last_import;
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

    make_new_function(parser, var);
    var->flags |= VAR_IS_FOREIGN_FUNC;
    collect_generics_for(parser, NULL);
    lily_tm_add(parser->tm, lily_unit_type);
    collect_call_args(parser, var, F_COLLECT_DYNALOAD);

    lily_value *v = lily_literal_at(parser->symtab, var->reg_spot);
    lily_function_val *f = v->value.function;

    f->foreign_func = m->call_table[ds->index];
    f->reg_count = var->type->subtype_count;
    dyna_restore(parser, ds);
    ds->result = (lily_item *)var;
}

static void dynaload_fail(lily_parse_state *parser, lily_dyna_state *ds)
{
    (void)parser;
    /* Either the end (Z) or an unknown record. Give up either way. */
    ds->result = NULL;
}

static lily_item *run_dynaload(lily_parse_state *parser,
        lily_dyna_state *ds)
{
    char rec = dyna_record_type(ds);
    void (*fn)(lily_parse_state *, lily_dyna_state *);

    switch (rec) {
        case 'C': fn = dynaload_foreign; break;
        case 'E': fn = dynaload_enum; break;
        case 'F': fn = dynaload_function; break;
        case 'N': fn = dynaload_native; break;
        case 'R': fn = dynaload_var; break;
        case 'V': fn = dynaload_variant; break;
        case 'M': fn = dynaload_module; break;
        case 'O': fn = dynaload_constant; break;
        default: fn = dynaload_fail; break;
    }

    fn(parser, ds);
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

    if (result && (result->item_kind & (ITEM_IS_CLASS | ITEM_IS_ENUM)) == 0)
        result = NULL;

    return (lily_class *)result;
}

static lily_module_entry *find_run_module_dynaload(lily_parse_state *parser,
        lily_module_entry *m, const char *name)
{
    lily_dyna_state ds;

    if (dyna_find_toplevel_item(&ds, m, name) == 0 ||
        dyna_record_type(&ds) != 'M')
        return NULL;

    dynaload_module(parser, &ds);
    return (lily_module_entry *)ds.result;
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
/* Want a value, but don't write about expecting a value in errors. */
#define ST_TOPLEVEL             5

/* Normally, the next token is pulled up after an expression_* helper has been
   called. If this is or'd onto the state, then it's assumed that the next token
   has already been pulled up.
   Dispatch functions will never see this. */
#define ST_FORWARD              0x8

static int constant_by_name(const char *name)
{
    int i;
    uint64_t shorthash = shorthash_for_name(name);

    for (i = 0;i < CONST_BAD_ID;i++) {
        if (constants[i].shorthash == shorthash &&
            strcmp(constants[i].name, name) == 0)
            return i;
        else if (constants[i].shorthash > shorthash)
            break;
    }

    return CONST_BAD_ID;
}

static void verify_self_access(lily_parse_state *parser, uint16_t flag)
{
    lily_block *block = parser->emit->scope_block;

    if ((block->flags & flag) ||
        lily_emit_can_use_self(parser->emit, flag))
        return;

    while (block->block_type == block_lambda)
        block = block->prev_scope_block;

    char *what = "";

    if (flag & SELF_KEYWORD) {
        if (block->scope_var->flags & VAR_IS_STATIC)
            what = "Static methods cannot use 'self'.";
        else if (block->block_type == block_class)
            what = "Class constructors cannot use 'self'.";
        else
            what = "Cannot use 'self' outside of a class or enum.";
    }
    else if (flag & SELF_METHOD) {
        if (block->scope_var->flags & VAR_IS_STATIC)
            what = "Static methods cannot call instance methods.";
        else
            what = "Class constructors cannot call non-static methods.";
    }
    else
        what = "Cannot use a class property here.";

    lily_raise_syn(parser->raiser, what);
}

static void push_integer(lily_parse_state *parser, int64_t value)
{
    if (CAN_INLINE_INTEGER(value)) {
        lily_es_push_integer(parser->expr, (int16_t)value);
        return;
    }

    lily_type *t;
    lily_literal *lit = lily_get_integer_literal(parser->symtab, &t, value);

    lily_es_push_literal(parser->expr, t, lit->reg_spot);
}

static void push_string(lily_parse_state *parser, const char *str)
{
    lily_type *t;
    lily_literal *lit = lily_get_string_literal(parser->symtab, &t, str);

    lily_es_push_literal(parser->expr, t, lit->reg_spot);
}

static void push_unit(lily_parse_state *parser)
{
    lily_literal *lit = lily_get_unit_literal(parser->symtab);

    lily_es_push_literal(parser->expr, lily_unit_type, lit->reg_spot);
}

/* There's this annoying problem where 1-1 can be 1 - 1 or 1 -1. This is called
   if an operator is wanted but a digit is given instead. It checks to see if
   the numeric token can be broken up into an operator and a value, instead of
   just an operator. */
static int maybe_digit_fixup(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    int result = 0;
    int is_positive = lex->n.integer_val >= 0;

    if (lily_lexer_digit_rescan(lex)) {
        if (is_positive)
            lily_es_push_binary_op(parser->expr, tk_plus);
        else
            lily_es_push_binary_op(parser->expr, tk_minus);

        result = 1;
    }

    return result;
}

static void push_dir_constant(lily_parse_state *parser)
{
    lily_module_entry *module = parser->symtab->active_module;
    char *dir = lily_ims_dir_from_path(module->path);
    const char *push_dir = "." LILY_PATH_SLASH;

    if (dir[0] != '\0') {
        lily_msgbuf *msgbuf = parser->msgbuf;

        push_dir = lily_mb_sprintf(msgbuf, "%s" LILY_PATH_SLASH, dir);
    }

    lily_free(dir);
    push_string(parser, push_dir);
}

/* This takes an id that corresponds to some id in the table of magic constants.
   From that, it determines that value of the magic constant, and then adds that
   value to the current ast pool. */
static int expr_word_try_constant(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    int const_id = constant_by_name(lex->label);

    if (const_id == CONST_BAD_ID)
        return 0;

    /* These literal fetching routines are guaranteed to return a literal with
       the given value. */
    if (const_id == CONST___LINE__)
        push_integer(parser, (int64_t)lex->line_num);
    else if (const_id == CONST___FILE__)
        push_string(parser, parser->symtab->active_module->path);
    else if (const_id == CONST___FUNCTION__)
        push_string(parser, parser->emit->scope_block->scope_var->name);
    else if (const_id == CONST___DIR__)
        push_dir_constant(parser);
    else if (const_id == CONST_TRUE)
        lily_es_push_boolean(parser->expr, 1);
    else if (const_id == CONST_FALSE)
        lily_es_push_boolean(parser->expr, 0);
    else if (const_id == CONST_SELF) {
        verify_self_access(parser, SELF_KEYWORD);
        lily_es_push_self(parser->expr);
    }
    else if (const_id == CONST_UNIT)
        push_unit(parser);

    return 1;
}

static int expr_word_try_use_self(lily_parse_state *parser)
{
    lily_item *item = NULL;

    if (parser->current_class) {
        lily_class *self_cls = parser->current_class;
        const char *name = parser->lex->label;

        item = lily_find_or_dl_member(parser, self_cls, name);

        if (item) {
            if (item->item_kind == ITEM_DEFINE) {
                /* Pushing the item as a method tells emitter to add an implicit
                   self to the mix. */
                if ((item->flags & VAR_IS_STATIC) == 0) {
                    verify_self_access(parser, SELF_METHOD);
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

static void expr_word_ctor(lily_parse_state *parser, lily_class *cls)
{
    if (cls->item_kind & ITEM_IS_ENUM)
        lily_raise_syn(parser->raiser,
                "To construct an enum, specify a variant.");

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
static void expr_word_as_class(lily_parse_state *parser, lily_class *cls,
        uint16_t *state)
{
    lily_lex_state *lex = parser->lex;
    lily_next_token(lex);

    if (lex->token != tk_dot) {
        expr_word_ctor(parser, cls);
        *state = ST_WANT_OPERATOR | ST_FORWARD;
        return;
    }

    NEED_NEXT_IDENT("Expected a class member name here.")

    lily_item *item = (lily_item *)lily_find_member_in_class(cls, lex->label);

    if (item == NULL)
        item = try_dynaload_method(parser, cls, lex->label);

    if (item == NULL) {
        lily_raise_syn(parser->raiser,
                "Class %s does not have a member named %s.", cls->name,
                lex->label);
    }

    if (item->item_kind == ITEM_DEFINE)
        lily_es_push_static_func(parser->expr, (lily_var *)item);
    else if (item->item_kind & ITEM_IS_VARIANT)
        lily_es_push_variant(parser->expr, (lily_variant_class *)item);
    else if (item->item_kind == ITEM_PROPERTY)
        lily_raise_syn(parser->raiser,
                "Cannot use a class property without a class instance.");
}

static void expr_word_as_match_var(lily_parse_state *parser, lily_var *var)
{
    if (var->type) {
        lily_es_push_local_var(parser->expr, var);
        return;
    }

    /* Match vars with a NULL type are from another branch. */
    lily_raise_syn(parser->raiser, "%s has not been declared.", var->name);
}

static void expr_word_as_constant(lily_parse_state *parser, lily_var *var)
{
    if ((var->flags & VAR_INLINE_CONSTANT) == 0) {
        /* The var's reg_spot is the literal's id. */
        lily_es_push_literal(parser->expr, var->type, var->reg_spot);
        return;
    }

    uint16_t cls_id = var->type->cls_id;

    if (cls_id == LILY_ID_INTEGER)
        lily_es_push_integer(parser->expr, var->constant_value);
    else
        lily_es_push_boolean(parser->expr, var->constant_value);
}

static void expr_word_as_define(lily_parse_state *parser, lily_var *var)
{
    if (var->flags & SYM_NOT_INITIALIZED)
        lily_raise_syn(parser->raiser,
                "Attempt to use uninitialized value '%s'.",
                var->name);

    lily_es_push_defined_func(parser->expr, var);
}

static void expr_word_as_var(lily_parse_state *parser, lily_var *var)
{
    if (var->flags & SYM_NOT_INITIALIZED)
        lily_raise_syn(parser->raiser,
                "Attempt to use uninitialized value '%s'.",
                var->name);

    if (var->flags & VAR_IS_GLOBAL)
        lily_es_push_global_var(parser->expr, var);
    else if (var->function_depth == parser->emit->function_depth)
        lily_es_push_local_var(parser->expr, var);
    else
        lily_es_push_upvalue(parser->expr, var);
}

static void expr_match(lily_parse_state *, uint16_t *);

static lily_module_entry *walk_module(lily_parse_state *parser,
        lily_module_entry *m)
{
    lily_lex_state *lex = parser->lex;
    lily_module_entry *result = m;

    while (1) {
        NEED_NEXT_TOK(tk_dot)
        NEED_NEXT_IDENT("Expected a symbol name (module, class, etc.) here.")

        /* No need to dynaload here: expr_word will do it (only caller). */
        m = lily_find_module(m, lex->label);

        if (m == NULL)
            break;

        /* Caller will search this module for a valid symbol. */
        result = m;
    }

    return result;
}

/* This is called by expression when there is a word. This is complicated,
   because a word could be a lot of things. */
static void expr_word(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR) {
        *state = (parser->expr->save_depth == 0);
        return;
    }

    lily_symtab *symtab = parser->symtab;
    lily_lex_state *lex = parser->lex;
    lily_module_entry *m = symtab->active_module;
    const char *name = lex->label;
    lily_sym *sym = find_existing_sym(m, name);

    /* Words are always values, so an operator should always come next. */
    *state = ST_WANT_OPERATOR;

    if (sym) {
        /* If the name given is a module, there could be more submodules after
           it to look through. Walk the modules, then do another search in the
           final module. */
        if (sym->item_kind == ITEM_MODULE) {
handle_module:;
            m = walk_module(parser, (lily_module_entry *)sym);
            sym = find_existing_sym(m, name);
        }
    }
    else if (expr_word_try_constant(parser) ||
             expr_word_try_use_self(parser))
        return;
    else {
        /* Since no module was explicitly provided, look through predefined
           symbols. */
        m = symtab->prelude_module;
        sym = find_existing_sym(m, name);
    }

    /* As a last resort, try running a dynaload. This will check either the
       module explicitly provided, or the prelude module.
       In most other situations, the active module should be checked as well
       since it could be a foreign module. Since expressions are limited to
       native modules, it is impossible for the active module to have a dynaload
       as a last resort. */
    if (sym == NULL)
        sym = (lily_sym *)try_toplevel_dynaload(parser, m, name);

    if (sym) {
        if (sym->item_kind == ITEM_VAR)
            expr_word_as_var(parser, (lily_var *)sym);
        else if (sym->item_kind == ITEM_DEFINE)
            expr_word_as_define(parser, (lily_var *)sym);
        else if (sym->item_kind & ITEM_IS_VARIANT)
            lily_es_push_variant(parser->expr, (lily_variant_class *)sym);
        else if (sym->item_kind == ITEM_CONSTANT)
            expr_word_as_constant(parser, (lily_var *)sym);
        else if (sym->item_kind & (ITEM_IS_CLASS | ITEM_IS_ENUM))
            expr_word_as_class(parser, (lily_class *)sym, state);
        else if (sym->item_kind == ITEM_MODULE)
            goto handle_module;
        else if (sym->item_kind == ITEM_MATCH_TEMP)
            expr_word_as_match_var(parser, (lily_var *)sym);
    }
    else if (m == symtab->prelude_module &&
             strcmp(name, "match") == 0)
        expr_match(parser, state);
    else
        lily_raise_syn(parser->raiser, "%s has not been declared.", name);
}

static void expr_arrow(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR) {
        if (parser->expr->save_depth == 0) {
            *state = ST_DONE;
            return;
        }
    }
    else if (*state == ST_TOPLEVEL) {
        *state = ST_BAD_TOKEN;
        return;
    }
    else
        lily_raise_syn(parser->raiser, "Expected a value, not '=>'.");

    lily_ast *last_tree = lily_es_get_saved_tree(parser->expr);
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

static void expr_binary(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR) {
        lily_es_push_binary_op(parser->expr, parser->lex->token);
        *state = ST_DEMAND_VALUE;
    }
    else
        *state = ST_BAD_TOKEN;
}

static void expr_byte(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR) {
        *state = (parser->expr->save_depth == 0);
        return;
    }

    lily_es_push_byte(parser->expr, (uint8_t) parser->lex->n.integer_val);
    *state = ST_WANT_OPERATOR;
}

static void expr_bytestring(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR) {
        *state = (parser->expr->save_depth == 0);
        return;
    }

    lily_lex_state *lex = parser->lex;
    lily_type *t;
    lily_literal *lit = lily_get_bytestring_literal(parser->symtab, &t,
            lex->label, lex->string_length);

    lily_es_push_literal(parser->expr, t, lit->reg_spot);
    *state = ST_WANT_OPERATOR;
}

static void expr_close_token(lily_parse_state *parser, uint16_t *state)
{
    uint16_t depth = parser->expr->save_depth;
    lily_token token = parser->lex->token;

    if (*state == ST_DEMAND_VALUE)
        lily_raise_syn(parser->raiser,
                "Expected a value (var, literal, lambda, etc.) here.");
    else if (depth == 0) {
        *state = (*state == ST_WANT_OPERATOR);
        return;
    }

    *state = ST_WANT_OPERATOR;

    lily_ast *tree = lily_es_get_saved_tree(parser->expr);
    lily_tree_type tt = tree->tree_type;
    lily_token expect;

    if (tt == tree_call || tt == tree_parenth || tt == tree_typecast ||
        tt == tree_named_call || tt == tree_ternary_second) {
        if ((parser->flags & PARSER_SUPER_EXPR) && depth == 1)
            /* Super expressions should stop when they're done. */
            *state = ST_DONE;

        expect = tk_right_parenth;
    }
    else if (tt == tree_tuple)
        expect = tk_tuple_close;
    else if (tt != tree_ternary_first)
        expect = tk_right_bracket;
    /* Currently here `(cond ? truthy --> : <-- falsey)`. */
    else if (token == tk_colon) {
        lily_es_collect_arg(parser->expr);
        tree->tree_type = tree_ternary_second;

        /* Not done yet (must collect falsey part). */
        *state = ST_DEMAND_VALUE;
        return;
    }
    else
        expect = tk_colon;

    if (token != expect)
        lily_raise_syn(parser->raiser, "Expected closing token '%s', not '%s'.",
                tokname(expect), tokname(token));

    lily_es_leave_tree(parser->expr);
}

static void expr_comma(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR) {
        if (parser->expr->save_depth == 0) {
            *state = ST_DONE;
            return;
        }
    }
    else if (*state == ST_TOPLEVEL) {
        *state = ST_BAD_TOKEN;
        return;
    }
    else
        lily_raise_syn(parser->raiser, "Expected a value, not ','.");

    lily_ast *last_tree = lily_es_get_saved_tree(parser->expr);
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
    else if (last_tt == tree_ternary_first || last_tt == tree_ternary_second)
        lily_raise_syn(parser->raiser, "Comma not allowed within ternary.");

    lily_es_collect_arg(parser->expr);

    if (last_tt == tree_call || last_tt == tree_named_call)
        *state = ST_DEMAND_VALUE;
    /* Allow a trailing comma for lists, hashes, and tuples. */
    else
        *state = ST_WANT_VALUE;
}

static void expr_maybe_variant_shorthand(lily_parse_state *parser,
        uint16_t *state)
{
    lily_expr_state *es = parser->expr;
    uint16_t spot = es->pile_current;
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);

    if (lex->token == tk_typecast_parenth) {
        *state = ST_BAD_TOKEN;
        return;
    }
    else if (lex->token != tk_word)
        lily_raise_syn(parser->raiser,
                "Expected a name for variant shorthand.");

    lily_sp_insert(parser->expr_strings, lex->label, &es->pile_current);
    lily_es_push_text(es, tree_dot_variant, lex->line_num, spot);
    *state = ST_WANT_OPERATOR;
}

/* Lexer handles numbers with dots inside, so this is a member access, a cast,
   or variant shorthand. */
static void expr_dot(lily_parse_state *parser, uint16_t *state)
{
    if (*state != ST_WANT_OPERATOR) {
        expr_maybe_variant_shorthand(parser, state);
        return;
    }

    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);

    if (lex->token == tk_word) {
        lily_expr_state *es = parser->expr;
        uint16_t spot = es->pile_current;
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
                "Expected a property name or '@(' here.");
}

static void expr_double(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR &&
        maybe_digit_fixup(parser) == 0) {
        *state = (parser->expr->save_depth == 0);
        return;
    }

    lily_lex_state *lex = parser->lex;
    lily_type *t;
    lily_literal *lit = lily_get_double_literal(parser->symtab, &t,
            lex->n.double_val);

    lily_es_push_literal(parser->expr, t, lit->reg_spot);
    *state = ST_WANT_OPERATOR;
}

static void expr_double_quote(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR) {
        *state = (parser->expr->save_depth == 0);
        return;
    }

    push_string(parser, parser->lex->label);
    *state = ST_WANT_OPERATOR;
}

static void expr_integer(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR &&
        maybe_digit_fixup(parser) == 0) {
        *state = (parser->expr->save_depth == 0);
        return;
    }

    lily_lex_state *lex = parser->lex;

    push_integer(parser, lex->n.integer_val);
    *state = ST_WANT_OPERATOR;
}

static void expr_question(lily_parse_state *parser, uint16_t *state)
{
    /* Ternary operations must be with parentheses and have a prior value. */
    if (*state != ST_WANT_OPERATOR ||
        parser->expr->save_depth == 0) {
        *state = ST_BAD_TOKEN;
        return;
    }

    lily_ast *last_tree = lily_es_get_saved_tree(parser->expr);

    if (last_tree->tree_type != tree_parenth) {
        *state = ST_BAD_TOKEN;
        return;
    }

    /* Change this so ':' knows it's taking the truthy value of a ternary. */
    last_tree->tree_type = tree_ternary_first;
    lily_es_collect_arg(parser->expr);
    *state = ST_DEMAND_VALUE;
}

static void expr_invalid(lily_parse_state *parser, uint16_t *state)
{
    (void)parser;
    *state = ST_BAD_TOKEN;
}

static void expr_keyword_arg(lily_parse_state *parser, uint16_t *state)
{
    lily_expr_state *es = parser->expr;

    if (es->root || parser->expr->save_depth == 0) {
        *state = ST_BAD_TOKEN;
        return;
    }

    lily_ast *last_tree = lily_es_get_saved_tree(parser->expr);

    if (last_tree->tree_type != tree_call &&
        last_tree->tree_type != tree_named_call) {
        *state = ST_BAD_TOKEN;
        return;
    }

    last_tree->tree_type = tree_named_call;

    uint16_t spot = es->pile_current;
    lily_sp_insert(parser->expr_strings, parser->lex->label, &es->pile_current);
    lily_es_push_text(es, tree_oo_access, 0, spot);
    lily_es_push_binary_op(es, tk_keyword_arg);
    *state = ST_DEMAND_VALUE;
}

static void expr_lambda(lily_parse_state *parser, uint16_t *state)
{
    if (parser->flags & PARSER_SIMPLE_EXPR)
        lily_raise_syn(parser->raiser, "A lambda is too complex to be here.");

    /* Checking for an operator allows this

       `x.some_call(|x| ... )`

       to act like

       `x.some_call((|x| ... ))`

       which cuts a level of parentheses. */

    lily_lex_state *lex = parser->lex;
    lily_expr_state *es = parser->expr;
    uint16_t spot = es->pile_current;

    if (*state == ST_WANT_OPERATOR)
        lily_es_enter_tree(es, tree_call);

    lily_sp_insert(parser->expr_strings, lex->label, &es->pile_current);
    lily_es_push_lambda(es, lex->expand_start_line, spot, lex->lambda_offset);

    if (*state == ST_WANT_OPERATOR)
        lily_es_leave_tree(es);

    *state = ST_WANT_OPERATOR;
}

static void expr_left_bracket(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR) {
        lily_es_enter_tree(parser->expr, tree_subscript);
        *state = ST_DEMAND_VALUE;
    }
    else {
        lily_es_enter_tree(parser->expr, tree_list);
        *state = ST_WANT_VALUE;
    }
}

static void expr_left_parenth(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR) {
        lily_es_enter_tree(parser->expr, tree_call);
        *state = ST_WANT_VALUE;
    }
    else {
        lily_es_enter_tree(parser->expr, tree_parenth);
        *state = ST_DEMAND_VALUE;
    }
}

static void expr_minus_mul(lily_parse_state *parser, uint16_t *state)
{
    lily_token t = parser->lex->token;

    if (*state == ST_WANT_OPERATOR)
        lily_es_push_binary_op(parser->expr, t);
    else
        lily_es_push_unary_op(parser->expr, t);

    *state = ST_DEMAND_VALUE;
}

/* This is called to handle `@<prop>` accesses. */
static void expr_prop_word(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR) {
        *state = (parser->expr->save_depth == 0);
        return;
    }

    verify_self_access(parser, SELF_PROPERTY);

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
    else if (sym->item_kind == ITEM_DEFINE) {
        lily_raise_syn(parser->raiser,
                "%s is a method (use %s instead of @%s).",
                name, name, name);
    }

    if (sym->flags & SYM_NOT_INITIALIZED)
        lily_raise_syn(parser->raiser,
                "Invalid use of uninitialized property '@%s'.",
                sym->name);

    lily_es_push_property(parser->expr, (lily_prop_entry *)sym);
    *state = ST_WANT_OPERATOR;
}

static void expr_tuple_open(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR)
        *state = (parser->expr->save_depth == 0);
    else {
        lily_es_enter_tree(parser->expr, tree_tuple);
        *state = ST_WANT_VALUE;
    }
}

static void expr_unary(lily_parse_state *parser, uint16_t *state)
{
    if (*state == ST_WANT_OPERATOR)
        *state = ST_BAD_TOKEN;
    else {
        lily_es_push_unary_op(parser->expr, parser->lex->token);
        *state = ST_DEMAND_VALUE;
    }
}

/* This is the magic function that handles expressions. The states it uses are
   defined above. Most callers will use expression instead of this. */
static void expression_raw(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    uint16_t state;

    if (parser->expr->root || parser->expr->save_depth)
        state = ST_DEMAND_VALUE;
    else
        state = ST_TOPLEVEL;

    while (1) {
        expr_handlers[lex->token](parser, &state);

        if (state == ST_DONE)
            break;
        else if (state == ST_BAD_TOKEN)
            lily_raise_syn(parser->raiser,
                    "Unexpected token within an expression.");
        else if (state & ST_FORWARD)
            state &= ~ST_FORWARD;
        else
            lily_next_token(lex);
    }
}

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

static void parse_block_exit(lily_parse_state *);

/* This runs through the body of a lambda, running any statements inside. The
   result of this function is the type of the last expression that was run.
   If the last thing was a block, or did not return a value, then NULL is
   returned. */
static lily_type *parse_lambda_body(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_type *result_type = lily_unit_type;

    lily_next_token(parser->lex);

    if (lex->token == tk_end_lambda)
        lily_raise_syn(parser->raiser, "Lambda does not have a value.");

    while (1) {
        int key_id = KEY_BAD_ID;

        if (lex->token == tk_word)
            key_id = keyword_by_name(lex->label);

        if (key_id != KEY_BAD_ID)
            handlers[key_id](parser);
        else if (lex->token == tk_left_curly) {
            lily_emit_enter_anon_block(parser->emit);
            lily_next_token(lex);
        }
        else if (lex->token == tk_right_curly)
            parse_block_exit(parser);
        else if (lex->token != tk_end_lambda) {
            expression(parser);

            if (lex->token != tk_end_lambda)
                lily_eval_expr(parser->emit, parser->expr);
            else {
                /* Inference is provided by the scope var's type. */
                result_type = lily_eval_lambda_result(parser->emit,
                        parser->expr);
                break;
            }
        }
        else if (parser->emit->block->block_type == block_lambda) {
            lily_eval_lambda_exit(parser->emit);
            break;
        }
        else
            lily_raise_syn(parser->raiser,
                    "End of lambda while inside a block.");
    }

    return result_type;
}

/* Collect arguments until the second `|` of a lambda, where they end. The type
   given for inference is either a Function (which may have args), or a
   placeholder (which will have 0). Arguments may be just names, or names with
   types. */
static uint16_t collect_lambda_args(lily_parse_state *parser,
        lily_type *expect_type, uint16_t *flags)
{
    uint16_t infer_count = expect_type->subtype_count;
    uint16_t num_args = 1;
    lily_lex_state *lex = parser->lex;
    lily_var *arg_var = NULL;

    while (1) {
        NEED_NEXT_IDENT("Expected a variable name here.")
        arg_var = declare_local_var(parser, NULL);
        lily_type *arg_type;

        if (lex->token == tk_colon) {
            lily_next_token(lex);
            arg_type = get_type(parser);
        }
        else {
            if (num_args < infer_count)
                /* This could be '?' if expecting a generic, but the generic
                   doesn't have any solutions yet. */
                arg_type = expect_type->subtypes[num_args];
            else
                arg_type = lily_question_type;

            /* This is mostly about blocking '?', but uses TYPE_TO_BLOCK to make
               sure scoop can't flow through here. */
            if (arg_type->flags & TYPE_TO_BLOCK)
                lily_raise_syn(parser->raiser,
                        "'%s' has an incomplete inferred type (^T).",
                        lex->label, arg_type);
            else if (arg_type->cls_id == LILY_ID_OPTARG)
                arg_type = arg_type->subtypes[0];
        }

        arg_var->type = arg_type;
        lily_tm_add(parser->tm, arg_type);
        num_args++;

        if (lex->token == tk_comma)
            continue;
        else if (lex->token == tk_bitwise_or)
            break;
        else
            lily_raise_syn(parser->raiser, "Expected either ',' or '|' here.");
    }

    /* Only mark the lambda as varargs if it can do that. */
    if (expect_type->flags & TYPE_IS_VARARGS &&
        infer_count == num_args) {

        lily_type *last_type = expect_type->subtypes[num_args - 1];

        if (last_type == arg_var->type)
            *flags = TYPE_IS_VARARGS;
    }

    return num_args - 1;
}

void lily_parser_lambda_init(lily_parse_state *parser, const char *lambda_body,
        uint16_t start_line, uint16_t lambda_offset)
{
    lily_lexer_load(parser->lex, et_lambda, lambda_body);
    lily_lexer_setup_lambda(parser->lex, start_line, lambda_offset);
}

/* Emitter calls this when tree eval has reached a lambda. Lexer collects
   lambdas as blocks of text, since the types are unknown during collection. Now
   that the types are known, eval the lambda and yield the definition. */
lily_sym *lily_parser_lambda_eval(lily_parse_state *parser,
        lily_type *expect_type)
{
    lily_lex_state *lex = parser->lex;
    uint16_t args_collected = 0, tm_return = parser->tm->pos;
    lily_type *root_result;
    uint16_t flags = 0;

    lily_var *lambda_var = new_define_var(parser, "(lambda)", lex->line_num);

    make_new_function(parser, lambda_var);

    if (expect_type->cls_id == LILY_ID_FUNCTION)
        lambda_var->type = expect_type->subtypes[0];
    else
        lambda_var->type = expect_type;

    lily_emit_enter_lambda_block(parser->emit, lambda_var);

    /* Placeholder for the return type (if one is found later). */
    lily_tm_add(parser->tm, lily_unit_type);
    lily_next_token(lex);

    if (lex->token == tk_bitwise_or)
        args_collected = collect_lambda_args(parser, expect_type, &flags);
    /* Otherwise the token is ||, meaning the lambda does not have args. */

    /* The current expression may not be done. This makes sure that the pool
       won't use the same trees again. */
    lily_es_checkpoint_save(parser->expr);
    root_result = parse_lambda_body(parser);
    lily_es_checkpoint_restore(parser->expr);

    if (root_result)
        lily_tm_insert(parser->tm, tm_return, root_result);

    lambda_var->type = lily_tm_make_call(parser->tm, flags,
            parser->symtab->function_class, args_collected + 1);

    hide_block_vars(parser);
    lily_emit_leave_lambda_block(parser->emit);
    lily_pop_lex_entry(lex);

    return (lily_sym *)lambda_var;
}

/***
 *      ____  _        _                            _
 *     / ___|| |_ __ _| |_ ___ _ __ ___   ___ _ __ | |_ ___
 *     \___ \| __/ _` | __/ _ \ '_ ` _ \ / _ \ '_ \| __/ __|
 *      ___) | || (_| | ||  __/ | | | | |  __/ | | | |_\__ \
 *     |____/ \__\__,_|\__\___|_| |_| |_|\___|_| |_|\__|___/
 *
 */

static void expect_manifest_header(lily_parse_state *);
static void parse_modifier(lily_parse_state *, int);

/** The rest of this focuses on handling handling keywords and blocks. Much of
    this is straightforward and kept in small functions that rely on the above
    stuff. As such, there's no real special attention paid to the rest.
    Near the bottom is parser_loop, which is the entry point of the parser. **/

static int keyword_by_name(const char *name)
{
    int i;
    uint64_t shorthash = shorthash_for_name(name);

    for (i = 0;i < KEY_BAD_ID;i++) {
        if (keywords[i].shorthash == shorthash &&
            strcmp(keywords[i].name, name) == 0)
            return i;
        else if (keywords[i].shorthash > shorthash)
            break;
    }

    return KEY_BAD_ID;
}

static void error_member_redeclaration(lily_parse_state *parser,
        lily_class *cls, lily_named_sym *sym)
{
    if (sym->item_kind == ITEM_DEFINE)
        lily_raise_syn(parser->raiser,
                "A method in class '%s' already has the name '%s'.",
                cls->name, sym->name);
    else
        lily_raise_syn(parser->raiser,
                "A property in class '%s' already has the name @%s.",
                cls->name, sym->name);
}

static lily_prop_entry *declare_property(lily_parse_state *parser,
        uint16_t flags)
{
    char *name = parser->lex->label;
    lily_class *cls = parser->current_class;
    lily_named_sym *sym = lily_find_visible_member(cls, name);

    if (sym)
        error_member_redeclaration(parser, cls, sym);

    lily_prop_entry *prop = lily_add_class_property(cls, lily_question_type, name,
            parser->lex->line_num, flags);

    if (parser->flags & PARSER_HAS_DOCBLOCK)
        prop->doc_id = store_docblock(parser);

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
        message = "Only class properties can use @<name>.";

    lily_raise_syn(parser->raiser, message);
}

static void add_unresolved_defines_to_msgbuf(lily_parse_state *parser,
        lily_msgbuf *msgbuf)
{
    lily_module_entry *m = parser->symtab->active_module;
    lily_var *var_iter;

    if (parser->emit->block->block_type == block_file)
        var_iter = m->var_chain;
    else
        var_iter = (lily_var *)m->class_chain->members;

    while (var_iter) {
        if (var_iter->flags & SYM_IS_FORWARD)
            lily_mb_add_fmt(msgbuf, "\n* %s at line %d", var_iter->name,
                    var_iter->line_num);

        var_iter = var_iter->next;
    }
}

static void add_unresolved_methods_to_msgbuf(lily_parse_state *parser,
        lily_msgbuf *msgbuf)
{
    lily_module_entry *m = parser->symtab->active_module;
    lily_block *block = parser->emit->block;
    lily_class *class_iter = m->class_chain;

    while (class_iter) {
        if (class_iter->flags & SYM_IS_FORWARD) {
            lily_var *var_iter = (lily_var *)class_iter->members;

            /* Forward classes can't have properties, so every member is a
               forward method to unpack. */
            while (var_iter) {
                lily_proto *p = lily_emit_proto_for_var(parser->emit, var_iter);

                lily_mb_add_fmt(msgbuf, "\n* %s at line %d", p->name,
                        var_iter->line_num);
                var_iter = var_iter->next;
            }

            block->forward_count -= class_iter->forward_count;
        }

        class_iter = class_iter->next;
    }
}

static void error_forward_decl_keyword(lily_parse_state *parser, int key)
{
    lily_msgbuf *msgbuf = lily_mb_flush(parser->msgbuf);

    lily_mb_add_fmt(msgbuf,
            "The following definitions must be resolved first:");

    if (key == KEY_VAR && parser->emit->block->forward_class_count)
        add_unresolved_methods_to_msgbuf(parser, msgbuf);

    add_unresolved_defines_to_msgbuf(parser, msgbuf);
    lily_raise_syn(parser->raiser, lily_mb_raw(msgbuf));
}

static void keyword_var(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_sym *sym = NULL;
    lily_block *block = parser->emit->block;
    uint16_t modifiers = parser->modifiers;
    lily_token want_token, other_token;

    if (modifiers) {
        want_token = tk_prop_word;
        other_token = tk_word;
    }
    else {
        want_token = tk_word;
        other_token = tk_prop_word;
    }

    if (block->forward_count && block->block_type != block_class)
        error_forward_decl_keyword(parser, KEY_VAR);

    lily_next_token(lex);

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

        if (lex->token != tk_equal)
            lily_raise_syn(parser->raiser, "Expected '=' <expression> here.");

        lily_es_push_assign_to(parser->expr, sym);
        lily_next_token(lex);
        expression_raw(parser);
        lily_eval_expr(parser->emit, parser->expr);

        if (lex->token != tk_comma)
            break;

        lily_next_token(lex);
    }
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

static void add_unresolved_classes_to_msgbuf(lily_parse_state *parser,
        lily_msgbuf *msgbuf)
{
    lily_module_entry *m = parser->symtab->active_module;
    lily_class *class_iter = m->class_chain;

    while (class_iter) {
        if (class_iter->flags & SYM_IS_FORWARD)
            lily_mb_add_fmt(msgbuf, "\n* %s at line %d", class_iter->name,
                    class_iter->line_num);

        class_iter = class_iter->next;
    }
}

static void error_forward_decl_pending(lily_parse_state *parser)
{
    lily_msgbuf *msgbuf = lily_mb_flush(parser->msgbuf);
    lily_block *block = parser->emit->block;

    if (block->block_type == block_file) {
        lily_mb_add_fmt(msgbuf,
                "Reached end of module with unresolved forward(s):");

        if (block->forward_class_count)
            add_unresolved_classes_to_msgbuf(parser, msgbuf);
    }
    else
        lily_mb_add_fmt(msgbuf, "Class %s has unresolved forward(s):",
                parser->current_class->name);

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
    uint16_t modifiers = define_var->flags;
    const char *scope_str = scope_to_str(modifiers);
    const char *static_str = static_to_str(modifiers);

    lily_raise_syn(parser->raiser,
            "Wrong qualifiers in resolution of ^I (expected: %s%s).",
            define_var, scope_str, static_str);
}

static void keyword_if(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);
    lily_emit_enter_if_block(parser->emit);
    expression(parser);
    lily_eval_entry_condition(parser->emit, parser->expr);
    NEED_COLON_AND_BRACE;
    lily_next_token(lex);
}

static void keyword_elif(lily_parse_state *parser)
{
    lily_block *block = parser->emit->block;
    lily_lex_state *lex = parser->lex;

    if (block->block_type != block_if)
        lily_raise_syn(parser->raiser, "elif outside of if.");

    if (block->flags & BLOCK_FINAL_BRANCH)
        lily_raise_syn(parser->raiser, "elif in exhaustive if.");

    lily_next_token(lex);
    hide_block_vars(parser);
    lily_emit_branch_switch(parser->emit);
    expression(parser);
    lily_eval_entry_condition(parser->emit, parser->expr);
    NEED_COLON_AND_NEXT;
}

static void keyword_else(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_block *block = parser->emit->block;
    const char *what = NULL;

    switch (block->block_type) {
        case block_if:
            what = "if";
            break;
        case block_match:
            what = "match";
            break;
        case block_with:
            what = "with";
            break;
        default:
            break;
    }

    if (what == NULL)
        lily_raise_syn(parser->raiser, "else outside of if, match, or with.");

    if (block->flags & BLOCK_FINAL_BRANCH)
        lily_raise_syn(parser->raiser, "else in exhaustive %s.", what);

    lily_next_token(lex);
    hide_block_vars(parser);
    lily_emit_branch_finalize(parser->emit);
    NEED_COLON_AND_NEXT;
}

static int code_is_after_exit(lily_parse_state *parser)
{
    lily_token token = parser->lex->token;

    /* It's not dead if this block is being exited. */
    if (token == tk_right_curly ||
        token == tk_eof ||
        token == tk_end_lambda)
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
    lily_type *return_type = block->scope_var->type;

    if (block->block_type == block_class)
        lily_raise_syn(parser->raiser,
                "'return' not allowed in a class constructor.");
    else if (block->block_type == block_file)
        lily_raise_syn(parser->raiser, "'return' used outside of a function.");
    else if (block->block_type != block_lambda)
        return_type = return_type->subtypes[0];
    else if (return_type == lily_unset_type)
        lily_raise_syn(parser->raiser,
                "'return' inside of a lambda requires inference, but none given.");
    else
        block->flags |= BLOCK_LAMBDA_RESULT;

    lily_next_token(parser->lex);

    if (return_type != lily_unit_type) {
        expression(parser);
        lily_eval_return(parser->emit, parser->expr, return_type);
    }
    else
        lily_eval_unit_return(parser->emit);

    /* Lambdas are allowed to have an explicit return at the end due to a lack
       of checking for tk_end_lambda here. It's harmless. */

    if (code_is_after_exit(parser)) {
        const char *extra = ".";
        if (return_type == lily_unit_type)
            extra = " (no return type given).";

        lily_raise_syn(parser->raiser,
                "Statement(s) after 'return' will not execute%s", extra);
    }

    block->flags &= ~BLOCK_LAMBDA_RESULT;
}

static void keyword_while(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);
    lily_emit_enter_while_block(parser->emit);

    expression(parser);
    lily_eval_entry_condition(parser->emit, parser->expr);
    NEED_COLON_AND_BRACE;
    lily_next_token(lex);
}

static int get_val_if_bool_word(const char *label, int64_t *val)
{
    int c = constant_by_name(label);
    int result = 1;

    if (c == CONST_TRUE)
        *val = 1;
    else if (c == CONST_FALSE)
        *val = 0;
    else
        result = 0;

    return result;
}

static void parse_one_constant(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;

    NEED_IDENT("Expected a name for a constant here.")

    lily_var *var = declare_constant(parser);

    if (lex->token == tk_colon)
        lily_raise_syn(parser->raiser,
                "Constants cannot explicitly specify a type.");
    else if (lex->token != tk_equal)
        lily_raise_syn(parser->raiser,
                "An initialization expression is required here.");

    lily_literal *lit;
    lily_type *t;
    int64_t bool_val;

    lily_next_token(lex);

    if (lex->token == tk_integer) {
        make_integer_constant(parser, var, lex->n.integer_val);
        return;
    }
    else if (lex->token == tk_double)
        lit = lily_get_double_literal(symtab, &t, lex->n.double_val);
    else if (lex->token == tk_double_quote)
        lit = lily_get_string_literal(symtab, &t, lex->label);
    else if (lex->token == tk_word &&
             get_val_if_bool_word(lex->label, &bool_val)) {
        make_boolean_constant(parser, var, bool_val);
        return;
    }
    else if (lex->token == tk_bytestring)
        lit = lily_get_bytestring_literal(parser->symtab, &t, lex->label,
                lex->string_length);
    else {
        /* Silence a warning about uninitialized use. */
        lit = NULL;
        lily_raise_syn(parser->raiser,
                "Constant initialization expects a primitive value (ex: 1 or \"abc\").");
    }

    var->type = t;
    var->reg_spot = lit->reg_spot;
}

static void keyword_constant(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_block *block = parser->emit->block;

    if (block->block_type != block_file)
        lily_raise_syn(parser->raiser,
                "Cannot declare a constant while inside a block.");

    lily_next_token(lex);

    while (1) {
        parse_one_constant(parser);
        lily_next_token(lex);

        if (lex->token != tk_comma)
            break;

        lily_next_token(lex);
    }
}

static void keyword_continue(lily_parse_state *parser)
{
    if (lily_emit_try_write_continue(parser->emit) == 0)
        lily_raise_syn(parser->raiser, "'continue' used outside of a loop.");

    lily_next_token(parser->lex);

    if (code_is_after_exit(parser))
        lily_raise_syn(parser->raiser,
                "Statement(s) after 'continue' will not execute.");
}

static void keyword_break(lily_parse_state *parser)
{
    if (lily_emit_try_write_break(parser->emit) == 0)
        lily_raise_syn(parser->raiser, "'break' used outside of a loop.");

    lily_next_token(parser->lex);

    if (code_is_after_exit(parser))
        lily_raise_syn(parser->raiser,
                "Statement(s) after 'break' will not execute.");
}

static void parse_for_expr(lily_parse_state *parser, lily_var *var)
{
    lily_next_token(parser->lex);
    expression(parser);
    lily_eval_to_loop_var(parser->emit, parser->expr, var);
}

static void expect_word(lily_parse_state *parser, const char *what)
{
    if (parser->lex->token == tk_word && strcmp(parser->lex->label, what) == 0)
        return;

    lily_raise_syn(parser->raiser, "Expected '%s' here.", what);
}

static int extra_if_word(lily_parse_state *parser, const char *what)
{
    lily_lex_state *lex = parser->lex;
    int result = 0;

    if (lex->token == tk_word) {
        if (strcmp(lex->label, what) != 0)
            lily_raise_syn(parser->raiser, "Expected '%s' here.", what);

        result = 1;
    }

    return result;
}

static lily_var *parse_for_var(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    NEED_NEXT_IDENT("Expected a variable name here.")

    lily_var *result = find_active_var(parser, lex->label);

    if (result == NULL) {
        /* For loop variables must have a suitable scope. */
        if (parser->emit->function_depth != 1)
            result = new_local_var(parser, lex->label, lex->line_num);
        else
            result = new_global_var(parser, lex->label, lex->line_num);

        /* Could be the index or the element. Use ? for now, fix it later. */
        result->type = lily_question_type;
    }
    else if (parser->emit->function_depth != result->function_depth &&
            (result->flags & VAR_IS_GLOBAL) == 0)
        /* Blocked because closures and for loop vars tend to mix poorly. */
        lily_raise_syn(parser->raiser, "Loop var cannot be an upvalue.");

    result->flags |= SYM_NOT_INITIALIZED;
    lily_next_token(lex);

    return result;
}

static void ensure_valid_for_loop_index(lily_parse_state *parser,
        lily_var *index_var)
{
    if (index_var->type->cls_id == LILY_ID_INTEGER)
        return;

    lily_type *integer_type = (lily_type *)parser->symtab->integer_class;

    if (index_var->type == lily_question_type)
        index_var->type = integer_type;
    else {
        lily_raise_syn(parser->raiser,
                "For loop index must be Integer, but %s has type ^T.",
                index_var->name, index_var->type);
    }
}

/* Parse a `for` over a single expression.
   If there's only one var, it's the element.
   If there's two, the first is the index, the second is the element. */
static void parse_for_list(lily_parse_state *parser, lily_var *first_var,
        lily_var *second_var)
{
    lily_var *backing;

    if (second_var == NULL) {
        /* Make it so the second spot is always the element. The first spot
           needs to be filled, because for loops need a counter. */
        lily_type *integer_type = (lily_type *)parser->symtab->integer_class;

        second_var = first_var;
        first_var = new_typed_local_var(parser, integer_type, "", 0);

        /* No loop var so no need for a backing var. */
        backing = first_var;
    }
    else {
        ensure_valid_for_loop_index(parser, first_var);
        backing = new_typed_local_var(parser, lily_question_type, "", 0);
    }

    /* Emitter will fix this type in eval. */
    lily_var *source = new_typed_local_var(parser, lily_question_type, "", 0);

    lily_eval_for_of(parser->emit, parser->expr, source, second_var);
    lily_emit_write_for_of(parser->emit, source, backing, first_var, second_var,
            parser->lex->line_num);

    /* Caller fixes the other var. */
    second_var->flags &= ~SYM_NOT_INITIALIZED;
}

static void parse_for_range(lily_parse_state *parser, lily_var *first_var,
        lily_var *second_var)
{
    if (second_var)
        lily_raise_syn(parser->raiser,
                "For range does not have elements for %s.\n", second_var->name);

    ensure_valid_for_loop_index(parser, first_var);

    lily_type *integer_type = (lily_type *)parser->symtab->integer_class;
    lily_var *for_start = new_typed_local_var(parser, integer_type, "", 0);
    lily_var *for_end = new_typed_local_var(parser, integer_type, "", 0);
    lily_var *for_step = new_typed_local_var(parser, integer_type, "", 0);

    lily_eval_to_loop_var(parser->emit, parser->expr, for_start);
    parse_for_expr(parser, for_end);

    if (extra_if_word(parser, "by"))
        parse_for_expr(parser, for_step);
    else {
        lily_es_flush(parser->expr);
        lily_es_push_assign_to(parser->expr, (lily_sym *)for_step);
        lily_es_push_integer(parser->expr, 1);
        lily_eval_expr(parser->emit, parser->expr);
    }

    lily_emit_write_for_header(parser->emit, first_var, for_start, for_end,
                               for_step, parser->lex->line_num);
}

/* Either `for <index> in start...end (by step): { ... }`
   or     `for <element> in range: { ... }`. */
static void keyword_for(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_emit_state *emit = parser->emit;
    lily_var *first_var, *second_var = NULL;

    lily_emit_enter_for_in_block(emit);
    first_var = parse_for_var(parser);

    if (lex->token == tk_comma)
        second_var = parse_for_var(parser);

    expect_word(parser, "in");
    lily_next_token(parser->lex);
    expression(parser);

    if (lex->token == tk_colon)
        parse_for_list(parser, first_var, second_var);
    else if (lex->token == tk_three_dots)
        parse_for_range(parser, first_var, second_var);
    else
        lily_raise_syn(parser->raiser, "Expected either ':' or '...' here.");

    /* For list handling fixes the second var, if that's necessary. */
    first_var->flags &= ~SYM_NOT_INITIALIZED;
    NEED_COLON_AND_BRACE;
    lily_next_token(lex);
}

static void keyword_do(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);
    lily_emit_enter_do_while_block(parser->emit);
    NEED_COLON_AND_BRACE;
    lily_next_token(lex);
}

/* This links 'count' symbols from 'source' into the active module. The symbol
   names come from popping names inserted by collect_import_refs. */
static void link_import_syms(lily_parse_state *parser,
        lily_module_entry *source, uint16_t count)
{
    lily_symtab *symtab = parser->symtab;
    lily_module_entry *active = symtab->active_module;
    lily_buffer_u16 *buffer = parser->data_stack;
    uint16_t start = lily_u16_pos(buffer) - (count * 2);
    uint16_t iter = start, restore_to = start;

    do {
        uint16_t search_pos = lily_u16_get(buffer, iter + 1);
        char *name = lily_sp_get(parser->data_strings, search_pos);
        lily_sym *sym = find_existing_sym(active, name);

        if (sym) {
            uint16_t line = lily_u16_get(buffer, iter);

            lily_raise_syn_at(parser->raiser, line,
                    "'%s' has already been declared.", name);
        }

        sym = find_existing_sym(source, name);

        if (sym == NULL)
            sym = (lily_sym *)try_toplevel_dynaload(parser, source, name);

        if (sym == NULL) {
            uint16_t line = lily_u16_get(buffer, iter);

            lily_raise_syn_at(parser->raiser, line,
                    "Cannot find symbol '%s' inside of module '%s'.",
                    name, source->loadname);
        }

        if (sym->item_kind != ITEM_MODULE)
            lily_add_symbol_ref(active, sym);
        else
            lily_ims_link_module_to(active, (lily_module_entry *)sym, name);

        iter += 2;
        count--;
    } while (count);

    lily_u16_set_pos(parser->data_stack, restore_to);
}

/* This function collects symbol names within parentheses for import. The result
   is how many names were collected, and is never zero. */
static void parse_import_refs(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    uint16_t count = 0;

    if (lex->token == tk_left_parenth) {
        lily_u16_write_1(parser->data_stack, parser->data_string_pos);

        while (1) {
            NEED_NEXT_IDENT(
                    "Expected a symbol name (module, class, etc.) here.")

            lily_u16_write_1(parser->data_stack, lex->line_num);
            lily_pa_add_data_string(parser, lex->label);
            count++;

            lily_next_token(lex);

            if (lex->token == tk_right_parenth)
                break;
            else if (lex->token != tk_comma)
                lily_raise_syn(parser->raiser,
                        "Expected either ',' or ')' here.");
        }

        lily_next_token(lex);
    }

    lily_u16_write_1(parser->data_stack, count);
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
                "'import' expected a path (identifier or string) here.");

    ims->source_module = parser->symtab->active_module;
    ims->last_import = NULL;
    ims->dirname = NULL;
}

static void parse_import_target(lily_parse_state *parser)
{
    lily_import_state *ims = parser->ims;
    lily_module_entry *active = parser->symtab->active_module;

    parse_import_refs(parser);
    parse_import_path_into_ims(parser);

    /* Will the name that is going to be added conflict with something that
       has already been added? */
    if (lily_find_module(active, ims->pending_loadname))
        lily_raise_syn(parser->raiser,
                "A module named '%s' has already been imported here.",
                ims->pending_loadname);
}

static void parse_import_link(lily_parse_state *parser, lily_module_entry *m)
{
    uint16_t import_sym_count = lily_u16_pop(parser->data_stack);

    lily_module_entry *active = parser->symtab->active_module;
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);

    if (import_sym_count == 0) {
        char *name = NULL;

        if (lex->token == tk_word && strcmp(lex->label, "as") == 0) {
            NEED_NEXT_IDENT("Expected a name for the module here.")
            name = lex->label;
        }

        /* This link must be done now, because the next token may be a word
           and lex->label would be modified. */
        lily_ims_link_module_to(active, m, name);

        if (name != NULL)
            lily_next_token(lex);
    }
    else if (lex->token != tk_word || strcmp(lex->label, "as") != 0) {
        link_import_syms(parser, m, import_sym_count);
        parser->data_string_pos = lily_u16_pop(parser->data_stack);
    }
    else
        lily_raise_syn(parser->raiser,
                "Cannot use 'as' when only specific items are being imported.");
}

static void enter_module(lily_parse_state *parser, lily_module_entry *m)
{
    /* The flag is changed so that rewind can identify modules that didn't
       fully load and hide them from a subsequent pass. */
    m->flags &= ~MODULE_NOT_EXECUTED;
    m->flags |= MODULE_IN_EXECUTION;
    parser->symtab->active_module = m;

    /* This is either `__main__` or another `__module__`. */
    lily_type *module_type = parser->emit->scope_block->scope_var->type;
    lily_var *module_var = new_define_var(parser, "__module__",
            parser->lex->line_num);

    make_new_function(parser, module_var);
    module_var->type = module_type;
    module_var->module = m;
    lily_emit_enter_file_block(parser->emit, module_var);

    if ((parser->flags & PARSER_IN_MANIFEST))
        expect_manifest_header(parser);

    lily_next_token(parser->lex);
}

static void import_loop(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    while (1) {
        parse_import_target(parser);

        lily_module_entry *m = lily_ims_open_module(parser);

        if (m->flags & MODULE_NOT_EXECUTED) {
            enter_module(parser, m);
            break;
        }
        else if (parser->flags & PARSER_IN_MANIFEST && m->call_table) {
            lily_raise_syn(parser->raiser,
                    "Cannot import '%s' while in manifest mode.", m->loadname);
        }

        parse_import_link(parser, m);

        if (lex->token != tk_comma)
            break;

        lily_next_token(lex);
    }
}

static void finish_import(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;
    lily_module_entry *m = symtab->active_module;
    uint16_t last_line = lex->line_num;

    /* Don't hide vars (these are globals). */
    lily_pop_lex_entry(lex);

    /* Since this writes the call to the `__module__` function in the
       importer, it has to come after the lex entry is gone. */
    lily_emit_leave_import_block(parser->emit, last_line);

    m->flags &= ~MODULE_IN_EXECUTION;

    /* This is not the same scope block as above. */
    symtab->active_module = parser->emit->scope_block->scope_var->module;
    parse_import_link(parser, m);

    if (lex->token == tk_comma) {
        lily_next_token(lex);
        import_loop(parser);
    }
}

static void keyword_import(lily_parse_state *parser)
{
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file)
        lily_raise_syn(parser->raiser, "Cannot import a file here.");

    lily_next_token(parser->lex);

    if (block->forward_count)
        error_forward_decl_keyword(parser, KEY_IMPORT);

    import_loop(parser);
}

static void keyword_try(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);
    lily_emit_enter_try_block(parser->emit);
    NEED_COLON_AND_BRACE;
    lily_next_token(lex);
}

static void keyword_except(lily_parse_state *parser)
{
    lily_emit_state *emit = parser->emit;
    lily_block *block = emit->block;

    if (block->block_type != block_try)
        lily_raise_syn(parser->raiser, "except outside of try.");

    lily_next_token(parser->lex);

    lily_class *except_cls = get_type(parser)->cls;
    lily_var *exception_var = NULL;

    /* If it's 'except Exception', then all possible cases have been handled. */
    if (lily_class_greater_eq_id(LILY_ID_EXCEPTION, except_cls) == 0)
        lily_raise_syn(parser->raiser, "'%s' is not a valid exception class.",
                except_cls->name);
    else if (except_cls->generic_count != 0)
        lily_raise_syn(parser->raiser, "'except' type cannot have subtypes.");

    if (emit->block->flags & BLOCK_FINAL_BRANCH)
        lily_raise_syn(parser->raiser, "'except' clause is unreachable.");

    lily_lex_state *lex = parser->lex;

    hide_block_vars(parser);

    if (extra_if_word(parser, "as")) {
        NEED_NEXT_IDENT("Expected a variable name here.")
        exception_var = declare_local_var(parser, except_cls->self_type);
    }

    lily_emit_except_switch(emit, except_cls, exception_var);
    NEED_COLON_AND_NEXT;
}

static void keyword_raise(lily_parse_state *parser)
{
    lily_next_token(parser->lex);
    expression(parser);
    lily_eval_raise(parser->emit, parser->expr);

    if (code_is_after_exit(parser))
        lily_raise_syn(parser->raiser,
                "Statement(s) after 'raise' will not execute.");
}

static void error_class_redeclaration(lily_parse_state *parser,
        lily_item *item)
{
    const char *prefix;
    const char *suffix;
    const char *name;
    const char *what = "";
    lily_class *cls = NULL;

    /* Only classes, enums, and variants will reach here. Find out which one
       and report accordingly. */
    if (item->item_kind & (ITEM_IS_CLASS | ITEM_IS_ENUM)) {
        cls = (lily_class *)item;
        name = cls->name;
    }
    else if (item->item_kind & ITEM_IS_VARIANT) {
        lily_variant_class *variant = (lily_variant_class *)item;

        name = variant->name;
        cls = variant->parent;
    }
    else
        name = "";

    if (cls->module == parser->symtab->prelude_module) {
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

static lily_item *search_for_valid_classlike(lily_parse_state *parser,
        const char *name)
{
    if (name[1] == '\0')
        lily_raise_syn(parser->raiser,
                "'%s' is not a valid class name (too short).", name);

    lily_module_entry *m = parser->symtab->active_module;
    lily_item *item = (lily_item *)find_or_dl_class(parser, m, name);

    return item;
}

static void parse_super(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;

    NEED_NEXT_IDENT("Expected the name of a parent class here.")

    lily_class *super_class = resolve_class_name(parser);

    if (super_class == cls)
        lily_raise_syn(parser->raiser, "A class cannot inherit from itself!");
    else if (super_class->item_kind != ITEM_CLASS_NATIVE)
        lily_raise_syn(parser->raiser, "'%s' cannot be inherited from.",
                lex->label);
    else if (super_class->flags & SYM_IS_FORWARD)
        lily_raise_syn(parser->raiser,
                "Cannot inherit from an incomplete class.");

    cls->parent = super_class;
    cls->prop_count += super_class->prop_count;
    cls->inherit_depth = super_class->inherit_depth + 1;

    if (cls->members) {
        /* These have already been checked for uniqueness against each other,
           but now a parent class is involved. Hide the new symbols, or visible
           symbol search will send them back and not dive in. This is the only
           time that member hiding is necessary. */
        lily_named_sym *save_members, *sym;
        uint16_t adjust = super_class->prop_count;

        sym = save_members = cls->members;
        cls->members = NULL;

        while (sym) {
            if (sym->item_kind == ITEM_PROPERTY) {
                lily_named_sym *search_sym = lily_find_visible_member(cls,
                        sym->name);

                if (search_sym) {
                    cls->members = save_members;
                    error_member_redeclaration(parser, super_class, search_sym);
                }

                sym->reg_spot += adjust;
            }

            sym = sym->next;
        }

        cls->members = save_members;
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
    lily_next_token(parser->lex);

    if (lex->token == tk_left_parenth) {
        lily_next_token(lex);
        if (lex->token == tk_right_parenth)
            lily_raise_syn(parser->raiser,
                    "() must be removed if there are no arguments inside.");

        /* Call expression to handle the rest. This flag tells expression to
           stop on ')'. If this raises, rewind will fix the flags. */
        parser->flags |= PARSER_SUPER_EXPR;
        expression_raw(parser);
        parser->flags &= ~PARSER_SUPER_EXPR;

        /* Move past the closing ')' to line up with the else case below. */
        lily_next_token(lex);
    }
    else
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
    uint16_t save_doc;

    if (parser->flags & PARSER_EXTRA_INFO) {
        /* Classes allow for shorthand properties which will scoop up the
           docblock. Hide it until the class is ready for it. */
        save_doc = parser->flags & PARSER_HAS_DOCBLOCK;
        parser->flags &= ~PARSER_HAS_DOCBLOCK;
    }
    else
        save_doc = 0;

    make_new_function(parser, call_var);
    lily_emit_enter_class_block(parser->emit, cls, call_var);
    parser->current_class = cls;
    lily_tm_add(parser->tm, cls->self_type);
    collect_call_args(parser, call_var, F_COLLECT_CLASS);

    if (lex->token == INHERITANCE_TOKEN)
        parse_super(parser, cls);

    /* Don't make 'self' available until the class is fully constructed. */
    lily_emit_create_block_self(parser->emit, cls->self_type);
    lily_emit_write_class_init(parser->emit);
    finish_define_init(parser, call_var);

    if (cls->members->item_kind == ITEM_PROPERTY)
        lily_emit_write_shorthand_ctor(parser->emit, cls,
                parser->symtab->active_module->var_chain);

    if (cls->parent)
        super_expression(parser, cls);

    lily_emit_activate_block_self(parser->emit);

    if (parser->flags & PARSER_EXTRA_INFO) {
        parser->flags |= save_doc;
        set_definition_doc(parser);
    }
}

/* This is a helper function that scans 'target' to determine if it will require
   any gc information to hold. */
static uint16_t get_gc_flags_for(lily_type *target)
{
    uint16_t result_flag = target->cls->flags;

    if (result_flag & (CLS_GC_TAGGED | CLS_VISITED))
        result_flag = CLS_GC_TAGGED;
    else if ((result_flag & SYM_IS_FORWARD) == 0) {
        result_flag &= CLS_GC_SPECULATIVE;

        if (target->subtype_count) {
            for (uint16_t i = 0;i < target->subtype_count;i++)
                result_flag |= get_gc_flags_for(target->subtypes[i]);
        }
    }
    else {
        /* Forward classes could have anything. Tag both ends. */
        result_flag = CLS_GC_TAGGED;

        /* This type might be polymorphic, so use the class. */
        target->cls->flags |= CLS_GC_TAGGED;
    }

    return result_flag;
}

/* A user-defined class is about to close. This scans over 'target' to find out
   if any properties inside of the class may require gc information. If such
   information is needed, then the class will have the appropriate flags set. */
static void determine_class_gc_flag(lily_class *target)
{
    lily_class *parent_iter = target->parent;
    uint16_t mark = 0;

    if (target->flags & CLS_GC_TAGGED)
        /* This is a forward class that was used before declaration. For safety,
           tag both ends. */
        return;

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

static void collect_and_verify_generics_for(lily_parse_state *parser,
        lily_class *cls)
{
    int16_t generic_count = cls->generic_count;
    lily_type *self_type = cls->self_type;

    /* Default value if no generics are collected. */
    cls->generic_count = 0;
    collect_generics_for(parser, cls);

    if (cls->generic_count == generic_count)
        return;

    int16_t new_count = cls->generic_count;
    lily_msgbuf *msgbuf = lily_mb_flush(parser->msgbuf);

    /* This is the only case wherein the generics of a type can be wrong. */
    lily_mb_add_fmt(msgbuf, "Wrong generics in resolution of %s:\n", cls->name);

    /* If the class expected 0 but was given more, then the self_type has been
       changed. So use just the class name instead. */
    if (generic_count == 0)
        lily_mb_add_fmt(msgbuf, "Expected: %s\n", cls->name);
    else
        lily_mb_add_fmt(msgbuf, "Expected: ^T\n", self_type);

    /* Like above, except it's the current self_type now. */
    if (new_count == 0)
        lily_mb_add_fmt(msgbuf, "Received: %s", cls->name);
    else
        lily_mb_add_fmt(msgbuf, "Received: ^T", cls->self_type);

    /* This class will be hidden, so don't bother fixing the generic count. */
    lily_raise_syn(parser->raiser, lily_mb_raw(msgbuf));
}

static void parse_forward_class_methods(lily_parse_state *parser,
        lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    uint16_t generic_start = lily_gp_save(parser->generics);
    uint16_t method_count = 0;

    /* Manifest mode doesn't need forward methods. */
    if (parser->flags & PARSER_IN_MANIFEST) {
        NEED_CURRENT_TOK(tk_three_dots)
    }

    lily_emit_enter_forward_class_block(parser->emit, cls);
    parser->current_class = cls;

    do {
        uint16_t key_id = keyword_by_name(lex->label);

        parser->modifiers = SYM_IS_FORWARD;
        parse_modifier(parser, key_id);
        lily_gp_restore(parser->generics, generic_start);
        lily_emit_leave_scope_block(parser->emit);
        lily_next_token(lex);
        method_count++;
    } while (lex->token == tk_word);

    cls->forward_count = method_count;
    lily_emit_exit_class_scope(parser->emit);
    lily_emit_leave_block(parser->emit);
    parser->current_class = NULL;
}

static void parse_forward_class_body(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;

    NEED_CURRENT_TOK(tk_left_curly)
    lily_next_token(lex);

    if (lex->token == tk_word)
        parse_forward_class_methods(parser, cls);
    else if (lex->token != tk_three_dots)
        lily_raise_syn(parser->raiser,
                "Expected a forward method or '...' here.");
    else
        lily_next_token(lex);

    NEED_CURRENT_TOK(tk_right_curly)
    lily_next_token(lex);

    /* Classes are always at toplevel, where prior generics are always 0. */
    lily_gp_restore(parser->generics, 0);

    lily_block *scope_block = parser->emit->scope_block;

    scope_block->forward_class_count++;
    scope_block->forward_count += cls->forward_count;
    cls->flags |= SYM_IS_FORWARD;
}

static void keyword_class(lily_parse_state *parser)
{
    lily_block *block = parser->emit->block;
    if (block->block_type != block_file)
        lily_raise_syn(parser->raiser, "Cannot define a class here.");

    lily_lex_state *lex = parser->lex;

    NEED_NEXT_IDENT("Expected a name for the class here.")

    lily_class *search_cls = (lily_class *)search_for_valid_classlike(parser,
                lex->label);
    lily_class *new_cls = NULL;

    if (search_cls &&
        ((parser->modifiers & SYM_IS_FORWARD) ||
         (search_cls->flags & SYM_IS_FORWARD) == 0))
        error_class_redeclaration(parser, (lily_item *)search_cls);

    if (search_cls == NULL) {
        new_cls = lily_new_class(parser->symtab, lex->label, lex->line_num);
        collect_generics_for(parser, new_cls);

        if ((parser->modifiers & SYM_IS_FORWARD)) {
            parse_forward_class_body(parser, new_cls);
            return;
        }
    }
    else {
        new_cls = search_cls;

        /* Generics must match what was requested before. Forward fixing will be
           done by class block entry (in header parse below). */
        collect_and_verify_generics_for(parser, new_cls);
    }

    parse_class_header(parser, new_cls);

    NEED_CURRENT_TOK(tk_left_curly)
    lily_next_token(lex);
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

static void parse_enum_inheritance(lily_parse_state *parser,
        lily_class *enum_cls)
{
    /* These don't take arguments so this isn't useful. */
    if (enum_cls->generic_count != 0)
        lily_raise_syn(parser->raiser,
                "Enums with generics are not allowed to inherit.");

    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);

    if (lex->token != tk_word || strcmp(lex->label, "Integer") != 0)
        lily_raise_syn(parser->raiser,
                "Enums are only allowed to inherit from Integer.");

    enum_cls->parent = parser->symtab->integer_class;
    enum_cls->flags |= CLS_IS_BASIC_NUMBER | CLS_IS_HAS_VALUE;
    lily_next_token(lex);
}

static void parse_value_variant(lily_parse_state *parser,
        lily_variant_class *variant_cls)
{
    lily_lex_state *lex = parser->lex;
    lily_class *enum_cls = variant_cls->parent;

    /* Variants always come before methods, so this is always a variant. */
    lily_variant_class *last = (lily_variant_class *)variant_cls->next;
    int64_t value;

    if (lex->token == tk_equal) {
        lily_next_token(lex);

        if (lex->token != tk_integer)
            lily_raise_syn(parser->raiser, "A number is required here.");

        value = lex->n.integer_val;
        /* Don't pull the next token yet (error context will be off). */
    }
    else if (last)
        value = last->raw_value + 1;
    else
        value = 0;

    lily_type *t;
    lily_literal *lit = lily_get_integer_literal(parser->symtab, &t, value);

    /* Value enums are always monomorphic so this is fine. */
    variant_cls->build_type = enum_cls->self_type;

    /* This works because variants are heavily gated: Parser doesn't allow them
       in type declaration, and they are dispatched by item_kind in expression
       handling. Emitter never feeds raw variants to ts, so these ids won't be
       seen. Finally, symtab doesn't register these variant classes. */
    variant_cls->cls_id = (last ? last->cls_id + 1 : 0);
    variant_cls->raw_value = value;
    variant_cls->backing_lit = lit->reg_spot;
    variant_cls->flags |= CLS_IS_HAS_VALUE | CLS_IS_BASIC_NUMBER;

    if (last) {
        lily_variant_class *c = lily_find_variant_with_lit(enum_cls,
                lit->reg_spot);

        /* This is enough (the above never returns the current variant). */
        if (c)
            lily_raise_syn(parser->raiser,
                    "Duplicate variant value (already in use by %s).", c->name);
    }

    if (lex->token == tk_integer)
        lily_next_token(parser->lex);
}

static void parse_enum_header(lily_parse_state *parser, lily_class *enum_cls)
{
    int is_scoped = (enum_cls->item_kind == ITEM_ENUM_SCOPED);
    lily_lex_state *lex = parser->lex;

    lily_emit_enter_enum_block(parser->emit, enum_cls);
    parser->current_class = enum_cls;
    collect_generics_for(parser, enum_cls);

    lily_type *empty_type = lily_tm_build_empty_variant_type(parser->tm,
            enum_cls);
    int is_value_enum = (lex->token == INHERITANCE_TOKEN);

    if (is_value_enum)
        parse_enum_inheritance(parser, enum_cls);

    NEED_CURRENT_TOK(tk_left_curly)
    NEED_NEXT_IDENT("Expected a variant name here.")

    while (1) {
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

        if (is_value_enum == 0) {
            if (lex->token == tk_left_parenth)
                parse_variant_header(parser, variant_cls);
            else
                variant_cls->build_type = empty_type;
        }
        else
            parse_value_variant(parser, variant_cls);

        if (lex->token == tk_comma) {
            lily_next_token(lex);

            if (lex->token == tk_word) {
                if (lex->label[0] == 'd' &&
                   (strcmp(lex->label, "define") == 0))
                    break;
                else
                    continue;
            }
            else if (lex->token == tk_right_curly)
                break;

            lily_raise_syn(parser->raiser,
                    "Expected a variant name, '}', or 'define' here.");
        }
        else if (lex->token == tk_equal && is_value_enum == 0)
            lily_raise_syn(parser->raiser,
                "Enums must inherit from Integer to have values.");

        break;
    }

    if (enum_cls->variant_size < 2) {
        lily_raise_syn(parser->raiser,
                "An enum must have at least two variants.");
    }

    lily_fix_enum_variant_ids(parser->symtab, enum_cls);

    if (parser->flags & PARSER_EXTRA_INFO)
        enum_cls->doc_id = store_enum_docblock(parser);
}

static void enum_method_check(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    if (lex->token == tk_right_curly)
        return;

    if (lex->token == tk_docblock) {
        save_docblock(parser);
        lily_next_token(lex);
    }

    if (lex->token == tk_word &&
        strcmp(lex->label, "define") == 0) {
        keyword_define(parser);
        return;
    }

    lily_raise_syn(parser->raiser,
            "Expected either 'define' or '}' (did you forget a comma?)");
}

static void determine_enum_gc_flag(lily_class *enum_cls)
{
    lily_named_sym *member_iter = enum_cls->members;
    lily_type *self_type = enum_cls->self_type;

    while ((member_iter->item_kind & ITEM_IS_VARIANT) == 0)
        member_iter = member_iter->next;

    enum_cls->flags |= CLS_VISITED;

    uint16_t enum_flag = 0;

    while (member_iter) {
        if (member_iter->item_kind == ITEM_VARIANT_FILLED) {
            lily_variant_class *variant = (lily_variant_class *)member_iter;
            lily_type *build_type = variant->build_type;
            lily_type **types = build_type->subtypes;
            uint16_t count = build_type->subtype_count;
            uint16_t variant_flag = 0;
            uint16_t i;

            for (i = 0;i < count;i++) {
                lily_type *t = types[i];

                if (t == self_type)
                    continue;

                variant_flag = get_gc_flags_for(t);
                variant->flags |= variant_flag;
                enum_flag |= variant_flag;
            }
        }

        member_iter = member_iter->next;
    }

    /* For simplicity, make sure there's only one of these set. */
    if (enum_flag & CLS_GC_TAGGED)
        enum_flag &= ~CLS_GC_SPECULATIVE;

    enum_cls->flags |= enum_flag;
    enum_cls->flags &= ~CLS_VISITED;
}

static void keyword_enum(lily_parse_state *parser)
{
    lily_block *block = parser->emit->block;
    lily_lex_state *lex = parser->lex;

    if (block->block_type != block_file)
        lily_raise_syn(parser->raiser,
                "Cannot declare an enum while inside a block.");

    NEED_NEXT_IDENT("Expected a name for the enum here.")

    lily_item *search_item = search_for_valid_classlike(parser, lex->label);

    if (search_item)
        error_class_redeclaration(parser, search_item);

    lily_class *enum_cls = lily_new_enum_class(parser->symtab, lex->label,
            lex->line_num);

    parse_enum_header(parser, enum_cls);

    if ((parser->flags & PARSER_IN_MANIFEST) == 0)
        enum_method_check(parser);
}

static void keyword_scoped(lily_parse_state *parser)
{
    lily_block *block = parser->emit->block;
    lily_lex_state *lex = parser->lex;

    if (block->block_type != block_file)
        lily_raise_syn(parser->raiser,
            "Cannot declare an enum while inside a block.");

    lily_next_token(lex);
    expect_word(parser, "enum");
    NEED_NEXT_IDENT("Expected an enum name here.")

    lily_item *search_item = search_for_valid_classlike(parser, lex->label);

    if (search_item)
        error_class_redeclaration(parser, search_item);

    lily_class *enum_cls = lily_new_enum_class(parser->symtab, lex->label,
            lex->line_num);

    enum_cls->item_kind = ITEM_ENUM_SCOPED;
    parse_enum_header(parser, enum_cls);

    if ((parser->flags & PARSER_IN_MANIFEST) == 0)
        enum_method_check(parser);
}

static lily_class *parse_target_to_match(lily_parse_state *parser,
        lily_class *match_cls)
{
    lily_lex_state *lex = parser->lex;
    lily_class *cls;

    NEED_IDENT("Expected a name to match here.")

    if (match_cls->item_kind & ITEM_IS_CLASS) {
        cls = resolve_class_name(parser);

        if (lily_class_greater_eq(match_cls, cls) == 0) {
            lily_raise_syn(parser->raiser,
                    "Class %s does not inherit from matching class %s.",
                    cls->name, match_cls->name);
        }

        /* Forbid non-monomorphic types to avoid the question of what to do
           if the match class has more generics. */
        if (cls->generic_count != 0) {
            lily_raise_syn(parser->raiser,
                    "Class matching only works for types without generics.",
                    cls->name);
        }
    }
    else {
        if (match_cls->item_kind == ITEM_ENUM_SCOPED) {
            /* Lily v2.1 and prior required scoped enums to have their matches
               written as `EnumName.VariantName`. This is done to prevent
               breaking older code unnecessarily. However, it will eventually be
               removed. */
            if (strcmp(match_cls->name, lex->label) == 0) {
                NEED_NEXT_TOK(tk_dot)
                NEED_NEXT_TOK(tk_word)
            }
        }

        cls = (lily_class *)lily_find_variant(match_cls, lex->label);

        if (cls == NULL)
            lily_raise_syn(parser->raiser, "%s is not a member of enum %s.",
                    lex->label, match_cls->name);
    }

    return cls;
}

static lily_var *parse_decompose_var(lily_parse_state *parser, lily_type *type)
{
    lily_lex_state *lex = parser->lex;
    lily_var *result;

    NEED_NEXT_IDENT("Expected a name here (or _ to ignore the value).")

    if (strcmp(lex->label, "_") != 0)
        result = declare_local_var(parser, type);
    else {
        lily_next_token(lex);
        result = NULL;
    }

    return result;
}

static void parse_match_decompositions(lily_parse_state *parser,
        lily_class *case_cls)
{
    lily_lex_state *lex = parser->lex;
    uint16_t kind = case_cls->item_kind;

    if (kind == ITEM_VARIANT_EMPTY)
        return;

    NEED_NEXT_TOK(tk_left_parenth)

    if (kind & ITEM_IS_CLASS) {
        lily_var *var = parse_decompose_var(parser, case_cls->self_type);

        if (var)
            lily_emit_write_class_case(parser->emit, var);
    }
    else if (kind == ITEM_VARIANT_FILLED) {
        lily_type *t = lily_emit_type_for_variant(parser->emit,
                (lily_variant_class *)case_cls);

        /* Skip [0] since it's the return and not one of the arguments. */
        lily_type **var_types = t->subtypes + 1;
        uint16_t stop = t->subtype_count - 1;
        uint16_t i;

        for (i = 0;i < stop;i++) {
            lily_var *var = parse_decompose_var(parser, var_types[i]);

            if (var)
                lily_emit_write_variant_case(parser->emit, var, i);

            if (i == stop - 1)
                break;

            NEED_CURRENT_TOK(tk_comma)
        }
    }

    NEED_CURRENT_TOK(tk_right_parenth)
}

static void parse_multi_match(lily_parse_state *parser, lily_class *match_cls)
{
    uint16_t count = 0;

    while (1) {
        lily_next_token(parser->lex);

        lily_class *case_cls = parse_target_to_match(parser, match_cls);

        if (case_cls->item_kind == ITEM_VARIANT_FILLED)
            lily_raise_syn(parser->raiser,
                "Multi case match is only available to empty variants.");

        if (lily_emit_try_add_match_case(parser->emit, case_cls) == 0)
            lily_raise_syn(parser->raiser, "Already have a case for %s.",
                    parser->lex->label);

        lily_emit_write_multi_match_jump(parser->emit);
        lily_emit_write_match_switch(parser->emit, case_cls);
        lily_next_token(parser->lex);
        count++;

        if (parser->lex->token == tk_comma)
            continue;

        break;
    }

    lily_emit_finish_multi_match(parser->emit, count);
}

static lily_class *parse_match_case(lily_parse_state *parser,
        lily_class *match_cls)
{
    lily_class *case_cls = parse_target_to_match(parser, match_cls);

    lily_emit_branch_switch(parser->emit);

    if (lily_emit_try_add_match_case(parser->emit, case_cls) == 0)
        lily_raise_syn(parser->raiser, "Already have a case for %s.",
                parser->lex->label);

    lily_emit_write_match_switch(parser->emit, case_cls);
    parse_match_decompositions(parser, case_cls);
    lily_next_token(parser->lex);
    return case_cls;
}

static void keyword_case(lily_parse_state *parser)
{
    lily_emit_state *emit = parser->emit;
    lily_block *block = emit->block;

    if (block->block_type != block_match)
        lily_raise_syn(parser->raiser, "case outside of match.");

    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);

    if (block->flags & BLOCK_FINAL_BRANCH)
        lily_raise_syn(parser->raiser, "case in exhaustive match.");

    hide_block_vars(parser);

    lily_class *case_cls = parse_match_case(parser, block->match_type->cls);

    if (lex->token == tk_comma) {
        if (case_cls->item_kind == ITEM_VARIANT_EMPTY)
            parse_multi_match(parser, block->match_type->cls);
        else
            lily_raise_syn(parser->raiser,
                "Multi case match is only available to empty variants.");
    }

    NEED_COLON_AND_NEXT;
}

static void verify_match_with_type(lily_parse_state *parser, lily_type *t,
        const char *what)
{
    uint16_t kind = t->cls->item_kind;

    if ((kind & ITEM_IS_ENUM) == 0 &&
        kind != ITEM_CLASS_NATIVE)
        lily_raise_syn(parser->raiser,
                "%s statement value must be a user class or enum.\n"
                "Received: ^T", what, t);

    if (t->flags & TYPE_IS_INCOMPLETE)
        lily_raise_syn(parser->raiser,
                "%s statement value cannot be an incomplete type.\n"
                "Received: ^T", what, t);
}

static void keyword_match(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);
    expression(parser);

    lily_sym *target = lily_eval_for_result(parser->emit, parser->expr->root);

    verify_match_with_type(parser, target->type, "Match");
    lily_emit_enter_match_block(parser->emit, target);
    NEED_COLON_AND_BRACE;
    lily_next_token(lex);

    if (lex->token != tk_word || strcmp(lex->label, "case") != 0)
        lily_raise_syn(parser->raiser, "Match must start with a case.");

    keyword_case(parser);
}

static void parse_expr_match_case(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_expr_state *es = parser->expr;
    uint16_t spot = parser->expr->pile_current;
    uint16_t var_count = 0;

    NEED_NEXT_IDENT("Expected a variant name to match here.")
    lily_sp_insert(parser->expr_strings, lex->label,
            &parser->expr->pile_current);
    lily_next_token(lex);

    lily_var *last_var = NULL;

    if (lex->token == tk_left_parenth) {
        while (1) {
            NEED_NEXT_IDENT(
                    "Expected a name here (or _ to ignore the value).")

            last_var = declare_match_var(parser, lex->label, lex->line_num);
            lily_next_token(lex);
            var_count++;

            if (lex->token == tk_comma)
                continue;
            else
                break;
        }

        NEED_CURRENT_TOK(tk_right_parenth)
        lily_next_token(lex);
    }

    lily_es_push_expr_match_case(es, last_var, spot, var_count);
    lily_es_collect_arg(es);
    NEED_CURRENT_TOK(tk_colon)
}

static void parse_expr_branch_else(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    NEED_NEXT_TOK(tk_colon)
    lily_es_push_expr_branch_else(parser->expr);
    lily_es_collect_arg(parser->expr);
}

static void hide_match_var_types(lily_parse_state *parser)
{
    int count = parser->emit->block->var_count;

    if (count == 0)
        return;

    lily_var *var_iter = parser->symtab->active_module->var_chain;

    while (count) {
        var_iter->type = NULL;
        var_iter = var_iter->next;
        count--;
    }
}

static void expr_match(lily_parse_state *parser, uint16_t *state)
{
    if ((parser->flags & PARSER_SIMPLE_EXPR) ||
        parser->expr->save_depth != 0)
        lily_raise_syn(parser->raiser, "Cannot use a match expression here.");

    lily_lex_state *lex = parser->lex;

    lily_es_enter_tree(parser->expr, tree_expr_match);

    /* This prevents a closing token from closing this tree. */
    parser->expr->save_depth--;
    lily_next_token(lex);
    simple_expression(parser);
    lily_es_collect_arg(parser->expr);
    NEED_COLON_AND_BRACE;
    lily_next_token(lex);
    lily_emit_enter_expr_match_block(parser->emit);

    if (lex->token != tk_word || strcmp(lex->label, "case") != 0)
        lily_raise_syn(parser->raiser, "Match must start with a case.");

    goto first_case;

    while (1) {
        do {
            if (lex->token == tk_word) {
                if (strcmp(lex->label, "case") == 0) {
first_case:
                    parse_expr_match_case(parser);
                    break;
                }
                else if (strcmp(lex->label, "else") == 0) {
                    parse_expr_branch_else(parser);
                    break;
                }
            }

            lily_raise_syn(parser->raiser,
                    "Expected either 'case' or 'else' here.");
        } while (0);

        lily_next_token(lex);
        simple_expression(parser);
        hide_match_var_types(parser);
        lily_es_collect_arg(parser->expr);

        if (lex->token == tk_right_curly)
            break;
    }

    lily_next_token(parser->lex);
    parser->expr->save_depth++;
    lily_es_leave_tree(parser->expr);
    *state = ST_DONE;
}

void lily_parser_hide_match_vars(lily_parse_state *parser)
{
    hide_block_vars(parser);
}

static void keyword_with(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);
    expression(parser);

    lily_sym *target = lily_eval_for_result(parser->emit, parser->expr->root);
    lily_class *pattern_cls = target->type->cls;

    verify_match_with_type(parser, target->type, "With");
    lily_emit_enter_with_block(parser->emit, target);
    expect_word(parser, "as");
    lily_next_token(lex);
    parse_match_case(parser, pattern_cls);
    NEED_COLON_AND_BRACE;
    lily_next_token(lex);
}

#define ALL_MODIFIERS (ANY_SCOPE | VAR_IS_STATIC)

static void verify_resolve_define_var(lily_parse_state *parser,
        lily_var *define_var, int modifiers)
{
    lily_block *block = parser->emit->block;

    if ((define_var->flags & SYM_IS_FORWARD) == 0)
        error_var_redeclaration(parser, define_var);
    else if (modifiers & SYM_IS_FORWARD)
        lily_raise_syn(parser->raiser,
                "A forward declaration for %s already exists.",
                define_var->name);
    else if ((define_var->flags & ALL_MODIFIERS) !=
             (modifiers & ALL_MODIFIERS))
        error_forward_decl_modifiers(parser, define_var);
    else if (block->block_type == block_anon)
        lily_raise_syn(parser->raiser,
                "Cannot resolve a definition in an anonymous block.");

    /* This forward definition is no longer forward. */
    define_var->flags &= ~SYM_IS_FORWARD;
    block->forward_count--;
}

#undef ALL_MODIFIERS

static lily_var *parse_new_define(lily_parse_state *parser, lily_class *parent,
        uint16_t modifiers)
{
    lily_lex_state *lex = parser->lex;
    const char *name = lex->label;
    lily_var *define_var = find_active_var(parser, name);

    if (define_var == NULL && parent) {
        lily_named_sym *named_sym = lily_find_visible_member(parent, name);

        if (named_sym == NULL)
            ;
        else if (named_sym->flags & SYM_IS_FORWARD)
            define_var = (lily_var *)named_sym;
        else
            error_member_redeclaration(parser, parent, named_sym);
    }

    if (define_var)
        verify_resolve_define_var(parser, define_var, modifiers);
    else if (parent) {
        define_var = new_method_var(parser, parent, name, modifiers,
                lex->line_num);
        make_new_function(parser, define_var);
    }
    else {
        define_var = new_define_var(parser, name, lex->line_num);
        make_new_function(parser, define_var);
    }

    /* Make flags consistent no matter which above case was selected. */
    define_var->flags |= modifiers | SYM_NOT_INITIALIZED;

    return define_var;
}

static void keyword_define(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_class *parent = NULL;
    lily_block_type block_type = parser->emit->block->block_type;
    uint16_t generic_start = lily_gp_save(parser->generics);
    uint16_t modifiers = parser->modifiers;

    if ((parser->emit->block->flags & BLOCK_ALLOW_DEFINE) == 0)
        lily_raise_syn(parser->raiser, "Cannot define a function here.");
    else if (block_type == block_class &&
        (modifiers & ANY_SCOPE) == 0)
        lily_raise_syn(parser->raiser,
                "Class method declaration must start with a scope.");
    else if (block_type & (SCOPE_CLASS | SCOPE_ENUM))
        parent = parser->current_class;

    NEED_NEXT_IDENT("Expected a definition name here.")

    lily_var *define_var = parse_new_define(parser, parent, modifiers);

    lily_emit_enter_define_block(parser->emit, define_var, generic_start);
    collect_generics_for(parser, NULL);
    lily_tm_add(parser->tm, lily_unit_type);

    if (define_var->parent && (define_var->flags & VAR_IS_STATIC) == 0)
        /* Toplevel non-static class methods have 'self' as an implicit first
           argument. */
        lily_tm_add(parser->tm, define_var->parent->self_type);

    collect_call_args(parser, define_var, F_COLLECT_DEFINE);
    finish_define_init(parser, define_var);

    if (parser->flags & PARSER_EXTRA_INFO)
        set_definition_doc(parser);

    if (parser->flags & PARSER_IN_MANIFEST)
        return;

    NEED_CURRENT_TOK(tk_left_curly)
    lily_next_token(lex);

    if (modifiers & SYM_IS_FORWARD) {
        NEED_CURRENT_TOK(tk_three_dots)
        NEED_NEXT_TOK(tk_right_curly)
    }
}

static void verify_static_modifier(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    lily_next_token(lex);

    if (lex->token == tk_word && strcmp(lex->label, "define") == 0)
        return;

    lily_raise_syn(parser->raiser, "'static' must be followed by 'define'.");
}

static void parse_modifier(lily_parse_state *parser, int key)
{
    lily_lex_state *lex = parser->lex;
    uint16_t modifiers = parser->modifiers;
    int in_class = (parser->emit->block->block_type == block_class);

    if (key == KEY_PUBLIC ||
        key == KEY_PROTECTED ||
        key == KEY_PRIVATE) {
        if (key == KEY_PUBLIC)
            modifiers |= SYM_SCOPE_PUBLIC;
        else if (key == KEY_PROTECTED)
            modifiers |= SYM_SCOPE_PROTECTED;
        else
            modifiers |= SYM_SCOPE_PRIVATE;

        if (in_class == 0)
            lily_raise_syn(parser->raiser,
                "Class method scope must be within a class block.");

        lily_next_token(lex);

        if (lex->token == tk_word)
            key = keyword_by_name(lex->label);
    }
    else if (modifiers & SYM_IS_FORWARD && in_class) {
        lily_raise_syn(parser->raiser,
                "Expected a scope here (public, protected, or private).");
    }

    if (key == KEY_STATIC) {
        modifiers |= VAR_IS_STATIC;
        verify_static_modifier(parser);
        key = KEY_DEFINE;
    }

    parser->modifiers = modifiers;

    if (key == KEY_VAR) {
        if (modifiers & SYM_IS_FORWARD)
            lily_raise_syn(parser->raiser, "Cannot use 'forward' with 'var'.");

        keyword_var(parser);
    }
    else if (key == KEY_DEFINE)
        keyword_define(parser);
    else if (key == KEY_CLASS)
        keyword_class(parser);
    else {
        const char *what = "'class', 'var', or 'define'";

        if (modifiers & SYM_IS_FORWARD)
            what = "'class' or 'define'";

        lily_raise_syn(parser->raiser, "Expected %s here.", what);
    }

    parser->modifiers = 0;
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
    lily_lex_state *lex = parser->lex;
    lily_emit_state *emit = parser->emit;
    lily_block_type block_type = emit->block->block_type;
    int key;

    if ((block_type & (SCOPE_CLASS | SCOPE_FILE)) == 0)
        lily_raise_syn(parser->raiser,
                "'forward' must be outside of a block or in a class.");

    parser->modifiers = SYM_IS_FORWARD;
    lily_next_token(lex);

    if (lex->token == tk_word)
        key = keyword_by_name(lex->label);
    else
        key = -1;

    /* This will either be a forward class or forward define. Forward defines
       are complicated, so define handling leaves the block open for the caller
       to fix. */
    parse_modifier(parser, key);

    lily_block *block = emit->block;

    if (block->block_type == block_define) {
        lily_gp_restore(parser->generics, block->generic_start);
        lily_emit_leave_scope_block(emit);
        block->prev->forward_count++;
        lily_next_token(lex);
    }
}

static void keyword_private(lily_parse_state *parser)
{
    parse_modifier(parser, KEY_PRIVATE);
}

static void keyword_protected(lily_parse_state *parser)
{
    parse_modifier(parser, KEY_PROTECTED);
}

static void maybe_capture_stdout(lily_parse_state *parser)
{
    lily_global_state *gs = parser->vm->gs;

    if (gs->stdout_reg_spot != UINT16_MAX)
        return;

    lily_symtab *symtab = parser->symtab;
    lily_module_entry *prelude = symtab->prelude_module;
    lily_var *stdout_var = lily_find_var(prelude, "stdout");

    if (stdout_var)
        gs->stdout_reg_spot = stdout_var->reg_spot;
}

static void maybe_fix_print(lily_parse_state *parser)
{
    lily_global_state *gs = parser->vm->gs;

    if (gs->stdout_reg_spot == UINT16_MAX)
        return;

    lily_symtab *symtab = parser->symtab;
    lily_module_entry *prelude = symtab->prelude_module;
    lily_var *print_var = lily_find_var(prelude, "print");

    if (print_var == NULL)
        return;

    /* Swap out the default implementation of print for one that will check if
       stdin is closed first. */
    lily_value *print_value = gs->readonly_table[print_var->reg_spot];
    lily_function_val *print_func = print_value->value.function;

    print_func->foreign_func = lily_stdout_print;
}

static void main_func_setup(lily_parse_state *parser)
{
    lily_register_classes(parser->symtab, parser->vm);
    lily_prepare_main(parser->emit, parser->toplevel_func);

    parser->vm->gs->readonly_table = parser->symtab->literals->data;

    maybe_capture_stdout(parser);
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
    parser->flags &= ~PARSER_IS_EXECUTING;
}

static void parse_block_exit(lily_parse_state *parser)
{
    lily_emit_state *emit = parser->emit;
    lily_block *block = emit->block;
    lily_lex_state *lex = parser->lex;

    if (block->block_type & (SCOPE_FILE | SCOPE_LAMBDA))
        lily_raise_syn(parser->raiser, "'}' outside of a block.");

    hide_block_vars(parser);

    switch (block->block_type) {
        case block_define: {
            lily_gp_restore(parser->generics, block->generic_start);
            lily_emit_leave_define_block(emit);
            lily_next_token(lex);

            if (emit->block->block_type == block_enum)
                enum_method_check(parser);

            break;
        }
        case block_class:
            if (block->forward_count)
                error_forward_decl_pending(parser);

            determine_class_gc_flag(parser->current_class);
            parser->current_class = NULL;
            lily_gp_restore(parser->generics, 0);
            lily_emit_leave_class_block(emit);
            lily_next_token(lex);
            break;
        case block_enum:
            determine_enum_gc_flag(parser->current_class);
            parser->current_class = NULL;
            lily_gp_restore(parser->generics, 0);
            lily_emit_exit_class_scope(emit);
            lily_emit_leave_scope_block(emit);

            lily_next_token(lex);
            break;
        case block_do_while:
            lily_next_token(parser->lex);
            expect_word(parser, "while");
            lily_next_token(parser->lex);
            expression(parser);
            lily_eval_do_while_condition(parser->emit, parser->expr);
            lily_emit_leave_block(parser->emit);
            break;
        case block_match:
            if (lily_emit_try_leave_match_block(emit) == 0)
                lily_raise_syn(parser->raiser,
                        lily_mb_raw(parser->raiser->aux_msgbuf));

            lily_next_token(lex);
            break;
        default:
            lily_emit_leave_block(parser->emit);
            lily_next_token(lex);
            break;
    }
}

static void process_docblock(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    save_docblock(parser);
    lily_next_token(lex);

    int key_id;

    if (lex->token == tk_word)
        key_id = keyword_by_name(lex->label);
    else
        key_id = KEY_BAD_ID;

    if (valid_docblock_table[key_id]) {
        lily_block_type block_type = parser->emit->block->block_type;

        if ((block_type & (SCOPE_CLASS | SCOPE_ENUM | SCOPE_FILE)) == 0)
            lily_raise_syn(parser->raiser,
                    "Docblocks are only allowed on toplevel symbols.");

        handlers[key_id](parser);
    }
    else
        lily_raise_syn(parser->raiser, "A docblock is not allowed here.");
}

/* This is the entry point into parsing regardless of the starting mode. This
   should only be called by the content handling functions that do the proper
   initialization beforehand.
   This does not execute code. If it returns, it was successful (an error is
   raised otherwise). */
static void parser_loop(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    int key_id = KEY_BAD_ID;

    lily_next_token(lex);

    if (parser->config->extra_info == 1)
        init_doc(parser);

    while (1) {
        if (lex->token == tk_word) {
            /* Is this a keyword or an expression? */
            key_id = keyword_by_name(lex->label);
            if (key_id != KEY_BAD_ID)
                handlers[key_id](parser);
            else {
                /* Give this to expression to figure out. */
                expression(parser);
                lily_eval_expr(parser->emit, parser->expr);
            }
        }
        else if (lex->token == tk_left_curly) {
            lily_emit_enter_anon_block(parser->emit);
            lily_next_token(lex);
        }
        else if (lex->token == tk_right_curly)
            parse_block_exit(parser);
        else if (lex->token == tk_eof) {
            lily_block *b = parser->emit->block;

            if (b->block_type != block_file)
                lily_raise_syn(parser->raiser,
                        "Reached end of file while still inside a block.");
            else if (b->forward_count || b->forward_class_count)
                error_forward_decl_pending(parser);

            if (b->prev != NULL)
                finish_import(parser);
            else
                break;
        }
        else if (lex->token == tk_docblock)
            process_docblock(parser);
        else {
            expression(parser);
            lily_eval_expr(parser->emit, parser->expr);
        }
    }
}

static void manifest_define(lily_parse_state *parser)
{
    lily_emit_state *emit = parser->emit;

    keyword_define(parser);

    /* Close the definition to prevent storing code. */
    hide_block_vars(parser);
    lily_gp_restore(parser->generics, emit->block->generic_start);

    /* This exits the definition without doing a return check. */
    lily_emit_leave_scope_block(emit);
}

static void manifest_forward(lily_parse_state *parser)
{
    lily_block_type block_type = parser->emit->block->block_type;

    if (block_type != block_file)
        lily_raise_syn(parser->raiser, "'forward' must be at toplevel.");

    lily_next_token(parser->lex);
    expect_word(parser, "class");
    parser->modifiers |= SYM_IS_FORWARD;
    keyword_class(parser);
    parser->modifiers = 0;
}

static void manifest_foreign(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    int is_static = 0;

    lily_next_token(lex);

    if (lex->token == tk_word && strcmp(lex->label, "static") == 0) {
        is_static = 1;
        lily_next_token(lex);
    }

    expect_word(parser, "class");
    keyword_class(parser);
    parser->current_class->item_kind = ITEM_CLASS_FOREIGN;

    lily_named_sym *sym = parser->current_class->members;

    if (sym->item_kind == ITEM_PROPERTY)
        lily_raise_syn(parser->raiser,
                "Only native classes can have class properties.");

    if (is_static) {
        /* Hide the constructor (it's the only entry). */
        lily_var *ctor = (lily_var *)sym;

        ctor->next = parser->symtab->hidden_function_chain;
        parser->symtab->hidden_function_chain = ctor;
        parser->current_class->members = NULL;
    }
}

static void manifest_constant(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    if (parser->current_class)
        lily_raise_syn(parser->raiser,
                "Cannot declare a constant inside a class or enum.");

    NEED_NEXT_IDENT("Expected a name for a constant here.")

    lily_var *var = declare_constant(parser);

    NEED_CURRENT_TOK(tk_colon)
    lily_next_token(lex);
    var->type = get_type(parser);

    uint16_t cls_id = var->type->cls_id;

    if (cls_id != LILY_ID_DOUBLE &&
        cls_id != LILY_ID_INTEGER &&
        cls_id != LILY_ID_STRING &&
        cls_id != LILY_ID_BOOLEAN &&
        cls_id != LILY_ID_BYTESTRING)
        lily_raise_syn(parser->raiser,
                "Constants must have a primitive type (this has ^T).",
                var->type);
}

static void manifest_var(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_class *cls = parser->current_class;
    uint16_t modifiers = parser->modifiers;
    lily_named_sym *sym;
    lily_token want_token, other_token;

    /* Manifest can use parser's current class because definitions aren't
       actually entered. */
    if (cls) {
        if (cls->item_kind & ITEM_IS_ENUM)
            lily_raise_syn(parser->raiser,
                    "Var declaration not allowed inside of an enum.");
        else if (cls->item_kind == ITEM_CLASS_FOREIGN)
            lily_raise_syn(parser->raiser,
                    "Only native classes can have class properties.");
        else if (modifiers == 0)
            lily_raise_syn(parser->raiser,
                    "Class var declaration must start with a scope.");

        want_token = tk_prop_word;
        other_token = tk_word;
    }
    else {
        want_token = tk_word;
        other_token = tk_prop_word;
    }

    lily_next_token(lex);

    /* For this special case, give a useful error message. */
    if (lex->token == other_token)
        bad_decl_token(parser);

    NEED_CURRENT_TOK(want_token)

    if (lex->token == tk_word)
        sym = (lily_named_sym *)declare_scoped_var(parser);
    else
        sym = (lily_named_sym *)declare_property(parser, modifiers);

    NEED_CURRENT_TOK(tk_colon)
    lily_next_token(lex);
    sym->type = get_type(parser);
}

static void manifest_modifier(lily_parse_state *parser, int key)
{
    lily_lex_state *lex = parser->lex;
    uint16_t modifiers = 0;

    if (key == KEY_PUBLIC)
        modifiers |= SYM_SCOPE_PUBLIC;
    else if (key == KEY_PROTECTED)
        modifiers |= SYM_SCOPE_PROTECTED;
    else if (key == KEY_PRIVATE)
        modifiers |= SYM_SCOPE_PRIVATE;

    NEED_NEXT_IDENT("Expected either 'static' or 'define' here.")
    key = keyword_by_name(lex->label);

    if (key == KEY_STATIC) {
        modifiers |= VAR_IS_STATIC;
        verify_static_modifier(parser);
        key = KEY_DEFINE;
    }

    parser->modifiers = modifiers;

    /* No else because invalid keywords will be caught on the next pass. */
    if (key == KEY_DEFINE) {
        if (modifiers & (SYM_SCOPE_PROTECTED | SYM_SCOPE_PRIVATE))
            lily_raise_syn(parser->raiser,
                    "Class methods defined in manifest mode must be public.");

        manifest_define(parser);
    }
    else if (key == KEY_VAR)
        manifest_var(parser);

    parser->modifiers = 0;
}

static void manifest_override_prelude(lily_parse_state *parser)
{
    lily_symtab *symtab = parser->symtab;
    lily_module_entry *prelude = symtab->prelude_module;
    lily_block *scope_block = parser->emit->scope_block;

    parser->symtab->active_module = prelude;
    scope_block->scope_var->module = prelude;

    lily_var *var_iter = prelude->var_chain;
    uint16_t count = 0;

    while (var_iter) {
        count++;
        var_iter = var_iter->next;
    }

    parser->emit->block->var_count = count;
    hide_block_vars(parser);
}

static void manifest_library(lily_parse_state *parser)
{
    lily_module_entry *m = parser->symtab->active_module;
    lily_block *scope_block = parser->emit->scope_block;
    lily_lex_state *lex = parser->lex;

    if (scope_block->block_type != block_file)
        lily_raise_syn(parser->raiser,
                "Library keyword must be at toplevel.");

    if (m->doc_id != UINT16_MAX)
        lily_raise_syn(parser->raiser,
                "Library keyword has already been used.");

    lily_var *scope_var = scope_block->scope_var;

    if (m->class_chain || m->var_chain != scope_var)
        lily_raise_syn(parser->raiser,
                "Library keyword must come before other keywords.");

    /* This keyword takes an identifier to use in place of the loadname. The
       manifest files for predefined modules need this so they can export the
       right name for tooling. */
    NEED_NEXT_IDENT("Expected a library name here.")

    if (strcmp(lex->label, "prelude") == 0) {
        manifest_override_prelude(parser);
        m = parser->symtab->prelude_module;
    }

    if (parser->flags & PARSER_HAS_DOCBLOCK)
        m->doc_id = store_docblock(parser);

    lily_free(m->loadname);
    m->loadname = lily_malloc((strlen(lex->label) + 1) * sizeof(*m->loadname));
    strcpy(m->loadname, lex->label);

    lily_next_token(lex);
}

static void expect_manifest_header(lily_parse_state *parser)
{
    /* This is a trick inspired by "use strict". Code files passed to manifest
       parsing will fail here. Manifest files passed to code parsing will fail
       to load this module. */
    if (lily_read_manifest_header(parser->lex) == 0)
        lily_raise_syn(parser->raiser,
                "Files in manifest mode must start with 'import manifest'.");
}

static void manifest_import(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    if (parser->flags & PARSER_HAS_DOCBLOCK)
        lily_raise_syn(parser->raiser,
                "Import keyword should not have a docblock.");

    lily_next_token(lex);

    /* The loop will check the header and queue up the first token. */
    import_loop(parser);
}

static void manifest_block_exit(lily_parse_state *parser)
{
    lily_block_type block_type = parser->emit->block->block_type;

    if (block_type == block_file)
        lily_raise_syn(parser->raiser, "'}' outside of a block.");
    else if (block_type == block_class)
        hide_block_vars(parser);
    /* Otherwise it's an enum, and there's nothing more to do for those. */

    parser->current_class = NULL;
    lily_gp_restore(parser->generics, 0);
    lily_emit_leave_scope_block(parser->emit);
    lily_next_token(parser->lex);
}

static void manifest_predefined(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;
    lily_module_entry *prelude = symtab->prelude_module;

    /* This keyword is strictly for the prelude manifest. It allows the prelude
       module to redefine prelude classes/enums.
       Unlike other keywords, this one performs limited error checking because
       it assumes the prelude manifest is correct. */
    if (symtab->active_module != prelude)
        lily_raise_syn(parser->raiser,
                "'predefined' only available to the prelude module.");

    NEED_NEXT_IDENT("Predefined symbol name expected here.")

    lily_class *cls = find_or_dl_class(parser, prelude, lex->label);

    /* Drop everything in this target. The methods have been loaded into vm
       tables already, so this won't break existing declarations. This will,
       however, prevent new declarations from doing anything. Tooling works
       around that by being loaded first.
       Deleting properties doesn't NULL the members because the usual callers
       don't need it to do that. */
    lily_free_properties(cls);
    cls->members = NULL;

    if (cls->item_kind & ITEM_IS_ENUM) {
        parse_enum_header(parser, cls);
        fix_option_result_class_ids(cls);
        lily_fix_enum_type_ids(cls);
    }
    else {
        collect_generics_for(parser, cls);
        parse_class_header(parser, cls);
        lily_next_token(lex);

        /* Parsing a class header creates a constructor that only native
           predefined classes need. */
        if (cls->item_kind == ITEM_CLASS_FOREIGN) {
            lily_free_properties(cls);
            cls->members = NULL;
        }
        else
            fix_predefined_class_id(parser, parser->current_class);
    }
}

static void manifest_loop(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    int key_id = KEY_BAD_ID;

    parser->flags |= PARSER_IN_MANIFEST;
    init_doc(parser);
    expect_manifest_header(parser);
    lily_next_token(lex);

    while (1) {
        if (lex->token == tk_docblock) {
            /* Store documentation for the keyword to pull. */
            save_docblock(parser);
            lily_next_token(lex);

            if (lex->token != tk_word)
                lily_raise_syn(parser->raiser,
                        "Docblocks must be followed by a declaration keyword.");
        }

        if (lex->token == tk_word) {
            key_id = keyword_by_name(lex->label);

            if (key_id == KEY_DEFINE)
                manifest_define(parser);
            else if (key_id == KEY_CLASS ||
                     key_id == KEY_ENUM ||
                     key_id == KEY_SCOPED) {
                handlers[key_id](parser);
            }
            else if (key_id == KEY_PUBLIC ||
                     key_id == KEY_PROTECTED ||
                     key_id == KEY_PRIVATE)
                manifest_modifier(parser, key_id);
            else if (key_id == KEY_VAR)
                manifest_var(parser);
            else if (key_id == KEY_CONSTANT)
                manifest_constant(parser);
            else if (key_id == KEY_IMPORT)
                manifest_import(parser);
            else if (key_id == KEY_FORWARD)
                manifest_forward(parser);
            else if (strcmp("foreign", lex->label) == 0)
                manifest_foreign(parser);
            else if (strcmp("library", lex->label) == 0)
                manifest_library(parser);
            else if (strcmp("predefined", lex->label) == 0)
                manifest_predefined(parser);
            else
                lily_raise_syn(parser->raiser,
                        "Invalid keyword %s for manifest.", lex->label);
        }
        else if (lex->token == tk_right_curly)
            manifest_block_exit(parser);
        else if (lex->token == tk_eof) {
            lily_block *b = parser->emit->block;

            if (b->block_type != block_file)
                lily_raise_syn(parser->raiser,
                        "Reached end of file while still inside a block.");

            if (b->forward_class_count)
                error_forward_decl_pending(parser);

            if (b->prev != NULL)
                finish_import(parser);
            else
                break;
        }
        else
            lily_raise_syn(parser->raiser,
                    "Expected a keyword, '}', or end of file here.");
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
    lily_type *t;
    lily_literal *lit = lily_get_string_literal(parser->symtab, &t, filename);
    char *path = lily_as_string_raw((lily_value *)lit);

    lily_free(module->dirname);

    /* Strange errors occur when the first module is allowed to import itself.
       Setting this prevents those errors. */
    module->flags = MODULE_IN_EXECUTION;
    module->path = path;
    module->dirname = lily_ims_dir_from_path(path);
    module->cmp_len = (uint16_t)strlen(path);
    module->root_dirname = module->dirname;
    /* The loadname isn't set because the first module isn't importable. */

    parser->emit->protos->data[0]->module_path = path;
}

static FILE *load_file_to_parse(lily_parse_state *parser, const char *path)
{
    FILE *load_file = fopen(path, "r");
    if (load_file == NULL) {
        char buffer[LILY_STRERROR_BUFFER_SIZE];

        lily_strerror(buffer);
        lily_raise_raw(parser->raiser, "Failed to open %s: (%s).", path,
                buffer);
    }

    return load_file;
}

static int open_first_content(lily_state *s, const char *filename,
        char *content)
{
    lily_parse_state *parser = s->gs->parser;

    if (parser->flags & PARSER_HAS_CONTENT ||
        parser->rs->has_exited)
        return 0;

    /* Loading initial content should only be done outside of execution, so
       using the parser's base jump is okay. */
    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        lily_ims_process_sys_dirs(parser, parser->config);

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
            /* Strings sent to be parsed are expected to be on a caller's stack
               somewhere. There shouldn't be a need to copy this string. */
            load_type = et_shallow_string;
            load_content = content;
        }

        /* Rewind before loading content so it starts with a fresh slate. */
        if (parser->rs->pending)
            rewind_interpreter(parser);

        /* Always rewind the raiser to account for content loading not setting a
           pending rewind if it fails. */
        lily_rewind_raiser(parser->raiser);
        initialize_rewind(parser);
        lily_lexer_load(parser->lex, load_type, load_content);
        /* The first module is now rooted based on the name given. */
        update_main_name(parser, filename);

        parser->flags = PARSER_HAS_CONTENT;
        return 1;
    }

    /* Do not set a pending rewind here, because no processing took place. */

    return 0;
}

void lily_parser_exit(lily_state *s, uint8_t status)
{
    lily_parse_state *parser = s->gs->parser;
    lily_rewind_state *rs = parser->rs;
    lily_jump_link *jump_iter = parser->raiser->all_jumps;

    rs->exit_status = status;
    rs->has_exited = 1;

    while (jump_iter->prev != NULL)
        jump_iter = jump_iter->prev;

    /* Fix this so raiser deletes all the jumps. */
    parser->raiser->all_jumps = jump_iter;
    longjmp(jump_iter->jump, 1);
}

uint8_t lily_exit_code(lily_state *s)
{
    lily_parse_state *parser = s->gs->parser;
    lily_rewind_state *rs = parser->rs;
    uint8_t result;

    if (rs->has_exited)
        result = rs->exit_status;
    else if (parser->raiser->source == err_from_none)
        result = EXIT_SUCCESS;
    else
        result = EXIT_FAILURE;

    return result;
}

int lily_has_exited(lily_state *s)
{
    return s->gs->parser->rs->has_exited;
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

int lily_parse_manifest(lily_state *s)
{
    lily_parse_state *parser = s->gs->parser;

    if ((parser->flags & PARSER_HAS_CONTENT) == 0)
        return 0;

    parser->flags = 0;

    if (setjmp(parser->raiser->all_jumps->jump) == 0) {
        manifest_loop(parser);

        lily_pop_lex_entry(parser->lex);
        lily_mb_flush(parser->msgbuf);

        /* Manifest should never run code. */
        lily_clear_main(parser->emit);

        return 1;
    }
    else
        parser->rs->pending = 1;

    return 0;
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

            /* Add value doesn't quote String values, because most callers do
               not want that. This one does, so bypass that. */
            if (reg->flags & V_STRING_FLAG)
                lily_mb_add_fmt(msgbuf, "\"%s\"\n", reg->value.string->string);
            else if (sym->type != lily_unit_type) {
                lily_mb_add_value(msgbuf, s, reg);

                /* Traceback has a newline at the end. Follow that example. */
                lily_mb_add_char(msgbuf, '\n');
            }

            /* Unit values are not interesting enough to print. */

            *text = lily_mb_raw(msgbuf);
        }

        return 1;
    }
    else
        parser->rs->pending = 1;

    return 0;
}

lily_function_val *lily_find_function(lily_vm_state *vm, const char *name)
{
    lily_var *v = find_active_var(vm->gs->parser, name);
    lily_function_val *result;

    if (v)
        result = vm->gs->readonly_table[v->reg_spot]->value.function;
    else
        result = NULL;

    return result;
}

lily_config *lily_config_get(lily_state *s)
{
    return s->gs->parser->config;
}

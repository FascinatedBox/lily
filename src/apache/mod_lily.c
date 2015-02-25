/*  mod_lily.c
    This is an apache binding for the Lily language. */
#include <ctype.h>

#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "apr_file_io.h"
#include "util_script.h"

#include "lily_parser.h"
#include "lily_bind.h"
#include "lily_lexer.h"
#include "lily_value.h"
#include "lily_impl.h"

#define malloc_mem(size) mem_func(NULL, size)
#define free_mem(ptr)    mem_func(ptr, 0)

void lily_impl_puts(void *data, char *text)
{
    ap_rputs(text, (request_rec *)data);
}

static int apache_read_line_fn(lily_lex_entry *entry)
{
    char ch;
    int bufsize, i, ok, utf8_check;
    lily_lex_state *lexer = entry->lexer;
    char *input_buffer = lexer->input_buffer;
    apr_file_t *input_file = (apr_file_t *)entry->source;
    apr_status_t result;
    bufsize = lexer->input_size;

    i = 0;
    utf8_check = 0;

    while (1) {
        result = apr_file_getc(&ch, input_file);
        if ((i + 2) == bufsize) {
            lily_grow_lexer_buffers(lexer);

            input_buffer = lexer->input_buffer;
        }

        if (result != APR_SUCCESS) {
            lexer->input_buffer[i] = '\n';
            lexer->input_buffer[i+1] = '\0';
            lexer->input_end = i + 1;
            /* If i is 0, then nothing is on this line except eof. Return 0 to
               let the caller know this is the end. Don't bump the line number
               because it's already been counted.

             * If it isn't 0, then the last line ended with an eof instead of a
               newline. Return 1 to let the caller know that there's more. The
               line number is also bumped (since a line has been scanned in). */
            ok = !!i;
            lexer->line_num += ok;
            break;
        }

        input_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            lexer->input_end = i;
            lexer->line_num++;
            ok = 1;

            if (ch == '\r') {
                input_buffer[i] = '\n';
                apr_file_getc(&ch, input_file);
                if (ch != '\n')
                    apr_file_ungetc(ch, input_file);
            }

            input_buffer[i + 1] = '\0';
            break;
        }
        else if (((unsigned char)ch) > 127)
            utf8_check = 1;

        i++;
    }

    if (utf8_check)
        lily_lexer_utf8_check(lexer);

    return ok;
}

static void apache_close_fn(lily_lex_entry *entry)
{
    apr_file_close(entry->source);
}


/** Shared common functions **/


static lily_hash_elem *bind_hash_elem_with_values(lily_mem_func mem_func,
        char *sipkey, lily_value *key, lily_value *value)
{
    lily_hash_elem *elem = malloc_mem(sizeof(lily_hash_elem));

    elem->next = NULL;
    elem->key_siphash = lily_calculate_siphash(sipkey, key);
    elem->elem_key = key;
    elem->elem_value = value;

    return elem;
}

static lily_var *bind_hash_str_str_var(lily_symtab *symtab, char *name)
{
    /* hash[str, str] */
    const int ids[] = {SYM_CLASS_HASH, SYM_CLASS_STRING, SYM_CLASS_STRING};

    lily_type *hash_type = lily_try_type_from_ids(symtab, ids);
    lily_var *bind_var = lily_try_new_var(symtab, hash_type, name, 0);
    lily_hash_val *var_hash = lily_try_new_hash_val(symtab->mem_func);

    bind_var->value.hash = var_hash;
    bind_var->flags &= ~VAL_IS_NIL;
    return bind_var;
}

static void make_package(lily_parse_state *parser, lily_var *package_var,
        int var_count, int register_save, lily_var *save_chain)
{
    lily_mem_func mem_func = parser->mem_func;
    lily_symtab *symtab = parser->symtab;

    lily_package_val *pval = malloc_mem(sizeof(lily_package_val));
    lily_var **package_vars = malloc_mem(var_count * sizeof(lily_var *));

    int i = 0;
    lily_var *var_iter = parser->symtab->var_chain;
    while (var_iter != save_chain) {
        package_vars[i] = var_iter;
        i++;
        var_iter = var_iter->next;
    }
    symtab->var_chain = save_chain;
    symtab->next_register_spot = register_save;

    pval->refcount = 1;
    pval->name = package_var->name;
    pval->gc_entry = NULL;
    pval->var_count = i;
    pval->vars = package_vars;
    package_var->flags &= ~VAL_IS_NIL;
    package_var->value.package = pval;
}


/** Binding server::env and server::get **/


struct table_bind_data {
    lily_parse_state *parser;
    lily_symtab *symtab;
    request_rec *r;
    int ok;
    lily_hash_val *hash_val;
    char *sipkey;
};

static int bind_table_entry(void *data, const char *key, const char *value)
{
    struct table_bind_data *d = data;
    lily_mem_func mem_func = d->parser->mem_func;

    lily_value *elem_key = lily_bind_string(d->symtab, key);
    lily_value *elem_value = lily_bind_string(d->symtab, value);
    lily_hash_elem *new_elem = bind_hash_elem_with_values(mem_func, d->sipkey,
            elem_key, elem_value);

    new_elem->next = d->hash_val->elem_chain;
    d->hash_val->elem_chain = new_elem;
    return TRUE;
}

static void bind_table_as(lily_parse_state *parser, request_rec *r,
        apr_table_t *table, char *name)
{
    lily_symtab *symtab = parser->symtab;

    lily_var *hash_var = bind_hash_str_str_var(symtab, name);

    struct table_bind_data data;
    data.parser = parser;
    data.symtab = parser->symtab;
    data.r = r;
    data.ok = 1;
    data.hash_val = hash_var->value.hash;
    data.sipkey = parser->vm->sipkey;
    apr_table_do(bind_table_entry, &data, table, NULL);
}


/** Bind server::httpmethod. This data is already available through
    server::env["REQUEST_METHOD"], so this is simply for convenience. **/


static void bind_httpmethod(lily_parse_state *parser, request_rec *r)
{
    lily_mem_func mem_func = parser->mem_func;
    lily_class *string_cls = lily_class_by_id(parser->symtab,
            SYM_CLASS_STRING);

    lily_type *string_type = string_cls->type;
    lily_var *var = lily_try_new_var(parser->symtab, string_type, "httpmethod",
            0);

    lily_string_val *sv = malloc_mem(sizeof(lily_string_val));
    char *sv_buffer = malloc_mem(strlen(r->method) + 1);

    strcpy(sv_buffer, r->method);

    sv->string = sv_buffer;
    sv->refcount = 1;
    sv->size = strlen(r->method);

    var->value.string = sv;
    var->flags &= ~VAL_IS_NIL;
}


/** Binding server::post **/


static void bind_post(lily_parse_state *parser, request_rec *r)
{
    lily_var *post_var = bind_hash_str_str_var(parser->symtab, "post");
    lily_hash_val *hash_val = post_var->value.hash;

    apr_array_header_t *pairs;
    apr_off_t len;
    apr_size_t size;
    char *buffer;
    char *sipkey = parser->vm->sipkey;
    lily_symtab *symtab = parser->symtab;
    lily_mem_func mem_func = parser->mem_func;

    /* Credit: I found out how to use this by reading httpd 2.4's mod_lua
       (specifically req_parsebody of lua_request.c). */
    int res = ap_parse_form_data(r, NULL, &pairs, -1, 1024 * 8);
    if (res == OK) {
        while (pairs && !apr_is_empty_array(pairs)) {
            ap_form_pair_t *pair = (ap_form_pair_t *) apr_array_pop(pairs);
            apr_brigade_length(pair->value, 1, &len);
            size = (apr_size_t) len;
            buffer = malloc_mem(size + 1);

            apr_brigade_flatten(pair->value, buffer, &size);
            buffer[len] = 0;

            lily_value *elem_key = lily_bind_string(symtab, pair->name);
            /* Give the buffer to the value to save memory. */
            lily_value *elem_value = lily_bind_string_take_buffer(symtab,
                    buffer);
            lily_hash_elem *new_elem = bind_hash_elem_with_values(mem_func,
                    sipkey, elem_key, elem_value);

            new_elem->next = hash_val->elem_chain;
            hash_val->elem_chain = new_elem;
        }
    }
}


/** Binding the server package itself **/


static void apache_bind_server(lily_parse_state *parser, request_rec *r)
{
    lily_symtab *symtab = parser->symtab;
    lily_class *package_cls = lily_class_by_id(symtab, SYM_CLASS_PACKAGE);
    lily_type *package_type = lily_try_type_for_class(symtab, package_cls);
    lily_var *bound_var = lily_try_new_var(symtab, package_type, "server", 0);

    lily_var *save_chain = symtab->var_chain;
    int save_spot = symtab->next_register_spot;

    ap_add_cgi_vars(r);
    ap_add_common_vars(r);
    bind_table_as(parser, r, r->subprocess_env, "env");

    apr_table_t *http_get_args;
    ap_args_to_table(r, &http_get_args);

    bind_table_as(parser, r, http_get_args, "get");
    bind_post(parser, r);
    bind_httpmethod(parser, r);

    /* env, get, and post need to be pulled into the server namespace. That's
       what the 3 is for. */
    make_package(parser, bound_var, 3, save_spot, save_chain);
}

static int lily_handler(request_rec *r)
{
    if (strcmp(r->handler, "lily"))
        return DECLINED;

    r->content_type = "text/html";

    apr_file_t *lily_file;
    apr_status_t result;
    result = apr_file_open(&lily_file, r->filename, APR_READ, APR_OS_DEFAULT,
            r->pool);

    /* File not found? Give up now. */
    if (result != APR_SUCCESS)
        return DECLINED;

    lily_parse_state *parser = lily_new_parse_state(NULL, r, 0, NULL);

    apache_bind_server(parser, r);

    lily_parse_special(parser, lm_tags, lily_file, r->filename,
        apache_read_line_fn, apache_close_fn);

    lily_free_parse_state(parser);

    return OK;
}

static void lily_register_hooks(apr_pool_t *p)
{
    ap_hook_handler(lily_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA lily_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                  /* create per-dir    config structures */
    NULL,                  /* merge  per-dir    config structures */
    NULL,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
    NULL,                  /* table of config file commands       */
    lily_register_hooks    /* register hooks                      */
};


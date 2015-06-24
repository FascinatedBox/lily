/*  mod_lily.c
    This is an apache binding for the Lily language. */
#include <ctype.h>

#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "util_script.h"

#include "lily_alloc.h"
#include "lily_parser.h"
#include "lily_bind.h"
#include "lily_lexer.h"
#include "lily_value.h"
#include "lily_impl.h"
#include "lily_seed.h"

#include "lily_cls_hash.h"

void lily_impl_puts(void *data, char *text)
{
    ap_rputs(text, (request_rec *)data);
}

static lily_hash_elem *bind_hash_elem_with_values(char *sipkey,
        lily_value *key, lily_value *value)
{
    lily_hash_elem *elem = lily_malloc(sizeof(lily_hash_elem));

    elem->next = NULL;
    elem->key_siphash = lily_calculate_siphash(sipkey, key);
    elem->elem_key = key;
    elem->elem_value = value;

    return elem;
}

static lily_hash_val *get_new_tied_hash(lily_symtab *symtab, lily_var *tie_var)
{
    lily_hash_val *hash_val = lily_new_hash_val();
    lily_value v;
    v.type = tie_var->type;
    v.flags = 0;
    v.value.hash = hash_val;

    lily_tie_value(symtab, tie_var, &v);

    return hash_val;
}

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

    lily_value *elem_key = lily_bind_string(d->symtab, key);
    lily_value *elem_value = lily_bind_string(d->symtab, value);
    lily_hash_elem *new_elem = bind_hash_elem_with_values(d->sipkey, elem_key,
            elem_value);

    new_elem->next = d->hash_val->elem_chain;
    d->hash_val->elem_chain = new_elem;
    return TRUE;
}

static void bind_table_as(lily_parse_state *parser, request_rec *r,
        apr_table_t *table, char *name, lily_var *var)
{
    lily_symtab *symtab = parser->symtab;

    lily_hash_val *hash_val = get_new_tied_hash(symtab, var);

    struct table_bind_data data;
    data.parser = parser;
    data.symtab = parser->symtab;
    data.r = r;
    data.ok = 1;
    data.hash_val = hash_val;
    data.sipkey = parser->vm->sipkey;
    apr_table_do(bind_table_entry, &data, table, NULL);
}

static void bind_post(lily_parse_state *parser, request_rec *r,
        lily_var *var)
{
    lily_hash_val *hash_val = get_new_tied_hash(parser->symtab, var);

    apr_array_header_t *pairs;
    apr_off_t len;
    apr_size_t size;
    char *buffer;
    char *sipkey = parser->vm->sipkey;
    lily_symtab *symtab = parser->symtab;

    /* Credit: I found out how to use this by reading httpd 2.4's mod_lua
       (specifically req_parsebody of lua_request.c). */
    int res = ap_parse_form_data(r, NULL, &pairs, -1, 1024 * 8);
    if (res == OK) {
        while (pairs && !apr_is_empty_array(pairs)) {
            ap_form_pair_t *pair = (ap_form_pair_t *) apr_array_pop(pairs);
            apr_brigade_length(pair->value, 1, &len);
            size = (apr_size_t) len;
            buffer = lily_malloc(size + 1);

            apr_brigade_flatten(pair->value, buffer, &size);
            buffer[len] = 0;

            lily_value *elem_key = lily_bind_string(symtab, pair->name);
            /* Give the buffer to the value to save memory. */
            lily_value *elem_value = lily_bind_string_take_buffer(symtab,
                    buffer);
            lily_hash_elem *new_elem = bind_hash_elem_with_values(sipkey,
                    elem_key, elem_value);

            new_elem->next = hash_val->elem_chain;
            hash_val->elem_chain = new_elem;
        }
    }
}

static void bind_get(lily_parse_state *parser, request_rec *r,
        lily_var *var)
{
    apr_table_t *http_get_args;
    ap_args_to_table(r, &http_get_args);

    bind_table_as(parser, r, http_get_args, "get", var);
}

static void bind_env(lily_parse_state *parser, request_rec *r,
        lily_var *var)
{
    ap_add_cgi_vars(r);
    ap_add_common_vars(r);

    bind_table_as(parser, r, r->subprocess_env, "env", var);
}

static void bind_httpmethod(lily_parse_state *parser, request_rec *r,
        lily_var *var)
{
    lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
    char *sv_buffer = lily_malloc(strlen(r->method) + 1);

    strcpy(sv_buffer, r->method);

    sv->string = sv_buffer;
    sv->refcount = 1;
    sv->size = strlen(r->method);

    lily_value v;
    v.type = var->type;
    v.flags = 0;
    v.value.string = sv;

    lily_tie_value(parser->symtab, var, &v);
}

void apache_var_dynaloader(lily_parse_state *parser, lily_var *var)
{
    request_rec *r = (request_rec *)parser->data;
    char *name = var->name;

    if (strcmp("httpmethod", name) == 0)
        bind_httpmethod(parser, r, var);
    else if (strcmp("post", name) == 0)
        bind_post(parser, r, var);
    else if (strcmp("get", name) == 0)
        bind_get(parser, r, var);
    else if (strcmp("env", name) == 0)
        bind_env(parser, r, var);
}

const lily_var_seed httpmethod_seed =
        {NULL, "httpmethod", dyna_var, "string"};

const lily_var_seed post_seed =
        {&httpmethod_seed, "post", dyna_var, "hash[string, string]"};

const lily_var_seed get_seed =
        {&post_seed, "get", dyna_var, "hash[string, string]"};

const lily_var_seed env_seed =
        {&get_seed, "env", dyna_var, "hash[string, string]"};

static int lily_handler(request_rec *r)
{
    if (strcmp(r->handler, "lily"))
        return DECLINED;

    r->content_type = "text/html";

    lily_options *options = lily_new_default_options();
    options->data = r;

    lily_parse_state *parser = lily_new_parse_state(options);
    lily_register_import(parser, "server", &env_seed, apache_var_dynaloader);

    lily_parse_file(parser, lm_tags, r->filename);

    lily_free_parse_state(parser);
    lily_free(options);

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


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
#include "lily_utf8.h"

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
    lily_type *tainted_type;
    char *sipkey;
};

lily_value *bind_tainted_of(lily_parse_state *parser,
        lily_type *tainted_type, lily_value *input)
{
    lily_instance_val *iv = lily_new_instance_val_for(tainted_type);
    iv->values[0] = input;
    lily_value *v = lily_new_value(0, tainted_type, (lily_raw_value)iv);
    return v;
}

static int bind_table_entry(void *data, const char *key, const char *value)
{
    /* Don't allow anything to become a string that has invalid utf-8, because
       Lily's string type assumes valid utf-8. */
    if (lily_is_valid_utf8(key) == 0 ||
        lily_is_valid_utf8(value) == 0)
        return TRUE;

    struct table_bind_data *d = data;

    lily_value *elem_key = lily_bind_string(d->symtab, key);
    lily_value *elem_raw_value = lily_bind_string(d->symtab, value);
    lily_value *elem_value = bind_tainted_of(d->parser, d->tainted_type,
            elem_raw_value);
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
    data.tainted_type = var->type->subtypes[1];
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
    lily_type *tainted_type = var->type->subtypes[1];

    /* Credit: I found out how to use this by reading httpd 2.4's mod_lua
       (specifically req_parsebody of lua_request.c). */
    int res = ap_parse_form_data(r, NULL, &pairs, -1, 1024 * 8);
    if (res == OK) {
        while (pairs && !apr_is_empty_array(pairs)) {
            ap_form_pair_t *pair = (ap_form_pair_t *) apr_array_pop(pairs);
            if (lily_is_valid_utf8(pair->name) == 0)
                continue;

            apr_brigade_length(pair->value, 1, &len);
            size = (apr_size_t) len;
            buffer = lily_malloc(size + 1);

            if (lily_is_valid_utf8(buffer) == 0) {
                lily_free(buffer);
                continue;
            }

            apr_brigade_flatten(pair->value, buffer, &size);
            buffer[len] = 0;

            lily_value *elem_key = lily_bind_string(symtab, pair->name);
            /* Give the buffer to the value to save memory. */
            lily_value *elem_raw_value = lily_bind_string_take_buffer(symtab,
                    buffer);
            lily_value *elem_value = bind_tainted_of(parser, tainted_type,
                    elem_raw_value);
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
        {&httpmethod_seed, "post", dyna_var, "hash[string, Tainted[string]]"};

const lily_var_seed get_seed =
        {&post_seed, "get", dyna_var, "hash[string, Tainted[string]]"};

const lily_var_seed env_seed =
        {&get_seed, "env", dyna_var, "hash[string, Tainted[string]]"};


/*  Implements server::write

    This function takes a string and writes the content directly to the server.
    This function is unique in that only string literals are accepted. If a
    string is passed that is not a literal, then ValueError is raised. */
void lily_apache_server_write(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *write_reg = vm_regs[code[1]];
    if ((write_reg->flags & VAL_IS_LITERAL) == 0)
        lily_raise(vm->raiser, lily_ValueError,
                "The string passed must be a literal.\n");

    char *value = write_reg->value.string->string;

    ap_rputs(value, (request_rec *)vm->data);
}

/*  Implements server::write_raw

    This function takes a string and writes it directly to the server. It is
    assumed that escaping has already been done by server::escape. */
void lily_apache_server_write_raw(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    char *value = vm_regs[code[1]]->value.string->string;

    ap_rputs(value, (request_rec *)vm->data);
}

void lily_string_htmlencode(lily_vm_state *vm, uint16_t argc, uint16_t *code);

/*  Implements server::escape

    This function takes a string and performs basic html encoding upon it. The
    resulting string is safe to pass to server::write_raw. */
void lily_apache_server_escape(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_string_htmlencode(vm, argc, code);
}


const lily_func_seed escape =
        {&env_seed, "escape", dyna_function, "(string):string", lily_apache_server_escape};

const lily_func_seed write_raw =
        {&escape, "write_raw", dyna_function, "(string)", lily_apache_server_write_raw};

const lily_func_seed write_seed =
        {&write_raw, "write", dyna_function, "(string)", lily_apache_server_write};


static int lily_handler(request_rec *r)
{
    if (strcmp(r->handler, "lily"))
        return DECLINED;

    r->content_type = "text/html";

    lily_options *options = lily_new_default_options();
    options->data = r;

    lily_parse_state *parser = lily_new_parse_state(options);
    lily_register_import(parser, "server", &write_seed, apache_var_dynaloader);

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


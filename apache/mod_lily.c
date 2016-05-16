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
#include "lily_lexer.h"
#include "lily_utf8.h"
#include "lily_cls_hash.h"

#include "lily_api_dynaload.h"
#include "lily_api_value.h"
#include "lily_api_options.h"

void lily_impl_puts(void *data, char *text)
{
    ap_rputs(text, (request_rec *)data);
}

struct table_bind_data {
    lily_parse_state *parser;
    lily_symtab *symtab;
    request_rec *r;
    int ok;
    lily_hash_val *hash_val;
    char *sipkey;
};

lily_value *bind_tainted_of(lily_parse_state *parser, lily_value *input)
{
    lily_instance_val *iv = lily_new_instance_val();
    iv->values = lily_malloc(1 * sizeof(lily_value *));
    iv->instance_id = SYM_CLASS_TAINTED;
    iv->values[0] = input;
    lily_value *result = lily_new_empty_value();
    lily_move_instance_f(MOVE_DEREF_NO_GC, result, iv);
    return result;
}

static int bind_table_entry(void *data, const char *key, const char *value)
{
    /* Don't allow anything to become a string that has invalid utf-8, because
       Lily's string type assumes valid utf-8. */
    if (lily_is_valid_utf8(key) == 0 ||
        lily_is_valid_utf8(value) == 0)
        return TRUE;

    struct table_bind_data *d = data;

    lily_value *elem_key = lily_new_string(key);
    lily_value *elem_raw_value = lily_new_string(value);
    lily_value *elem_value = bind_tainted_of(d->parser, elem_raw_value);

    lily_hash_add_unique(d->parser->vm, d->hash_val, elem_key, elem_value);
    return TRUE;
}

static void bind_table_as(lily_parse_state *parser, request_rec *r,
        apr_table_t *table, char *name, lily_foreign_tie *tie)
{
    lily_move_hash_f(MOVE_DEREF_NO_GC, &tie->data, lily_new_hash_val());

    struct table_bind_data data;
    data.parser = parser;
    data.symtab = parser->symtab;
    data.r = r;
    data.ok = 1;
    data.hash_val = tie->data.value.hash;
    data.sipkey = parser->vm->sipkey;
    apr_table_do(bind_table_entry, &data, table, NULL);
}

static void bind_post(lily_parse_state *parser, request_rec *r,
        lily_foreign_tie *tie)
{
    lily_move_hash_f(MOVE_DEREF_NO_GC, &tie->data, lily_new_hash_val());
    lily_hash_val *hash_val = tie->data.value.hash;

    apr_array_header_t *pairs;
    apr_off_t len;
    apr_size_t size;
    char *buffer;

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

            lily_value *elem_key = lily_new_string(pair->name);
            /* Give the buffer to the value to save memory. */
            lily_value *elem_raw_value = lily_new_string_take(buffer);
            lily_value *elem_value = bind_tainted_of(parser, elem_raw_value);

            lily_hash_add_unique(parser->vm, hash_val, elem_key, elem_value);
        }
    }
}

static void bind_get(lily_parse_state *parser, request_rec *r,
        lily_foreign_tie *tie)
{
    apr_table_t *http_get_args;
    ap_args_to_table(r, &http_get_args);

    bind_table_as(parser, r, http_get_args, "get", tie);
}

static void bind_env(lily_parse_state *parser, request_rec *r,
        lily_foreign_tie *tie)
{
    ap_add_cgi_vars(r);
    ap_add_common_vars(r);

    bind_table_as(parser, r, r->subprocess_env, "env", tie);
}

static void bind_httpmethod(lily_parse_state *parser, request_rec *r,
        lily_foreign_tie *tie)
{
    lily_move_string(&tie->data, lily_new_raw_string(r->method));
}

void apache_var_dynaloader(lily_parse_state *parser, const char *name,
        lily_foreign_tie *tie)
{
    request_rec *r = (request_rec *)parser->data;

    if (strcmp("httpmethod", name) == 0)
        bind_httpmethod(parser, r, tie);
    else if (strcmp("post", name) == 0)
        bind_post(parser, r, tie);
    else if (strcmp("get", name) == 0)
        bind_get(parser, r, tie);
    else if (strcmp("env", name) == 0)
        bind_env(parser, r, tie);
}

const lily_var_seed httpmethod_seed =
        {NULL, "httpmethod", dyna_var, "String"};

const lily_var_seed post_seed =
        {&httpmethod_seed, "post", dyna_var, "Hash[String, Tainted[String]]"};

const lily_var_seed get_seed =
        {&post_seed, "get", dyna_var, "Hash[String, Tainted[String]]"};

const lily_var_seed env_seed =
        {&get_seed, "env", dyna_var, "Hash[String, Tainted[String]]"};


/*  Implements server.write_literal

    This writes a literal directly to the server, with no escaping being done.
    If the value provided is not a literal, then ValueError is raised. */
void lily_apache_server_write_literal(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *write_reg = vm_regs[code[1]];
    if (write_reg->flags & VAL_IS_DEREFABLE)
        lily_raise(vm->raiser, lily_ValueError,
                "The string passed must be a literal.\n");

    char *value = write_reg->value.string->string;

    ap_rputs(value, (request_rec *)vm->data);
}

/*  Implements server.write_raw

    This function takes a string and writes it directly to the server. It is
    assumed that escaping has already been done by server.escape. */
void lily_apache_server_write_raw(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    char *value = vm_regs[code[1]]->value.string->string;

    ap_rputs(value, (request_rec *)vm->data);
}

extern void lily_string_html_encode(lily_vm_state *, uint16_t, uint16_t *);
extern int lily_maybe_html_encode_to_buffer(lily_vm_state *, lily_value *);

/*  Implements server.write

    This function takes a string and creates a copy with html encoding performed
    upon it. The resulting string is then sent to the server. */
void lily_apache_server_write(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value *input = vm->vm_regs[code[1]];
    const char *source;

    /* String.html_encode can't be called directly, for a couple reasons.
       1: It expects a result register, and there isn't one.
       2: It may create a new String, which is unnecessary. */
    if (lily_maybe_html_encode_to_buffer(vm, input) == 0)
        source = input->value.string->string;
    else
        source = vm->vm_buffer->message;

    ap_rputs(source, (request_rec *)vm->data);
}

/*  Implements server.escape

    This function takes a string and performs basic html encoding upon it. The
    resulting string is safe to pass to server.write_raw. */
void lily_apache_server_escape(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_string_html_encode(vm, argc, code);
}

#define DYNA_NAME apache_server

DYNA_FUNCTION(NULL,                escape,        "(String):String")
DYNA_FUNCTION(&seed_escape,        write_raw,     "(String)")
DYNA_FUNCTION(&seed_write_raw,     write_literal, "(String)")
DYNA_FUNCTION(&seed_write_literal, write,         "(String)")

static int lily_handler(request_rec *r)
{
    if (strcmp(r->handler, "lily"))
        return DECLINED;

    r->content_type = "text/html";

    lily_options *options = lily_new_default_options();
    options->data = r;

    lily_parse_state *parser = lily_new_parse_state(options);
    lily_register_package(parser, "server", &seed_write, apache_var_dynaloader);

    lily_parse_file(parser, lm_tags, r->filename);

    lily_free_parse_state(parser);
    lily_free_options(options);

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


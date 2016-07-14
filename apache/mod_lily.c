/*  mod_lily.c
    This is an apache binding for the Lily language. */
#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "util_script.h"

#include "lily_parser.h"
#include "lily_utf8.h"
#include "lily_move.h"

#include "lily_api_hash.h"
#include "lily_api_alloc.h"
#include "lily_api_value.h"
#include "lily_api_value_flags.h"
#include "lily_api_options.h"

struct table_bind_data {
    lily_hash_val *hash_val;
    const char *sipkey;
};

/**
package server

This package is registered when Lily is run by Apache through mod_lily. This
package provides Lily with information inside of Apache (such as POST), as well
as functions for sending data through the Apache server.
*/
lily_value *bind_tainted_of(lily_value *input)
{
    lily_instance_val *iv = lily_new_instance_val();
    iv->values = lily_malloc(1 * sizeof(lily_value *));
    iv->instance_id = SYM_CLASS_TAINTED;
    iv->values[0] = input;
    lily_value *result = lily_new_empty_value();
    lily_move_instance_f(MOVE_DEREF_NO_GC, result, iv);
    return result;
}

extern uint64_t siphash24(const void *src, unsigned long src_sz, const char key[16]);

/* This is temporary. I've added this because I don't want to 'fix' the hash api
   when it's going to be removed soon. */
static void apache_add_unique_hash_entry(const char *sipkey,
        lily_hash_val *hash_val, lily_value *pair_key, lily_value *pair_value)
{
    lily_hash_elem *elem = lily_malloc(sizeof(lily_hash_elem));

    uint64_t key_siphash = siphash24(pair_key->value.string->string,
            pair_key->value.string->size, sipkey);

    elem->key_siphash = key_siphash;
    elem->elem_key = pair_key;
    elem->elem_value = pair_value;

    if (hash_val->elem_chain)
        hash_val->elem_chain->prev = elem;

    elem->prev = NULL;
    elem->next = hash_val->elem_chain;
    hash_val->elem_chain = elem;

    hash_val->num_elems++;
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
    lily_value *elem_value = bind_tainted_of(elem_raw_value);

    apache_add_unique_hash_entry(d->sipkey, d->hash_val, elem_key, elem_value);
    return TRUE;
}

static lily_value *bind_table_as(lily_options *options, apr_table_t *table,
        char *name)
{
    lily_value *v = lily_new_empty_value();
    lily_move_hash_f(MOVE_DEREF_NO_GC, v, lily_new_hash_val());

    struct table_bind_data data;
    data.hash_val = v->value.hash;
    data.sipkey = options->sipkey;
    apr_table_do(bind_table_entry, &data, table, NULL);
    return v;
}

/**
var env: Hash[String, Tainted[String]]

This contains key+value pairs containing the current environment of the server.
*/
static lily_value *load_var_env(lily_options *options, uint16_t *unused)
{
    request_rec *r = (request_rec *)options->data;
    ap_add_cgi_vars(r);
    ap_add_common_vars(r);

    return bind_table_as(options, r->subprocess_env, "env");
}

/**
var get: Hash[String, Tainted[String]]

This contains key+value pairs that were sent to the server as GET variables.
Any pair that has a key or a value that is not valid utf-8 will not be present.
*/
static lily_value *load_var_get(lily_options *options, uint16_t *unused)
{
    apr_table_t *http_get_args;
    ap_args_to_table((request_rec *)options->data, &http_get_args);

    return bind_table_as(options, http_get_args, "get");
}

/**
var httpmethod: String

This is the method that was used to make the request to the server.
Common values are "GET", and "POST".
*/
static lily_value *load_var_httpmethod(lily_options *options, uint16_t *unused)
{
    lily_value *v = lily_new_empty_value();
    request_rec *r = (request_rec *)options->data;

    lily_move_string(v, lily_new_raw_string(r->method));
    return v;
}

/**
var post: Hash[String, Tainted[String]]

This contains key+value pairs that were sent to the server as POST variables.
Any pair that has a key or a value that is not valid utf-8 will not be present.
*/
static lily_value *load_var_post(lily_options *options, uint16_t *unused)
{
    lily_value *v = lily_new_empty_value();
    lily_move_hash_f(MOVE_DEREF_NO_GC, v, lily_new_hash_val());
    lily_hash_val *hash_val = v->value.hash;
    request_rec *r = (request_rec *)options->data;

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
            lily_value *elem_value = bind_tainted_of(elem_raw_value);

            apache_add_unique_hash_entry(options->sipkey, hash_val, elem_key,
                    elem_value);
        }
    }

    return v;
}

extern void lily_string_html_encode(lily_vm_state *);
extern int lily_maybe_html_encode_to_buffer(lily_vm_state *, lily_value *);

/**
define escape(text: String): String

This checks self for having "&", "<", or ">". If any are found, then a new
String is created where those html entities are replaced (& becomes &amp;, <
becomes &lt;, > becomes &gt;).
*/
void lily_apache_server_escape(lily_vm_state *vm)
{
    lily_string_html_encode(vm);
}

/**
define write(text: String)

This escapes, then writes 'text' to the server. It is equivalent to
'server.write_raw(server.escape(text))', except faster because it skips building
an intermediate `String` value.
*/
void lily_apache_server_write(lily_vm_state *vm)
{
    lily_value *input = lily_arg_value(vm, 0);
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

/**
define write_literal(text: String)

This writes 'text' directly to the server. If 'text' is not a `String` literal,
then `ValueError` is raised. No escaping is performed.
*/
void lily_apache_server_write_literal(lily_vm_state *vm)
{
    lily_value *write_reg = lily_arg_value(vm, 0);

    if (write_reg->flags & VAL_IS_DEREFABLE)
        lily_vm_raise(vm, SYM_CLASS_VALUEERROR,
                "The string passed must be a literal.\n");

    char *value = write_reg->value.string->string;

    ap_rputs(value, (request_rec *)vm->data);
}

/**
define write_raw(text: String)

This writes 'text' directly to the server without performing any HTML character
escaping. Use this only if you are certain that there is no possibility of HTML
injection.
*/
void lily_apache_server_write_raw(lily_vm_state *vm)
{
    char *value = lily_arg_string_raw(vm, 0);

    ap_rputs(value, (request_rec *)vm->data);
}

/* Loader and dynaload table generated by dyna_tools.py */
void *lily_apache_server_loader(lily_options *o, uint16_t *c, int id)
{
    switch (id) {
         case 1: return load_var_env(o, c);
         case 2: return load_var_get(o, c);
         case 3: return load_var_httpmethod(o, c);
         case 4: return load_var_post(o, c);
         case 5: return lily_apache_server_escape;
         case 6: return lily_apache_server_write;
         case 7: return lily_apache_server_write_literal;
         case 8: return lily_apache_server_write_raw;
         default: return NULL;
    }
}

const char *apache_server_dynaload_table[] = {
     "\0\0"
     ,"R\0env\0Hash[String,Tainted[String]]"
     ,"R\0get\0Hash[String,Tainted[String]]"
     ,"R\0httpmethod\0String"
     ,"R\0post\0Hash[String,Tainted[String]]"
     ,"F\0escape\0(String):String"
     ,"F\0write\0(String)"
     ,"F\0write_literal\0(String)"
     ,"F\0write_raw\0(String)"
     ,"Z"
};

static int lily_handler(request_rec *r)
{
    if (strcmp(r->handler, "lily"))
        return DECLINED;

    r->content_type = "text/html";

    lily_options *options = lily_new_default_options();
    options->data = r;
    options->html_sender = (lily_html_sender) ap_rputs;
    options->allow_sys = 0;

    lily_parse_state *parser = lily_new_parse_state(options);
    lily_register_package(parser, "server", apache_server_dynaload_table,
            lily_apache_server_loader);

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


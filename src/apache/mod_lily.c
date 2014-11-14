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
        if ((i + 1) == bufsize) {
            lily_grow_lexer_buffers(lexer);

            input_buffer = lexer->input_buffer;
        }

        if (result != APR_SUCCESS) {
            lexer->input_buffer[i] = '\n';
            lexer->input_end = i + 1;
            /* If i is 0, then nothing is on this line except eof. Return 0 to
               let the caller know this is the end.
             * If it isn't, then there's stuff with an unexpected eof at the
               end. Return 1, then 0 the next time. */
            ok = !!i;
            /* Make sure the second eof found does not increment the line
               number again. */
            if (lexer->hit_eof == 0) {
                lexer->line_num++;
                lexer->hit_eof = 1;
            }
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


static lily_hash_elem *bind_hash_elem_with_values(char *sipkey,
    lily_value *key, lily_value *value)
{
    lily_hash_elem *elem = lily_malloc(sizeof(lily_hash_elem));

    if (elem == NULL || key == NULL) {
        lily_free(elem);
        return NULL;
    }

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

    lily_sig *hash_sig = lily_try_sig_from_ids(symtab, ids);
    if (hash_sig == NULL)
        return NULL;

    lily_var *bind_var = lily_try_new_var(symtab, hash_sig, name, 0);
    if (bind_var == NULL)
        return NULL;

    lily_hash_val *var_hash = lily_try_new_hash_val();
    if (var_hash == NULL)
        return NULL;

    bind_var->value.hash = var_hash;
    bind_var->flags &= ~VAL_IS_NIL;
    return bind_var;
}

static void make_package(int *ok, lily_parse_state *parser,
        lily_var *package_var, int var_count, int register_save,
        lily_var *save_chain)
{
    lily_symtab *symtab = parser->symtab;
    lily_package_val *pval = lily_malloc(sizeof(lily_package_val));
    lily_var **package_vars = lily_malloc(var_count * sizeof(lily_var *));
    if (pval == NULL || package_vars == NULL) {
        lily_free(pval);
        lily_free(package_vars);
        *ok = 0;
    }
    else {
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

    lily_value *elem_key = lily_bind_string(d->symtab, key);
    lily_value *elem_value = lily_bind_string(d->symtab, value);
    lily_hash_elem *new_elem = bind_hash_elem_with_values(d->sipkey,
            elem_key, elem_value);

    if (elem_key == NULL || elem_value == NULL || new_elem == NULL) {
        lily_free(new_elem);
        lily_bind_destroy(elem_key);
        lily_bind_destroy(elem_value);
        d->ok = 0;
        return FALSE;
    }

    new_elem->next = d->hash_val->elem_chain;
    d->hash_val->elem_chain = new_elem;
    return TRUE;
}

static void bind_table_as(int *count, lily_parse_state *parser, request_rec *r,
        apr_table_t *table, char *name)
{
    if (*count == -1)
        return;

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

    if (data.ok == 0)
        *count = -1;
    else
        (*count)++;
}


/** Bind server::httpmethod. This data is already available through
    server::env["REQUEST_METHOD"], so this is simply for convenience. **/


static void bind_httpmethod(int *count, lily_parse_state *parser, request_rec *r)
{
    if (*count == -1)
        return;

    lily_class *string_cls = lily_class_by_id(parser->symtab,
            SYM_CLASS_STRING);

    lily_sig *string_sig = string_cls->sig;
    lily_var *var = lily_try_new_var(parser->symtab, string_sig, "httpmethod",
            0);

    lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
    char *sv_buffer = lily_malloc(strlen(r->method) + 1);
    if (var == NULL || sv == NULL || sv_buffer == NULL) {
        lily_free(sv);
        lily_free(sv_buffer);
        *count = -1;
        return;
    }

    strcpy(sv_buffer, r->method);

    sv->string = sv_buffer;
    sv->refcount = 1;
    sv->size = strlen(r->method);

    var->value.string = sv;
    var->flags &= ~VAL_IS_NIL;
    (*count)++;
}


/** Binding server::post **/


static void bind_post(int *count, lily_parse_state *parser, request_rec *r)
{
    if (*count == -1)
        return;

    lily_var *post_var = bind_hash_str_str_var(parser->symtab, "post");
    lily_hash_val *hash_val = post_var->value.hash;

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
            if (buffer == NULL) {
                *count = -1;
                return;
            }

            apr_brigade_flatten(pair->value, buffer, &size);
            buffer[len] = 0;

            lily_value *elem_key = lily_bind_string(symtab, pair->name);
            /* Give the buffer to the value to save memory. */
            lily_value *elem_value = lily_bind_string_take_buffer(symtab,
                    buffer);
            lily_hash_elem *new_elem = bind_hash_elem_with_values(sipkey,
                    elem_key, elem_value);

            if (elem_key == NULL || elem_value == NULL || new_elem == NULL) {
                lily_free(new_elem);
                lily_bind_destroy(elem_key);
                lily_bind_destroy(elem_value);
                *count = -1;
                return;
            }

            new_elem->next = hash_val->elem_chain;
            hash_val->elem_chain = new_elem;
        }
    }

    (*count)++;
}


/** Binding the server package itself **/


static int apache_bind_server(lily_parse_state *parser, request_rec *r)
{
    int ret = 1;
    lily_symtab *symtab = parser->symtab;
    lily_class *package_cls = lily_class_by_id(symtab, SYM_CLASS_PACKAGE);
    lily_sig *package_sig = lily_try_sig_for_class(symtab, package_cls);
    lily_var *bound_var = lily_try_new_var(symtab, package_sig, "server", 0);
    if (bound_var) {
        lily_var *save_chain = symtab->var_chain;
        int save_spot = symtab->next_register_spot;
        int count = 0;

        ap_add_cgi_vars(r);
        ap_add_common_vars(r);
        bind_table_as(&count, parser, r, r->subprocess_env, "env");

        apr_table_t *http_get_args;
        ap_args_to_table(r, &http_get_args);
        bind_table_as(&count, parser, r, http_get_args, "get");

        bind_post(&count, parser, r);

        bind_httpmethod(&count, parser, r);

        if (count != -1) {
            int ok = 1;
            make_package(&ok, parser, bound_var, count, save_spot, save_chain);
            if (ok == 0)
                ret = 0;
        }
        else
            ret = 0;
    }
    else
        ret = 0;

    return ret;
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

    lily_parse_state *parser = lily_new_parse_state(r, 0, NULL);
    if (parser == NULL)
        return DECLINED;

    if (apache_bind_server(parser, r) == 0)
        return DECLINED;

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


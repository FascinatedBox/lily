/*  mod_lily.c
    This is an apache binding for the Lily language. */

#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "apr_file_io.h"
#include "util_script.h"

#include "lily_parser.h"
#include "lily_lexer.h"
#include "lily_value.h"
#include "lily_impl.h"

void lily_impl_puts(void *data, char *text)
{
    ap_rputs(text, (request_rec *)data);
}

/*  This table indicates how many more bytes need to be successfully read after
    that particular byte for proper utf-8. -1 = invalid.
    80-BF : These only follow.
    C0-C1 : Can only be used for overlong encoding of ascii.
    F5-FD : RFC 3629 took these out.
    FE-FF : The standard was originally 31-bits.

    Table idea, above info came from wikipedia. */
static const char follower_table[256] =
{
     /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/* 0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 1 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 2 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 3 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 4 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 6 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 7 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 8 */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* 9 */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* A */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* B */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* C */-1,-1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* D */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* E */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
/* F */ 4, 4, 4, 4, 4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

static int apache_read_line_fn(lily_lex_entry *entry)
{
    char ch;
    int bufsize, followers, i, ok;
    lily_lex_state *lexer = entry->lexer;
    char *input_buffer = lexer->input_buffer;
    apr_file_t *input_file = (apr_file_t *)entry->source;
    apr_status_t result;
    bufsize = lexer->input_size;
    i = 0;

    while (1) {
        result = apr_file_getc(&ch, input_file);
        if (result != APR_SUCCESS) {
            if ((i + 1) == bufsize) {
                lily_grow_lexer_buffers(lexer);

                input_buffer = lexer->input_buffer;
            }

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

        /* i + 2 is used so that when \r\n that the \n can be safely added
           to the buffer. Otherwise, it would be i + 1. */
        if ((i + 2) == bufsize) {
            lily_grow_lexer_buffers(lexer);
            /* Do this in case the realloc decides to use a different block
               instead of growing what it had. */
            input_buffer = lexer->input_buffer;
        }

        lexer->input_buffer[i] = ch;

        if (ch == '\r' || ch == '\n') {
            lexer->input_end = i;
            lexer->line_num++;
            ok = 1;

            if (ch == '\r') {
                apr_file_getc(&ch, input_file);
                if (ch != '\n')
                    apr_file_ungetc(ch, input_file);
                else {
                    /* This is safe: See i + 2 == size comment above. */
                    lexer->input_buffer[i+1] = ch;
                    lexer->input_end++;
                }
            }
            break;
        }
        else if (ch > 127) {
            followers = follower_table[(unsigned int)ch];
            if (followers >= 2) {
                int j;
                i++;
                for (j = 1;j < followers;j++,i++) {
                    result = apr_file_getc(&ch, input_file);
                    if ((unsigned char)ch < 128 || result != APR_SUCCESS) {
                        lily_raise(lexer->raiser, lily_ErrEncoding,
                                   "Invalid utf-8 sequence on line %d.\n",
                                   lexer->line_num);
                    }
                    input_buffer[i] = ch;
                }
            }
            else if (followers == -1) {
                lily_raise(lexer->raiser, lily_ErrEncoding,
                           "Invalid utf-8 sequence on line %d.\n",
                           lexer->line_num);
            }
        }
        else
            i++;
    }
    return ok;
}

static void apache_close_fn(lily_lex_entry *entry)
{
    apr_file_close(entry->source);
}

static lily_value *bind_str(lily_sig *str_sig, const char *str)
{
    lily_value *new_value = lily_malloc(sizeof(lily_value));
    lily_str_val *sv = lily_malloc(sizeof(lily_str_val));
    int str_size = strlen(str);
    char *buffer = lily_malloc(str_size + 1);

    if (sv == NULL || buffer == NULL || new_value == NULL) {
        lily_free(sv);
        lily_free(buffer);
        lily_free(new_value);
        return NULL;
    }

    strcpy(buffer, str);
    sv->refcount = 1;
    sv->str = buffer;
    sv->size = str_size;

    new_value->value.str = sv;
    new_value->flags = 0;
    new_value->sig = str_sig;

    return new_value;
}

static void deref_destroy_value(lily_value *value)
{
    if (value != NULL) {
        lily_deref_unknown_val(value);
        lily_free(value);
    }
}

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

struct bind_data {
    lily_parse_state *parser;
    request_rec *r;
    int ok;
    lily_sig *str_sig;
    lily_hash_val *hash_val;
    char *sipkey;
};

static int bind_env_entry(void *data, const char *key, const char *value)
{
    struct bind_data *d = data;

    lily_value *elem_key = bind_str(d->str_sig, key);
    lily_value *elem_value = bind_str(d->str_sig, value);
    lily_hash_elem *new_elem = bind_hash_elem_with_values(d->sipkey,
            elem_key, elem_value);

    if (elem_key == NULL || elem_value == NULL || new_elem == NULL) {
        lily_free(new_elem);
        deref_destroy_value(elem_key);
        deref_destroy_value(elem_value);
        d->ok = 0;
        return FALSE;
    }

    new_elem->next = d->hash_val->elem_chain;
    d->hash_val->elem_chain = new_elem;
    return TRUE;
}

static void bind_env(int *count, lily_parse_state *parser, request_rec *r)
{
    if (*count == -1)
        return;

    lily_symtab *symtab = parser->symtab;

    /* hash[str, str] */
    const int ids[] = {SYM_CLASS_HASH, SYM_CLASS_STR, SYM_CLASS_STR};

    lily_sig *hash_sig = lily_try_sig_from_ids(symtab, ids);
    if (hash_sig == NULL) {
        *count = -1;
        return;
    }

    lily_var *env_var = lily_try_new_var(symtab, hash_sig, "env", 0);
    if (env_var == NULL) {
        *count = -1;
        return;
    }

    lily_hash_val *env_hash = lily_try_new_hash_val();
    if (env_hash == NULL) {
        *count = -1;
        return;
    }

    env_var->value.hash = env_hash;
    env_var->flags &= ~VAL_IS_NIL;

    lily_class *str_cls = lily_class_by_id(parser->symtab, SYM_CLASS_STR);
    struct bind_data data;
    data.parser = parser;
    data.r = r;
    data.ok = 1;
    data.str_sig = str_cls->sig;
    data.hash_val = env_hash;
    data.sipkey = parser->vm->sipkey;
    apr_table_do(bind_env_entry, &data, r->subprocess_env, NULL);

    if (data.ok == 0)
        *count = -1;
    else
        *count = *count + 1;
}

static int apache_bind_server(lily_parse_state *parser, request_rec *r)
{
    int ret = 1;
    lily_symtab *symtab = parser->symtab;
    lily_class *package_cls = lily_class_by_id(symtab, SYM_CLASS_PACKAGE);
    lily_sig *package_sig = lily_try_sig_for_class(symtab, package_cls);
    lily_var *bound_var = lily_try_new_var(symtab, package_sig, "server", 0);
    if (bound_var) {
        lily_var *save_top = symtab->var_top;
        int save_spot = symtab->next_register_spot;
        int count = 0;

        bind_env(&count, parser, r);

        if (count != -1) {
            lily_package_val *pval = lily_malloc(sizeof(lily_package_val));
            lily_var **package_vars = lily_malloc(count * sizeof(lily_var *));
            if (pval == NULL || package_vars == NULL) {
                lily_free(pval);
                lily_free(package_vars);
                ret = 0;
            }
            else {
                int i = 0;
                lily_var *var_iter = save_top->next;
                while (var_iter) {
                    package_vars[i] = var_iter;
                    i++;
                    var_iter = var_iter->next;
                }
                symtab->var_top = save_top;
                save_top->next = NULL;
                symtab->next_register_spot = save_spot;

                pval->refcount = 1;
                pval->name = bound_var->name;
                pval->gc_entry = NULL;
                pval->var_count = i;
                pval->vars = package_vars;
                bound_var->flags &= ~VAL_IS_NIL;
                bound_var->value.package = pval;
            }
        }
        else
            ret = 0;
    }
    else
        ret = 0;

    return ret;
}

/* The sample content handler */
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

    /* This populates r->subprocess_env, which is then looped over to make
       server::env. */
    ap_add_cgi_vars(r);
    ap_add_common_vars(r);
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


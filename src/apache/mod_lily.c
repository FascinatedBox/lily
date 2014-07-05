/*  mod_lily.c
    This is an apache binding for the Lily language. */

#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"

#include "lily_parser.h"

void lily_impl_debugf(char *format, ...)
{
    /* todo: The interpreter needs to provide the request_rec. */
}

void lily_impl_puts(char *text)
{
    /* todo: The interpreter needs to provide the request_rec. */
}

/* The sample content handler */
static int lily_handler(request_rec *r)
{
    if (strcmp(r->handler, "lily"))
        return DECLINED;

    r->content_type = "text/html";

    if (!r->header_only)
        ap_rputs("mod_lily is up and running. Yay!\n", r);

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


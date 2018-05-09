/**
library extend

This provides extension functions to the testing suite.
*/

#include "lily.h"

/** Begin autogen section. **/
const char *lily_extend_info_table[] = {
    "\0\0"
    ,"F\0render_string\0(String,String): Result[String,Boolean]"
    ,"F\0parse_string\0(String,String): Result[String,Boolean]"
    ,"F\0parse_expr\0(String,String): Result[String,String]"
    ,"F\0parse_rewind\0(String,String,String): String"
    ,"Z"
};
void lily_extend__render_string(lily_state *);
void lily_extend__parse_string(lily_state *);
void lily_extend__parse_expr(lily_state *);
void lily_extend__parse_rewind(lily_state *);
lily_call_entry_func lily_extend_call_table[] = {
    NULL,
    lily_extend__render_string,
    lily_extend__parse_string,
    lily_extend__parse_expr,
    lily_extend__parse_rewind,
};
/** End autogen section. **/

void noop_render(const char *to_render, void *data)
{
    (void)data;
    (void)to_render;
}

static void run_interp(lily_state *s, int parse)
{
    const char *context = lily_arg_string_raw(s, 0);
    const char *data = lily_arg_string_raw(s, 1);

    lily_config config;

    lily_config_init(&config);
    config.render_func = noop_render;

    lily_state *subinterp = lily_new_state(&config);
    lily_container_val *con;

    int result;

    if (parse)
        result = lily_parse_string(subinterp, context, data);
    else
        result = lily_render_string(subinterp, context, data);

    if (result == 0) {
        con = lily_push_failure(s);
        lily_push_string(s, lily_error_message(subinterp));
    }
    else {
        con = lily_push_success(s);
        lily_push_boolean(s, 1);
    }

    lily_free_state(subinterp);

    lily_con_set_from_stack(s, con, 0);
    lily_return_top(s);
}

/**
define render_string(context: String, to_interpret: String): Result[String, Boolean]

This function processes `to_interpret` as a `String` containing template tags
with `context` used as the filename.
*/
void lily_extend__render_string(lily_state *s)
{
    run_interp(s, 0);
}

/**
define parse_string(context: String, to_interpret: String): Result[String, Boolean]

This function processes `to_interpret` as a `String` containing code with
`context` used as the filename.
*/
void lily_extend__parse_string(lily_state *s)
{
    run_interp(s, 1);
}

/**
define parse_expr(context: String, to_interpret: String): Result[String, String]

This function processes `to_interpret` as a single expression. The output is
either an interpreter error or the result of interpolating the expression.
*/
void lily_extend__parse_expr(lily_state *s)
{
    const char *context = lily_arg_string_raw(s, 0);
    char *data = (char *)lily_arg_string_raw(s, 1);

    lily_config config;

    lily_config_init(&config);
    config.render_func = noop_render;

    lily_state *subinterp = lily_new_state(&config);
    lily_container_val *con;

    const char *output;
    int result = lily_parse_expr(subinterp, context, data, &output);

    if (result == 0) {
        con = lily_push_failure(s);
        lily_push_string(s, lily_error_message(subinterp));
    }
    else {
        con = lily_push_success(s);
        lily_push_string(s, output);
    }

    lily_free_state(subinterp);

    lily_con_set_from_stack(s, con, 0);
    lily_return_top(s);
}

/**
define parse_rewind(context: String, prelude: String, to_interpret: String): String

This function processes `prelude` once, then `to_interpret` twice. This checks
if the error message from `to_interpret` is the same for each pass.
*/
void lily_extend__parse_rewind(lily_state *s)
{
    const char *context = lily_arg_string_raw(s, 0);
    char *header = (char *)lily_arg_string_raw(s, 1);
    char *data = (char *)lily_arg_string_raw(s, 2);
    lily_msgbuf *msgbuf = lily_msgbuf_get(s);
    lily_config config;

    lily_config_init(&config);
    config.render_func = noop_render;

    lily_state *subinterp = lily_new_state(&config);

    lily_parse_string(subinterp, context, header);
    lily_parse_string(subinterp, context, data);
    lily_mb_add(msgbuf, lily_error_message(subinterp));
    lily_parse_string(subinterp, context, data);
    lily_mb_add(msgbuf, lily_error_message(subinterp));

    /* The prelude shouldn't fail, and the two passes both should. Return the
       output of both parses in case they fail for different reasons. */
    lily_free_state(subinterp);
    lily_push_string(s, lily_mb_raw(msgbuf));
    lily_return_top(s);
}

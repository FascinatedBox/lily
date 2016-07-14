/* This creates a shared library that allows Lily to access postgres, using
   libpq. This can be used by any Lily runner. */
#include <string.h>

#include "libpq-fe.h"

#include "lily_core_types.h"
#include "lily_vm.h"

#include "lily_api_alloc.h"
#include "lily_api_value.h"

#define CID_RESULT cid_table[0]
#define CID_CONN   cid_table[1]
#define GET_CID_TABLE vm->call_chain->function->cid_table

/**
package postgres

This package provides a limited, but usable interface for interacting with a
postgres server.
*/

/**
class Result

The `Result` class provides a wrapper over the result of querying the postgres
database. The class provides a very basic set of methods to allow interaction
with the rows as a `List[String]`.
*/
typedef struct {
    uint32_t refcount;
    uint16_t instance_id;
    uint16_t is_closed;
    class_destroy_func destroy_func;
    uint64_t column_count;
    uint64_t row_count;
    uint64_t current_row;
    PGresult *pg_result;
} lily_pg_result;

void close_result(lily_value *v)
{
    lily_pg_result *result = (lily_pg_result *)v->value.generic;
    if (result->is_closed == 0)
        PQclear(result->pg_result);

    result->is_closed = 1;
}

void destroy_result(lily_value *v)
{
    close_result(v);
    lily_free(v->value.generic);
}

/**
method Result.close(self: Result)

Close a `Result` and free all data associated with it. If this is not done
manually, then it is done automatically when the `Result` is destroyed through
either the gc or refcounting.
*/
void lily_postgres_Result_close(lily_vm_state *vm)
{
    lily_value *to_close_reg = lily_arg_value(vm, 0);
    lily_pg_result *to_close = (lily_pg_result *)to_close_reg->value.generic;

    close_result(to_close_reg);
    to_close->row_count = 0;
}

/**
method Result.each_row(self: Result, fn: Function(List[String]))

This loops through each row in 'self', calling 'fn' for each row that is found.
If 'self' has no rows, or has been closed, then this does nothing.
*/
void lily_postgres_Result_each_row(lily_vm_state *vm)
{
    lily_pg_result *boxed_result = (lily_pg_result *)
            lily_arg_generic(vm, 0);

    PGresult *raw_result = boxed_result->pg_result;
    if (raw_result == NULL || boxed_result->row_count == 0)
        return;

    lily_vm_prepare_call(vm, lily_arg_function(vm, 1));

    int row;
    for (row = 0;row < boxed_result->row_count;row++) {
        int num_cols = boxed_result->column_count;
        lily_list_val *lv = lily_new_list_of_n(num_cols);

        int col;
        for (col = 0;col < num_cols;col++) {
            char *field_text;

            if (PQgetisnull(raw_result, row, col))
                field_text = "(null)";
            else
                field_text = PQgetvalue(raw_result, row, col);

            lily_list_set_string(lv, col, lily_new_raw_string(field_text));
        }

        lily_push_list(vm, lv);
        lily_vm_exec_prepared_call(vm, 1);
    }
}

/**
method Result.row_count(self: Result): Integer

Returns the number of rows present within 'self'.
*/
void lily_postgres_Result_row_count(lily_vm_state *vm)
{
    lily_pg_result *boxed_result = (lily_pg_result *)
            lily_arg_generic(vm, 0);

    lily_return_integer(vm, boxed_result->current_row);
}

/**
class Conn

The `Conn` class represents a connection to a postgres server.
*/
typedef struct lily_pg_conn_value_ {
    uint32_t refcount;
    uint16_t instance_id;
    uint16_t is_open;
    class_destroy_func destroy_func;
    PGconn *conn;
} lily_pg_conn_value;

void destroy_conn(lily_value *v)
{
    lily_pg_conn_value *conn_value = (lily_pg_conn_value *)v->value.generic;

    PQfinish(conn_value->conn);
    lily_free(conn_value);
}

/**
method Conn.query(self: Conn, format: String, values: List[String]...):Either[String, Result]

Perform a query using 'format'. Any "?" value found within 'format' will be
replaced with an entry from 'values'.

On success, the result is a `Right` containing a `Result`.

On failure, the result is a `Left` containing a `String` describing the error.
*/
void lily_postgres_Conn_query(lily_vm_state *vm)
{
    char *fmt;
    int arg_pos, fmt_index;
    lily_list_val *vararg_lv;
    lily_msgbuf *vm_buffer = vm->vm_buffer;
    uint16_t *cid_table = GET_CID_TABLE;

    lily_msgbuf_flush(vm_buffer);

    lily_pg_conn_value *conn_value =
            (lily_pg_conn_value *)lily_arg_generic(vm, 0);
    fmt = lily_arg_string_raw(vm, 1);
    vararg_lv = lily_arg_list(vm, 2);
    arg_pos = 0;
    fmt_index = 0;
    int text_start = 0;
    int text_stop = 0;

    while (1) {
        char ch = fmt[fmt_index];

        if (ch == '?') {
            if (arg_pos == vararg_lv->num_values) {
                lily_instance_val *variant = lily_new_left();
                lily_string_val *sv = lily_new_raw_string(
                        "Not enough arguments for format.\n");
                lily_variant_set_string(variant, 0, sv);
                lily_return_filled_variant(vm, variant);
                return;
            }

            lily_msgbuf_add_text_range(vm_buffer, fmt, text_start, text_stop);
            text_start = fmt_index + 1;
            text_stop = text_start;

            lily_value *arg = vararg_lv->elems[arg_pos];
            lily_msgbuf_add(vm_buffer, arg->value.string->string);
            arg_pos++;
        }
        else {
            if (ch == '\0')
                break;

            text_stop++;
        }

        fmt_index++;
    }

    char *query_string;

    /* If there are no ?'s in the format string, then it can be used as-is. */
    if (text_start == 0)
        query_string = fmt;
    else {
        lily_msgbuf_add_text_range(vm_buffer, fmt, text_start, text_stop);
        query_string = vm_buffer->message;
    }

    PGresult *raw_result = PQexec(conn_value->conn, query_string);

    ExecStatusType status = PQresultStatus(raw_result);
    if (status == PGRES_BAD_RESPONSE ||
        status == PGRES_NONFATAL_ERROR ||
        status == PGRES_FATAL_ERROR) {
        lily_instance_val *variant = lily_new_left();
        lily_string_val *sv = lily_new_raw_string(
                PQerrorMessage(conn_value->conn));
        lily_variant_set_string(variant, 0, sv);
        lily_return_filled_variant(vm, variant);
        return;
    }

    lily_pg_result *res = lily_malloc(sizeof(lily_pg_result));
    res->refcount = 0;
    res->current_row = 0;
    res->is_closed = 0;
    res->instance_id = CID_CONN;
    res->destroy_func = destroy_result;
    res->pg_result = raw_result;
    res->row_count = PQntuples(raw_result);
    res->column_count = PQnfields(raw_result);

    lily_instance_val *variant = lily_new_right();
    lily_variant_set_foreign(variant, 0, (lily_foreign_val *)res);
    lily_return_filled_variant(vm, variant);
}

/**
method Conn.open(host: *String="", port: *String="", dbname: *String="", name: *String="", pass: *String=""):Option[Conn]

Attempt to connect to the postgres server, using the values provided.

On success, the result is a `Some` containing a newly-made `Conn`.

On failure, the result is a `None`.
*/
void lily_postgres_Conn_open(lily_vm_state *vm)
{
    const char *host = NULL;
    const char *port = NULL;
    const char *dbname = NULL;
    const char *name = NULL;
    const char *pass = NULL;
    uint16_t *cid_table = GET_CID_TABLE;

    switch (lily_arg_count(vm)) {
        case 5:
            pass = lily_arg_string_raw(vm, 4);
        case 4:
            name = lily_arg_string_raw(vm, 3);
        case 3:
            dbname = lily_arg_string_raw(vm, 2);
        case 2:
            port = lily_arg_string_raw(vm, 1);
        case 1:
            host = lily_arg_string_raw(vm, 0);
    }

    PGconn *conn = PQsetdbLogin(host, port, NULL, NULL, dbname, name, pass);
    lily_pg_conn_value *new_val;

    switch (PQstatus(conn)) {
        case CONNECTION_OK:
            new_val = lily_malloc(sizeof(lily_pg_conn_value));
            new_val->instance_id = CID_CONN;
            new_val->destroy_func = destroy_conn;
            new_val->refcount = 0;
            new_val->is_open = 1;
            new_val->conn = conn;

            lily_instance_val *variant = lily_new_some();
            lily_variant_set_foreign(variant, 0,
                    (lily_foreign_val *)new_val);
            lily_return_filled_variant(vm, variant);
            break;
        default:
            lily_return_empty_variant(vm, lily_get_none(vm));
            return;
    }
}

const char *lily_dynaload_table[] =
{
    "\002Result\0Conn\0"
    ,"C\003Result"
    ,"m:close\0(Result)"
    ,"m:each_row\0(Result,Function(List[String]))"
    ,"m:row_count\0(Result):Integer"
    ,"C\002Conn"
    ,"m:query\0(Conn, String, List[String]...):Either[String, Result]"
    ,"m:open\0(*String, *String, *String, *String, *String):Option[Conn]"
    ,"Z"
};

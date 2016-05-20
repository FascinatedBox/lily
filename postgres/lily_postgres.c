/* This creates a shared library that allows Lily to access postgres, using
   libpq. This can be used by any Lily runner. */
#include <string.h>

#include "libpq-fe.h"

#include "lily_core_types.h"
#include "lily_symtab.h"
#include "lily_parser.h"
#include "lily_vm.h"

#include "lily_api_alloc.h"
#include "lily_api_dynaload.h"
#include "lily_api_value_ops.h"

#define ID_OFFSET    vm->call_chain->function->module->cid_start
#define CLSID_RESULT 2
#define CLSID_CONN   1

/******************************************************************************/
/* Result                                                                     */
/******************************************************************************/

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

void lily_pg_result_close(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value *to_close_reg = vm->vm_regs[code[1]];
    lily_pg_result *to_close = (lily_pg_result *)to_close_reg->value.generic;

    close_result(to_close_reg);
    to_close->row_count = 0;
}

void lily_pg_result_each_row(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_pg_result *boxed_result = (lily_pg_result *)
            vm_regs[code[1]]->value.generic;

    PGresult *raw_result = boxed_result->pg_result;
    if (raw_result == NULL || boxed_result->row_count == 0)
        return;

    lily_value *function_reg = vm_regs[code[2]];
    int cached = 0;

    int row;
    for (row = 0;row < boxed_result->row_count;row++) {
        lily_list_val *lv = lily_new_list_val();
        /* This List will get a ref bump when it is given to lily_foreign_call.
           If the function given raises an error, then the List will have two
           refs but only be in a register once (and thus leak). Thus, an offset
           ref is done here. */
        lv->refcount--;

        lily_value fake_reg;

        lv->elems = lily_malloc(boxed_result->column_count * sizeof(lily_value *));

        int col;
        for (col = 0;col < boxed_result->column_count;col++) {
            char *field_text;

            if (PQgetisnull(raw_result, row, col))
                field_text = "(null)";
            else
                field_text = PQgetvalue(raw_result, row, col);

            lily_value *v = lily_new_empty_value();
            lily_move_string(v, lily_new_raw_string(field_text));

            lv->elems[col] = v;
        }

        lv->num_values = col;
        fake_reg.value.list = lv;
        fake_reg.flags = VAL_IS_LIST | VAL_IS_DEREFABLE;

        lily_foreign_call(vm, &cached, 0, function_reg, 1, &fake_reg);
    }
}

void lily_pg_result_row_count(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_pg_result *boxed_result = (lily_pg_result *)
            vm_regs[code[1]]->value.generic;
    lily_value *result_reg = vm_regs[code[0]];
    int row = boxed_result->current_row;

    lily_move_integer(result_reg, row);
}

#define DYNA_NAME pg_result

DYNA_FUNCTION(NULL,           close,     "(Result)")
DYNA_FUNCTION(&seed_close,    each_row,  "(Result, Function(List[String]))")
DYNA_FUNCTION(&seed_each_row, row_count, "(Result):Integer")

const lily_class_seed result_seed =
{
    NULL,                   /* next */
    "Result",               /* name */
    dyna_class,             /* load_type */
    1,                      /* is_refcounted */
    0,                      /* generic_count */
    &seed_row_count         /* dynaload_table */
};

/******************************************************************************/
/* Conn                                                                       */
/******************************************************************************/

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

void lily_pg_conn_query(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    char *fmt;
    int arg_pos, fmt_index;
    lily_list_val *vararg_lv;
    lily_value **vm_regs = vm->vm_regs;
    lily_msgbuf *vm_buffer = vm->vm_buffer;
    lily_value *result_reg;

    lily_msgbuf_flush(vm_buffer);

    result_reg = vm_regs[code[0]];
    lily_pg_conn_value *conn_value =
            (lily_pg_conn_value *)vm_regs[code[1]]->value.generic;
    fmt = vm_regs[code[2]]->value.string->string;
    vararg_lv = vm_regs[code[3]]->value.list;
    arg_pos = 0;
    fmt_index = 0;
    int text_start = 0;
    int text_stop = 0;

    while (1) {
        char ch = fmt[fmt_index];

        if (ch == '?') {
            if (arg_pos == vararg_lv->num_values) {
                lily_value *v = lily_new_string("Not enough arguments for format.\n");
                lily_move_enum_f(MOVE_DEREF_NO_GC, result_reg, lily_new_left(v));
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
        lily_value *v = lily_new_string(PQerrorMessage(conn_value->conn));
        lily_move_enum_f(MOVE_DEREF_NO_GC, result_reg, lily_new_left(v));
        return;
    }

    lily_pg_result *new_result = lily_malloc(sizeof(lily_pg_result));
    new_result->refcount = 1;
    new_result->current_row = 0;
    new_result->is_closed = 0;
    new_result->instance_id = ID_OFFSET + CLSID_RESULT;
    new_result->destroy_func = destroy_result;
    new_result->pg_result = raw_result;
    new_result->row_count = PQntuples(raw_result);
    new_result->column_count = PQnfields(raw_result);

    lily_value *v = lily_new_empty_value();
    lily_move_foreign_f(MOVE_DEREF_NO_GC, v, (lily_foreign_val *)new_result);
    lily_move_enum_f(MOVE_DEREF_NO_GC, result_reg, lily_new_right(v));
}

void lily_pg_conn_open(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    const char *host = NULL;
    const char *port = NULL;
    const char *dbname = NULL;
    const char *name = NULL;
    const char *pass = NULL;

    switch (argc) {
        case 5:
            pass = vm_regs[code[5]]->value.string->string;
        case 4:
            name = vm_regs[code[4]]->value.string->string;
        case 3:
            dbname = vm_regs[code[3]]->value.string->string;
        case 2:
            port = vm_regs[code[2]]->value.string->string;
        case 1:
            host = vm_regs[code[1]]->value.string->string;
    }

    lily_value *result = vm_regs[code[0]];

    PGconn *conn = PQsetdbLogin(host, port, NULL, NULL, dbname, name, pass);
    lily_pg_conn_value *new_val;

    switch (PQstatus(conn)) {
        case CONNECTION_OK:
            new_val = lily_malloc(sizeof(lily_pg_conn_value));
            new_val->instance_id = ID_OFFSET + CLSID_CONN;
            new_val->destroy_func = destroy_conn;
            new_val->refcount = 1;
            new_val->is_open = 1;
            new_val->conn = conn;

            lily_value *v = lily_new_empty_value();
            lily_move_foreign_f(MOVE_DEREF_NO_GC, v,
                    (lily_foreign_val *)new_val);
            lily_move_enum_f(MOVE_DEREF_NO_GC, result, lily_new_some(v));
            break;
        default:
            lily_move_enum_f(MOVE_DEREF_NO_GC, result, lily_get_none(vm));
            return;
    }
}

#undef DYNA_NAME
#define DYNA_NAME pg_conn

DYNA_FUNCTION(NULL,        query, "(Conn, String, List[String]...):Either[String, Result]")
DYNA_FUNCTION(&seed_query, open,  "(*String, *String, *String, *String, *String):Option[Conn]")

const lily_class_seed lily_dynaload_table =
{
    &result_seed,         /* next */
    "Conn",               /* name */
    dyna_class,           /* load_type */
    1,                    /* is_refcounted */
    0,                    /* generic_count */
    &seed_query           /* dynaload_table */
};

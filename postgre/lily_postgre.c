/* This creates a shared library that allows Lily to access postgre, using
   libpq. This can be used by any Lily runner. */
#include <string.h>

#include "libpq-fe.h"

#include "lily_alloc.h"
#include "lily_core_types.h"
#include "lily_symtab.h"
#include "lily_parser.h"
#include "lily_value.h"
#include "lily_vm.h"
#include "lily_seed.h"

#include "lily_cls_list.h"

/******************************************************************************/
/* Errors                                                                     */
/******************************************************************************/

const lily_base_seed error_seed = {NULL, "Error", dyna_exception};

/******************************************************************************/
/* Result                                                                     */
/******************************************************************************/

typedef struct {
    uint32_t refcount;
    uint32_t pad;
    uint64_t column_count;
    uint64_t row_count;
    uint64_t current_row;
    PGresult *pg_result;
} lily_pg_result;

void destroy_result(lily_value *v)
{
    lily_pg_result *result = (lily_pg_result *)v->value.generic;
    PQclear(result->pg_result);
    lily_free(result);
}

void lily_pg_result_fetchrow(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_pg_result *boxed_result = (lily_pg_result *)
            vm_regs[code[0]]->value.generic;
    lily_value *result_reg = vm_regs[code[1]];
    PGresult *raw_result = boxed_result->pg_result;
    int row = boxed_result->current_row;

    if (boxed_result->row_count == 0) {
        lily_vm_module_raise(vm, &error_seed,
                "Result does not contain any rows.\n");
    }
    else if (boxed_result->row_count > row) {
        lily_vm_module_raise(vm, &error_seed,
                "Attempt to read past last row.\n");
    }

    lily_list_val *lv = lily_new_list_val();
    lv->elems = lily_malloc(boxed_result->column_count * sizeof(lily_value *));
    lily_type *string_type = result_reg->type->subtypes[0];

    int i;
    for (i = 0;i < boxed_result->column_count;i++) {
        char *field_text = PQgetvalue(raw_result, row, i);
        int len;

        if (field_text[0] != '\0' || PQgetisnull(raw_result, row, i) == 0)
            len = PQgetlength(raw_result, row, i);
        else {
            field_text = "(null)";
            len = strlen("(null)");
        }

        lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
        sv->refcount = 1;
        sv->string = lily_malloc(len + 1);
        strcpy(sv->string, field_text);
        sv->size = len;

        lily_value *v = lily_malloc(sizeof(lily_value));
        v->type = string_type;
        v->flags = 0;
        v->value.string = sv;

        lv->elems[i] = v;
    }

    lv->num_values = i;

    boxed_result->current_row++;

    lily_raw_value v = {.list = lv};
    lily_move_raw_value(vm, result_reg, 0, v);
}

static const lily_func_seed result_dynaload_start =
    {NULL, "fetchrow", dyna_function, "function fetchrow(Result => list[string])", &lily_pg_result_fetchrow};

const lily_class_seed result_seed =
{
    &error_seed,            /* next */
    "Result",               /* name */
    dyna_class,             /* load_type */
    1,                      /* is_refcounted */
    0,                      /* generic_count */
    0,                      /* flags */
    &result_dynaload_start, /* dynaload_table */
    NULL,                   /* gc_marker */
    lily_generic_eq,        /* eq_func */
    destroy_result          /* destroy_func */
};

/******************************************************************************/
/* Conn                                                                       */
/******************************************************************************/

typedef struct lily_pg_conn_value_ {
    uint32_t refcount;
    uint32_t is_open;
    PGconn *conn;
} lily_pg_conn_value;

void lily_pg_conn_query(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    char *fmt;
    int arg_pos, fmt_index;
    lily_list_val *vararg_lv;
    lily_value **vm_regs = vm->vm_regs;
    lily_msgbuf *vm_buffer = vm->vm_buffer;
    lily_value *result_reg;

    lily_msgbuf_flush(vm_buffer);

    result_reg = vm_regs[code[3]];
    lily_pg_conn_value *conn_value =
            (lily_pg_conn_value *)vm_regs[code[0]]->value.generic;
    fmt = vm_regs[code[1]]->value.string->string;
    vararg_lv = vm_regs[code[2]]->value.list;
    arg_pos = 0;
    fmt_index = 0;
    int text_start = 0;
    int text_stop = 0;

    while (1) {
        char ch = fmt[fmt_index];

        if (ch == '?') {
            if (arg_pos == vararg_lv->num_values)
                lily_raise(vm->raiser, lily_FormatError,
                        "Not enough args for format.\n");

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
        lily_vm_set_error(vm, &error_seed, PQresStatus(status));
        PQclear(raw_result);
        lily_vm_raise_prepared(vm);
    }

    lily_pg_result *new_result = lily_malloc(sizeof(lily_pg_result));
    new_result->refcount = 1;
    new_result->current_row = 0;
    new_result->pg_result = raw_result;
    new_result->row_count = PQntuples(raw_result);
    new_result->column_count = PQnfields(raw_result);

    lily_raw_value v = {.generic = (lily_generic_val *)new_result};
    lily_move_raw_value(vm, result_reg, 0, v);
}

void lily_pg_conn_new(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    const char *host = NULL;
    const char *port = NULL;
    const char *dbname = NULL;
    const char *name = NULL;
    const char *pass = NULL;

    switch (argc) {
        case 6:
            pass = vm_regs[code[4]]->value.string->string;
        case 5:
            name = vm_regs[code[3]]->value.string->string;
        case 4:
            dbname = vm_regs[code[2]]->value.string->string;
        case 3:
            port = vm_regs[code[1]]->value.string->string;
        case 2:
            host = vm_regs[code[0]]->value.string->string;
    }

    lily_value *result = vm_regs[code[argc]];

    PGconn *conn = PQsetdbLogin(host, port, NULL, NULL, dbname, name, pass);
    lily_pg_conn_value *new_val;

    switch (PQstatus(conn)) {
        case CONNECTION_OK:
            new_val = lily_malloc(sizeof(lily_pg_conn_value));
            new_val->refcount = 1;
            new_val->is_open = 1;
            new_val->conn = conn;
            break;
        case CONNECTION_BAD:
            lily_vm_set_error(vm, &error_seed, PQerrorMessage(conn));
            PQfinish(conn);
            lily_vm_raise_prepared(vm);
            break;
        default:
            /* Not possible (synchronous connection), but keeps gcc quiet. */
            new_val = NULL;
            break;
    }

    lily_raw_value v = {.generic = (lily_generic_val *)new_val};
    lily_move_raw_value(vm, result, 0, v);
}

void destroy_conn(lily_value *v)
{
    lily_pg_conn_value *conn_value = (lily_pg_conn_value *)v->value.generic;

    PQfinish(conn_value->conn);
    lily_free(conn_value);
}

static const lily_func_seed conn_query =
    {NULL, "query", dyna_function, "function query(Conn, string, list[string]... => Result)", lily_pg_conn_query};

static const lily_func_seed conn_dynaload_start =
    {&conn_query, "new", dyna_function, "function new(*string, *string, *string, *string, *string => Conn)", lily_pg_conn_new};

const lily_class_seed lily_dynaload_table =
{
    &result_seed,         /* next */
    "Conn",               /* name */
    dyna_class,           /* load_type */
    1,                    /* is_refcounted */
    0,                    /* generic_count */
    0,                    /* flags */
    &conn_dynaload_start, /* dynaload_table */
    NULL,                 /* gc_marker */
    lily_generic_eq,      /* eq_func */
    destroy_conn          /* destroy_func */
};

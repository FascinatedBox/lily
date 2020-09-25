#include <string.h>

#include "lily.h"
#include "lily_alloc.h"
#include "lily_backbone_bindings.h"
#include "lily_value_flags.h"
#include "lily_value_structs.h"
#include "lily_vm.h"

typedef struct lily_backbone_RawInterpreter_ {
    LILY_FOREIGN_HEADER
    lily_state *subi;
    lily_config config;
    lily_state *sourcei;
    lily_value interp_reg;
} lily_backbone_RawInterpreter;

void lily_backbone_TestCaseBase_new(lily_state *s)
{
    lily_container_val *con = SUPER_TestCaseBase(s);

    lily_push_integer(s, 0);
    SETFS_TestCaseBase__pass_count(s, con);

    lily_push_integer(s, 0);
    SETFS_TestCaseBase__fail_count(s, con);

    lily_push_integer(s, 0);
    SETFS_TestCaseBase__skip_count(s, con);

    lily_return_super(s);
}

static lily_raw_value value_for_id(lily_state *s, uint16_t id)
{
    return s->gs->readonly_table[id]->value;
}

/* The interpreter does not provide a mechanism to allow foreign functions to
   capture exceptions. Tests must be run through a driver function. */
static lily_function_val *get_driver_fn(lily_state *s)
{
    lily_container_val *con = lily_arg_container(s, 0);

    uint16_t class_id = con->class_id;
    lily_class *cls = s->gs->class_table[class_id];

    /* Get the parent of that class. It should be 'TestCase'. */
    lily_class *parent_cls = cls->parent;

    /* Foreign code can't catch exceptions, so send functions to this. */
    lily_var *drive_fn_var = (lily_var *)lily_find_member(parent_cls,
            "run_one_test");

    if (drive_fn_var == NULL)
        lily_RuntimeError(s, "Parent class is missing test driving function.");

    uint16_t drive_spot = drive_fn_var->reg_spot;
    lily_raw_value drive_fn_value = value_for_id(s, drive_spot);

    return drive_fn_value.function;
}

static lily_class *get_container_cls(lily_state *s)
{
    lily_container_val *con = lily_arg_container(s, 0);

    /* Get the class that was passed. */
    uint16_t class_id = con->class_id;
    lily_class *cls = s->gs->class_table[class_id];

    return cls;
}

/* Select all methods on the class provided. The returned list is ordered from
   first-to-last (versus the interpreter's last-to-first order).
   The end of the list is denoted with NULL. To mark tests that should be
   ignored, replace the var with the constructor var at 0. */
static lily_var **methods_for_class(lily_class *cls)
{
    uint16_t method_count = 0;
    lily_named_sym *member_iter = cls->members;

    while (member_iter) {
        if (member_iter->item_kind == ITEM_VAR)
            method_count++;

        member_iter = member_iter->next;
    }

    lily_var **method_vars = lily_malloc((method_count + 1) * sizeof(*method_vars));
    int i = method_count - 1;
    member_iter = cls->members;

    while (member_iter) {
        /* Members are stored in first in, last out order. Reverse that while
           also collecting them. */
        if (member_iter->item_kind == ITEM_VAR) {
            method_vars[i] = (lily_var *)member_iter;
            i--;
        }

        member_iter = member_iter->next;
    }

    method_vars[method_count] = NULL;

    return method_vars;
}

/* Given a list of method vars, reject any that are not testing methods. Tests
   that are dropped are replaced with the constructor at 0. */
static void filter_test_methods(lily_var **method_vars)
{
    int i;
    /* The constructor is always first. If any function is going to be rejected,
       replace it with the constructor. */
    lily_var *skip = method_vars[0];
    /* The constructor returns the self id. */
    uint16_t self_id = skip->type->subtypes[0]->cls->id;

    for (i = 1;method_vars[i] != NULL;i++) {
        lily_var *v = method_vars[i];

        /* Has to start with 'test_'. */
        if (strncmp(v->name, "test_", 5) != 0) {
            method_vars[i] = skip;
            continue;
        }

        lily_type *v_type = v->type;

        /* Must take only one argument (self) and return Unit. */
        if (v_type->subtype_count != 2 ||
            v_type->subtypes[1]->cls->id != self_id ||
            v_type->subtypes[0]->cls->id != LILY_ID_UNIT) {
            method_vars[i] = skip;
            continue;
        }
    }
}

/* Execute the methods provided, skipping any removed (replaced by constructor)
   methods. It is assumed that the driver function will trap any exceptions that
   are raised. */
static void execute_test_methods(lily_state *s, lily_function_val *drive_fn,
        lily_var **method_vars)
{
    int i;
    lily_var *skip = method_vars[0];
    lily_value *source_class_value = lily_arg_value(s, 0);

    lily_call_prepare(s, drive_fn);

    for (i = 1;method_vars[i] != NULL;i++) {
        lily_var *m = method_vars[i];

        if (m == skip)
            continue;

        lily_raw_value raw_fn_value = value_for_id(s, m->reg_spot);
        lily_value fn_value;

        fn_value.flags = V_FUNCTION_BASE;
        fn_value.value = raw_fn_value;

        /* The first is the driver's self, and the second is the 'A' of the
           driver function. */
        lily_push_value(s, source_class_value);
        lily_push_value(s, source_class_value);
        lily_push_value(s, &fn_value);
        lily_push_string(s, m->name);
        lily_call(s, 4);
    }
}

void lily_backbone_TestCaseBase_run_tests(lily_state *s)
{
    lily_function_val *drive_fn = get_driver_fn(s);
    lily_class *container_cls = get_container_cls(s);
    lily_var **method_vars = methods_for_class(container_cls);

    filter_test_methods(method_vars);
    execute_test_methods(s, drive_fn, method_vars);

    lily_free(method_vars);

    lily_return_unit(s);
}

static void destroy_RawInterpreter(lily_backbone_RawInterpreter *raw)
{
    lily_free_state(raw->subi);
}

void lily_backbone_RawInterpreter_new(lily_state *s)
{
    lily_RuntimeError(s, "Not allowed to construct RawInterpreter instances.");
}

void render_noop(const char *to_render, void *data)
{
    (void)data;
    (void)to_render;
}

void lily_backbone_Interpreter_new(lily_state *s)
{
    lily_container_val *interp = SUPER_Interpreter(s);

    lily_backbone_RawInterpreter *raw = INIT_RawInterpreter(s);
    lily_config_init(&raw->config);
    raw->subi = lily_new_state(&raw->config);
    raw->sourcei = s;
    raw->config.render_func = render_noop;

    SETFS_Interpreter__raw(s, interp);

    lily_return_super(s);
}

/* Helper function to unpack the foreign RawInterpreter. Since all of these
   functions take `Interpreter` as argument 0, this takes a state and unpacks
   from argument 0. */
static lily_backbone_RawInterpreter *unpack_rawinterp(lily_state *s)
{
    lily_value *boxed_interp = lily_arg_value(s, 0);
    lily_container_val *interp = lily_as_container(boxed_interp);
    lily_value *field_raw = GET_Interpreter__raw(interp);
    lily_backbone_RawInterpreter *raw = AS_RawInterpreter(field_raw);

    return raw;
}

void lily_backbone_Interpreter_error(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);

    lily_push_string(s, lily_error_message(raw->subi));
    lily_return_top(s);
}

void lily_backbone_Interpreter_error_message(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);

    lily_push_string(s, lily_error_message_no_trace(raw->subi));
    lily_return_top(s);
}

void lily_backbone_Interpreter_import_use_local_dir(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);
    const char *dir = lily_arg_string_raw(s, 1);
    lily_state *subi = raw->subi;

    lily_import_use_local_dir(subi, dir);
    lily_return_unit(s);
}

void lily_backbone_Interpreter_import_use_package_dir(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);
    const char *dir = lily_arg_string_raw(s, 1);
    lily_state *subi = raw->subi;

    lily_import_use_package_dir(subi, dir);
    lily_return_unit(s);
}

void lily_backbone_Interpreter_import_file(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);
    const char *path = lily_arg_string_raw(s, 1);
    lily_state *subi = raw->subi;

    lily_return_boolean(s, lily_import_file(subi, path));
}

void lily_backbone_Interpreter_import_library(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);
    const char *path = lily_arg_string_raw(s, 1);
    lily_state *subi = raw->subi;

    lily_return_boolean(s, lily_import_library(subi, path));
}

void lily_backbone_Interpreter_import_string(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);
    const char *target = lily_arg_string_raw(s, 1);
    const char *content = lily_arg_string_raw(s, 2);
    lily_state *subi = raw->subi;

    lily_return_boolean(s, lily_import_string(subi, target, content));
}

void lily_backbone_Interpreter_import_current_root_dir(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);
    lily_state *subi = raw->subi;

    lily_push_string(s, lily_import_current_root_dir(subi));
    lily_return_top(s);
}

void lily_backbone_Interpreter_parse_expr(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);

    char *context = lily_arg_string_raw(s, 1);
    char *text = lily_arg_string_raw(s, 2);
    const char *out_text;

    lily_load_string(raw->subi, context, text);
    int ok = lily_parse_expr(raw->subi, &out_text);

    if (ok) {
        if (out_text == NULL)
            out_text = "";

        lily_container_val *somev = lily_push_some(s);
        lily_push_string(s, out_text);
        lily_con_set_from_stack(s, somev, 0);
        lily_return_top(s);
    }
    else
        lily_return_none(s);
}

void lily_backbone_Interpreter_parse_file(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);

    char *filename = lily_arg_string_raw(s, 1);
    int result = lily_load_file(raw->subi, filename) &&
                 lily_parse_content(raw->subi);

    lily_return_boolean(s, result);
}

void lily_backbone_Interpreter_parse_string(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);

    char *context = lily_arg_string_raw(s, 1);
    char *text = lily_arg_string_raw(s, 2);
    int result = lily_load_string(raw->subi, context, text) &&
                 lily_parse_content(raw->subi);

    lily_return_boolean(s, result);
}

void lily_backbone_Interpreter_render_string(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);

    char *context = lily_arg_string_raw(s, 1);
    char *text = lily_arg_string_raw(s, 2);
    int result = lily_load_string(raw->subi, context, text) &&
                 lily_render_content(raw->subi);

    lily_return_boolean(s, result);
}

void lily_backbone_Interpreter_validate_file(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);

    char *filename = lily_arg_string_raw(s, 1);
    int result = lily_load_file(raw->subi, filename) &&
                 lily_validate_content(raw->subi);

    lily_return_boolean(s, result);
}

void lily_backbone_Interpreter_validate_string(lily_state *s)
{
    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);

    char *context = lily_arg_string_raw(s, 1);
    char *text = lily_arg_string_raw(s, 2);
    int result = lily_load_string(raw->subi, context, text) &&
                 lily_validate_content(raw->subi);

    lily_return_boolean(s, result);
}

static void backbone_import_hook(lily_state *s, const char *target)
{
    /* The state passed is the subinterp's state. Need to get the source
       interpreter from the data stored. */
    lily_config *config = lily_config_get(s);
    lily_container_val *interp = (lily_container_val *)config->data;
    lily_value *field_raw = GET_Interpreter__raw(interp);
    lily_backbone_RawInterpreter *raw = AS_RawInterpreter(field_raw);

    /* This is the calling interpreter to jump back into. */
    lily_state *sourcei = raw->sourcei;

    /* Here's the stored hook to call in it. */
    lily_value *field_hook = GET_Interpreter__import_hook(interp);
    lily_function_val *hook_fn = lily_as_function(field_hook);

    lily_call_prepare(sourcei, hook_fn);
    lily_push_value(sourcei, &raw->interp_reg);
    lily_push_string(sourcei, target);
    lily_call(sourcei, 2);

    /* This is a raw C callback so no returning to either interp. */
}

void lily_backbone_Interpreter_set_hook(lily_state *s)
{
    lily_value *interp_reg = lily_arg_value(s, 0);
    lily_container_val *interp = lily_as_container(interp_reg);
    lily_value *hook_arg = lily_arg_value(s, 1);

    SET_Interpreter__import_hook(interp, hook_arg);

    lily_backbone_RawInterpreter *raw = unpack_rawinterp(s);
    lily_state *subinterp = raw->subi;
    lily_config *config = lily_config_get(subinterp);

    /* Raw instances don't know all of their information (such as if they have a
       gc tag), so the interpreter doesn't provide a push function for that use
       case. In lieu of that, keep a value in the foreign instance that doesn't
       have a refcount. It's a hack. */
    memcpy(&raw->interp_reg, interp_reg, sizeof(raw->interp_reg));

    /* The import hook is a raw C function. Give the subinterp the calling
       interp's state so it can be fetched back out in the hook. */
    config->import_func = backbone_import_hook;
    config->data = interp;

    lily_return_unit(s);
}

LILY_DECLARE_BACKBONE_CALL_TABLE

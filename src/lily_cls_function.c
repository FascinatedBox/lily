#include "lily_alloc.h"
#include "lily_core_types.h"

lily_function_val *lily_new_foreign_function_val(lily_foreign_func func,
        char *class_name, char *name)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = func;
    f->code = NULL;
    f->reg_info = NULL;
    f->reg_count = -1;
    f->has_generics = 0;
    return f;
}

lily_function_val *lily_new_native_function_val(char *class_name,
        char *name)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = NULL;
    f->code = NULL;
    f->reg_info = NULL;
    f->reg_count = -1;
    f->has_generics = 0;
    return f;
}

void lily_destroy_function(lily_value *v)
{
    lily_function_val *fv = v->value.function;

    if (fv->reg_info != NULL) {
        int i;
        for (i = 0;i < fv->reg_count;i++)
            lily_free(fv->reg_info[i].name);
    }

    lily_free(fv->reg_info);
    lily_free(fv->code);
    lily_free(fv);
}

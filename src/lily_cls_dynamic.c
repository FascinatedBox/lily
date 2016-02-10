#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_value.h"
#include "lily_seed.h"

lily_dynamic_val *lily_new_dynamic_val()
{
    lily_dynamic_val *d = lily_malloc(sizeof(lily_dynamic_val));

    d->inner_value = lily_new_value(0,
            (lily_raw_value){.integer = 0});
    d->gc_entry = NULL;
    d->refcount = 1;

    return d;
}

void lily_gc_dynamic_marker(int pass, lily_value *v)
{
    lily_value *inner_value = v->value.dynamic->inner_value;

    if (inner_value->flags & VAL_IS_GC_TAGGED)
        lily_gc_mark(pass, inner_value);
}

void lily_destroy_dynamic(lily_value *v)
{
    lily_dynamic_val *dv = v->value.dynamic;

    dv->gc_entry->value.generic = NULL;

    lily_deref(dv->inner_value);

    lily_free(dv->inner_value);
    lily_free(dv);
}

void lily_gc_collect_dynamic(lily_value *v)
{
    lily_dynamic_val *dynamic_val = v->value.dynamic;
    if (dynamic_val->gc_entry->value.generic != NULL &&
        dynamic_val->gc_entry->last_pass != -1) {
        /* This lets the gc know that everything inside is gone. */
        dynamic_val->gc_entry->last_pass = -1;
        lily_value *inner_value = dynamic_val->inner_value;
        if (inner_value->flags & VAL_IS_DEREFABLE) {
            lily_generic_val *generic_val = inner_value->value.generic;
            if (generic_val->refcount == 1)
                lily_gc_collect_value(inner_value);
            else
                generic_val->refcount--;
        }

        lily_free(dynamic_val->inner_value);
        /* Do not free dynamic_val here: Let the gc do that later. */
    }
}

void lily_dynamic_new(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result = vm_regs[code[0]];
    lily_value *input = vm_regs[code[1]];

    if (input->flags & VAL_IS_DEREFABLE)
        input->value.generic->refcount++;

    lily_dynamic_val *dynamic_val = lily_new_dynamic_val();

    *(dynamic_val->inner_value) = *input;
    lily_move_dynamic(result, dynamic_val);
    lily_tag_value(vm, result);
}

const lily_func_seed lily_dynamic_dl_start =
    {NULL, "new", dyna_function, "[A](A):Dynamic", &lily_dynamic_new};

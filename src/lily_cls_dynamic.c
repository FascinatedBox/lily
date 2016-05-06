#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_seed.h"

#include "lily_api_value.h"

extern lily_gc_entry *lily_gc_stopper;

void lily_gc_dynamic_marker(int pass, lily_value *v)
{
    lily_value *inner_value = v->value.dynamic->inner_value;

    if (inner_value->flags & VAL_IS_GC_SWEEPABLE)
        lily_gc_mark(pass, inner_value);
}

void lily_destroy_dynamic(lily_value *v)
{
    lily_dynamic_val *dv = v->value.dynamic;

    int full_destroy = 1;
    if (dv->gc_entry) {
        if (dv->gc_entry->last_pass == -1) {
            full_destroy = 0;
            dv->gc_entry = lily_gc_stopper;
        }
        else
            dv->gc_entry->value.generic = NULL;
    }

    lily_deref(dv->inner_value);
    lily_free(dv->inner_value);

    if (full_destroy)
        lily_free(dv);
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

#include "lily.h"
#include "lily_value_raw.h"
#include "lily_value_structs.h"
#include "lily_vm.h"
#define LILY_NO_EXPORT
#include "lily_pkg_coroutine_bindings.h"

typedef lily_coroutine_val lily_coroutine_Coroutine;

void lily_coroutine_Coroutine_build(lily_state *s)
{
    lily_vm_state *base_vm = lily_vm_coroutine_build(s, ID_Coroutine(s));

    lily_vm_coroutine_call_prep(base_vm, 1);
    lily_return_top(s);
}

void lily_coroutine_Coroutine_build_with_value(lily_state *s)
{
    lily_vm_state *base_vm = lily_vm_coroutine_build(s, ID_Coroutine(s));

    lily_push_value(base_vm, lily_arg_value(s, 1));
    lily_vm_coroutine_call_prep(base_vm, 2);
    lily_return_top(s);
}

void lily_coroutine_Coroutine_is_failed(lily_state *s)
{
    lily_coroutine_val *co_val = ARG_Coroutine(s, 0);
    lily_return_boolean(s, co_val->status == co_failed);
}

void lily_coroutine_Coroutine_is_done(lily_state *s)
{
    lily_coroutine_val *co_val = ARG_Coroutine(s, 0);
    lily_return_boolean(s, co_val->status == co_done);
}

void lily_coroutine_Coroutine_is_running(lily_state *s)
{
    lily_coroutine_val *co_val = ARG_Coroutine(s, 0);
    lily_return_boolean(s, co_val->status == co_running);
}

void lily_coroutine_Coroutine_is_waiting(lily_state *s)
{
    lily_coroutine_val *co_val = ARG_Coroutine(s, 0);
    lily_return_boolean(s, co_val->status == co_waiting);
}

void lily_coroutine_Coroutine_receive(lily_state *s)
{
    lily_coroutine_val *co_val = ARG_Coroutine(s, 0);

    if (co_val->vm != s)
        lily_RuntimeError(s,
                "Attempt to receive a value from another coroutine.");

    lily_push_value(s, co_val->receiver);
    lily_return_top(s);
}

void lily_coroutine_Coroutine_resume(lily_state *s)
{
    lily_vm_coroutine_resume(s, ARG_Coroutine(s, 0), NULL);
    lily_return_top(s);
}

void lily_coroutine_Coroutine_resume_with(lily_state *s)
{
    lily_vm_coroutine_resume(s, ARG_Coroutine(s, 0), lily_arg_value(s, 1));
    lily_return_top(s);
}

void lily_coroutine_Coroutine_yield(lily_state *s)
{
    lily_coroutine_val *co_target = ARG_Coroutine(s, 0);
    lily_value *to_yield = lily_arg_value(s, 1);

    lily_vm_state *co_vm = co_target->vm;

    if (co_vm != s)
        lily_RuntimeError(s, "Cannot yield from another coroutine.");

    lily_raiser *co_raiser = co_vm->raiser;

    /* A vm always has at least two jumps currently active:
     * 1: Parser, or the coroutine base.
     * 2: vm main loop.
       If there are any more jumps, the vm is in a foreign call. A Coroutine in
       a foreign call cannot be restored, because restoration happens by calling
       the vm main loop again. */
    if (co_raiser->all_jumps->prev->prev != NULL)
        lily_RuntimeError(s, "Cannot yield while in a foreign call.");

    /* The yield will not come back, so the return must come first. */
    lily_return_unit(s);

    /* Push the value to be yielded so that the caller has an obvious place to
       find it (top of the stack). The value must be popped before the
       Coroutine is resumed. */
    lily_push_value(co_vm, to_yield);

    /* Since yield is jumping back into the base jump, the main loop is never
       properly exited. The main loop's jump needs to be popped so that it can
       be properly restored when the main loop is entered again. */
    lily_release_jump(co_raiser);

    longjmp(co_raiser->all_jumps->jump, 1);
}

LILY_DECLARE_COROUTINE_CALL_TABLE

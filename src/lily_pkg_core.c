#include "lily.h"
#include "lily_vm.h"

/* This file is automatically generated by scripts/prelude.lily. */

extern const char *lily_prelude_info_table[];
extern const char *lily_coroutine_info_table[];
extern const char *lily_introspect_info_table[];
extern const char *lily_math_info_table[];
extern const char *lily_random_info_table[];
extern const char *lily_subprocess_info_table[];
extern const char *lily_sys_info_table[];
extern const char *lily_time_info_table[];

extern lily_call_entry_func lily_prelude_call_table[];
extern lily_call_entry_func lily_coroutine_call_table[];
extern lily_call_entry_func lily_introspect_call_table[];
extern lily_call_entry_func lily_math_call_table[];
extern lily_call_entry_func lily_random_call_table[];
extern lily_call_entry_func lily_subprocess_call_table[];
extern lily_call_entry_func lily_sys_call_table[];
extern lily_call_entry_func lily_time_call_table[];

void lily_prelude_register(lily_vm_state *vm)
{
    lily_module_register(vm, "coroutine", lily_coroutine_info_table, lily_coroutine_call_table);
    lily_module_register(vm, "introspect", lily_introspect_info_table, lily_introspect_call_table);
    lily_module_register(vm, "math", lily_math_info_table, lily_math_call_table);
    lily_module_register(vm, "random", lily_random_info_table, lily_random_call_table);
    lily_module_register(vm, "subprocess", lily_subprocess_info_table, lily_subprocess_call_table);
    lily_module_register(vm, "sys", lily_sys_info_table, lily_sys_call_table);
    lily_module_register(vm, "time", lily_time_info_table, lily_time_call_table);
}

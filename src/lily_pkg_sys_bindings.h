#ifndef LILY_SYS_BINDINGS_H
#define LILY_SYS_BINDINGS_H
/* Generated by lily-bindgen, do not edit. */

#if defined(_WIN32) && !defined(LILY_NO_EXPORT)
#define LILY_SYS_EXPORT __declspec(dllexport)
#else
#define LILY_SYS_EXPORT
#endif

LILY_SYS_EXPORT
const char *lily_sys_info_table[] = {
    "\0\0"
    ,"F\0exit\0(Byte)"
    ,"F\0exit_failure\0"
    ,"F\0exit_success\0"
    ,"F\0getenv\0(String): Option[String]"
    ,"F\0recursion_limit\0: Integer"
    ,"F\0set_recursion_limit\0(Integer)"
    ,"R\0argv\0List[String]"
    ,"Z"
};
#define LILY_DECLARE_SYS_CALL_TABLE \
LILY_SYS_EXPORT \
lily_call_entry_func lily_sys_call_table[] = { \
    NULL, \
    lily_sys__exit, \
    lily_sys__exit_failure, \
    lily_sys__exit_success, \
    lily_sys__getenv, \
    lily_sys__recursion_limit, \
    lily_sys__set_recursion_limit, \
    lily_sys_var_argv, \
};
#endif

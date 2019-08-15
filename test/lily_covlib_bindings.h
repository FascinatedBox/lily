#ifndef LILY_COVLIB_BINDINGS_H
#define LILY_COVLIB_BINDINGS_H
/* Generated by lily-bindgen, do not edit. */

#if defined(_WIN32) && !defined(LILY_NO_EXPORT)
#define LILY_COVLIB_EXPORT __declspec(dllexport)
#else
#define LILY_COVLIB_EXPORT
#endif

typedef struct lily_covlib_Foreign_ {
    LILY_FOREIGN_HEADER
} lily_covlib_Foreign;
#define ARG_Foreign(state, index) \
(lily_covlib_Foreign *)lily_arg_generic(state, index)
#define AS_Foreign(v_)\
((lily_covlib_Foreign *)(lily_as_generic(v_)))
#define ID_Foreign(state) lily_cid_at(state, 0)
#define INIT_Foreign(state)\
(lily_covlib_Foreign *) lily_push_foreign(state, ID_Foreign(state), (lily_destroy_func)destroy_Foreign, sizeof(lily_covlib_Foreign))

#define GET_Container__value(c_) \
lily_con_get(c_, 0)
#define SET_Container__value(c_, v_) \
lily_con_set(c_, 0, v_)
#define SETFS_Container__value(state, c_) \
lily_con_set_from_stack(state, c_, 0)
#define ID_Container(state) lily_cid_at(state, 1)
#define SUPER_Container(state)\
lily_push_super(state, ID_Container(state), 1)

#define PUSH_FlatOne(state)\
lily_push_empty_variant(state, lily_cid_at(state, 2) + 1)
#define PUSH_FlatTwo(state)\
lily_push_empty_variant(state, lily_cid_at(state, 2) + 2)
#define PUSH_FlatThree(state)\
lily_push_empty_variant(state, lily_cid_at(state, 2) + 3)

#define PUSH_ScopedOne(state)\
lily_push_empty_variant(state, lily_cid_at(state, 3) + 1)
#define PUSH_ScopedTwo(state)\
lily_push_empty_variant(state, lily_cid_at(state, 3) + 2)
#define PUSH_ScopedThree(state)\
lily_push_empty_variant(state, lily_cid_at(state, 3) + 3)

LILY_COVLIB_EXPORT
const char *lily_covlib_info_table[] = {
    "\04Foreign\0Container\0FlatEnum\0ScopedEnum\0"
    ,"C\01Foreign\0"
    ,"m\0<new>\0: Foreign"
    ,"N\04Container\0"
    ,"m\0<new>\0(String): Container"
    ,"m\0update\0(Container,String)"
    ,"m\0fetch\0(Container): String"
    ,"1\0value\0String"
    ,"E\0FlatEnum\0"
    ,"V\0FlatOne\0"
    ,"V\0FlatTwo\0"
    ,"V\0FlatThree\0"
    ,"E\03ScopedEnum\0"
    ,"V\0ScopedOne\0"
    ,"V\0ScopedTwo\0"
    ,"V\0ScopedThree\0"
    ,"F\0isa_integer\0[A](A): Boolean"
    ,"F\0cover_list_reserve\0"
    ,"F\0cover_func_check\0(Function(Integer),Function(Integer=>String)): Boolean"
    ,"F\0cover_list_sfs\0"
    ,"F\0cover_id_checks\0[A](Coroutine[Integer,Integer],Unit,A,String): Boolean"
    ,"F\0cover_value_as\0(Byte,ByteString,Exception,Coroutine[Integer,Integer],Double,File,Function(Integer),Hash[Integer,Integer],Integer,String)"
    ,"F\0cover_value_group\0(Boolean,Byte,ByteString,Coroutine[Integer,Integer],Double,Option[Integer],File,Function(Integer),Hash[Integer,Integer],Foreign,Exception,Integer,List[Integer],String,Tuple[Integer],Unit,Option[Integer]): Boolean"
    ,"F\0cover_ci_from_native\0(Function(Integer))"
    ,"F\0cover_misc_api\0"
    ,"Z"
};
#define LILY_DECLARE_COVLIB_CALL_TABLE \
LILY_COVLIB_EXPORT \
lily_call_entry_func lily_covlib_call_table[] = { \
    NULL, \
    NULL, \
    lily_covlib_Foreign_new, \
    NULL, \
    lily_covlib_Container_new, \
    lily_covlib_Container_update, \
    lily_covlib_Container_fetch, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    lily_covlib__isa_integer, \
    lily_covlib__cover_list_reserve, \
    lily_covlib__cover_func_check, \
    lily_covlib__cover_list_sfs, \
    lily_covlib__cover_id_checks, \
    lily_covlib__cover_value_as, \
    lily_covlib__cover_value_group, \
    lily_covlib__cover_ci_from_native, \
    lily_covlib__cover_misc_api, \
};
#endif

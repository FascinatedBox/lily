#ifndef LILY_COVLIB_BINDINGS_H
#define LILY_COVLIB_BINDINGS_H
/* Generated by lily-bindgen, do not edit. */

#if defined(_WIN32) && !defined(LILY_NO_EXPORT)
#define LILY_COVLIB_EXPORT __declspec(dllexport)
#else
#define LILY_COVLIB_EXPORT
#endif

#define GET_Container__value(c_) \
lily_con_get(c_, 0)
#define SET_Container__value(c_, v_) \
lily_con_set(c_, 0, v_)
#define SETFS_Container__value(state, c_) \
lily_con_set_from_stack(state, c_, 0)
#define ID_Container(s_) \
lily_cid_at(s_, 0)
#define SUPER_Container(s_) \
lily_push_super(s_, ID_Container(s_), 1)

#define ARG_Foreign(s_, i_) \
(lily_covlib_Foreign *)lily_arg_generic(s_, i_)
#define AS_Foreign(v_) \
(lily_covlib_Foreign *)lily_as_generic(v_)
#define ID_Foreign(s_) \
lily_cid_at(s_, 1)
#define INIT_Foreign(s_) \
(lily_covlib_Foreign *)lily_push_foreign(s_, ID_Foreign(s_), (lily_destroy_func)destroy_Foreign, sizeof(lily_covlib_Foreign))

#define ARG_ForeignGeneric(s_, i_) \
(lily_covlib_ForeignGeneric *)lily_arg_generic(s_, i_)
#define AS_ForeignGeneric(v_) \
(lily_covlib_ForeignGeneric *)lily_as_generic(v_)
#define ID_ForeignGeneric(s_) \
lily_cid_at(s_, 2)
#define INIT_ForeignGeneric(s_) \
(lily_covlib_ForeignGeneric *)lily_push_foreign(s_, ID_ForeignGeneric(s_), (lily_destroy_func)destroy_ForeignGeneric, sizeof(lily_covlib_ForeignGeneric))

#define ID_FlatOne(s_) \
(lily_cid_at(s_, 3) + 1)
#define PUSH_FlatOne(state)\
lily_push_empty_variant(state, lily_cid_at(state, 3) + 1)
#define ID_FlatThree(s_) \
(lily_cid_at(s_, 3) + 2)
#define PUSH_FlatThree(state)\
lily_push_empty_variant(state, lily_cid_at(state, 3) + 2)
#define ID_FlatTwo(s_) \
(lily_cid_at(s_, 3) + 3)
#define PUSH_FlatTwo(state)\
lily_push_empty_variant(state, lily_cid_at(state, 3) + 3)

#define ID_ScopedOne(s_) \
(lily_cid_at(s_, 4) + 1)
#define PUSH_ScopedOne(state)\
lily_push_empty_variant(state, lily_cid_at(state, 4) + 1)
#define ID_ScopedThree(s_) \
(lily_cid_at(s_, 4) + 2)
#define PUSH_ScopedThree(state)\
lily_push_empty_variant(state, lily_cid_at(state, 4) + 2)
#define ID_ScopedTwo(s_) \
(lily_cid_at(s_, 4) + 3)
#define PUSH_ScopedTwo(state)\
lily_push_empty_variant(state, lily_cid_at(state, 4) + 3)

LILY_COVLIB_EXPORT
const char *lily_covlib_info_table[] = {
    "\05Container\0Foreign\0ForeignGeneric\0FlatEnum\0ScopedEnum\0"
    ,"N\04Container\0"
    ,"m\0<new>\0(String): Container"
    ,"m\0fetch\0(Container): String"
    ,"m\0update\0(Container,String)"
    ,"1\0value\0String"
    ,"C\01Foreign\0"
    ,"m\0<new>\0: Foreign"
    ,"C\0ForeignGeneric\0[A,B]"
    ,"E\0FlatEnum\0"
    ,"V\0FlatOne\0"
    ,"V\0FlatThree\0"
    ,"V\0FlatTwo\0"
    ,"E\03ScopedEnum\0"
    ,"V\0ScopedOne\0"
    ,"V\0ScopedThree\0"
    ,"V\0ScopedTwo\0"
    ,"F\0cover_func_check\0(Function(Integer),Function(Integer=>String)): Boolean"
    ,"F\0cover_function_bytecode\0(Function(String),Function(Integer)): Boolean"
    ,"F\0cover_id_checks\0[A](Unit,A,String): Boolean"
    ,"F\0cover_list_reserve\0"
    ,"F\0cover_list_sfs\0"
    ,"F\0cover_misc_api\0"
    ,"F\0cover_optional_boolean\0(*Boolean,:b *Boolean,:c *Boolean): Integer"
    ,"F\0cover_optional_integer\0(*Integer,:b *Integer,:c *Integer): Integer"
    ,"F\0cover_optional_keyarg_call\0(Function(*Integer,*Integer,*Integer=>Integer)): Integer"
    ,"F\0cover_optional_string\0(*String,:b *String,:c *String): String"
    ,"F\0cover_push_boolean\0: Boolean"
    ,"F\0cover_value_as\0(Byte,ByteString,Exception,Double,File,Function(Integer),Foreign,Hash[Integer,Integer],Integer,String)"
    ,"F\0cover_value_group\0(Boolean,Byte,ByteString,Double,Option[Integer],File,Function(Integer),Hash[Integer,Integer],Foreign,Exception,Integer,List[Integer],String,Tuple[Integer],Unit,Option[Integer]): Boolean"
    ,"F\0make_flat_n\0(Integer): FlatEnum"
    ,"F\0make_scoped_n\0(Integer): ScopedEnum"
    ,"F\0raise_dbzerror\0"
    ,"F\0raise_keyerror\0"
    ,"F\0scoop_narrow\0(Function($1))"
    ,"F\0scoop_narrow_with_args\0(Function(Integer,String,$1=>Boolean))"
    ,"Z"
};
#define LILY_DECLARE_COVLIB_CALL_TABLE \
LILY_COVLIB_EXPORT \
lily_call_entry_func lily_covlib_call_table[] = { \
    NULL, \
    NULL, \
    lily_covlib_Container_new, \
    lily_covlib_Container_fetch, \
    lily_covlib_Container_update, \
    NULL, \
    NULL, \
    lily_covlib_Foreign_new, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    lily_covlib__cover_func_check, \
    lily_covlib__cover_function_bytecode, \
    lily_covlib__cover_id_checks, \
    lily_covlib__cover_list_reserve, \
    lily_covlib__cover_list_sfs, \
    lily_covlib__cover_misc_api, \
    lily_covlib__cover_optional_boolean, \
    lily_covlib__cover_optional_integer, \
    lily_covlib__cover_optional_keyarg_call, \
    lily_covlib__cover_optional_string, \
    lily_covlib__cover_push_boolean, \
    lily_covlib__cover_value_as, \
    lily_covlib__cover_value_group, \
    lily_covlib__make_flat_n, \
    lily_covlib__make_scoped_n, \
    lily_covlib__raise_dbzerror, \
    lily_covlib__raise_keyerror, \
    lily_covlib__scoop_narrow, \
    lily_covlib__scoop_narrow_with_args, \
};
#endif

#ifndef LILY_INTROSPECT_BINDINGS_H
#define LILY_INTROSPECT_BINDINGS_H
/* Generated by lily-bindgen, do not edit. */

#if defined(_WIN32) && !defined(LILY_NO_EXPORT)
#define LILY_INTROSPECT_EXPORT __declspec(dllexport)
#else
#define LILY_INTROSPECT_EXPORT
#endif

typedef struct lily_introspect_TypeEntry_ {
    LILY_FOREIGN_HEADER
    lily_type *entry;
} lily_introspect_TypeEntry;
#define ARG_TypeEntry(state, index) \
(lily_introspect_TypeEntry *)lily_arg_generic(state, index)
#define AS_TypeEntry(v_)\
((lily_introspect_TypeEntry *)(lily_as_generic(v_)))
#define ID_TypeEntry(state) lily_cid_at(state, 0)
#define INIT_TypeEntry(state)\
(lily_introspect_TypeEntry *) lily_push_foreign(state, ID_TypeEntry(state), (lily_destroy_func)destroy_TypeEntry, sizeof(lily_introspect_TypeEntry))

#define GET_ParameterEntry__name(c_) \
lily_con_get(c_, 0)
#define SET_ParameterEntry__name(c_, v_) \
lily_con_set(c_, 0, v_)
#define SETFS_ParameterEntry__name(state, c_) \
lily_con_set_from_stack(state, c_, 0)
#define GET_ParameterEntry__keyword(c_) \
lily_con_get(c_, 1)
#define SET_ParameterEntry__keyword(c_, v_) \
lily_con_set(c_, 1, v_)
#define SETFS_ParameterEntry__keyword(state, c_) \
lily_con_set_from_stack(state, c_, 1)
#define GET_ParameterEntry__type(c_) \
lily_con_get(c_, 2)
#define SET_ParameterEntry__type(c_, v_) \
lily_con_set(c_, 2, v_)
#define SETFS_ParameterEntry__type(state, c_) \
lily_con_set_from_stack(state, c_, 2)
#define ID_ParameterEntry(state) lily_cid_at(state, 1)
#define SUPER_ParameterEntry(state)\
lily_push_super(state, ID_ParameterEntry(state), 3)

typedef struct lily_introspect_VarEntry_ {
    LILY_FOREIGN_HEADER
    lily_var *entry;
} lily_introspect_VarEntry;
#define ARG_VarEntry(state, index) \
(lily_introspect_VarEntry *)lily_arg_generic(state, index)
#define AS_VarEntry(v_)\
((lily_introspect_VarEntry *)(lily_as_generic(v_)))
#define ID_VarEntry(state) lily_cid_at(state, 2)
#define INIT_VarEntry(state)\
(lily_introspect_VarEntry *) lily_push_foreign(state, ID_VarEntry(state), (lily_destroy_func)destroy_VarEntry, sizeof(lily_introspect_VarEntry))

typedef struct lily_introspect_PropertyEntry_ {
    LILY_FOREIGN_HEADER
    lily_prop_entry *entry;
    lily_class *parent;
} lily_introspect_PropertyEntry;
#define ARG_PropertyEntry(state, index) \
(lily_introspect_PropertyEntry *)lily_arg_generic(state, index)
#define AS_PropertyEntry(v_)\
((lily_introspect_PropertyEntry *)(lily_as_generic(v_)))
#define ID_PropertyEntry(state) lily_cid_at(state, 3)
#define INIT_PropertyEntry(state)\
(lily_introspect_PropertyEntry *) lily_push_foreign(state, ID_PropertyEntry(state), (lily_destroy_func)destroy_PropertyEntry, sizeof(lily_introspect_PropertyEntry))

typedef struct lily_introspect_FunctionEntry_ {
    LILY_FOREIGN_HEADER
    lily_var *entry;
} lily_introspect_FunctionEntry;
#define ARG_FunctionEntry(state, index) \
(lily_introspect_FunctionEntry *)lily_arg_generic(state, index)
#define AS_FunctionEntry(v_)\
((lily_introspect_FunctionEntry *)(lily_as_generic(v_)))
#define ID_FunctionEntry(state) lily_cid_at(state, 4)
#define INIT_FunctionEntry(state)\
(lily_introspect_FunctionEntry *) lily_push_foreign(state, ID_FunctionEntry(state), (lily_destroy_func)destroy_FunctionEntry, sizeof(lily_introspect_FunctionEntry))

typedef struct lily_introspect_MethodEntry_ {
    LILY_FOREIGN_HEADER
    lily_var *entry;
    lily_class *parent;
} lily_introspect_MethodEntry;
#define ARG_MethodEntry(state, index) \
(lily_introspect_MethodEntry *)lily_arg_generic(state, index)
#define AS_MethodEntry(v_)\
((lily_introspect_MethodEntry *)(lily_as_generic(v_)))
#define ID_MethodEntry(state) lily_cid_at(state, 5)
#define INIT_MethodEntry(state)\
(lily_introspect_MethodEntry *) lily_push_foreign(state, ID_MethodEntry(state), (lily_destroy_func)destroy_MethodEntry, sizeof(lily_introspect_MethodEntry))

typedef struct lily_introspect_ClassEntry_ {
    LILY_FOREIGN_HEADER
    lily_class *entry;
} lily_introspect_ClassEntry;
#define ARG_ClassEntry(state, index) \
(lily_introspect_ClassEntry *)lily_arg_generic(state, index)
#define AS_ClassEntry(v_)\
((lily_introspect_ClassEntry *)(lily_as_generic(v_)))
#define ID_ClassEntry(state) lily_cid_at(state, 6)
#define INIT_ClassEntry(state)\
(lily_introspect_ClassEntry *) lily_push_foreign(state, ID_ClassEntry(state), (lily_destroy_func)destroy_ClassEntry, sizeof(lily_introspect_ClassEntry))

typedef struct lily_introspect_VariantEntry_ {
    LILY_FOREIGN_HEADER
    lily_variant_class *entry;
    lily_class *parent;
} lily_introspect_VariantEntry;
#define ARG_VariantEntry(state, index) \
(lily_introspect_VariantEntry *)lily_arg_generic(state, index)
#define AS_VariantEntry(v_)\
((lily_introspect_VariantEntry *)(lily_as_generic(v_)))
#define ID_VariantEntry(state) lily_cid_at(state, 7)
#define INIT_VariantEntry(state)\
(lily_introspect_VariantEntry *) lily_push_foreign(state, ID_VariantEntry(state), (lily_destroy_func)destroy_VariantEntry, sizeof(lily_introspect_VariantEntry))

typedef struct lily_introspect_EnumEntry_ {
    LILY_FOREIGN_HEADER
    lily_class *entry;
} lily_introspect_EnumEntry;
#define ARG_EnumEntry(state, index) \
(lily_introspect_EnumEntry *)lily_arg_generic(state, index)
#define AS_EnumEntry(v_)\
((lily_introspect_EnumEntry *)(lily_as_generic(v_)))
#define ID_EnumEntry(state) lily_cid_at(state, 8)
#define INIT_EnumEntry(state)\
(lily_introspect_EnumEntry *) lily_push_foreign(state, ID_EnumEntry(state), (lily_destroy_func)destroy_EnumEntry, sizeof(lily_introspect_EnumEntry))

typedef struct lily_introspect_ModuleEntry_ {
    LILY_FOREIGN_HEADER
    lily_module_entry *entry;
} lily_introspect_ModuleEntry;
#define ARG_ModuleEntry(state, index) \
(lily_introspect_ModuleEntry *)lily_arg_generic(state, index)
#define AS_ModuleEntry(v_)\
((lily_introspect_ModuleEntry *)(lily_as_generic(v_)))
#define ID_ModuleEntry(state) lily_cid_at(state, 9)
#define INIT_ModuleEntry(state)\
(lily_introspect_ModuleEntry *) lily_push_foreign(state, ID_ModuleEntry(state), (lily_destroy_func)destroy_ModuleEntry, sizeof(lily_introspect_ModuleEntry))

LILY_INTROSPECT_EXPORT
const char *lily_introspect_info_table[] = {
    "\012TypeEntry\0ParameterEntry\0VarEntry\0PropertyEntry\0FunctionEntry\0MethodEntry\0ClassEntry\0VariantEntry\0EnumEntry\0ModuleEntry\0"
    ,"C\04TypeEntry\0"
    ,"m\0as_string\0(TypeEntry): String"
    ,"m\0class_name\0(TypeEntry): String"
    ,"m\0class_id\0(TypeEntry): Integer"
    ,"m\0inner_types\0(TypeEntry): List[TypeEntry]"
    ,"N\04ParameterEntry\0"
    ,"m\0<new>\0(String,String,TypeEntry): ParameterEntry"
    ,"3\0name\0String"
    ,"3\0keyword\0String"
    ,"3\0type\0TypeEntry"
    ,"C\04VarEntry\0"
    ,"m\0doc\0(VarEntry): String"
    ,"m\0line_number\0(VarEntry): Integer"
    ,"m\0name\0(VarEntry): String"
    ,"m\0type\0(VarEntry): TypeEntry"
    ,"C\06PropertyEntry\0"
    ,"m\0doc\0(PropertyEntry): String"
    ,"m\0is_private\0(PropertyEntry): Boolean"
    ,"m\0is_protected\0(PropertyEntry): Boolean"
    ,"m\0is_public\0(PropertyEntry): Boolean"
    ,"m\0name\0(PropertyEntry): String"
    ,"m\0type\0(PropertyEntry): TypeEntry"
    ,"C\07FunctionEntry\0"
    ,"m\0doc\0(FunctionEntry): String"
    ,"m\0generics\0(FunctionEntry): List[TypeEntry]"
    ,"m\0name\0(FunctionEntry): String"
    ,"m\0line_number\0(FunctionEntry): Integer"
    ,"m\0parameters\0(FunctionEntry): List[ParameterEntry]"
    ,"m\0result_type\0(FunctionEntry): TypeEntry"
    ,"m\0type\0(FunctionEntry): TypeEntry"
    ,"C\012MethodEntry\0"
    ,"m\0function_name\0(MethodEntry): String"
    ,"m\0generics\0(MethodEntry): List[TypeEntry]"
    ,"m\0line_number\0(MethodEntry): Integer"
    ,"m\0is_private\0(MethodEntry): Boolean"
    ,"m\0is_protected\0(MethodEntry): Boolean"
    ,"m\0is_public\0(MethodEntry): Boolean"
    ,"m\0is_static\0(MethodEntry): Boolean"
    ,"m\0parameters\0(MethodEntry): List[ParameterEntry]"
    ,"m\0result_type\0(MethodEntry): TypeEntry"
    ,"m\0type\0(MethodEntry): TypeEntry"
    ,"C\011ClassEntry\0"
    ,"m\0doc\0(ClassEntry): String"
    ,"m\0generics\0(ClassEntry): List[TypeEntry]"
    ,"m\0id\0(ClassEntry): Integer"
    ,"m\0is_foreign\0(ClassEntry): Boolean"
    ,"m\0is_native\0(ClassEntry): Boolean"
    ,"m\0methods\0(ClassEntry): List[MethodEntry]"
    ,"m\0name\0(ClassEntry): String"
    ,"m\0parent\0(ClassEntry): Option[ClassEntry]"
    ,"m\0properties\0(ClassEntry): List[PropertyEntry]"
    ,"C\07VariantEntry\0"
    ,"m\0enum_id\0(VariantEntry): Integer"
    ,"m\0enum_name\0(VariantEntry): String"
    ,"m\0is_empty\0(VariantEntry): Boolean"
    ,"m\0is_scoped\0(VariantEntry): Boolean"
    ,"m\0name\0(VariantEntry): String"
    ,"m\0parameters\0(VariantEntry): List[ParameterEntry]"
    ,"m\0type\0(VariantEntry): TypeEntry"
    ,"C\010EnumEntry\0"
    ,"m\0doc\0(EnumEntry): String"
    ,"m\0generics\0(EnumEntry): List[TypeEntry]"
    ,"m\0id\0(EnumEntry): Integer"
    ,"m\0is_flat\0(EnumEntry): Boolean"
    ,"m\0is_scoped\0(EnumEntry): Boolean"
    ,"m\0methods\0(EnumEntry): List[MethodEntry]"
    ,"m\0variants\0(EnumEntry): List[VariantEntry]"
    ,"m\0name\0(EnumEntry): String"
    ,"C\016ModuleEntry\0"
    ,"m\0boxed_classes\0(ModuleEntry): List[ClassEntry]"
    ,"m\0boxed_enums\0(ModuleEntry): List[EnumEntry]"
    ,"m\0boxed_functions\0(ModuleEntry): List[FunctionEntry]"
    ,"m\0boxed_variants\0(ModuleEntry): List[VariantEntry]"
    ,"m\0boxed_vars\0(ModuleEntry): List[VarEntry]"
    ,"m\0classes\0(ModuleEntry): List[ClassEntry]"
    ,"m\0dirname\0(ModuleEntry): String"
    ,"m\0doc\0(ModuleEntry): String"
    ,"m\0enums\0(ModuleEntry): List[EnumEntry]"
    ,"m\0functions\0(ModuleEntry): List[FunctionEntry]"
    ,"m\0modules_used\0(ModuleEntry): List[ModuleEntry]"
    ,"m\0name\0(ModuleEntry): String"
    ,"m\0path\0(ModuleEntry): String"
    ,"m\0vars\0(ModuleEntry): List[VarEntry]"
    ,"F\0main_module\0: ModuleEntry"
    ,"F\0module_list\0: List[ModuleEntry]"
    ,"Z"
};
#define LILY_DECLARE_INTROSPECT_CALL_TABLE \
LILY_INTROSPECT_EXPORT \
lily_call_entry_func lily_introspect_call_table[] = { \
    NULL, \
    NULL, \
    lily_introspect_TypeEntry_as_string, \
    lily_introspect_TypeEntry_class_name, \
    lily_introspect_TypeEntry_class_id, \
    lily_introspect_TypeEntry_inner_types, \
    NULL, \
    lily_introspect_ParameterEntry_new, \
    NULL, \
    NULL, \
    NULL, \
    NULL, \
    lily_introspect_VarEntry_doc, \
    lily_introspect_VarEntry_line_number, \
    lily_introspect_VarEntry_name, \
    lily_introspect_VarEntry_type, \
    NULL, \
    lily_introspect_PropertyEntry_doc, \
    lily_introspect_PropertyEntry_is_private, \
    lily_introspect_PropertyEntry_is_protected, \
    lily_introspect_PropertyEntry_is_public, \
    lily_introspect_PropertyEntry_name, \
    lily_introspect_PropertyEntry_type, \
    NULL, \
    lily_introspect_FunctionEntry_doc, \
    lily_introspect_FunctionEntry_generics, \
    lily_introspect_FunctionEntry_name, \
    lily_introspect_FunctionEntry_line_number, \
    lily_introspect_FunctionEntry_parameters, \
    lily_introspect_FunctionEntry_result_type, \
    lily_introspect_FunctionEntry_type, \
    NULL, \
    lily_introspect_MethodEntry_function_name, \
    lily_introspect_MethodEntry_generics, \
    lily_introspect_MethodEntry_line_number, \
    lily_introspect_MethodEntry_is_private, \
    lily_introspect_MethodEntry_is_protected, \
    lily_introspect_MethodEntry_is_public, \
    lily_introspect_MethodEntry_is_static, \
    lily_introspect_MethodEntry_parameters, \
    lily_introspect_MethodEntry_result_type, \
    lily_introspect_MethodEntry_type, \
    NULL, \
    lily_introspect_ClassEntry_doc, \
    lily_introspect_ClassEntry_generics, \
    lily_introspect_ClassEntry_id, \
    lily_introspect_ClassEntry_is_foreign, \
    lily_introspect_ClassEntry_is_native, \
    lily_introspect_ClassEntry_methods, \
    lily_introspect_ClassEntry_name, \
    lily_introspect_ClassEntry_parent, \
    lily_introspect_ClassEntry_properties, \
    NULL, \
    lily_introspect_VariantEntry_enum_id, \
    lily_introspect_VariantEntry_enum_name, \
    lily_introspect_VariantEntry_is_empty, \
    lily_introspect_VariantEntry_is_scoped, \
    lily_introspect_VariantEntry_name, \
    lily_introspect_VariantEntry_parameters, \
    lily_introspect_VariantEntry_type, \
    NULL, \
    lily_introspect_EnumEntry_doc, \
    lily_introspect_EnumEntry_generics, \
    lily_introspect_EnumEntry_id, \
    lily_introspect_EnumEntry_is_flat, \
    lily_introspect_EnumEntry_is_scoped, \
    lily_introspect_EnumEntry_methods, \
    lily_introspect_EnumEntry_variants, \
    lily_introspect_EnumEntry_name, \
    NULL, \
    lily_introspect_ModuleEntry_boxed_classes, \
    lily_introspect_ModuleEntry_boxed_enums, \
    lily_introspect_ModuleEntry_boxed_functions, \
    lily_introspect_ModuleEntry_boxed_variants, \
    lily_introspect_ModuleEntry_boxed_vars, \
    lily_introspect_ModuleEntry_classes, \
    lily_introspect_ModuleEntry_dirname, \
    lily_introspect_ModuleEntry_doc, \
    lily_introspect_ModuleEntry_enums, \
    lily_introspect_ModuleEntry_functions, \
    lily_introspect_ModuleEntry_modules_used, \
    lily_introspect_ModuleEntry_name, \
    lily_introspect_ModuleEntry_path, \
    lily_introspect_ModuleEntry_vars, \
    lily_introspect__main_module, \
    lily_introspect__module_list, \
};
#endif

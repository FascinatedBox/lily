#ifndef LILY_API_VALUE_H
# define LILY_API_VALUE_H

# include <stdint.h>

/* This file contains the structures and API needed by foreign functions to
   communicate with Lily. */

typedef struct lily_vm_state_ lily_vm_state;

typedef struct lily_dynamic_val_    lily_dynamic_val;
typedef struct lily_file_val_       lily_file_val;
typedef struct lily_foreign_val_    lily_foreign_val;
typedef struct lily_function_val_   lily_function_val;
typedef struct lily_generic_val_    lily_generic_val;
typedef struct lily_hash_elem_      lily_hash_elem;
typedef struct lily_hash_val_       lily_hash_val;
typedef struct lily_instance_val_   lily_instance_val;
typedef struct lily_list_val_       lily_list_val;
typedef struct lily_string_val_     lily_string_val;
typedef struct lily_value_          lily_value;

/* Use this if you have a structure you want to be a foreign value, and you also
   want to use that 16 bits of space for a field. */
#define LILY_FOREIGN_HEADER_WITH_EXTRA(x) \
uint32_t refcount; \
uint16_t instance_id; \
uint16_t x; \
lily_destroy_func destroy_func;

/* Use this if you have a structure you want to be a foreign value, but you
   don't have a use for the extra space in the field. */
#define LILY_FOREIGN_HEADER_PADDED \
LILY_FOREIGN_HEADER_NOPAD(header_pad)

/* This function is called when Lily wishes to destroy the value that has been
   provided. This action may have been triggered by the gc destroying a
   container holding the foreign value, or the foreign value's refcount falling
   to zero.

   The generic value provided is a raw pointer to the foreign struct that was
   provided to Lily earlier. The destroy function is responsible for destroying
   the content of the value, as well as the value itself.

   Destruction of values in Lily is designed to be atomic. A proper destroy
   function should not have any side-effects. */
typedef void (*lily_destroy_func)(lily_generic_val *);

#define lily_isa_boolean(v)    (v & VAL_IS_BOOLEAN)
#define lily_isa_bytestring(v) (v & VAL_IS_BYTESTRING)
#define lily_isa_double(v)     (v & VAL_IS_DOUBLE)
#define lily_isa_dynamic(v)    (v & VAL_IS_DYNAMIC)
#define lily_isa_enum(v)       (v & VAL_IS_ENUM)
#define lily_isa_file(v)       (v & VAL_IS_FILE)
#define lily_isa_foreign(v)    (v & VAL_IS_FOREIGN)
#define lily_isa_function(v)   (v & VAL_IS_FUNCTION)
#define lily_isa_hash(v)       (v & VAL_IS_HASH)
#define lily_isa_instance(v)   (v & VAL_IS_INSTANCE)
#define lily_isa_integer(v)    (v & VAL_IS_INTEGER)
#define lily_isa_list(v)       (v & VAL_IS_LIST)
#define lily_isa_string(v)     (v & VAL_IS_STRING)
#define lily_isa_tuple(v)      (v & VAL_IS_TUPLE)

#define DECLARE_SETTERS(name, ...) \
void lily_##name##_boolean(__VA_ARGS__, int); \
void lily_##name##_bytestring(__VA_ARGS__, lily_string_val *); \
void lily_##name##_double(__VA_ARGS__, double); \
void lily_##name##_empty_variant(__VA_ARGS__, lily_instance_val *); \
void lily_##name##_file(__VA_ARGS__, lily_file_val *); \
void lily_##name##_foreign(__VA_ARGS__, lily_foreign_val *); \
void lily_##name##_filled_variant(__VA_ARGS__, lily_instance_val *); \
void lily_##name##_hash(__VA_ARGS__, lily_hash_val *); \
void lily_##name##_instance(__VA_ARGS__, lily_instance_val *); \
void lily_##name##_integer(__VA_ARGS__, int64_t); \
void lily_##name##_list(__VA_ARGS__, lily_list_val *); \
void lily_##name##_string(__VA_ARGS__, lily_string_val *); \
void lily_##name##_tuple(__VA_ARGS__, lily_list_val *); \
void lily_##name##_value(__VA_ARGS__, lily_value *); \

#define DECLARE_GETTERS(name, ...) \
int                lily_##name##_boolean(__VA_ARGS__); \
lily_string_val *  lily_##name##_bytestring(__VA_ARGS__); \
double             lily_##name##_double(__VA_ARGS__); \
lily_file_val *    lily_##name##_file(__VA_ARGS__); \
FILE *             lily_##name##_file_raw(__VA_ARGS__); \
lily_function_val *lily_##name##_function(__VA_ARGS__); \
lily_hash_val *    lily_##name##_hash(__VA_ARGS__); \
lily_generic_val * lily_##name##_generic(__VA_ARGS__); \
lily_instance_val *lily_##name##_instance(__VA_ARGS__); \
int64_t            lily_##name##_integer(__VA_ARGS__); \
lily_list_val *    lily_##name##_list(__VA_ARGS__); \
lily_string_val *  lily_##name##_string(__VA_ARGS__); \
char *             lily_##name##_string_raw(__VA_ARGS__); \
lily_value *       lily_##name##_value(__VA_ARGS__);

#define DECLARE_BOTH(name, ...) \
DECLARE_SETTERS(name##_set, __VA_ARGS__) \
DECLARE_GETTERS(name, __VA_ARGS__)

/* Operations for specific kinds of values. */
lily_value *lily_new_empty_value(void); /* Try to not use this. */

/* ByteString operations */
char *lily_bytestring_get_raw(lily_string_val *);
int lily_bytestring_length(lily_string_val *);

/* Dynamic operations */
lily_dynamic_val *lily_new_dynamic_val(void);
DECLARE_BOTH(dynamic, lily_dynamic_val *)

/* File operations */
lily_file_val *lily_new_file_val(FILE *, const char *);

/* Instance operations */
lily_instance_val *lily_new_instance_val_n_of(int, uint16_t);
DECLARE_BOTH(instance, lily_instance_val *, int);

/* Hash operations (still a work-in-progress) */
lily_hash_val *lily_new_hash_val(void);

/* List operations */
lily_list_val *lily_new_list_val_n(int);
DECLARE_BOTH(list, lily_list_val *, int)
int lily_list_num_values(lily_list_val *);

/* String operations */
lily_value *lily_new_string(const char *);
lily_string_val *lily_new_raw_string(const char *);
lily_string_val *lily_new_raw_string_take(char *);
lily_string_val *lily_new_raw_string_sized(const char *, int);
char *lily_string_get_raw(lily_string_val *);
int lily_string_length(lily_string_val *);

/* Enum operations */
lily_instance_val *lily_new_left(void);
lily_instance_val *lily_new_right(void);
lily_instance_val *lily_new_some(void);
lily_instance_val *lily_get_none(lily_vm_state *);

DECLARE_BOTH(variant, lily_instance_val *, int)

/* Stack operations
   Note: Push operations are sourced from vm. */
lily_value *lily_pop_value(lily_vm_state *);
void lily_drop_value(lily_vm_state *);
DECLARE_SETTERS(push, lily_vm_state *)

DECLARE_SETTERS(return, lily_vm_state *)
void lily_return_value_noref(lily_vm_state *, lily_value *);

/* Calling, and argument fetching */
void lily_vm_prepare_call(lily_vm_state *, lily_function_val *);
void lily_vm_exec_prepared_call(lily_vm_state *, int);

int lily_arg_count(lily_vm_state *);
void lily_result_return(lily_vm_state *);

/* Result operations */
DECLARE_GETTERS(arg, lily_vm_state *, int)
DECLARE_GETTERS(result, lily_vm_state *)

/* General operations. Special care should be taken with these. */

void lily_deref(lily_value *);
void lily_assign_value(lily_value *, lily_value *);
void lily_assign_value_noref(lily_value *, lily_value *);
lily_value *lily_copy_value(lily_value *);
int lily_eq_value(struct lily_vm_state_ *, lily_value *, lily_value *);

#endif

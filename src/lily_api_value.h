#ifndef LILY_API_VALUE_H
# define LILY_API_VALUE_H

/* For uint64_t. */
# include <stdint.h>
/* For FILE *. */
# include <stdio.h>

/* This file contains the structures and API needed by foreign functions to
   communicate with Lily. */

# ifndef LILY_STATE
#  define LILY_STATE
typedef struct lily_vm_state_ lily_state;
# endif

typedef struct lily_bytestring_val_ lily_bytestring_val;
typedef struct lily_dynamic_val_    lily_dynamic_val;
typedef struct lily_file_val_       lily_file_val;
typedef struct lily_foreign_val_    lily_foreign_val;
typedef struct lily_function_val_   lily_function_val;
typedef struct lily_generic_val_    lily_generic_val;
typedef struct lily_hash_val_       lily_hash_val;
typedef struct lily_instance_val_   lily_instance_val;
typedef struct lily_list_val_       lily_list_val;
typedef struct lily_string_val_     lily_string_val;
typedef struct lily_tuple_val_      lily_tuple_val;
typedef struct lily_value_          lily_value;
typedef struct lily_variant_val_    lily_variant_val;

/* Put this macro at the top of any struct that you'll send to Lily as a foreign
   value. Don't rely on 'do_not_use', in case it changes in the future. */
#define LILY_FOREIGN_HEADER \
uint32_t refcount; \
uint32_t do_not_use; \
lily_destroy_func destroy_func;

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

/* Build operations
   These operations have been made to help with dynaloading vars. These will
   wrap over a raw value to provide the lily_value * that the loader expects, so
   that the embedder need not worry about raw moves. */
lily_value *lily_new_value_of_byte(uint8_t);
lily_value *lily_new_value_of_bytestring(lily_bytestring_val *);
lily_value *lily_new_value_of_double(double);
lily_value *lily_new_value_of_enum(uint16_t, lily_instance_val *);
lily_value *lily_new_value_of_file(lily_file_val *);
lily_value *lily_new_value_of_hash(lily_hash_val *);
lily_value *lily_new_value_of_instance(uint16_t, lily_instance_val *);
lily_value *lily_new_value_of_integer(int64_t);
lily_value *lily_new_value_of_list(lily_list_val *);
lily_value *lily_new_value_of_string(lily_string_val *);
lily_value *lily_new_value_of_string_raw(const char *);

/* These are the ids of the predefined variants of Option and Some. */
#define LILY_SOME_ID  14
#define LILY_NONE_ID  15

#define LILY_LEFT_ID  17
#define LILY_RIGHT_ID 18

/* Operations for specific kinds of values. */

/* ByteString operations */
lily_bytestring_val *lily_new_bytestring(const char *);
lily_bytestring_val *lily_new_bytestring_take(char *);
lily_bytestring_val *lily_new_bytestring_sized(const char *, int);
char *lily_bytestring_raw(lily_bytestring_val *);
int lily_bytestring_length(lily_bytestring_val *);

/* Dynamic operations */
lily_dynamic_val *lily_new_dynamic(void);
int                  lily_dynamic_boolean   (lily_dynamic_val *);
uint8_t              lily_dynamic_byte      (lily_dynamic_val *);
lily_bytestring_val *lily_dynamic_bytestring(lily_dynamic_val *);
double               lily_dynamic_double    (lily_dynamic_val *);
lily_file_val *      lily_dynamic_file      (lily_dynamic_val *);
FILE *               lily_dynamic_file_raw  (lily_dynamic_val *);
lily_function_val *  lily_dynamic_function  (lily_dynamic_val *);
lily_hash_val *      lily_dynamic_hash      (lily_dynamic_val *);
lily_generic_val *   lily_dynamic_generic   (lily_dynamic_val *);
lily_instance_val *  lily_dynamic_instance  (lily_dynamic_val *);
int64_t              lily_dynamic_integer   (lily_dynamic_val *);
lily_list_val *      lily_dynamic_list      (lily_dynamic_val *);
lily_string_val *    lily_dynamic_string    (lily_dynamic_val *);
char *               lily_dynamic_string_raw(lily_dynamic_val *);
lily_tuple_val *     lily_dynamic_tuple     (lily_dynamic_val *);
lily_value *         lily_dynamic_value     (lily_dynamic_val *);
void lily_dynamic_set_boolean      (lily_dynamic_val *, int);
void lily_dynamic_set_byte         (lily_dynamic_val *, uint8_t);
void lily_dynamic_set_bytestring   (lily_dynamic_val *, lily_bytestring_val *);
void lily_dynamic_set_double       (lily_dynamic_val *, double);
void lily_dynamic_set_empty_variant(lily_dynamic_val *, uint16_t);
void lily_dynamic_set_file         (lily_dynamic_val *, lily_file_val *);
void lily_dynamic_set_foreign      (lily_dynamic_val *, uint16_t, lily_foreign_val *);
void lily_dynamic_set_hash         (lily_dynamic_val *, lily_hash_val *);
void lily_dynamic_set_instance     (lily_dynamic_val *, uint16_t, lily_instance_val *);
void lily_dynamic_set_integer      (lily_dynamic_val *, int64_t);
void lily_dynamic_set_list         (lily_dynamic_val *, lily_list_val *);
void lily_dynamic_set_string       (lily_dynamic_val *, lily_string_val *);
void lily_dynamic_set_tuple        (lily_dynamic_val *, lily_tuple_val *);
void lily_dynamic_set_unit         (lily_dynamic_val *);
void lily_dynamic_set_value        (lily_dynamic_val *, lily_value *);
void lily_dynamic_set_variant      (lily_dynamic_val *, uint16_t, lily_variant_val *);

/* File operations */
lily_file_val *lily_new_file(FILE *, const char *);
FILE *lily_file_raw(lily_file_val *);
void lily_file_ensure_readable(lily_state *, lily_file_val *);
void lily_file_ensure_writeable(lily_state *, lily_file_val *);

/* Function operations */
int lily_function_is_foreign(lily_function_val *);
int lily_function_is_native(lily_function_val *);

/* Instance operations */
lily_instance_val *lily_new_instance(int);
int                  lily_instance_boolean   (lily_instance_val *, int);
uint8_t              lily_instance_byte      (lily_instance_val *, int);
lily_bytestring_val *lily_instance_bytestring(lily_instance_val *, int);
double               lily_instance_double    (lily_instance_val *, int);
lily_file_val *      lily_instance_file      (lily_instance_val *, int);
FILE *               lily_instance_file_raw  (lily_instance_val *, int);
lily_function_val *  lily_instance_function  (lily_instance_val *, int);
lily_hash_val *      lily_instance_hash      (lily_instance_val *, int);
lily_generic_val *   lily_instance_generic   (lily_instance_val *, int);
lily_instance_val *  lily_instance_instance  (lily_instance_val *, int);
int64_t              lily_instance_integer   (lily_instance_val *, int);
lily_list_val *      lily_instance_list      (lily_instance_val *, int);
lily_string_val *    lily_instance_string    (lily_instance_val *, int);
char *               lily_instance_string_raw(lily_instance_val *, int);
lily_tuple_val *     lily_instance_tuple     (lily_instance_val *, int);
lily_value *         lily_instance_value     (lily_instance_val *, int);
void lily_instance_set_boolean      (lily_instance_val *, int, int);
void lily_instance_set_byte         (lily_instance_val *, int, uint8_t);
void lily_instance_set_bytestring   (lily_instance_val *, int, lily_bytestring_val *);
void lily_instance_set_double       (lily_instance_val *, int, double);
void lily_instance_set_empty_variant(lily_instance_val *, int, uint16_t);
void lily_instance_set_file         (lily_instance_val *, int, lily_file_val *);
void lily_instance_set_foreign      (lily_instance_val *, int, uint16_t, lily_foreign_val *);
void lily_instance_set_hash         (lily_instance_val *, int, lily_hash_val *);
void lily_instance_set_instance     (lily_instance_val *, int, uint16_t, lily_instance_val *);
void lily_instance_set_integer      (lily_instance_val *, int, int64_t);
void lily_instance_set_list         (lily_instance_val *, int, lily_list_val *);
void lily_instance_set_string       (lily_instance_val *, int, lily_string_val *);
void lily_instance_set_tuple        (lily_instance_val *, int, lily_tuple_val *);
void lily_instance_set_unit         (lily_instance_val *, int);
void lily_instance_set_value        (lily_instance_val *, int, lily_value *);
void lily_instance_set_variant      (lily_instance_val *, int, uint16_t, lily_variant_val *);

/* Hash operations */
lily_hash_val *lily_new_hash_numtable(void);
lily_hash_val *lily_new_hash_numtable_sized(int);
lily_hash_val *lily_new_hash_strtable(void);
lily_hash_val *lily_new_hash_strtable_sized(int);
lily_hash_val *lily_new_hash_like_sized(lily_hash_val *, int);
lily_value *lily_hash_find_value(lily_hash_val *, lily_value *);
void lily_hash_insert_value(lily_hash_val *, lily_value *, lily_value *);
void lily_hash_insert_str(lily_hash_val *, lily_string_val *, lily_value *);
int lily_hash_delete(lily_hash_val *, lily_value **, lily_value **);

/* List operations */
lily_list_val *lily_new_list(int);
int lily_list_num_values(lily_list_val *);
int                  lily_list_boolean   (lily_list_val *, int);
uint8_t              lily_list_byte      (lily_list_val *, int);
lily_bytestring_val *lily_list_bytestring(lily_list_val *, int);
double               lily_list_double    (lily_list_val *, int);
lily_file_val *      lily_list_file      (lily_list_val *, int);
FILE *               lily_list_file_raw  (lily_list_val *, int);
lily_function_val *  lily_list_function  (lily_list_val *, int);
lily_hash_val *      lily_list_hash      (lily_list_val *, int);
lily_generic_val *   lily_list_generic   (lily_list_val *, int);
lily_instance_val *  lily_list_instance  (lily_list_val *, int);
int64_t              lily_list_integer   (lily_list_val *, int);
lily_list_val *      lily_list_list      (lily_list_val *, int);
lily_string_val *    lily_list_string    (lily_list_val *, int);
char *               lily_list_string_raw(lily_list_val *, int);
lily_tuple_val *     lily_list_tuple     (lily_list_val *, int);
lily_value *         lily_list_value     (lily_list_val *, int);
void lily_list_set_boolean      (lily_list_val *, int, int);
void lily_list_set_byte         (lily_list_val *, int, uint8_t);
void lily_list_set_bytestring   (lily_list_val *, int, lily_bytestring_val *);
void lily_list_set_double       (lily_list_val *, int, double);
void lily_list_set_empty_variant(lily_list_val *, int, uint16_t);
void lily_list_set_file         (lily_list_val *, int, lily_file_val *);
void lily_list_set_foreign      (lily_list_val *, int, uint16_t, lily_foreign_val *);
void lily_list_set_hash         (lily_list_val *, int, lily_hash_val *);
void lily_list_set_instance     (lily_list_val *, int, uint16_t, lily_instance_val *);
void lily_list_set_integer      (lily_list_val *, int, int64_t);
void lily_list_set_list         (lily_list_val *, int, lily_list_val *);
void lily_list_set_string       (lily_list_val *, int, lily_string_val *);
void lily_list_set_tuple        (lily_list_val *, int, lily_tuple_val *);
void lily_list_set_unit         (lily_list_val *, int);
void lily_list_set_value        (lily_list_val *, int, lily_value *);
void lily_list_set_variant      (lily_list_val *, int, uint16_t, lily_variant_val *);

/* String operations */
lily_string_val *lily_new_string(const char *);
lily_string_val *lily_new_string_take(char *);
lily_string_val *lily_new_string_sized(const char *, int);
char *lily_string_raw(lily_string_val *);
int lily_string_length(lily_string_val *);

/* Tuple operations */
lily_tuple_val *lily_new_tuple(int);
int lily_tuple_num_values(lily_tuple_val *);
int                  lily_tuple_boolean   (lily_tuple_val *, int);
uint8_t              lily_tuple_byte      (lily_tuple_val *, int);
lily_bytestring_val *lily_tuple_bytestring(lily_tuple_val *, int);
double               lily_tuple_double    (lily_tuple_val *, int);
lily_file_val *      lily_tuple_file      (lily_tuple_val *, int);
FILE *               lily_tuple_file_raw  (lily_tuple_val *, int);
lily_function_val *  lily_tuple_function  (lily_tuple_val *, int);
lily_hash_val *      lily_tuple_hash      (lily_tuple_val *, int);
lily_generic_val *   lily_tuple_generic   (lily_tuple_val *, int);
lily_instance_val *  lily_tuple_instance  (lily_tuple_val *, int);
int64_t              lily_tuple_integer   (lily_tuple_val *, int);
lily_list_val *      lily_tuple_list      (lily_tuple_val *, int);
lily_string_val *    lily_tuple_string    (lily_tuple_val *, int);
char *               lily_tuple_string_raw(lily_tuple_val *, int);
lily_tuple_val *     lily_tuple_tuple     (lily_tuple_val *, int);
lily_value *         lily_tuple_value     (lily_tuple_val *, int);
void lily_tuple_set_boolean      (lily_tuple_val *, int, int);
void lily_tuple_set_byte         (lily_tuple_val *, int, uint8_t);
void lily_tuple_set_bytestring   (lily_tuple_val *, int, lily_bytestring_val *);
void lily_tuple_set_double       (lily_tuple_val *, int, double);
void lily_tuple_set_empty_variant(lily_tuple_val *, int, uint16_t);
void lily_tuple_set_file         (lily_tuple_val *, int, lily_file_val *);
void lily_tuple_set_foreign      (lily_tuple_val *, int, uint16_t, lily_foreign_val *);
void lily_tuple_set_hash         (lily_tuple_val *, int, lily_hash_val *);
void lily_tuple_set_instance     (lily_tuple_val *, int, uint16_t, lily_instance_val *);
void lily_tuple_set_integer      (lily_tuple_val *, int, int64_t);
void lily_tuple_set_list         (lily_tuple_val *, int, lily_list_val *);
void lily_tuple_set_string       (lily_tuple_val *, int, lily_string_val *);
void lily_tuple_set_tuple        (lily_tuple_val *, int, lily_tuple_val *);
void lily_tuple_set_unit         (lily_tuple_val *, int);
void lily_tuple_set_value        (lily_tuple_val *, int, lily_value *);
void lily_tuple_set_variant      (lily_tuple_val *, int, uint16_t, lily_variant_val *);

/* Enum operations */
lily_variant_val *lily_new_variant(int);
int                  lily_variant_boolean   (lily_variant_val *, int);
uint8_t              lily_variant_byte      (lily_variant_val *, int);
lily_bytestring_val *lily_variant_bytestring(lily_variant_val *, int);
double               lily_variant_double    (lily_variant_val *, int);
lily_file_val *      lily_variant_file      (lily_variant_val *, int);
FILE *               lily_variant_file_raw  (lily_variant_val *, int);
lily_function_val *  lily_variant_function  (lily_variant_val *, int);
lily_hash_val *      lily_variant_hash      (lily_variant_val *, int);
lily_generic_val *   lily_variant_generic   (lily_variant_val *, int);
lily_instance_val *  lily_variant_instance  (lily_variant_val *, int);
int64_t              lily_variant_integer   (lily_variant_val *, int);
lily_list_val *      lily_variant_list      (lily_variant_val *, int);
lily_string_val *    lily_variant_string    (lily_variant_val *, int);
char *               lily_variant_string_raw(lily_variant_val *, int);
lily_tuple_val *     lily_variant_tuple     (lily_variant_val *, int);
lily_value *         lily_variant_value     (lily_variant_val *, int);
void lily_variant_set_boolean      (lily_variant_val *, int, int);
void lily_variant_set_byte         (lily_variant_val *, int, uint8_t);
void lily_variant_set_bytestring   (lily_variant_val *, int, lily_bytestring_val *);
void lily_variant_set_double       (lily_variant_val *, int, double);
void lily_variant_set_empty_variant(lily_variant_val *, int, uint16_t);
void lily_variant_set_file         (lily_variant_val *, int, lily_file_val *);
void lily_variant_set_foreign      (lily_variant_val *, int, uint16_t, lily_foreign_val *);
void lily_variant_set_hash         (lily_variant_val *, int, lily_hash_val *);
void lily_variant_set_instance     (lily_variant_val *, int, uint16_t, lily_instance_val *);
void lily_variant_set_integer      (lily_variant_val *, int, int64_t);
void lily_variant_set_list         (lily_variant_val *, int, lily_list_val *);
void lily_variant_set_string       (lily_variant_val *, int, lily_string_val *);
void lily_variant_set_tuple        (lily_variant_val *, int, lily_tuple_val *);
void lily_variant_set_unit         (lily_variant_val *, int);
void lily_variant_set_value        (lily_variant_val *, int, lily_value *);
void lily_variant_set_variant      (lily_variant_val *, int, uint16_t, lily_variant_val *);

/* Stack operations
   Note: Push operations are sourced from vm. */
lily_value *lily_result_pop(lily_state *);
void lily_result_drop(lily_state *);
void lily_push_boolean      (lily_state *, int);
void lily_push_byte         (lily_state *, uint8_t);
void lily_push_bytestring   (lily_state *, lily_bytestring_val *);
void lily_push_double       (lily_state *, double);
void lily_push_empty_variant(lily_state *, uint16_t);
void lily_push_file         (lily_state *, lily_file_val *);
void lily_push_foreign      (lily_state *, lily_foreign_val *, uint16_t);
void lily_push_hash         (lily_state *, lily_hash_val *);
void lily_push_instance     (lily_state *, lily_instance_val *, uint16_t);
void lily_push_integer      (lily_state *, int64_t);
void lily_push_list         (lily_state *, lily_list_val *);
void lily_push_string       (lily_state *, lily_string_val *);
void lily_push_tuple        (lily_state *, lily_tuple_val *);
void lily_push_unit         (lily_state *);
void lily_push_value        (lily_state *, lily_value *);
void lily_push_variant      (lily_state *, lily_variant_val *, uint16_t);

void lily_return_boolean      (lily_state *, int);
void lily_return_byte         (lily_state *, uint8_t);
void lily_return_bytestring   (lily_state *, lily_bytestring_val *);
void lily_return_double       (lily_state *, double);
void lily_return_empty_variant(lily_state *, uint16_t);
void lily_return_file         (lily_state *, lily_file_val *);
void lily_return_foreign      (lily_state *, uint16_t, lily_foreign_val *);
void lily_return_hash         (lily_state *, lily_hash_val *);
void lily_return_instance     (lily_state *, uint16_t, lily_instance_val *);
void lily_return_integer      (lily_state *, int64_t);
void lily_return_list         (lily_state *, lily_list_val *);
void lily_return_string       (lily_state *, lily_string_val *);
void lily_return_tuple        (lily_state *, lily_tuple_val *);
void lily_return_unit         (lily_state *);
void lily_return_value        (lily_state *, lily_value *);
void lily_return_variant      (lily_state *, uint16_t, lily_variant_val *);
void lily_return_value_noref(lily_state *, lily_value *);

/* Calling, and argument fetching */
void lily_call_prepare(lily_state *, lily_function_val *);
void lily_call_exec_prepared(lily_state *, int);
void lily_call_simple(lily_state *, lily_function_val *, int);

int lily_arg_class_id(lily_state *, int);
int lily_arg_count(lily_state *);
int lily_arg_instance_for_id(lily_state *, int, lily_instance_val **);
int lily_arg_variant_for_id(lily_state *, int, lily_variant_val **);
void lily_result_return(lily_state *);

/* Result operations */
int                  lily_arg_boolean   (lily_state *, int);
uint8_t              lily_arg_byte      (lily_state *, int);
lily_bytestring_val *lily_arg_bytestring(lily_state *, int);
double               lily_arg_double    (lily_state *, int);
lily_file_val *      lily_arg_file      (lily_state *, int);
FILE *               lily_arg_file_raw  (lily_state *, int);
lily_function_val *  lily_arg_function  (lily_state *, int);
lily_hash_val *      lily_arg_hash      (lily_state *, int);
lily_generic_val *   lily_arg_generic   (lily_state *, int);
lily_instance_val *  lily_arg_instance  (lily_state *, int);
int64_t              lily_arg_integer   (lily_state *, int);
lily_list_val *      lily_arg_list      (lily_state *, int);
lily_string_val *    lily_arg_string    (lily_state *, int);
char *               lily_arg_string_raw(lily_state *, int);
lily_tuple_val *     lily_arg_tuple     (lily_state *, int);
lily_value *         lily_arg_value     (lily_state *, int);
int                  lily_result_boolean   (lily_state *);
uint8_t              lily_result_byte      (lily_state *);
lily_bytestring_val *lily_result_bytestring(lily_state *);
double               lily_result_double    (lily_state *);
lily_file_val *      lily_result_file      (lily_state *);
FILE *               lily_result_file_raw  (lily_state *);
lily_function_val *  lily_result_function  (lily_state *);
lily_hash_val *      lily_result_hash      (lily_state *);
lily_generic_val *   lily_result_generic   (lily_state *);
lily_instance_val *  lily_result_instance  (lily_state *);
int64_t              lily_result_integer   (lily_state *);
lily_list_val *      lily_result_list      (lily_state *);
lily_string_val *    lily_result_string    (lily_state *);
char *               lily_result_string_raw(lily_state *);
lily_tuple_val *     lily_result_tuple     (lily_state *);
lily_value *         lily_result_value     (lily_state *);

/* Do an action to a proper value. */
void lily_deref(lily_value *);
void lily_value_assign(lily_value *, lily_value *);
void lily_value_assign_noref(lily_value *, lily_value *);
lily_value *lily_value_copy(lily_value *);
int lily_value_compare(lily_state *, lily_value *, lily_value *);
int lily_value_is_derefable(lily_value *);
uint16_t lily_value_class_id(lily_value *);

/* Raise an exception within the interpreter. */
void lily_DivisionByZeroError(lily_state *, const char *, ...);
void lily_IndexError(lily_state *, const char *, ...);
void lily_IOError(lily_state *, const char *, ...);
void lily_KeyError(lily_state *, const char *, ...);
void lily_RuntimeError(lily_state *, const char *, ...);
void lily_ValueError(lily_state *, const char *, ...);

/* Miscellaneous operations. These are only valid in a function extending the
   interpreter. If they are called outside of the interpreter's parsing loop,
   the interpreter is likely to crash. */

int lily_is_valid_utf8(const char *);

/* Call this if your function is acting as the constructor for a native class.
   If your class is being inherited from, it will provide the class and set the
   properties for you.
   If your class constructor is being called directly, it will create a new
   instance with the id and # of values provided.
   In both cases, the instance is setup for being returned. The function should
   then plug in the properties it needs to. */
void lily_ctor_setup(lily_state *, lily_instance_val **, uint16_t, int);

#endif

#ifndef LILY_API_VALUE_H
# define LILY_API_VALUE_H

/* For uint64_t. */
# include <stdint.h>
/* For FILE *. */
# include <stdio.h>

/* This file contains functions for handling values, as well as creating new
   ones. If you're extending Lily, most of what you need is here. */

# ifndef LILY_STATE
#  define LILY_STATE
typedef struct lily_vm_state_ lily_state;
# endif

/* A lily_value * can contain one of these inside of it. The definitions are
   intentionally absent from this file (see lily_value_structs.h), so that the
   API is forced to treat values as opaque pointers. */
typedef struct lily_bytestring_val_ lily_bytestring_val;
typedef struct lily_container_val_  lily_container_val;
typedef struct lily_file_val_       lily_file_val;
typedef struct lily_foreign_val_    lily_foreign_val;
typedef struct lily_function_val_   lily_function_val;
typedef struct lily_generic_val_    lily_generic_val;
typedef struct lily_hash_val_       lily_hash_val;
typedef struct lily_string_val_     lily_string_val;
typedef struct lily_value_          lily_value;

/* Put this macro at the top of any struct that you'll send to Lily as a foreign
   value. Don't rely on 'do_not_use', in case it changes in the future. */
#define LILY_FOREIGN_HEADER \
uint32_t refcount; \
uint16_t class_id; \
uint16_t do_not_use; \
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

/* These lily_arg_* functions return the Nth argument passed to the current
   function, with 0 being the first argument.
   lily_arg_nth_get gets the container located at the first index, then the
   container's value at the second index. */
int                  lily_arg_boolean   (lily_state *, int);
uint8_t              lily_arg_byte      (lily_state *, int);
lily_bytestring_val *lily_arg_bytestring(lily_state *, int);
lily_container_val * lily_arg_container (lily_state *, int);
double               lily_arg_double    (lily_state *, int);
lily_file_val *      lily_arg_file      (lily_state *, int);
lily_function_val *  lily_arg_function  (lily_state *, int);
lily_generic_val *   lily_arg_generic   (lily_state *, int);
lily_hash_val *      lily_arg_hash      (lily_state *, int);
lily_value *         lily_arg_nth_get   (lily_state *, int, int);
int64_t              lily_arg_integer   (lily_state *, int);
lily_string_val *    lily_arg_string    (lily_state *, int);
char *               lily_arg_string_raw(lily_state *, int);
lily_value *         lily_arg_value     (lily_state *, int);

/* # of args passed to the function.
   Note: Functions taking a variable number of arguments will wrap their extra
   arguments into a list. */
int lily_arg_count(lily_state *);
uint16_t lily_arg_class_id(lily_state *, int);

int lily_arg_is_some(lily_state *, int);
int lily_arg_is_success(lily_state *, int);

/* The interpreter carries a single intermediate register, shared by all
   functions. These functions insert a raw value into it, then return that
   intermediate register. */
lily_value *lily_box_boolean      (lily_state *, int);
lily_value *lily_box_byte         (lily_state *, uint8_t);
lily_value *lily_box_bytestring   (lily_state *, lily_bytestring_val *);
lily_value *lily_box_double       (lily_state *, double);
lily_value *lily_box_empty_variant(lily_state *, uint16_t);
lily_value *lily_box_file         (lily_state *, lily_file_val *);
lily_value *lily_box_foreign      (lily_state *, lily_foreign_val *);
lily_value *lily_box_hash         (lily_state *, lily_hash_val *);
lily_value *lily_box_instance     (lily_state *, lily_container_val *);
lily_value *lily_box_integer      (lily_state *, int64_t);
lily_value *lily_box_list         (lily_state *, lily_container_val *);
lily_value *lily_box_none         (lily_state *);
lily_value *lily_box_string       (lily_state *, lily_string_val *);
lily_value *lily_box_tuple        (lily_state *, lily_container_val *);
lily_value *lily_box_unit         (lily_state *);
lily_value *lily_box_value        (lily_state *, lily_value *);
lily_value *lily_box_variant      (lily_state *, lily_container_val *);

/* Operations for specific kinds of values. */

/* ByteString operations */
lily_bytestring_val *lily_new_bytestring(const char *);
lily_bytestring_val *lily_new_bytestring_sized(const char *, int);
char *lily_bytestring_raw(lily_bytestring_val *);
int lily_bytestring_length(lily_bytestring_val *);

/* Container operations
   Any lily_new_* function that returns lily_container_val can use these. */
uint32_t lily_container_num_values(lily_container_val *);
lily_value *lily_boxed_nth_get(lily_value *, int);
lily_value *lily_nth_get(lily_container_val *, int);
void lily_boxed_nth_set(lily_value *, int, lily_value *);
void lily_nth_set(lily_container_val *, int, lily_value *);

/* Dynamic operations */
lily_container_val *lily_new_dynamic(void);

/* File operations */
lily_file_val *lily_new_file(FILE *, const char *);
FILE *lily_file_for_read(lily_state *, lily_file_val *);
FILE *lily_file_for_write(lily_state *, lily_file_val *);

/* Foreign operations */
lily_foreign_val *lily_new_foreign(lily_state *, uint16_t, lily_destroy_func,
        size_t);

/* Function operations */
int lily_function_is_foreign(lily_function_val *);
int lily_function_is_native(lily_function_val *);

/* Instance operations */
lily_container_val *lily_new_instance(uint16_t, int);
/* Call this if your function acts as a constructor. This pushes a value to the
   top of the stack, and you should return that value. */
void lily_instance_super(lily_state *, lily_container_val **, uint16_t, uint32_t);

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
lily_container_val *lily_new_list(int);

/* String operations */
lily_string_val *lily_new_string(const char *);
lily_string_val *lily_new_string_sized(const char *, int);
char *lily_string_raw(lily_string_val *);
int lily_string_length(lily_string_val *);
/* Return 1 if a given string is valid utf-8, 0 otherwise. */
int lily_is_valid_utf8(const char *);

/* Tuple operations */
lily_container_val *lily_new_tuple(int);

/* Enum operations */
lily_container_val *lily_new_some(void);
lily_container_val *lily_new_failure(void);
lily_container_val *lily_new_success(void);
lily_container_val *lily_new_variant(uint16_t, int);

/* Stack operations (defined within the vm) */
lily_value *lily_take_value(lily_state *);
void lily_pop_value(lily_state *);
void lily_push_boolean      (lily_state *, int);
void lily_push_byte         (lily_state *, uint8_t);
void lily_push_bytestring   (lily_state *, lily_bytestring_val *);
void lily_push_double       (lily_state *, double);
void lily_push_empty_variant(lily_state *, uint16_t);
void lily_push_file         (lily_state *, lily_file_val *);
void lily_push_foreign      (lily_state *, lily_foreign_val *);
void lily_push_hash         (lily_state *, lily_hash_val *);
void lily_push_instance     (lily_state *, lily_container_val *);
void lily_push_integer      (lily_state *, int64_t);
void lily_push_list         (lily_state *, lily_container_val *);
void lily_push_none         (lily_state *);
void lily_push_string       (lily_state *, lily_string_val *);
void lily_push_tuple        (lily_state *, lily_container_val *);
void lily_push_unit         (lily_state *);
void lily_push_value        (lily_state *, lily_value *);
void lily_push_variant      (lily_state *, lily_container_val *);

void lily_return_boolean      (lily_state *, int);
void lily_return_byte         (lily_state *, uint8_t);
void lily_return_bytestring   (lily_state *, lily_bytestring_val *);
void lily_return_double       (lily_state *, double);
void lily_return_empty_variant(lily_state *, uint16_t);
void lily_return_file         (lily_state *, lily_file_val *);
void lily_return_foreign      (lily_state *, lily_foreign_val *);
void lily_return_hash         (lily_state *, lily_hash_val *);
void lily_return_instance     (lily_state *, lily_container_val *);
void lily_return_integer      (lily_state *, int64_t);
void lily_return_list         (lily_state *, lily_container_val *);
void lily_return_none         (lily_state *);
void lily_return_string       (lily_state *, lily_string_val *);
void lily_return_tuple        (lily_state *, lily_container_val *);
void lily_return_unit         (lily_state *);
void lily_return_value        (lily_state *, lily_value *);
void lily_return_variant      (lily_state *, lily_container_val *);
void lily_return_value_noref  (lily_state *, lily_value *);

/* Calling into the vm.
   Functions that call into the vm from inside of it must use lily_call_prepare
   exactly once beforehand. Call prepare pushes one value onto the stack that
   the function will assign into (which is what lily_call_result returns). */
void lily_call(lily_state *, int);
void lily_call_prepare(lily_state *, lily_function_val *);
lily_value *lily_call_result(lily_state *);

int                  lily_value_boolean   (lily_value *);
uint8_t              lily_value_byte      (lily_value *);
lily_bytestring_val *lily_value_bytestring(lily_value *);
lily_container_val * lily_value_container (lily_value *);
double               lily_value_double    (lily_value *);
lily_file_val *      lily_value_file      (lily_value *);
lily_function_val *  lily_value_function  (lily_value *);
lily_generic_val *   lily_value_generic   (lily_value *);
lily_hash_val *      lily_value_hash      (lily_value *);
int64_t              lily_value_integer   (lily_value *);
lily_string_val *    lily_value_string    (lily_value *);
char *               lily_value_string_raw(lily_value *);

/* Do an action to a proper value. */
void lily_deref(lily_value *);
void lily_value_assign(lily_value *, lily_value *);
lily_value *lily_value_copy(lily_value *);
int lily_value_compare(lily_state *, lily_value *, lily_value *);
void lily_value_tag(lily_state *, lily_value *);
int lily_value_is_derefable(lily_value *);

/* Raise an exception within the interpreter. */
void lily_DivisionByZeroError(lily_state *, const char *, ...);
void lily_IndexError(lily_state *, const char *, ...);
void lily_IOError(lily_state *, const char *, ...);
void lily_KeyError(lily_state *, const char *, ...);
void lily_RuntimeError(lily_state *, const char *, ...);
void lily_ValueError(lily_state *, const char *, ...);

/* For use by autogen sections only. */
uint16_t lily_cid_at(lily_state *, int);

#endif

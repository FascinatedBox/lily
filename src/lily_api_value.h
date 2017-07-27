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

/* These functions fetch values from the start of the stack of the current
   frame, with 0 being the first argument. */
int                  lily_arg_boolean   (lily_state *, int);
uint8_t              lily_arg_byte      (lily_state *, int);
lily_bytestring_val *lily_arg_bytestring(lily_state *, int);
lily_container_val * lily_arg_container (lily_state *, int);
double               lily_arg_double    (lily_state *, int);
lily_file_val *      lily_arg_file      (lily_state *, int);
lily_function_val *  lily_arg_function  (lily_state *, int);
lily_generic_val *   lily_arg_generic   (lily_state *, int);
lily_hash_val *      lily_arg_hash      (lily_state *, int);
int64_t              lily_arg_integer   (lily_state *, int);
lily_string_val *    lily_arg_string    (lily_state *, int);
char *               lily_arg_string_raw(lily_state *, int);
lily_value *         lily_arg_value     (lily_state *, int);

/* Push a value into the vm. Some of these return the value so that content can
   be added. */
void                lily_push_boolean      (lily_state *, int);
void                lily_push_byte         (lily_state *, uint8_t);
void                lily_push_bytestring   (lily_state *, const char *, int);
void                lily_push_double       (lily_state *, double);
lily_container_val *lily_push_dynamic      (lily_state *);
void                lily_push_empty_variant(lily_state *, uint16_t);
lily_container_val *lily_push_failure      (lily_state *);
void                lily_push_file         (lily_state *, FILE *, const char *);
lily_foreign_val *  lily_push_foreign      (lily_state *, uint16_t,
                                            lily_destroy_func, size_t);
lily_hash_val *     lily_push_hash         (lily_state *, int);
lily_container_val *lily_push_instance     (lily_state *, uint16_t, uint32_t);
void                lily_push_integer      (lily_state *, int64_t);
lily_container_val *lily_push_list         (lily_state *, int);
void                lily_push_none         (lily_state *);
void                lily_push_string       (lily_state *, const char *);
void                lily_push_string_sized (lily_state *, const char *, int);
lily_container_val *lily_push_some         (lily_state *);
lily_container_val *lily_push_success      (lily_state *);
lily_container_val *lily_push_super        (lily_state *, uint16_t, uint32_t);
lily_container_val *lily_push_tuple        (lily_state *, int);
void                lily_push_unit         (lily_state *);
void                lily_push_value        (lily_state *, lily_value *);
lily_container_val *lily_push_variant      (lily_state *, uint16_t, int);

/* Return some value from the current foreign function. Callers will typically
   finish with one extra value on the stack and should use `lily_return_top`. */
void lily_return_boolean(lily_state *, int);
void lily_return_byte   (lily_state *, uint8_t);
void lily_return_double (lily_state *, double);
void lily_return_integer(lily_state *, int64_t);
void lily_return_none   (lily_state *);
void lily_return_super  (lily_state *);
void lily_return_top    (lily_state *);
void lily_return_unit   (lily_state *);
void lily_return_value  (lily_state *, lily_value *);

/* Return the content of a value. No safety checking is performed. */
int                  lily_as_boolean   (lily_value *);
uint8_t              lily_as_byte      (lily_value *);
lily_bytestring_val *lily_as_bytestring(lily_value *);
lily_container_val * lily_as_container (lily_value *);
double               lily_as_double    (lily_value *);
lily_file_val *      lily_as_file      (lily_value *);
lily_function_val *  lily_as_function  (lily_value *);
lily_generic_val *   lily_as_generic   (lily_value *);
lily_hash_val *      lily_as_hash      (lily_value *);
int64_t              lily_as_integer   (lily_value *);
lily_string_val *    lily_as_string    (lily_value *);
char *               lily_as_string_raw(lily_value *);

/* # of args passed to the function.
   Note: Functions taking a variable number of arguments will wrap their extra
   arguments into a list. */
int lily_arg_count(lily_state *);
uint16_t lily_arg_class_id(lily_state *, int);

int lily_arg_is_some(lily_state *, int);
int lily_arg_is_success(lily_state *, int);

/* Calling into the vm.
   Functions that call into the vm from inside of it must use lily_call_prepare
   exactly once beforehand. Call prepare pushes one value onto the stack that
   the function will assign into (which is what lily_call_result returns). */
void lily_call(lily_state *, int);
void lily_call_prepare(lily_state *, lily_function_val *);
lily_value *lily_call_result(lily_state *);

/* Operations for specific kinds of values. */

/* ByteString operations */
char *lily_bytestring_raw(lily_bytestring_val *);
int lily_bytestring_length(lily_bytestring_val *);

/* Container operations
   These are valid on any container and assume a proper size. */
lily_value *lily_con_get(lily_container_val *, int);
void lily_con_set(lily_container_val *, int, lily_value *);
void lily_con_set_from_stack(lily_state *, lily_container_val *, int);
uint32_t lily_con_size(lily_container_val *);

/* File operations */
FILE *lily_file_for_read(lily_state *, lily_file_val *);
FILE *lily_file_for_write(lily_state *, lily_file_val *);

/* Function operations */
int lily_function_is_foreign(lily_function_val *);
int lily_function_is_native(lily_function_val *);

/* Hash operations */
lily_value *lily_hash_get(lily_state *, lily_hash_val *, lily_value *);
void lily_hash_set(lily_state *, lily_hash_val *, lily_value *, lily_value *);
void lily_hash_set_from_stack(lily_state *, lily_hash_val *);
int lily_hash_take(lily_state *, lily_hash_val *, lily_value *);

/* List operations
   Lists are containers, but these operations should only be used on a List. */
void lily_list_insert(lily_container_val *, int, lily_value *);
void lily_list_reserve(lily_container_val *, int);
void lily_list_take(lily_state *, lily_container_val *, int);
void lily_list_push(lily_container_val *, lily_value *);

/* String operations */
char *lily_string_raw(lily_string_val *);
int lily_string_length(lily_string_val *);

/* Raise an exception within the interpreter. */
void lily_DivisionByZeroError(lily_state *, const char *, ...);
void lily_IndexError(lily_state *, const char *, ...);
void lily_IOError(lily_state *, const char *, ...);
void lily_KeyError(lily_state *, const char *, ...);
void lily_RuntimeError(lily_state *, const char *, ...);
void lily_ValueError(lily_state *, const char *, ...);

/* Stack operations */
void lily_stack_delete_top(lily_state *);
lily_value *lily_stack_top(lily_state *);

/* Do an action to a proper value. */
int lily_value_compare(lily_state *, lily_value *, lily_value *);
void lily_value_tag(lily_state *, lily_value *);

/* Return 1 if a given string is valid utf-8, 0 otherwise. */
int lily_is_valid_utf8(const char *);

/* For use by autogen sections only. */
uint16_t lily_cid_at(lily_state *, int);

#endif

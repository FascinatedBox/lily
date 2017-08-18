#ifndef LILY_H
# define LILY_H

# include <stdarg.h>
# include <stdint.h>
# include <stdio.h>

/* Place this header at the top of any struct that will be sent to the
   interpreter as a foreign value. */
#define LILY_FOREIGN_HEADER \
uint32_t refcount; \
uint16_t class_id; \
uint16_t do_not_use; \
lily_destroy_func destroy_func;

/* The message buffer is a struct that provides a (char *) buffer, plus
   functions to safely manipulate the buffer.
   The state object carries a msgbuf for use by foreign functions that need a
   small buffer. Foreign functions that need a large buffer of their own should
   create and destroy their own msgbuf. */
typedef struct lily_msgbuf_ lily_msgbuf;
typedef struct lily_vm_state_ lily_state;

typedef struct lily_bytestring_val_ lily_bytestring_val;
typedef struct lily_container_val_  lily_container_val;
typedef struct lily_file_val_       lily_file_val;
typedef struct lily_foreign_val_    lily_foreign_val;
typedef struct lily_function_val_   lily_function_val;
typedef struct lily_generic_val_    lily_generic_val;
typedef struct lily_hash_val_       lily_hash_val;
typedef struct lily_string_val_     lily_string_val;
typedef struct lily_value_          lily_value;

/* This is called when template mode needs to render content. `data` is the data
   initially set on the interpreter's config. */
typedef void (*lily_render_func)(const char *content, void *data);

/* The interpreter uses this when it wants to load a module. It's called after
   the interpreter tries modules that are registered, so the importer doesn't
   need to worry about that. The interpreter also remembers the paths that are
   given to `lily_open_*` functions, so this doesn't have to worry about that
   either.
   This function can use the state's msgbuf to build paths for `lily_open_*`
   functions. If unable to load a module, this function should return, at which
   point the interpreter will raise an error. */
typedef void (*lily_import_func)(lily_state *s, const char *root_dir,
                                 const char *current_dir, const char *name);

/* Sometimes a function needs to do cleanup if `lily_call` raises an exception.
   In such a case, the function can register an error callback for later.

   If an exception occurs, the error callback is called with the same stack as
   the function that registered it.

   If an exception does not occur, the function that pushed the error callback
   is responsible for popping it before returning. */
typedef void (*lily_error_callback_func)(lily_state *s);

/* This is called when a foreign value is to be destroyed. This function should
   only delete the contents of the value provided, and not execute code or
   modify any other values. */
typedef void (*lily_destroy_func)(lily_generic_val *);

/* This struct contains configuration information used to start the interpreter.
   Default values can be found in `src/lily_parser.c`'s `lily_config_init`.
   Note: This configuration struct should not be modified while parsing. */
typedef struct lily_config_ {
    int argc;
    char **argv;
    /* The interpreter's gc counts objects, not memory size.
       How many objects should be allowed before a gc pass? */
    int gc_start;
    /* If the gc fails to reclaim any objects, the number allowed is multiplied
       by this value. */
    int gc_multiplier;
    lily_render_func render_func;
    lily_import_func import_func;
    char sipkey[16];
    /* Extra data that the interpreter will hold onto, but not touch.  */
    void *data;
} lily_config;

/* Initialize the config object to default values. */
void lily_config_init(lily_config *config);

/* Fetch the config struct. Valid at any time. */
lily_config *lily_config_get(lily_state *s);

/* Create a new interpreter using the configuration given. Using the same config
   for multiple interpreters is okay, so long as the caller makes sure the
   config struct doesn't go out of scope. */
lily_state *lily_new_state(lily_config *config);

/* Destroy a state. */
void lily_free_state(lily_state *s);


/* Render/parse functions return 1 on success, 0 on failure.
   On failure, the next parse/render pass will first begin by rewinding away
   broken state from the last pass. */


/* Attempt to open and parse 'filename'. */
int lily_parse_file(lily_state *s, const char *filename);

/* Attempt to parse 'data' using 'context' as the filename in case of error. */
int lily_parse_string(lily_state *s, const char *context, const char *data);

/* Attempt to parse 'data' using 'context' as the filename in case of error.
   The resulting output is saved into an internal msgbuf, and valid until the
   next parse step. */
int lily_parse_expr(lily_state *s, const char *context, char *data,
                    const char **output);

/* Attempt to open and parse 'filename'. */
int lily_render_file(lily_state *s, const char *filename);

/* Attempt to render 'data' using 'context' as the filename in case of error. */
int lily_render_string(lily_state *s, const char *context, const char *data);

/* Fetch the last error message. The result is held within an internal msgbuf,
   and valid until the next parse/render step. */
const char *lily_error_message(lily_state *s);

/* The above, but without traceback. */
const char *lily_error_message_no_trace(lily_state *s);


/* Load functions for the import func */


/* Tell the interpreter to attempt to a file at 'path' for reading. */
int lily_open_file(lily_state *s, const char *path);

/* Attempt to open a library at 'path'. The library must include a dynaload
   table and a loader. */
int lily_open_library(lily_state *s, const char *path);

/* Load library data for 'path'. Since the loaded data is already valid, this
   only fails if another open has already succeded. */
int lily_open_library_data(lily_state *s, const char *path,
                           const char **dynaload_table, void *loader);

/* Load the string 'content' for 'path'. This only fails if another open
   function has already succeeded. */
int lily_open_string(lily_state *s, const char *path, const char *content);


/* The ids of builtin classes can be used in class id tests, just like the ids
   that are autogenerated.
   Note: Enum ids (Option and Result) exist for the frontend. Foreign functions
   should always be using variant ids. */


#define LILY_ID_INTEGER       1
#define LILY_ID_DOUBLE        2
#define LILY_ID_STRING        3
#define LILY_ID_BYTE          4
#define LILY_ID_BYTESTRING    5
#define LILY_ID_BOOLEAN       6
#define LILY_ID_FUNCTION      7
#define LILY_ID_DYNAMIC       8
#define LILY_ID_LIST          9
#define LILY_ID_HASH         10
#define LILY_ID_TUPLE        11
#define LILY_ID_FILE         12
#define LILY_ID_OPTION       13
#define LILY_ID_SOME         14
#define LILY_ID_NONE         15
#define LILY_ID_RESULT       16
#define LILY_ID_FAILURE      17
#define LILY_ID_SUCCESS      18
#define LILY_ID_EXCEPTION    19
#define LILY_ID_IOERROR      20
#define LILY_ID_KEYERROR     21
#define LILY_ID_RUNTIMEERROR 22
#define LILY_ID_VALUEERROR   23
#define LILY_ID_INDEXERROR   24
#define LILY_ID_DBZERROR     25 /* > 9000 */
#define LILY_ID_UNIT         26
#define START_CLASS_ID       27


/* Operations for values mentioned above. */


/* Get the raw buffer behind a `ByteString`. */
char *lily_bytestring_raw(lily_bytestring_val *byte_val);
/* Get the size (in bytes) of a `ByteString`. */
int lily_bytestring_length(lily_bytestring_val *byte_val);

/* Container operations. These are valid for variants, instances, `List`, and
   `Tuple`. These functions do not support negative indexes or perform safety
   checks. Indexes are 0-based. */

lily_value *lily_con_get(lily_container_val *con, int index);
void lily_con_set(lily_container_val *con, int index, lily_value *value);
/* Like above, but 'value' comes from popping a value from the stack. */
void lily_con_set_from_stack(lily_state *s, lily_container_val *con, int index);
uint32_t lily_con_size(lily_container_val *con);

/* Raise IOError if 'file' is not open for reading, or return the raw file. */
FILE *lily_file_for_read(lily_state *s, lily_file_val *file);
/* Raise IOError if 'file' is not open for writing, or return the raw file. */
FILE *lily_file_for_write(lily_state *s, lily_file_val *file);

int lily_function_is_foreign(lily_function_val *func);
int lily_function_is_native(lily_function_val *func);

/* Hash operations. The key must be a `String` or an `Integer`. */

/* Attempt to find the key within the hash. Returns a valid value, or NULL. */
lily_value *lily_hash_get(lily_state *s, lily_hash_val *hash, lily_value *key);
/* Perform an assignment into the hash provided.
   If 'key' already exists, the old key and record deref'd and replaced.
   If 'key' does not exist, then the new values are copied and inserted. */
void lily_hash_set(lily_state *s, lily_hash_val *hash, lily_value *key, lily_value *record);
/* Pops two elements from the top of the stack and calls 'lily_hash_set' with
   them. The top-most element is the record, and the next-to-top is the key. */
void lily_hash_set_from_stack(lily_state *s, lily_hash_val *hash);
/* Attempt to take 'key' out of the hash given.
   On success, 1 is returned and the value taken is popped from the stack.
   On failure, 0 is returned, and no value is put onto the stack. */
int lily_hash_take(lily_state *s, lily_hash_val *hash, lily_value *key);

/* Operations for `List` must only be sent containers for `List` values. Only
   `List`-based containers ever need to grow. These functions do not do any
   error checking, and do not support negative indexes. */

/* Insert 'value' at 'index' within the container. Elements at the given index
   and above are moved to the right. */
void lily_list_insert(lily_container_val *con, int index, lily_value *value);
/* Reserve a total of 'size' elements for future use. The reserved slots are not
   counted in the size of the `List`, and also do not need to be filled. */
void lily_list_reserve(lily_container_val *con, int size);
/* Take the element at 'index' out of the `List`. Elements to the right are
   shifted left. */
void lily_list_take(lily_state *s, lily_container_val *con, int index);
/* Add an element to the end of the `List`. */
void lily_list_push(lily_container_val *con, lily_value *value);

/* Return the raw buffer behind the string value. */
char *lily_string_raw(lily_string_val *string_val);
/* Return the size (in bytes) of the `String`. */
int lily_string_length(lily_string_val *string_val);


/* These grab a value from the bottom of the stack. These are typically used to
   get the arguments passed to a function, with 0 being the first argument. */


int                  lily_arg_boolean   (lily_state *s, int index);
uint8_t              lily_arg_byte      (lily_state *s, int index);
lily_bytestring_val *lily_arg_bytestring(lily_state *s, int index);
lily_container_val * lily_arg_container (lily_state *s, int index);
double               lily_arg_double    (lily_state *s, int index);
lily_file_val *      lily_arg_file      (lily_state *s, int index);
lily_function_val *  lily_arg_function  (lily_state *s, int index);
lily_generic_val *   lily_arg_generic   (lily_state *s, int index);
lily_hash_val *      lily_arg_hash      (lily_state *s, int index);
int64_t              lily_arg_integer   (lily_state *s, int index);
lily_string_val *    lily_arg_string    (lily_state *s, int index);
char *               lily_arg_string_raw(lily_state *s, int index);
lily_value *         lily_arg_value     (lily_state *s, int index);

/* How many arguments the foreign function was passed.
   This can be used for implementing optional arguments, as foreign functions
   are responsible for implementing their optional arguments.
   Note: Vararg functions place any extra arguments into a `List`, instead of
         having extra args on the stack. */
int lily_arg_count(lily_state *s);

/* Check if the arg at index has the given class id. This is a strict class id
   check, so subclasses will return false. */
int lily_arg_isa(lily_state *s, int index, uint16_t class_id);

#define lily_arg_is_failure(s, index) lily_arg_isa(s, index, LILY_ID_FAILURE)
#define lily_arg_is_none(s, index) lily_arg_isa(s, index, LILY_ID_NONE)
#define lily_arg_is_some(s, index) lily_arg_isa(s, index, LILY_ID_SOME)
#define lily_arg_is_success(s, index) lily_arg_isa(s, index, LILY_ID_SUCCESS)


/* These push a value onto the stack, returning the value if it is to be filled.
   Callers must not let control return to the interpreter with incomplete
   containers. For example, if a `Tuple` of size 3 is requested, then all 3
   slots must be filled before the interpreter uses it. This is because the
   interpreter does not allow for nil or missing values. */


void                lily_push_boolean      (lily_state *s, int value);
void                lily_push_byte         (lily_state *s, uint8_t value);
void                lily_push_bytestring   (lily_state *s, const char *source, int size);
void                lily_push_double       (lily_state *s, double value);
lily_container_val *lily_push_dynamic      (lily_state *s);
void                lily_push_empty_variant(lily_state *s, uint16_t class_id);
/* Note: 'f' must be an open file. */
void                lily_push_file         (lily_state *s, FILE *f,
                                            const char *mode);
lily_foreign_val *  lily_push_foreign      (lily_state *s, uint16_t class_id,
                                            lily_destroy_func, size_t);
lily_hash_val *     lily_push_hash         (lily_state *s, int size);
lily_container_val *lily_push_instance     (lily_state *s, uint16_t class_id,
                                            uint32_t size);
void                lily_push_integer      (lily_state *s, int64_t value);
lily_container_val *lily_push_list         (lily_state *s, uint32_t size);
void                lily_push_string       (lily_state *s, const char *source);
void                lily_push_string_sized (lily_state *s, const char *source, int size);
/* For use by foreign functions that are acting as a constructor.

   If the function is creating a new value, then this pushes a new value onto
   the stack.

   Otherwise, this pushes the superclass value onto the stack and marks the
   value as being fully constructed.

   In both cases, the caller is responsible for placing a gc tag onto the value
   if such is necessary. */
lily_container_val *lily_push_super        (lily_state *s, uint16_t class_id,
                                            uint32_t size);
lily_container_val *lily_push_tuple        (lily_state *s, uint32_t size);
void                lily_push_unit         (lily_state *s);
void                lily_push_value        (lily_state *s, lily_value *value);
lily_container_val *lily_push_variant      (lily_state *s, uint16_t class_id,
                                            uint32_t size);

# define lily_push_failure(s) lily_push_variant(s, LILY_ID_FAILURE, 1)
# define lily_push_none(s) lily_push_empty_variant(s, LILY_ID_NONE)
# define lily_push_some(s) lily_push_variant(s, LILY_ID_SOME, 1)
# define lily_push_success(s) lily_push_variant(s, LILY_ID_SUCCESS, 1)


/* Foreign functions must always end by returning some value back into the
   interpreter, even if it's a `Unit` value.
   Most foreign functions will use `lily_return_top` to return the most recent
   value in the stack. */


void lily_return_boolean(lily_state *s, int value);
void lily_return_byte   (lily_state *s, uint8_t value);
void lily_return_double (lily_state *s, double value);
void lily_return_integer(lily_state *s, int64_t value);
void lily_return_none   (lily_state *s);
void lily_return_super  (lily_state *s);
void lily_return_top    (lily_state *s);
void lily_return_unit   (lily_state *s);
void lily_return_value  (lily_state *s, lily_value *value);


/* Return the content of a value. No safety checking is performed. */


int                  lily_as_boolean   (lily_value *value);
uint8_t              lily_as_byte      (lily_value *value);
lily_bytestring_val *lily_as_bytestring(lily_value *value);
lily_container_val * lily_as_container (lily_value *value);
double               lily_as_double    (lily_value *value);
lily_file_val *      lily_as_file      (lily_value *value);
lily_function_val *  lily_as_function  (lily_value *value);
lily_generic_val *   lily_as_generic   (lily_value *value);
lily_hash_val *      lily_as_hash      (lily_value *value);
int64_t              lily_as_integer   (lily_value *value);
lily_string_val *    lily_as_string    (lily_value *value);
char *               lily_as_string_raw(lily_value *value);


/* Calling back into the vm */


/* Perform a call into the interpreter using 'count' values from the top of the
   stack as arguments. The function called is the last func sent to
   `lily_call_prepare`. */
void lily_call(lily_state *s, int count);
/* This reserves a slot on the stack and also does prep work for 'func'. The
   reserved slot is where the return value of `lily_call` will be placed unless
   an exception is raised.
   Once a call has been prepared, `lily_call` can be used any number of times
   with any number of arguments. */
void lily_call_prepare(lily_state *s, lily_function_val *func);
/* This returns the slot that `lily_call_prepare` reserved. The contents of the
   value will change, but the value itself is fixed. It is therefore unnecessary
   to call this after `lily_call`. */
lily_value *lily_call_result(lily_state *s);


/* Raise an exception in the interpreter using 'format' and extra arguments to
   construct a message. See 'lily_mb_add_fmt' for formatting options. */


void lily_DivisionByZeroError(lily_state *s, const char *format, ...);
void lily_IndexError(lily_state *s, const char *format, ...);
void lily_IOError(lily_state *s, const char *format, ...);
void lily_KeyError(lily_state *s, const char *format, ...);
void lily_RuntimeError(lily_state *s, const char *format, ...);
void lily_ValueError(lily_state *s, const char *format, ...);


/* Error callbacks for function cleanup in the event of an exception. */


void lily_error_callback_push(lily_state *s, lily_error_callback_func callback_fn);
void lily_error_callback_pop(lily_state *s);


/* Operations for the top of the stack */


/* Pop the value on the top of the stack and destroy it. */
void lily_stack_drop_top(lily_state *s);
/* Return the value on the top of the stack. */
lily_value *lily_stack_get_top(lily_state *s);


/* Message buffer operations
   Operations that return 'char *' may be invalidated by the next msgbuf use or
   a msgbuf flush. */


lily_msgbuf *lily_new_msgbuf(uint32_t size);
void lily_free_msgbuf(lily_msgbuf *msgbuf);

void lily_mb_add(lily_msgbuf *msgbuf, const char *source);
void lily_mb_add_char(lily_msgbuf *msgbuf, char ch);
void lily_mb_add_fmt(lily_msgbuf *msgbuf, const char *format, ...);
void lily_mb_add_fmt_va(lily_msgbuf *msgbuf, const char *format, va_list);
void lily_mb_add_slice(lily_msgbuf *msgbuf, const char *source, int start, int end);
void lily_mb_add_value(lily_msgbuf *msgbuf, lily_state *s, lily_value *value);

lily_msgbuf *lily_mb_flush(lily_msgbuf *msgbuf);
const char *lily_mb_raw(lily_msgbuf *msgbuf);
int lily_mb_pos(lily_msgbuf *msgbuf);

/* Escape html characters found within 'input'. */
const char *lily_mb_html_escape(lily_msgbuf *msgbuf, const char *input);
/* Equivalent to flushing the msgbuf, then add_fmt with the arguments given. */
const char *lily_mb_sprintf(lily_msgbuf *msgbuf, const char *format, ...);
/* This flushes and returns the state's msgbuf. */
lily_msgbuf *lily_msgbuf_get(lily_state *);


/* Miscellaneous operations */


/* Search for a function called 'name'. The search is done as if 'name' was
   written in the first module loaded.
   On success, returns a valid function for use with `lily_call`.
   On failure, NULL is returned. */
lily_function_val *lily_find_function(lily_state *s, const char *name);
/* This makes a module called 'name' available to the interpreter. The module is
   available anywhere, but still must be explicitly loaded through `import`.
   This should be called before any parse/render function. */
void lily_module_register(lily_state *s, const char *name,
                          const char **dynaload_table, void *loader);
/* Return 1 if a given string is valid utf-8, 0 otherwise.
   'source' must be non-NULL and \0 terminated. */
int lily_is_valid_utf8(const char *source);
/* Place a gc tag onto 'value'. If there are too many objects with gc tags
   already, the gc will be invoked beforehand. */
void lily_value_tag(lily_state *s, lily_value *value);
/* Autogenerated ID_ macros use this to get class ids. */
uint16_t lily_cid_at(lily_state *s, int index);

#endif

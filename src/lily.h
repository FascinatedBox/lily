#ifndef LILY_H
# define LILY_H

# include <stdarg.h>
# include <stdint.h>
# include <stdio.h>

/* API documentation is processed with NaturalDocs. */

// Title: API Reference - Lily

//////////////////////////
// Section: Basic typedefs
//////////////////////////

// Typedef: lily_bytestring_val
// Holds a ByteString value.
typedef struct lily_bytestring_val_ lily_bytestring_val;

// Typedef: lily_container_val
// Holds a variant, user-defined class, List, or Tuple.
typedef struct lily_container_val_  lily_container_val;

// Typedef: lily_file_val
// Holds a File value.
typedef struct lily_file_val_       lily_file_val;

// Typedef: lily_foreign_val
// Holds a foreign class.
typedef struct lily_foreign_val_    lily_foreign_val;

// Typedef: lily_function_val
// Holds a Function (could be native or foreign).
typedef struct lily_function_val_   lily_function_val;

// Typedef: lily_generic_val
// Holds some kind of pointer value (usually, to be cast to a foreign class).
typedef struct lily_generic_val_    lily_generic_val;

// Typedef: lily_hash_val
// Holds a Hash value.
typedef struct lily_hash_val_       lily_hash_val;

// Typedef: lily_string_val
// Holds a String value.
typedef struct lily_string_val_     lily_string_val;

// Typedef: lily_value
// A complete value with class and flag information.
typedef struct lily_value_          lily_value;

typedef struct lily_vm_state_       lily_state;

typedef void (*lily_destroy_func)(lily_generic_val *);

typedef void (*lily_import_func)(lily_state *s, const char *target);

typedef void (*lily_render_func)(const char *content, void *data);

typedef void (*lily_call_entry_func)(lily_state *);

/////////////////////////
// Section: Configuration
/////////////////////////
// The configuration struct and related functions.

// Struct: lily_config
// All interpreter config info goes here.
//
// Every interpreter requires a configuration struct to initialize it. The
// config struct is a struct with members publically exposed. This allows
// embedders to grab fields they want instead of going through the hoops of
// simple get/set functions for an opaque struct.
//
// Interpreters do not free the config when they are done, nor do they free any
// fields in the config. It is the caller's responsibility to clean up the
// config when the interpreter is done.
//
// Because of that, callers are permitted to use the same configuration to
// initialize multiple interpreters.
//
// Callers must not modify configuration members once an interpreter has been
// created that uses them.
//
// Fields:
//     argc          - (Default: 0)
//                     Number of values in argv.
//
//     argv          - (Default: NULL)
//                     The argument list (later used by Lily's sys.argv).
//
//     data          - (Default: stdin)
//                     This will later be sent as the data part of the
//                     import_func hook.
//
//     gc_multiplier - (Default: 4)
//                     If a gc sweep fails, how much should the count of allowed
//                     values be multiplied by?
//
//     gc_start      - (Default: 100)
//                     How many values should be allowed to exist at once before
//                     a gc pass.
//
//     import_func   - (Default: lily_default_import_func)
//                     What function should be called to handle imports?
//
//     render_func   - (Default: fputs)
//                     What function should be called if there is template
//                     content to be rendered?
//
//     sipkey        - (Default: [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]
//                     The sipkey is an array of 16 char values that helps to
//                     prevent collisions in Lily's Hash class.
//
//     extra_info    - (Default: 0)
//                     By default, the interpreter does not save parameter names
//                     or docblocks for introspection. If this is 1 when
//                     starting a parse, the information is saved and available.
typedef struct lily_config_ {
    int argc;
    char **argv;
    int gc_start;
    int gc_multiplier;
    lily_render_func render_func;
    lily_import_func import_func;
    char sipkey[16];
    void *data;
    int extra_info;
} lily_config;

// Function: lily_config_init
// Initialize a config to default values.
//
// This function assumes that the caller has a config struct on the stack. The
// caller can pass the address of that struct to this function, and the default
// values will be set.
void lily_config_init(lily_config *config);

// Function: lily_config_get
// Fetch an interpreter's config struct.
lily_config *lily_config_get(lily_state *s);

////////////////////////////
// Section: State Management
////////////////////////////

// Function: lily_exit_code
// Return a suitable exit code based on the status of the interpreter.
//
// If `sys.exit` has been called, this will return the exit code given to it.
// Otherwise, this returns EXIT_SUCCESS if the last content handling function
// succeeded, or EXIT_FAILURE if it did not.
int lily_exit_code(lily_state *s);

// Function: lily_new_state
// Create a new interpreter.
lily_state *lily_new_state(lily_config *config);

// Function: lily_free_state
// Destroy an interpreter and any values it holds.
void lily_free_state(lily_state *s);

/////////////////////////////
// Section: Parsing/Rendering
/////////////////////////////
// Parse or render some input.
//
// These functions must **not** be called while the interpreter is either
// executing or within an import hook.
//
// The lily_load_* functions prepare content for the interpreter. The other
// functions detailed in this sections consume the content.
//
// Attempting to parse or render an interpreter without content ready will do
// nothing. Similarly, attempting to load when content has already been loaded
// will do nothing.
//
// If any functions detailed here fail, the embedder can use the `lily_error_*`
// set of functions to determine what went wrong (except for the two cases that
// are ignored above).
//
// If necessary, the interpreter's rewind is invoked when there is a successful
// `lily_load_*` call after the failed parse or render.

// Function: lily_load_file
// Prepare a file for the interpreter.
//
// The path used for loading is exactly the one that is provided by this
// function. This function will raise an error if the path does not end in
// '.lily'.
//
// Parameters:
//     s    - The interpreter.
//     path - A path to a file to be loaded. Must end in '.lily'.
//
// Returns 1 on success, 0 on failure.
int lily_load_file(lily_state *s, const char *path);

// Function: lily_load_string
// Prepare a string for the interpreter.
//
// The interpreter assumes that the string data passed will not be modified
// during the interpreter's upcoming parse/render cycle.
//
// The context passed does not require '.lily' as a suffix. The context provided
// is exactly the one that is used for the filename.
//
// For pseudo-files like reading from stdin, the embedder may want to use a
// bracketed name (ex: `[cli]` or `[repl]`).
//
// Parameters:
//     s       - The interpreter.
//     context - This is the filename to use in case there is an error.
//     data    - The input for the interpreter.
//
// Returns 1 on success, 0 on failure.
int lily_load_string(lily_state *s, const char *context, const char *str);

// Function: lily_parse_content
// Parse content prepared for the interpreter.
//
// This parses the content provided in code-only mode. The content is consumed
// regardless of this function's result.
//
// If the parse is successful, the code is executed as well.
//
// Returns 1 on success, 0 on failure.
int lily_parse_content(lily_state *s);

// Function: lily_parse_manifest
// Parse content as a manifest file.
//
// This parses the content provided as a manifest file. A manifest file
// describes the api that a foreign library will export. The content is consumed
// regardless of this function's result.
//
// Manifest files are blocked from expressions, preventing them from running any
// code. After calling to parse a manifest, an embedder can use the
// introspection api to traverse the symtab.
//
// Returns 1 on success, 0 on failure.
int lily_parse_manifest(lily_state *s);

// Function: lily_render_content
// Parse content prepared for the interpreter.
//
// This parses the content provided in template mode. The content is consumed
// regardless of this function's result.
//
// Prior to parsing, the content is checked to make sure it begins with the
// `<?lily` header at the very top of the file. If it does not, no parsing is
// performed.
//
// Returns 1 on success, 0 on failure.
int lily_render_content(lily_state *s);

// Function: lily_parse_expr
// Parse an expression prepared for the interpreter.
//
// This parses the content provided as an expression in code mode. The content
// is consumed regardless of this function's result.
//
// If the expression is successfully processed, then 'output' is set to a buffer
// holding the type and the value of the type.
//
// The buffer that 'output' holds on success points to an internal msgbuf. It is
// valid until the next parse or render function is called.
//
// Parameters:
//     s      - The interpreter.
//     output - If 'data' is a valid expression, then the result is set to the
//              address of 'output'.
//
// Returns 1 on success, 0 on failure.
int lily_parse_expr(lily_state *s, const char **output);

// Function: lily_validate_content
// Parse (but don't execute) content prepared for the interpreter.
//
// This parses the content provided in code-only mode. The content is consumed
// regardless of this function's result. It is the same as lily_parse_content,
// except that no code is executed.
//
// This function is provided so that inspection can be done on an interpreter
// without running code, and also to allow for a syntax-only pass of the
// interpreter. Callers should not execute code on an interpreter after this
// function has been called.
//
// Returns 1 on success, 0 on failure.
int lily_validate_content(lily_state *s);

/////////////////////////
// Section: Error Capture
/////////////////////////
// Capture error information after a failed parse/render.
//
// These functions capture the error that is raised when a parse or render
// function fails. Error information from a failed parse or render exists until
// the next of either is called.
//
// Capture functions must not be called multiple times.

// Function: lily_error_message
// Fetch the message and traceback of the last failed parse/render.
//
// The output of this function is exactly what the 'lily' executable returns
// when there is an error (the kind of error, error message, then traceback).
//
// The result of this is a pointer to a msgbuf inside of the interpeter. The
// pointer is valid until the next parse/render step.
const char *lily_error_message(lily_state *s);

// Function: lily_error_message_no_trace
// Fetch only the message of the last failed parse/render.
//
// This returns the kind of error raised and the error message. Traceback is
// omitted. This function is used by the repl.
//
// The result of this is a pointer to a msgbuf inside of the interpeter. The
// pointer is valid until the next parse/render step.
const char *lily_error_message_no_trace(lily_state *s);

//////////////////////////////
// Section: Configuring render
//////////////////////////////
// Render hook and related functions.

// Typedef: lily_render_func
// Invoked when template mode needs to render content.
//
// Parameters:
//     content - The text to be rendered.
//     data    - This is the 'data' member of the interpreter's config.

//////////////////////////////
// Section: Configuring import
//////////////////////////////
// The import hook and functions it uses.
//
// Note: The loading functions mentioned here will return 0 on s

// Typedef: lily_import_func
// Invoked to find a module after registered modules have been tried.
//
// This hook is provided the interpreter state and a target. The target is what
// was sent to the 'import' keyword. If the current platform doesn't use forward
// slashes, then the slashes are replaced with what the platform uses. On
// Windows for example, they're replaced with backslashes. No suffix is given.
//
// Before any loading, the hook must use one of the lily_import_use_* functions.
// Those functions tell the interpreter of the subsequent file or string or
// other content is at the root of a package or not.
//
// The `lily_import_*` set of functions that this uses share some common
// behavior. They will return 1 if another import has succeeded or an
// already-imported module satisfying the target given has been found. If a
// `lily_import_*` function fails, it records the path that was attempted.
//
// If the hook cannot load a module for the interpreter, it should return
// normally. The interpreter will check if a module was loaded and write an
// error message detailing the paths that were tried.
//
// Parameters:
//     s      - The interpreter state.
//     target - The name passed to the import keyword.

// Typedef: lily_call_entry_func
// An dynaload entry in the call_table corresponding to the info_table.
//
// This is a typedef that represents an entry in the call_table half of
// dynaload. One of these entries could be a method implementation or a var
// loader. Both halves are automatically generated, and most users won't need to
// interact with them except to register embedded Lily code.

// Function: lily_default_import_func
// This is the default import hook.
//
// The default import hook is used by the 'lily' embedder. This is provided for
// the sake of completeness.
void lily_default_import_func(lily_state *s, const char *target);

// Function: lily_import_file
// Import a file for the interpreter.
//
// In most cases, a caller will want to use this with the target that was
// provided by the hook. The '.lily' suffix is automatically added.
//
// If no `lily_import_use_*` function has been called beforehand, this does
// nothing and fails.
//
// Parameters:
//     s      - The interpreter state.
//     target - The target to attempt loading.
//
// Returns 1 on success, 0 on failure.
int lily_import_file(lily_state *s, const char *target);

// Function: lily_import_library
// Load a library from a given path.
//
// In most cases, a caller will want to use this with the target that was
// provided by the hook. The appropriate library suffix is automatically added.
//
// If no `lily_import_use_*` function has been called beforehand, this does
// nothing and fails.
//
// Parameters:
//     s      - The interpreter state.
//     target - The target to attempt loading.
//
// Returns 1 on success, 0 on failure.
int lily_import_library(lily_state *s, const char *target);

// Function: lily_import_library_data
// Load a preloaded library.
//
// This function uses the target exactly as-is. As a result, any setup done by
// the `lily_import_use_*` functions is ignored.
//
// Parameters:
//     s          - The interpreter state.
//     target     - The path to register for the library.
//     info_table - An info table for the library.
//     call_table - The call table companion to the info table.
//
// Returns 1 on success, 0 on failure.
int lily_import_library_data(lily_state *s, const char *target,
                             const char **info_table,
                             lily_call_entry_func *call_table);

// Function: lily_import_string
// Load a string (context path, then content) as a library.
//
// The interpreter makes internal copies of 'target' and 'content'. It is thus
// safe to pass strings in without concern for their lifetime.
//
// This function does not add a suffix to the path given.
//
// If no `lily_import_use_*` function has been called beforehand, this does
// nothing and fails.
//
// Returns 1 on success, 0 on failure.
int lily_import_string(lily_state *s, const char *target, const char *content);

// Function: lily_import_use_local_dir
// Use a local directory for upcoming imports.
//
// This instructs the interpreter that subsequent `lily_import_*` calls should
// be done within the current package. Callers that want the default loading
// scheme that the 'lily' executable uses should pass this an empty string.
//
// If this function is passed forward slashes, they are replaced with the
// platform-appropriate slash if necessary (backslash on Windows for example).
//
// If, for example, an embedder wanted to allow for a 'test' directory to have
// access to a 'src' directory, it would execute a file in the parent directory
// of both. It could then use `lily_import_use_local_dir(s, "src")`, run
// imports, then use `lily_import_use_local_dir(s, "test")`.
//
// This lasts until the other function is called.
void lily_import_use_local_dir(lily_state *s, const char *dir);

// Function: lily_import_use_package_dir
// Use a package directory for upcoming imports.
//
// This instructs the interpreter that subsequent `lily_import_*` calls should
// be done inside a package. For a given target 'x', this will run imports
// within 'packages/x/src/x.suffix'. Callers that want the default loading
// scheme that the 'lily' executable uses should pass this an empty string.
//
// If this function is passed forward slashes, they are replaced with the
// platform-appropriate slash if necessary (backslash on Windows for example).
//
// If, for example, an embedder wanted to allow for a 'test' directory to have
// access to a 'src' directory, it would execute a file in the parent directory
// of both. It could then use `lily_import_use_local_dir(s, "src")`, run
// imports, then use `lily_import_use_local_dir(s, "test")`.
//
// This lasts until the other function is called.
void lily_import_use_package_dir(lily_state *s, const char *dir);

// Function: lily_import_current_root_dir
// Return the directory of the package that the source import belongs to.
//
// The first module imported is considered to be the root of a package. For all
// others, packages are found within 'packages/name/src/name.suffix'.
//
// The result of this function does not include the initial directory passed to
// the first source (ex: 'test/abc.lily' is the same as 'abc.lily').
//
// One use case of this is to allow files in a 'test' directory to import those
// in a 'src' directory.
//
// The result of this function uses the slashes of the source platform, but will
// not contain a trailing slash. If called from the first package, the result is
// an empty string.
const char *lily_import_current_root_dir(lily_state *s);

///////////////////////////
// Section: Class id macros
///////////////////////////
// Class ids for predefined classes.
//
// Lily contains several predefined classes. Their ids are made available in
// case they are useful to embedders. Variant ids, for example, can be used with
// 'lily_arg_isa' to determine what variant an enum holds.
//
// Embedders that add a foreign class to the interpreter will be fetching it
// with 'lily_cid_at'.
//
// Embedders must not rely on class ids having certain values, as they may (but
// should not) change between releases.

// Macro: LILY_ID_UNSET
// False identity for testing if a value is unset.
// 
// This id is for use by functions that have optional keyword arguments. Suppose
// a function takes three keyword arguments, but is only given the first and the
// third. In such a case, the interpreter sends an 'unset' value to stand in for
// the second argument to allow using the same calling opcode. 
#define LILY_ID_UNSET         0

// Macro: LILY_ID_INTEGER
// Identity of the Integer class.
#define LILY_ID_INTEGER       1

// Macro: LILY_ID_DOUBLE
// Identity of the Double class.
#define LILY_ID_DOUBLE        2

// Macro: LILY_ID_STRING
// Identity of the String class.
#define LILY_ID_STRING        3

// Macro: LILY_ID_BYTE
// Identity of the Byte class.
#define LILY_ID_BYTE          4

// Macro: LILY_ID_BYTESTRING
// Identity of the ByteString class.
#define LILY_ID_BYTESTRING    5

// Macro: LILY_ID_BOOLEAN
// Identity of the Boolean class.
#define LILY_ID_BOOLEAN       6

// Macro: LILY_ID_FUNCTION
// Identity of the Function class.
#define LILY_ID_FUNCTION      7

// Macro: LILY_ID_LIST
// Identity of the List class.
#define LILY_ID_LIST          8

// Macro: LILY_ID_HASH
// Identity of the Hash class.
#define LILY_ID_HASH          9

// Macro: LILY_ID_TUPLE
// Identity of the Tuple class.
#define LILY_ID_TUPLE        10

// Macro: LILY_ID_FILE
// Identity of the File class.
#define LILY_ID_FILE         11

// Macro: LILY_ID_OPTION
// Identity of the Option enum.
#define LILY_ID_OPTION       12

// Macro: LILY_ID_SOME
// Identity of the Some variant.
#define LILY_ID_SOME         13

// Macro: LILY_ID_NONE
// Identity of the None variant.
#define LILY_ID_NONE         14

// Macro: LILY_ID_RESULT
// Identity of the Result enum.
#define LILY_ID_RESULT       15

// Macro: LILY_ID_FAILURE
// Identity of the Failure variant of Result.
#define LILY_ID_FAILURE      16

// Macro: LILY_ID_SUCCESS
// Identity of the Success variant of Result.
#define LILY_ID_SUCCESS      17

// Macro: LILY_ID_EXCEPTION
// Identity of the Exception class.
#define LILY_ID_EXCEPTION    18

// Macro: LILY_ID_IOERROR
// Identity of the IOError class.
#define LILY_ID_IOERROR      19

// Macro: LILY_ID_KEYERROR
// Identity of the KeyError class.
#define LILY_ID_KEYERROR     20

// Macro: LILY_ID_RUNTIMEERROR
// Identity of the RuntimeError class.
#define LILY_ID_RUNTIMEERROR 21

// Macro: LILY_ID_VALUEERROR
// Identity of the ValueError class.
#define LILY_ID_VALUEERROR   22

// Macro: LILY_ID_INDEXERROR
// Identity of the IndexError class.
#define LILY_ID_INDEXERROR   23

// Macro: LILY_ID_DBZERROR
// Identity of the DivisionByZero class.
#define LILY_ID_DBZERROR     24 /* > 9000 */

// Macro: LILY_ID_UNIT
// Identity of the Unit class.
#define LILY_ID_UNIT         25

/* Internal use only: Where class ids start at. */
#define START_CLASS_ID       26

////////////////////////////////
// Section: Raw value operations
////////////////////////////////
// Functions that interact with raw values. 

// Function: lily_bytestring_raw
// Get the raw buffer behind a ByteString.
char *lily_bytestring_raw(lily_bytestring_val *byte_val);

// Function: lily_bytestring_length
// Get the size (in bytes) of a ByteString.
uint32_t lily_bytestring_length(lily_bytestring_val *byte_val);

// Function: lily_con_get
// Fetch an element from a container.
//
// This returns a pointer to an element at 'index' within 'con'. The value is
// **not** given a refcount increase (that happens when the value is put
// somewhere). The caller can therefore use the result of this function as part
// of an expression without worrying about a leak.
//
// No safety checking is performed on the index given.
//
// Parameters:
//     con   - The container (user-defined class, non-empty variant, List, or
//             Tuple).
//     index - Target index. 0 is the first element.
lily_value *lily_con_get(lily_container_val *con, uint32_t index);

// Function: lily_con_set
// Set an element into a container.
//
// This assigns a new value to 'con' at the position 'index'. The source value
// will receive a ref increase, whereas the old value will have a ref drop (and
// possibly be destroyed).
//
// No safety checking is performed on the index given.
//
// Parameters:
//     con   - The container (user-defined class, non-empty variant, List, or
//             Tuple).
//     index - Target index. 0 is the first element.
void lily_con_set(lily_container_val *con, uint32_t index, lily_value *value);

// Function: lily_con_set_from_stack
// (Stack: -1) Set an element into a container from the stack.
//
// This pops 1 value from the stack, to be used as the value. Following that,
// this performs the same work as 'lily_con_set' using that value.
//
// No safety checking is performed on the index given.
//
// Parameters:
//     con   - The container (user-defined class, non-empty variant, List, or
//             Tuple).
//     index - Target index. 0 is the first element.
void lily_con_set_from_stack(lily_state *s, lily_container_val *con,
                             uint32_t index);

// Function: lily_con_size
// Return the number of occupied values in a container.
//
// For List values, this does **not** include how many values were reserved.
uint32_t lily_con_size(lily_container_val *con);

// Function: lily_file_for_read
// Return the underlying FILE * of a File (if in read mode), or raise IOError.
FILE *lily_file_for_read(lily_state *s, lily_file_val *file);

// Function: lily_file_for_write
// Return the underlying FILE * of a File (if in write mode), or raise IOError.
FILE *lily_file_for_write(lily_state *s, lily_file_val *file);

// Function: lily_function_bytecode
// Return the bytecode of a Function.
//
// Native Function values: This returns the bytecode, and sets len to the length
// of the bytecode. The length is never zero.
//
// Foreign Function values: This returns NULL, and sets len to zero.
uint16_t *lily_function_bytecode(lily_function_val *func, uint16_t *length);

// Function: lily_function_is_foreign
// Return 1 if the Function has a foreign implementation, 0 otherwise.
int lily_function_is_foreign(lily_function_val *func);

// Function: lily_function_is_native
// Return 1 if the Function has a native implementation, 0 otherwise.
int lily_function_is_native(lily_function_val *func);

// Function: lily_hash_get
// Look for a given key within a Hash.
//
// This assumes that 'key' is valid for the hash given. The caller must not send
// the wrong kind of a key for a hash (ex: a String key for a Integer hash).
//
// The result of this function is either a pointer to the value found or NULL.
//
// Parameters:
//     s    - The interpreter. This must be the same interpreter that the hash
//            was created for, because this function uses the interpreter's
//            sipkey.
//     hash - The target hash value.
//     key  - A full value holding a key to search for.
lily_value *lily_hash_get(lily_state *s, lily_hash_val *hash, lily_value *key);

// Function: lily_hash_set
// Set 'key' to 'value' within a given hash.
//
// This assumes that both the key and record are valid for this kind of hash. Do
// not attempt to store the wrong kind of key or record in a hash.
//
// If the key provided does not already exist in the hash, then both the key and
// the value are copied into the hash.
//
// Otherwise, the value for the existing key is overwritten. The value provided
// will receive a ref increase, whereas the old value gets a ref drop (and may
// be destroyed.
//
// Parameters:
//     s      - The interpreter. This must be the same interpreter that the hash
//              was created for, because this function uses the interpreter's
//              sipkey.
//     hash   - The target hash value.
//     key    - A full value holding a key to search for.
//     record - A full value to store in the hash.
void lily_hash_set(lily_state *s, lily_hash_val *hash, lily_value *key, lily_value *record);

// Function: lily_hash_set_from_stack
// (Stack: -2) lily_hash_set, but key and value come from the stack.
//
// This pops 2 values from the top of the stack. The first value (the original
// top of the stack) is the record to use. The second value (the one next to the
// top) is the key.
//
// This function then performs the same work as lily_hash_set.
void lily_hash_set_from_stack(lily_state *s, lily_hash_val *hash);

// Function: lily_hash_take
// (Stack: +1?) Maybe take a value from a hash.
//
// This performs the same work as lily_hash_get.
//
// If the result is non-NULL, it is pushed onto the stack. Otherwise, no action
// is performed to the stack.
//
// Returns 1 if the stack had a value pushed, 0 otherwise.
int lily_hash_take(lily_state *s, lily_hash_val *hash, lily_value *key);

// Function: lily_list_insert
// Insert a value, pushing others to the right.
//
// This takes a container because List is implemented as a container. However,
// the caller must **not** send non-List values to this function.
//
// The index provided must be between existing elements in the List (reserved
// spaces do not count). This function may, however, use an element that has
// been reserved.
//
// Parameters:
//     con   - A List value.
//     index - Index position. Cannot be negative. 0 is the first element.
//     value - A full value to push.
void lily_list_insert(lily_container_val *con, uint32_t index, lily_value *value);

// Function: lily_list_reserve
// Reserve N elements in a List.
//
// This takes a container because List is implemented as a container. However,
// the caller must **not** send non-List values to this function.
//
// List values are not required to have reserved element in order for other
// List-based functions to work. This function is for callers who know they will
// need a very large List and do not want repeated growing.
//
// Parameters:
//     con  - A List value.
//     size - The total number of elements to have reserved. If this is less
//            than the number of elements currently reserved, no action is
//            taken. Must not be negative.
void lily_list_reserve(lily_container_val *con, uint32_t size);

// Function: lily_list_take
// (Stack: +1) Take an element out of a List, pushing it onto the stack.
//
// This takes a container because List is implemented as a container. However,
// the caller must **not** send non-List values to this function.
//
// This performs the same work as lily_list_get. Instead of simply returning the
// value, it is instead taken from the List and pushed onto the stack.
//
// Parameters:
//     s     - The interpreter.
//     con   - A List value.
//     index - Target index. Cannot be negative. 0 is the first element.
void lily_list_take(lily_state *s, lily_container_val *con, uint32_t index);

// Function: lily_list_push
// Push a value onto the end of a List.
//
// This takes a container because List is implemented as a container. However,
// the caller must **not** send non-List values to this function.
//
// This pushes the value provided onto the end of the List given.
void lily_list_push(lily_container_val *con, lily_value *value);

// Function: lily_string_raw
// Returns the raw buffer behind a String.
char *lily_string_raw(lily_string_val *string_val);

// Function: lily_string_length
// Returns the size (in bytes) of a String buffer.
uint32_t lily_string_length(lily_string_val *string_val);

/////////////////////////////
// Section: Argument handling
/////////////////////////////
// Fetch and/or test arguments passed to a foreign function.
//
// The lily_arg_* functions grab values from the bottom of the interpreter's
// stack. The first argument is at index 0.
//
// These functions are named lily_arg_* because their most common use is
// fetching arguments of a function. However, they are not limited to that. If,
// for example, a function takes 2 arguments and a Boolean is pushed, then
// lily_arg_boolean being passed an index of 2 (the third argument) will yield
// the value pushed.
//
// How various function features are implemented:
//
// Variable arguments - Extra arguments are placed into a List which is empty if
//                      no extra arguments were passed.
// Optional arguments - Functions are provided an argument count, and are
//                      responsible for implementing the default values they
//                      claim to have.
// Keyword arguments  - Arguments are rearranged into positional order before
//                      the function sees them. If keyword arguments are
//                      optional, then the 'holes' are filled by sending an
//                      unset value. Use LILY_ID_UNSET with lily_arg_isa to
//                      test for holes.
//
// These functions do **not** support negative indexes.

// Function: lily_arg_boolean
// Fetch a Boolean from the stack.
int                  lily_arg_boolean   (lily_state *s, int index);

// Function: lily_arg_byte
// Fetch a Byte from the stack.
uint8_t              lily_arg_byte      (lily_state *s, int index);

// Function: lily_arg_bytestring
// Fetch a ByteString from the stack.
lily_bytestring_val *lily_arg_bytestring(lily_state *s, int index);

// Function: lily_arg_container
// Fetch a user-defined class, (non-empty) variant, List, or Tuple.
//
// If the argument can hold an empty variant (such as Option), make sure to test
// that first. Otherwise, the container returned will be invalid, and using it
// will almost certainly cause a crash.
lily_container_val * lily_arg_container (lily_state *s, int index);

// Function: lily_arg_double
// Fetch a Double from the stack.
double               lily_arg_double    (lily_state *s, int index);

// Function: lily_arg_file
// Fetch a File from the stack.
lily_file_val *      lily_arg_file      (lily_state *s, int index);

// Function: lily_arg_function
// Fetch a Function from the stack.
lily_function_val *  lily_arg_function  (lily_state *s, int index);

// Function: lily_arg_generic
// Fetch a pointer-based (foreign) value from the stack.
//
// Bindings generated for use by foreign classes will cast the result of this to
// the appropriate foreign class.
lily_generic_val *   lily_arg_generic   (lily_state *s, int index);

// Function: lily_arg_hash
// Fetch a Hash from the stack.
lily_hash_val *      lily_arg_hash      (lily_state *s, int index);

// Function: lily_arg_integer
// Fetch a Integer from the stack.
int64_t              lily_arg_integer   (lily_state *s, int index);

// Function: lily_arg_string
// Fetch a String from the stack.
lily_string_val *    lily_arg_string    (lily_state *s, int index);

// Function: lily_arg_string_raw
// Fetch the underlying buffer of a String from the stack.
char *               lily_arg_string_raw(lily_state *s, int index);

// Function: lily_arg_value
// Fetch a complete value with flags and class information.
lily_value *         lily_arg_value     (lily_state *s, int index);

// Function: lily_arg_count
// How many arguments the function being called was given.
//
// Note: Variable argument functions place their extra arguments into a List.
uint16_t lily_arg_count(lily_state *s);

// Function: lily_arg_isa
// Check if an argument has an exact class id.
//
// This is a strict equality comparison to the value at the index provided.
// Attempting to test that a value of class ValueError isa Exception will
// therefore fail.
//
// One use of this function is to identify what variant that an enum is before
// using the enum.
//
// Returns 1 if true, 0 otherwise.
int lily_arg_isa(lily_state *s, int index, uint16_t class_id);

// Macro: lily_arg_is_failure
// Calls lily_arg_isa with LILY_ID_FAILURE.
#define lily_arg_is_failure(s, index) lily_arg_isa(s, index, LILY_ID_FAILURE)

// Macro: lily_arg_is_none
// Calls lily_arg_isa with LILY_ID_NONE.
#define lily_arg_is_none(s, index) lily_arg_isa(s, index, LILY_ID_NONE)

// Macro: lily_arg_is_some
// Calls lily_arg_isa with LILY_ID_SOME.
#define lily_arg_is_some(s, index) lily_arg_isa(s, index, LILY_ID_SOME)

// Macro: lily_arg_is_success
// Calls lily_arg_isa with LILY_ID_SUCCESS.
#define lily_arg_is_success(s, index) lily_arg_isa(s, index, LILY_ID_SUCCESS)

///////////////////////////
// Section: Optional Values
///////////////////////////
// Utility functions for getting optional values.
//
// These functions check for an argument existing, and provide a fallback if it
// doesn't. These functions work for optional arguments (where the function
// receives a smaller count of arguments), as well as keyed optional arguments
// (where the caller receives placeholder arguments).

// Function: lily_optional_boolean
// Fetch a Boolean at 'index' or use the 'fallback' value.
int lily_optional_boolean(lily_state *s, int pos, int fallback);

// Function: lily_optional_integer
// Fetch an Integer at 'index' or use the 'fallback' value.
int64_t lily_optional_integer(lily_state *s, int pos, int64_t fallback);

// Function: lily_optional_string_raw
// Fetch the backing of a String at 'index' or use the 'fallback' value.
const char *lily_optional_string_raw(lily_state *s, int pos,
                                     const char *fallback);

/////////////////////////
// Section: Stack pushing
/////////////////////////
// Pushing values onto the stack.
//
// Foreign functions and dynaload loaders are permitted to push extra values
// onto the interpreter's stack.
//
// Simple primitives return void, since there is nothing more to do after
// pushing the value onto the stack.
//
// Containers, on the other hand, return themselves. It is the caller's
// responsibility to fill containers befolre using them.
//
// When implementing a dynaload loader, the variable loading functions should
// finish with exactly 1 extra value on the stack. That extra value becomes the
// value for the var.

// Function: lily_push_boolean
// (Stack: +1) Push a Boolean value onto the stack.
void                lily_push_boolean      (lily_state *s, int value);

// Function: lily_push_byte
// (Stack: +1) Push a Byte value onto the stack.
void                lily_push_byte         (lily_state *s, uint8_t value);

// Function: lily_push_bytestring
// (Stack: +1) Push a ByteString value onto the stack.
void                lily_push_bytestring   (lily_state *s, const char *source,
                                            int size);

// Function: lily_push_double
// (Stack: +1) Push a Double value onto the stack.
void                lily_push_double       (lily_state *s, double value);

// Function: lily_push_empty_variant
// (Stack: +1) Push an empty variant (such as None) onto the stack.
void                lily_push_empty_variant(lily_state *s, uint16_t class_id);

// Function: lily_push_foreign
// (Stack: +1) Push a foreign class value onto the stack.
//
// This function is called by dynaload bindings to create foreign class
// instances. It should not be called directly.
lily_foreign_val *  lily_push_foreign      (lily_state *s, uint16_t class_id,
                                            lily_destroy_func destroy_fn,
                                            size_t size);

// Function: lily_push_hash
// (Stack: +1) Push a new Hash with 'size' slots reserved onto the stack.
lily_hash_val *     lily_push_hash         (lily_state *s, int size);

// Function: lily_push_instance
// (Stack: +1) Push a user-defined class instance with 'size' values onto the
// stack.
//
// This allocates 'size' values in the new instance. Slots from 0 up to size - 1
// must be filled before the instance is used.
lily_container_val *lily_push_instance     (lily_state *s, uint16_t class_id,
                                            uint32_t size);

// Function: lily_push_integer
// (Stack: +1) Push a new Integer onto the stack.
void                lily_push_integer      (lily_state *s, int64_t value);

// Function: lily_push_list
// (Stack: +1) Push a List with 'size' values onto the stack.
//
// This allocates 'size' values in the new List. Slots from 0 up to size - 1
// must be filled before the List is used.
lily_container_val *lily_push_list         (lily_state *s, uint32_t size);

// Function: lily_push_string
// (Stack: +1) Push a String wrapping over 'source' onto the stack.
//
// This creates a String value with a deep copy of 'source'.
//
// Caller is responsible for making sure that the new String value is valid. The
// interpreter is built with the expectation that all String values are utf-8.
// If the caller is unsure, the function 'lily_is_valid_utf8' can be used.
void                lily_push_string       (lily_state *s, const char *source);

// Function: lily_push_string_sized
// (Stack: +1) Push a String of 'size' bytes from 'source' onto the stack.
//
// This performs the same work as lily_push_string, except that 'size' bytes are
// copied from 'source' (instead of all of it).
//
// The source should not include a zero terminator. This function will add one
// at the very end. Callers should instead make sure that there are no zero
// terminators in 'source' (at least for as much as 'size').
void                lily_push_string_sized (lily_state *s, const char *source,
                                            int size);

// Function: lily_push_super
// (Stack: +1) Push a superclass onto the stack.
//
// This is solely for use by foreign functions acting as a constructor. Those
// functions are also required to use this function, as otherwise superclass
// values will not be completely initialized.
//
// This function checks if there is a superclass currently in progress. If so,
// then that existing value is pushed onto the stack. Otherwise, it creates a
// new value with 'size' slots reserved.
//
// In either case, the caller should then set the slots that are declared in the
// class it is creating. An example would be inheriting Exception. The Exception
// constructor should only initialize slots 0 and 1 (the message and traceback,
// respectively). It should not worry about initialization of other slots.
//
// This does not handle foreign classes inheriting other foreign classes,
// because the language does not support that (intentionally).
lily_container_val *lily_push_super        (lily_state *s, uint16_t class_id,
                                            uint32_t size);

// Function: lily_push_tuple
// (Stack: +1) Push a Tuple with 'size' values onto the stack.
//
// This allocates 'size' values in the new Tuple. Slots from 0 up to size - 1
// must be filled before the Tuple is used.
lily_container_val *lily_push_tuple        (lily_state *s, uint32_t size);

// Function: lily_push_unit
// (Stack: +1) Push the unit value (of class Unit) onto the stack.
void                lily_push_unit         (lily_state *s);

// Function: lily_push_unset
// (Stack: +1) Push an unset value onto the stack.
//
// This is solely for calling functions that have optional keyword arguments.
//
// Suppose there's a function that takes two optional keyed arguments. To pass
// only the second argument, push an unset, then push the value for the second
// argument.
void                lily_push_unset        (lily_state *s);

// Function: lily_push_value
// (Stack: +1) Push a full value onto the stack.
void                lily_push_value        (lily_state *s, lily_value *value);

// Function: lily_push_variant
// (Stack: +1) Push a variant of 'class_id' and 'size' values onto the stack.
//
// This allocates 'size' values in the new variant. Slots from 0 up to size - 1
// must be filled before the variant is used.
lily_container_val *lily_push_variant      (lily_state *s, uint16_t class_id,
                                            uint32_t size);

// Macro: lily_push_failure
// Shorthand for lily_push_variant with LILY_ID_FAILURE.
# define lily_push_failure(s) lily_push_variant(s, LILY_ID_FAILURE, 1)

// Macro: lily_push_none
// Shorthand for lily_push_empty_variant with LILY_ID_NONE.
# define lily_push_none(s) lily_push_empty_variant(s, LILY_ID_NONE)

// Macro: lily_push_some
// Shorthand for lily_push_some with LILY_ID_SOME.
# define lily_push_some(s) lily_push_variant(s, LILY_ID_SOME, 1)

// Macro: lily_push_success
// Shorthand for lily_push_success with LILY_ID_SUCCESS.
# define lily_push_success(s) lily_push_variant(s, LILY_ID_SUCCESS, 1)

/////////////////////////////
// Section: Returning a value
/////////////////////////////
// All foreign functions must finish by returning a value.
//
// The most common return is 'lily_return_top', wherein a caller returns the
// value at the top of the stack.
//
// Specific returns for pointer-based values are intentionally omitted. Callers
// should instead use lily_return_top or lily_return_value, both of which
// retain the flags behind a value.

// Function: lily_return_boolean
// Set a Boolean return value.
void lily_return_boolean(lily_state *s, int value);

// Function: lily_return_byte
// Set a Byte return value.
void lily_return_byte   (lily_state *s, uint8_t value);

// Function: lily_return_double
// Set a Double return value.
void lily_return_double (lily_state *s, double value);

// Function: lily_return_integer
// Set an Integer return value.
void lily_return_integer(lily_state *s, int64_t value);

// Function: lily_return_none
// Set a None return value.
void lily_return_none   (lily_state *s);

// Function: lily_return_some_of_top
// Set a Some holding the top of the stack as the return value.
void lily_return_some_of_top(lily_state *s);

// Function: lily_return_string
// Set a String return value.
void lily_return_string (lily_state *s, const char *value);

// Function: lily_return_super
// Use this if lily_push_super was used.
void lily_return_super  (lily_state *s);

// Function: lily_return_top
// Set the return value as the value currently at the top of the stack.
void lily_return_top    (lily_state *s);

// Function: lily_return_unit
// Set a Unit return value.
void lily_return_unit   (lily_state *s);

// Function: lily_return_value
// Set the value given as the return value.
void lily_return_value  (lily_state *s, lily_value *value);

////////////////////////////
// Section: Value extraction
////////////////////////////
// Extract a raw value from a full value.
//
// These functions assume the caller knows what group that the value falls into.
// If a caller isn't sure, lily_value_get_group can be used to find out.

// Enum: lily_value_group
//
// lily_isa_boolean       - The value is a 'Boolean'.
// lily_isa_byte          - The value is a 'Byte'.
// lily_isa_bytestring    - The value is a 'ByteString'.
// lily_isa_double        - The value is a 'Double'.
// lily_isa_empty_variant - This is a variant that does not have any values
//                          inside of it. Do not attempt to use this as a
//                          container.
// lily_isa_file          - The value is a 'File'.
// lily_isa_function      - The value is a 'Function'.
// lily_isa_hash          - The value is a 'Hash'. This is not a container.
// lily_isa_foreign_class - This is a class defined in C. It is not a container
//                          and it does not have fields to walk. Treat it as an
//                          opaque pointer to unknown content.
// lily_isa_native_class  - This is a class with fields, usually defined in
//                          native Lily code. This is a container.
// lily_isa_integer       - The value is an 'Integer'.
// lily_isa_list          - The value is a 'List'. This is a container.
// lily_isa_string        - The value is a 'String'.
// lily_isa_tuple         - The value is a 'Tuple'. This is a container.
// lily_isa_unit          - The value is a 'Unit'. This is always the Lily
//                          literal 'unit'. It should be treated similar to an
//                          empty variant.
// lily_isa_variant       - The value is a variant with a non-zero number of
//                          fields. This is a container.

typedef enum {
    lily_isa_boolean,
    lily_isa_byte,
    lily_isa_bytestring,
    lily_isa_double,
    lily_isa_empty_variant,
    lily_isa_file,
    lily_isa_function,
    lily_isa_hash,
    lily_isa_foreign_class,
    lily_isa_native_class,
    lily_isa_integer,
    lily_isa_list,
    lily_isa_string,
    lily_isa_tuple,
    lily_isa_unit,
    lily_isa_variant,
} lily_value_group;

// Function: lily_value_get_group
// Find out what group that a value belongs to. Refer to the 'lily_value_group'
// enum documentation for more info.
lily_value_group lily_value_get_group(lily_value *value);

// Function: lily_as_boolean
// Extract a Boolean.
int                  lily_as_boolean   (lily_value *value);

// Function: lily_as_byte
// Extract a Byte.
uint8_t              lily_as_byte      (lily_value *value);

// Function: lily_as_bytestring
// Extract a ByteString.
lily_bytestring_val *lily_as_bytestring(lily_value *value);

// Function: lily_as_container
// Extract a container (user-defined class, non-empty variant, List, or Tuple).
lily_container_val * lily_as_container (lily_value *value);

// Function: lily_as_double
// Extract a Double.
double               lily_as_double    (lily_value *value);

// Function: lily_as_file
// Extract a File.
lily_file_val *      lily_as_file      (lily_value *value);

// Function: lily_as_function
// Extract a Function.
lily_function_val *  lily_as_function  (lily_value *value);

// Function: lily_as_generic
// Extract a generic pointer (to be cast to some other type)..
lily_generic_val *   lily_as_generic   (lily_value *value);

// Function: lily_as_hash
// Extract a Hash.
lily_hash_val *      lily_as_hash      (lily_value *value);

// Function: lily_as_integer
// Extract a Integer.
int64_t              lily_as_integer   (lily_value *value);

// Function: lily_as_string
// Extract a String.
lily_string_val *    lily_as_string    (lily_value *value);

// Function: lily_as_string_raw
// Extract the raw buffer behind a String.
char *               lily_as_string_raw(lily_value *value);

//////////////////////////////
// Section: Calling a function
//////////////////////////////
// Functions for calling back into the interpreter.
//
// These functions should only be called when the interpreter is inside a
// foreign function, or outside of a parse/render call. Do not invoke these
// functions from a dynaload loader or a hook.

// Function: lily_call
// (Stack: -count) Perform a prepared call using values from the stack.
//
// This function performs a single call into the interpreter. The call in
// question does not trap for exceptions, so exceptions raised by this call will
// pass through the caller.
//
// This takes 'count' values off of the top of the stack. The section on
// argument handling details how various features (varargs, optargs, and
// keyargs) are implemented. Refer to that for how to call those kinds of
// functions.
//
// Function calls always have a result. The result is stored in the register
// that was returned by the last call to 'lily_call_prepare'. If a caller wants
// to save the result of a function call, it can use lily_push_value to push
// the result onto the stack.
void lily_call(lily_state *s, uint16_t count);

// Function: lily_call_prepare
// (Stack: +1) Reserve a result register and prepare 'func'.
//
// This function **must** be called **before** pushing arguments to a function.
// The function given is prepared so that subsequent calls to 'lily_call' will
// invoke it. Many functions in Lily's standard library (maps, selects, rejects)
// like to call the same function multiple times. Having prep outside of call
// makes it so that the interpreter does not do repeated repeated prep work
// for each calls. A single prep works for any number of subsequent 'lily_call'
// invocations.
//
// The other job of this function is to set a result storage. The result storage
// lives below a function's arguments and is updated each time the function is
// called. Callers that want to save the value can slide it over where they
// want it or push a copy onto the stack.
//
// If the caller wants to switch the target function often, the caller should
// make sure to pop the results that are stored. Otherwise, the interpreter's
// stack can accumulate result values.
void lily_call_prepare(lily_state *s, lily_function_val *func);

// Function: lily_call_result
// Return the register that 'lily_call_prepare' reserved.
//
// The value itself (not the contents) is valid until the next
// 'lily_call_prepare', or when the called foreign function goes out of scope.
lily_value *lily_call_result(lily_state *s);

///////////////////////////
// Section: Exception raise
///////////////////////////
// Functions for raising an exception.
//
// Each of these functions raises a specific exception. The format string goes
// by the same rules that 'lily_msgbuf_add_fmt' goes by, because it (and the
// arguments) are sent to that function.
//
// These functions must only be called when inside a foreign function.

// Function: lily_DivisionByZeroError
// Raise DivisionByZeroError with the given message and args.
void lily_DivisionByZeroError(lily_state *s, const char *format, ...);

// Function: lily_IndexError
// Raise IndexError with the given message and args.
void lily_IndexError(lily_state *s, const char *format, ...);

// Function: lily_IOError
// Raise IOError with the given message and args.
void lily_IOError(lily_state *s, const char *format, ...);

// Function: lily_KeyError
// Raise KeyError with the given message and args.
void lily_KeyError(lily_state *s, const char *format, ...);

// Function: lily_RuntimeError
// Raise RuntimeError with the given message and args.
void lily_RuntimeError(lily_state *s, const char *format, ...);

// Function: lily_ValueError
// Raise ValueError with the given message and args.
void lily_ValueError(lily_state *s, const char *format, ...);

///////////////////////////
// Section: Error callbacks
///////////////////////////
// Foreign function cleanup.

// Typedef: lily_error_callback_func
// Callback for when lily_call raises an exception.
//
// Error callbacks are invoked when lily_call has raised an exception that will
// go outside the invoking foreign function. The callback is given a state that
// has the same stack bottom as when the foreign function registering it had
// when first called.
//
// One use of error functions in the builtin api is to drop the iteration count
// of hashes. This makes them eligible for alteration again, as hashes that are
// being iterated over cannot be altered.
typedef void (*lily_error_callback_func)(lily_state *s);

// Function: lily_error_callback_push
// Push an error callback for the current foreign function.
void lily_error_callback_push(lily_state *s, lily_error_callback_func callback_fn);

// Function: lily_error_callback_pop
// Remove an error callback (typically at foreign function exit).
//
// A foreign function must call this for every error callback it registered if
// no error was raised.
void lily_error_callback_pop(lily_state *s);

////////////////////////////
// Section: Stack operations
////////////////////////////
// Operations working on the top of the stack.

// Function: lily_stack_drop_top
// Pop the top of the stack, deref-ing it if necessary.
void lily_stack_drop_top(lily_state *s);

// Function: lily_stack_get_top
// Return the value at the top of the stack.
lily_value *lily_stack_get_top(lily_state *s);

/////////////////////////////////////
// Section: Message buffer operations
/////////////////////////////////////
// A wrapper over an automatically-growing buffer.

// Typedef: lily_msgbuf
// Opaque typedef for a msgbuf.
//
// A message buffer (msgbuf for short) is a buffer with methods to add text that
// also grow it if necessary. The msgbuf also zero-terminates the internal
// buffer after every method adding data.
//
// The interpreter provides a general-purpose buffer ('lily_msgbuf_get') that
// operations can use if they know they'll be working with a small string.
//
// It is the responsibility of the caller to flush a msgbuf **before** it is
// used, not after. By making it the responsibility at the start of a function,
// a caller that doesn't flush a msgbuf before use becomes obvious. Note that
// 'lily_msgbuf_get' automatically does that flushing.
//
// Functions that need to store a potentially large amount of text should create
// their own temporary msgbuf. Do note that the msgbuf never receives an
// interpreter object, and therefore the interpreter will not clean up temporary
// msgbuf's for a caller.
typedef struct lily_msgbuf_ lily_msgbuf;

// Function: lily_new_msgbuf
// Create a new msgbuf with a starting size of 'size'.
lily_msgbuf *lily_new_msgbuf(uint32_t size);

// Function: lily_free_msgbuf
// Free a msgbuf struct and the underlying buffer.
//
// Unlike destroying a value, this process is very straightforward.
void lily_free_msgbuf(lily_msgbuf *msgbuf);

// Function: lily_mb_add
// Add 'source' to the msgbuf.
void lily_mb_add(lily_msgbuf *msgbuf, const char *source);

// Function: lily_mb_add_char
// Add a single character 'ch' to the msgbuf.
void lily_mb_add_char(lily_msgbuf *msgbuf, char ch);

// Function: lily_mb_add_fmt
// Add to the msgbuf based on a format string.
//
// The following format characters are supported:
//
// %d - int
// %c - char
// %s - char *
// %p - void *
// %lld - int64_t
// %% - A single % character.
// ^T - lily_type * (internal use only).
void lily_mb_add_fmt(lily_msgbuf *msgbuf, const char *format, ...);

// Function: lily_mb_add_fmt_va
// lily_mb_add_fmt, but using a va_list.
void lily_mb_add_fmt_va(lily_msgbuf *msgbuf, const char *format, va_list);

// Function: lily_mb_add_sized
// Add 'count' characters of 'source' to the msgbuf.
void lily_mb_add_sized(lily_msgbuf *msgbuf, const char *source, int count);

// Function: lily_mb_add_slice
// Add 'source' to the msgbuf, from 'start' to 'end'.
void lily_mb_add_slice(lily_msgbuf *msgbuf, const char *source, int start, int end);

// Function: lily_mb_add_value
// Interpolate a value into the msgbuf.
void lily_mb_add_value(lily_msgbuf *msgbuf, lily_state *s, lily_value *value);

// Function: lily_mb_flush
// Drop all contents in the msgbuf.
lily_msgbuf *lily_mb_flush(lily_msgbuf *msgbuf);

// Function: lily_mb_raw
// Return the underlying buffer of a msgbuf.
//
// The buffer can be invalidated by any method that adds to the buffer.
const char *lily_mb_raw(lily_msgbuf *msgbuf);

// Function: lily_mb_pos
// Return the length of the msgbuf.
int lily_mb_pos(lily_msgbuf *msgbuf);

// Function: lily_mb_html_escape
// Add html-escaped version of 'input' to the msgbuf.
const char *lily_mb_html_escape(lily_msgbuf *msgbuf, const char *input);

// Function: lily_mb_sprintf
// Equivalent to flush, add_fmt, returning the underlying buffer.
const char *lily_mb_sprintf(lily_msgbuf *msgbuf, const char *format, ...);

// Function: lily_msgbuf_get
// Return the common msgbuf of the interpreter.
lily_msgbuf *lily_msgbuf_get(lily_state *);

////////////////////////
// Section: Foreign bits
////////////////////////
// Items of interest for putting a foreign class into Lily.

// Macro: LILY_FOREIGN_HEADER
// Put this macro at the top of any struct that is later introduced as a foreign
// class to the interpreter.
#define LILY_FOREIGN_HEADER \
uint32_t refcount; \
uint16_t class_id; \
uint16_t do_not_use; \
lily_destroy_func destroy_func;

// Typedef: lily_destroy_func
// Hook called when a value is to be destroyed.
//
// This callback receives a generic_val, which is to be cast to the instance to
// be destroyed. The callback is responsible for destroying what exists inside
// of the value.
//
// The value itself should not be destroyed, because the interpreter will do
// that once the callback has returned.

/////////////////////////
// Section: Miscellaneous
/////////////////////////
// Miscellaneous operations that don't fit elsewhere.

// Function: lily_find_function
// Search for a function called 'name'.
//
// The search executes as if 'name' was typed in the first file loaded. This
// function is currently limited, as it only accepts just a name (no namespace
// lookup or class lookup).
//
// Returns a function on success, NULL on failure.
lily_function_val *lily_find_function(lily_state *s, const char *name);

// Function: lily_register_module
// Make a module called 'name' available to the interpreter.
//
// A registered module is available anywhere in the interpreter, but must still
// be explicitly loaded through the import keyword.
//
// This function must be called after the interpreter object is created, but
// before any parsing begins.
void lily_module_register(lily_state *s, const char *name,
                          const char **info_table,
                          lily_call_entry_func *call_table);

// Function: lily_is_valid_utf8
// Check if 'source' is valid utf-8.
int lily_is_valid_utf8(const char *source);

// Function: lily_value_tag
// Place a gc tag onto 'value'. May invoke a sweep.
void lily_value_tag(lily_state *s, lily_value *value);

// Function: lily_cid_at
// Function for autogenerated ID_* macros to get a class id.
uint16_t lily_cid_at(lily_state *s, int index);

#endif

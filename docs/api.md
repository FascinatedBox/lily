Lily Interpreter API
====================

## Introduction

This document aims to explain in great details how the interpreter's api works.
It is assumed that the reader has prior experience with native Lily code, as
well as familiarity with C.

Lily the interpreter is itself written entirely in C, as are the extensions to
it. Extending the api to allow new classes, methods, and so on is not detailed
here. For that information, see the binding generation docs in the parsekit
repo [located here](https://github.com/FascinatedBox/lily-parsekit/blob/master/README_bindgen.md).

## Design

At runtime, the interpreter has a stack of values for each function passed
(termed registers). The api is allowed to push values onto the stack as it
wishes. It is important that the caller always finish by returning a value, even
if that value is `Unit`, because all Lily functions are expected to return a
value.

Internally, the interpreter handles memory through a mixture of reference
counting, and a garbage collector. Currently, the garbage collector tags values
that have the potential to be cyclical. The api intentionally does not provide
an interface for doing ref/deref on values. Instead, the api user is expected to
use the stack so that refcounting is handled transparently.

Many functions take `lily_state` as a parameter. `lily_state` is the
interpreter, and each interpreter is entirely apart from every other
interpreter. This design allows multiple interpreters to coexist at the same
time, and even for one interpreter to embed another.

## Contents

### Configuration

Before creating a `lily_state`, one must first create the `lily_config` struct.
The `lily_config` struct provides data for starting up and for executing the
interpreter.

The embedder must not modify the configuration struct once a parse or render
function has used it. Since interpreters do not modify the config struct,
multiple interpreters can share configurations. However, the configuration must
outlast any interpreter that it is used in.

The configuration struct is currently defined as:

```
typedef void (*lily_render_func)(const char *content, void *data)

typedef void (*lily_import_func)(lily_state *s, const char *root_dir,
                                 const char *current_dir, const char *name)

typedef struct lily_config_ {
    int argc;
    char **argv;
    int gc_start;
    int gc_multiplier;
    lily_render_func render_func;
    lily_import_func import_func;
    char sipkey[16];
    void *data;
} lily_config;
```

* `argc` and `argv`: Used to initialize `sys.argv`. Defaults to 0 and NULL,
  resulting in `sys.argv` being empty.

* `gc_start` is how many values that the interpreter should allow before
  performing a mark and sweep.

* `gc_multiplier` is used when the gc does not find any values. The number of
  objects that are allowed (either `gc_start` or the current limit) is
  multiplied by this value.

* `lily_render_func` is called when the render series of functions have data
  outside of `<?lily ... ?>` tags. By default, this is fputs.

* `lily_import_func` is called when the interpreter wants to `import` a module.
  By default, this is a function that attempts to load files from different
  sources. A user can override this by supplying a function that loads paths
  using the `lily_load_*` set of functions.

* `sipkey` is a seed used when generating hash keys. The default value has no
  randomness included, and is very simple.

* `data` is a value purely for the user. It's sent as the `data` parameter of
  the render func, and can be whatever the user wants. The interpreter takes no
  responsibility for the cleanup of whatever is put into `data`. By default, it
  is `stdout`, so that data is rendered to the console by default.

The following functions are available:

#### void lily_config_init(lily_config *config)

This initializes the address of the configuration passed to it. The interpreter
is not responsible for cleanup of the config struct.

#### lily_config *lily_config_get(lily_state *s)

Returns the configuration struct passed to the interpreter state.

### State

#### lily_state *lily_new_state(lily_config *config)

This function creates a new interpreter that is ready to parse or render.

#### void lily_free_state(lily_state *s)

Destroy the interpreter and all values that it contains.

### Parse

Parsing functions will process all of the content passed as code. Once the
`lily_state` has been passed to a parse function, it should not later be passed
to a render function.

Once parsing is complete, the code is immediately executed. The result of any
parsing function is either success (1), or failure (0). On failure, the error
set of functions can be used to get error info.

A failed parse will rewind state (declared vars, classes, etc.) to what they
were before the parse began. Successful parses will carry over declarations that
future parses can use.

#### int lily_parse_file(lily_state *s, const char *filename)

Attempt to open 'filename' and parse the content. An error may be raised that
does not include traceback if 'filename' cannot be opened.

#### int lily_parse_string(lily_state *s, const char *context, const char *data)

Attempt to parse 'data', which will not be copied. 'context' is used as the
filename in case of an error.

#### int lily_parse_expr(lily_state *s, const char *context, char *data, const char **output)

Attempt to parse 'data' using 'context' as the filename in case of error. The
resulting output is saved into an internal msgbuf, and valid until the next
parse step.

### Render

Render functions act like parse functions, except with the following
differences:

* The content passed must have `<?lily` at the very top. This prevents mistaking
  a code-only file for a template file.

* The code within `<?lily` tags is executed immediately when the tag is closed.
  If the code contains an error, then future content will not be processed.

Regardless of parse or render being used, `import` always loads content in
code-only mode.

#### int lily_render_file(lily_state *s, const char *filename)

Attempt to open and render 'filename'.

#### int lily_render_string(lily_state *s, const char *context, const char *data)

Attempt to render 'data' using 'context' as the filename in case of
error.

### Error Messages

When a parse or render function fails, the function in question returns a 0
value. The error is stored internally until the next parse or render step is
invoked.

The result of error functions is held in a buffer stored inside of the
`lily_state` struct. The buffer is also valid until the next parse or render
call.

Behavior is undefined if the caller uses these functions without there being a
current error. Additionally, a subsequent call to one of these functions may
invalidate the last result.

#### const char *lily_error_message(lily_state *s)

Fetch the last error message with file or call traceback included.

#### const char *lily_error_message_no_trace(lily_state *s)

Fetch just the last error message.

### Loading functions

Loading functions are for the embedder to use only while within the import
function callback. The embedder is expected to use the path fragments provided
to provide a full path to any loader function. The embedder should stop
attempting to load content once any loader function returns 1 (success).

In the event that the import callback is unable to load a path, the callback
should simply return. The interpreter will check if some content has been
loaded, and either use it or raise an error message. The interpreter's error
message, if raised, will include the paths tried without any intervention needed
by the import callback.

The import callback is encouraged to use the interpreter's shared msgbuf
(`lily_msgbuf_get`) to construct paths.

All functions defined here return 1 on success, and 0 on failure. The result is
always 0 if the import hook tries to load two sources in one round. These
functions also check for 'path' already having been loaded, so the caller does
not need to do duplicate path checking.

#### int lily_open_file(lily_state *s, const char *path)

Try to open a file at 'path' for reading.

#### int lily_open_library(lily_state *s, const char *path)

Try to load a library at 'path'. This will fail if the library doesn't exist, or
if the library is missing a dynaload table/loader.

#### int lily_open_library_data(lily_state *s, const char *path, const char **dynaload_table, void *loader)

Load the given library contents, using 'path' as their path. Both the dynaload
table and the loader are assumed to be valid.

#### int lily_open_string(lily_state *s, const char *path, const char *content)

Load the string 'content' for 'path'. This only fails if another open function
has already succeeded.

### Identities

At runtime, a value only knows the id of the class that it was created with. The
api exports several identities for use with identity testing functions that are
defined later.

One important note is that enums exist at parse-time, while variants exist at
runtime. To test if an `Option` contains a `Some`, one would only need to test
the value against the `Some` id.

```
#define LILY_ID_INTEGER
#define LILY_ID_DOUBLE
#define LILY_ID_STRING
#define LILY_ID_BYTE
#define LILY_ID_BYTESTRING
#define LILY_ID_BOOLEAN
#define LILY_ID_FUNCTION
#define LILY_ID_DYNAMIC
#define LILY_ID_LIST
#define LILY_ID_HASH
#define LILY_ID_TUPLE
#define LILY_ID_FILE

#define LILY_ID_SOME
#define LILY_ID_NONE

#define LILY_ID_FAILURE
#define LILY_ID_SUCCESS

#define LILY_ID_EXCEPTION
#define LILY_ID_IOERROR
#define LILY_ID_KEYERROR
#define LILY_ID_RUNTIMEERROR
#define LILY_ID_VALUEERROR
#define LILY_ID_INDEXERROR
#define LILY_ID_DBZERROR
#define LILY_ID_UNIT
```

One should be careful when creating values at runtime, as the interpreter
provides no safety or sanity checks.

### Using ByteString values

`ByteString` values are a mixture of a raw `char *` buffer and a size. The
following functions are provided:

#### char *lily_bytestring_raw(lily_bytestring_val *byte_val)

Return the raw buffer that the `ByteString` is using.

#### int lily_bytestring_length(lily_bytestring_val *byte_val)

The number of bytes present in the `ByteString`.

### Using container values

A container is a value that can hold other values. Classes, variants, `List`,
and `Tuple` are all implemented at runtime as a container. The embedder is
responsible for making sure that container operations are consistent with
parse-time requirements.

#### lily_value *lily_con_get(lily_container_val *con, int index)

Fetch the 'index' position from 'con', with 0 being the first element. This
function does not do a bounds check on 'index', nor does it support negative
indexes wrapping around.

#### void lily_con_set(lily_container_val *con, int index, lily_value *value)

This places 'value' at the 'index' position within 'con'. The same caveats that
apply to `lily_con_get` apply here as well.

#### void lily_con_set_from_stack(lily_state *s, lily_container_val *con, int index)

Like above, but the value comes from popping a value from the stack.

#### uint32_t lily_con_size(lily_container_val *con)

Returns the size of the container, with the last element being at size - 1. This
can be considered equivalent to `List.size`.

### Using File values

This section handles getting the raw content of a file from one of the
interpreter's files.

#### FILE *lily_file_for_read(lily_state *s, lily_file_val *file)

Check if 'file' is open for reading. If not, `IOError` is raised. Otherwise, the
raw underlying file is returned.

#### FILE *lily_file_for_read(lily_state *s, lily_file_val *file)

Check if 'file' is open for writing. If not, `IOError` is raised. Otherwise, the
raw underlying file is returned.

### Using Function values

This section of the api is provided so that the `dis` module can inspect
functions. It may be phased out in the future.

#### int lily_function_is_foreign(lily_function_val *func)

Returns 1 if the function is foreign (defined outside the interpreter), 0
otherwise.

#### int lily_function_is_native(lily_function_val *func)

Returns 1 if the function contains pure Lily code, 0 otherwise.

### Using Hash values

#### lily_value *lily_hash_get(lily_state *s, lily_hash_val *hash, lily_value *key)

Attempt to find 'key' within 'hash'. Returns the record found, or NULL.

#### void lily_hash_set(lily_state *s, lily_hash_val *hash, lily_value *key, lily_value *record)

Perform an assignment into the hash provided. If 'key' already exists, the old
key and record deref'd and replaced. If 'key' does not exist, then the new
values are copied and inserted.

#### void lily_hash_set_from_stack(lily_state *s, lily_hash_val *hash)

Pops two elements from the top of the stack and calls `lily_hash_set` with them.
The top-most element is the record, and the next-to-top is the key.

#### int lily_hash_take(lily_state *s, lily_hash_val *hash, lily_value *key)

Attempt to take 'key' out of the hash given. On success, 1 is returned and the
value taken is popped from the stack. On failure, 0 is returned, and no value is
put onto the stack.

### Using List values

`List` is implemented as a container, but also carries extra space. The
functions here take `lily_container_val` as input, but should only be
passed a container that is implementing a `List`.

Similar to container operations, these operations do not check for out-of-bounds
indexes or wrap negative indexes around.

#### void lily_list_insert(lily_container_val *con, int index, lily_value *value)

Insert 'value' at 'index' within the container. Elements at the given index and
above are moved up.

#### void lily_list_reserve(lily_container_val *con, int size)

Reserve a total of 'size' elements for future use. The reserved slots are not
counted in the size of the `List`, and also do not need to be filled.

#### void lily_list_take(lily_state *s, lily_container_val *con, int index)

Take the element at 'index' out of the `List`. Elements after 'index' are moved
down.

#### void lily_list_push(lily_container_val *con, lily_value *value)

Add an element to the end of the `List`. Equivalent to `List.push`.

### Using String values

The interpreter requires that all `String` values passed in are both
\0 terminated and valid utf-8. The api is similar to that of `ByteString`, but
the size is not important.

#### char *lily_string_raw(lily_string_val *string_val)

Returns the raw buffer behind the `String` value.

#### int lily_string_length(lily_string_val *string_val)

Returns the size (in bytes) of the `String`.

### Argument fetching

These functions take an index and return the raw value of that index, starting
from the bottom of the stack. These functions are typically used to fetch
function arguments, hence the name.

As with other functions, index 0 is the first argument passed.

#### int                  lily_arg_boolean   (lily_state *s, int index)

#### uint8_t              lily_arg_byte      (lily_state *s, int index)

#### lily_bytestring_val *lily_arg_bytestring(lily_state *s, int index)

#### lily_container_val * lily_arg_container (lily_state *s, int index)

#### double               lily_arg_double    (lily_state *s, int index)

#### lily_file_val *      lily_arg_file      (lily_state *s, int index)

#### lily_function_val *  lily_arg_function  (lily_state *s, int index)

#### lily_generic_val *   lily_arg_generic   (lily_state *s, int index)

This is used by the ARG_ macros that bind generation creates, so that custom
classes can be fetched.

#### lily_hash_val *      lily_arg_hash      (lily_state *s, int index)

#### int64_t              lily_arg_integer   (lily_state *s, int index)

#### lily_string_val *    lily_arg_string    (lily_state *s, int index)

#### char *               lily_arg_string_raw(lily_state *s, int index)

#### lily_value *         lily_arg_value     (lily_state *s, int index)

### Argument checking

#### int lily_arg_count(lily_state *s)

How many arguments the foreign function was passed. This can be used for
implementing optional arguments, as foreign functions are responsible for
implementing their optional arguments.

Note: Vararg functions place any extra arguments into a `List`, instead of
having extra args on the stack.

#### int lily_arg_isa(lily_state *s, int index, uint16_t class_id)

Check if the argument at 'index' has a given class id. Make sure to use variant
ids when testing enum values. Also, since this is a strict identity test, it
will fail if the value is a subclass of the class specified by 'class_id'.
Returns 1 on success, 0 on failure.

#### define lily_arg_is_failure(s, index) lily_arg_isa(s, index, LILY_ID_FAILURE)

#### define lily_arg_is_none(s, index) lily_arg_isa(s, index, LILY_ID_NONE)

#### define lily_arg_is_some(s, index) lily_arg_isa(s, index, LILY_ID_SOME)

#### define lily_arg_is_success(s, index) lily_arg_isa(s, index, LILY_ID_SUCCESS)

### Pushing values

These push a value onto the stack, returning the value if it is to be filled.
Callers must not let control return to the interpreter with incomplete
containers. For example, if a `Tuple` of size 3 is requested, then all 3 slots
must be filled before the interpreter uses it. This is because the interpreter
does not allow for nil or missing values.

An exception to this rule is when a `List` has extra space reserved.

#### void                lily_push_boolean      (lily_state *s, int value)

#### void                lily_push_byte         (lily_state *s, uint8_t value)

#### void                lily_push_bytestring   (lily_state *s, const char *source, int size)

#### void                lily_push_double       (lily_state *s, double value)

#### lily_container_val *lily_push_dynamic      (lily_state *s)

#### void                lily_push_empty_variant(lily_state *s, uint16_t class_id)

#### void                lily_push_file         (lily_state *s, FILE *f, const char *mode)

'f' must already be open. 'mode' is the mode that was passed to open it.

#### lily_foreign_val *  lily_push_foreign      (lily_state *s, uint16_t class_id, lily_destroy_func, size_t)

This is used by the macros that `bindgen.lily` creates.

#### lily_hash_val *     lily_push_hash         (lily_state *s, int size)

#### lily_container_val *lily_push_instance     (lily_state *s, uint16_t class_id, uint32_t size)

#### void                lily_push_integer      (lily_state *s, int64_t value)

#### lily_container_val *lily_push_list         (lily_state *s, uint32_t size)

#### void                lily_push_string       (lily_state *s, const char *source)

#### void                lily_push_string_sized (lily_state *s, const char *source, int size)

#### lily_container_val *lily_push_super        (lily_state *s, uint16_t class_id, uint32_t size)

This is used by foreign functions that act as a constructor. This function will
determine if inside an existing superclass container can be used. A value is
always pushed to the stack (either the existing container, or a new one). The
caller should then set the values that are declared in that superclass alone.

This function will mark the class in question as done being constructed. The
caller should then use `lily_con_set` to assign the fields of the instance that
were declared in the class. The caller should finish by calling
`lily_return_super`.

#### lily_container_val *lily_push_tuple        (lily_state *s, uint32_t size)

#### void                lily_push_unit         (lily_state *s)

#### void                lily_push_value        (lily_state *s, lily_value *value)

#### lily_container_val *lily_push_variant      (lily_state *s, uint16_t class_id, uint32_t size)

#### define lily_push_failure(s) lily_push_variant(s, LILY_ID_FAILURE, 1)

#### define lily_push_none(s) lily_push_empty_variant(s, LILY_ID_NONE)

#### define lily_push_some(s) lily_push_variant(s, LILY_ID_SOME, 1)

#### define lily_push_success(s) lily_push_variant(s, LILY_ID_SUCCESS, 1)

### Returning a value

Lily requires all functions to return a value. If a function has nothing useful
to return, it must at least return `Unit`. Most foreign functions will finish
with the value they are to return at the top of the stack, and can use
`lily_return_top` to send it back.

#### void lily_return_boolean(lily_state *s, int value)

#### void lily_return_byte   (lily_state *s, uint8_t value)

#### void lily_return_double (lily_state *s, double value)

#### void lily_return_integer(lily_state *s, int64_t value)

#### void lily_return_none   (lily_state *s)

#### void lily_return_super  (lily_state *s)

This is for use with `lily_push_super`.

#### void lily_return_top    (lily_state *s)

#### void lily_return_unit   (lily_state *s)

#### void lily_return_value  (lily_state *s, lily_value *value)

### Value extraction

These functions handle extracting the content of a value. No safety checking is
performed.

#### int                  lily_as_boolean   (lily_value *value)

#### uint8_t              lily_as_byte      (lily_value *value)

#### lily_bytestring_val *lily_as_bytestring(lily_value *value)

#### lily_container_val * lily_as_container (lily_value *value)

#### double               lily_as_double    (lily_value *value)

#### lily_file_val *      lily_as_file      (lily_value *value)

#### lily_function_val *  lily_as_function  (lily_value *value)

#### lily_generic_val *   lily_as_generic   (lily_value *value)

#### lily_hash_val *      lily_as_hash      (lily_value *value)

#### int64_t              lily_as_integer   (lily_value *value)

#### lily_string_val *    lily_as_string    (lily_value *value)

#### char *               lily_as_string_raw(lily_value *value)

### Calling the interpreter

Calling back into the interpreter begins with `lily_call_prepare`. That function
reserves a slot for a call. Once the reservation has been made, a call can be
done as many time as needed. Each time, the result of the call will drop back
into the slot reserved.

It is permissible to call into the interpreter either from outside of it, or
within a foreign function. However, the embedder should not use these functions
inside of a hook or during a dynaload, as the interpreter is not fully prepared.

#### void lily_call(lily_state *s, int count)

This takes 'count' arguments off of the stack and uses them for a call to the
last function prepared. If calling a function that requires variable arguments,
be sure to send a `List` of the extra arguments.

If the call raises an exception, error callbacks registered will be fired.
Following that, the exception will pass through the foreign function. However,
if this function is used outside of the interpreter, an exception being raised
will cause a crash.

#### void lily_call_prepare(lily_state *s, lily_function_val *func)

Prepare the function in question to be called. This includes reserving a slot
for the return value. Since reserving a slot pushes a value onto the stack, this
must be done before any arguments are pushed.

#### lily_value *lily_call_result(lily_state *s)

Return the slot that `lily_call_prepare` reserved. The contents are subject to
change from the result of a `lily_call`. However, the value itself will remain
stationary. Because of that, the embedder only needs to use this once per
`lily_call_prepare`.

### Exception raise

The various exception raising functions raise a particular kind of error. It
may be permissible in the future to raise a custom exception.

The format and arguments these functions take is passed to `lily_mb_add_fmt`.
See that function for argument flag options.

#### void lily_DivisionByZeroError(lily_state *s, const char *format, ...)

#### void lily_IndexError(lily_state *s, const char *format, ...)

#### void lily_IOError(lily_state *s, const char *format, ...)

#### void lily_KeyError(lily_state *s, const char *format, ...)

#### void lily_RuntimeError(lily_state *s, const char *format, ...)

#### void lily_ValueError(lily_state *s, const char *format, ...)

### Error callbacks

Some functions need to do cleanup in the event of an exception. One example is
`Hash.each_pair`. Upon entry, the `Hash` is marked as having one more iterator.
That mark prevents the `Hash` in question from being modified during the
iteration. `Hash.each_pair` pushes an error callback that drops the iterator
count back down.

Pushing an error callback must only be done inside of a foreign function. The
foreign function is responsible for popping the error callback in the event that
no exception is raised (typically before calling to return a value).

An error callback is invoked when an exception occurs. During it, the stack is
the same as when the callback was pushed. It is therefore permissible to use
arg functions to inspect values. Once the error callback is done, the exception
will continue until caught or another error handler is seen.

#### typedef void (*lily_error_callback_func)(lily_state *s)

#### void lily_error_callback_push(lily_state *s, lily_error_callback_func callback_fn)

Push an error callback. A foreign function should only need to register one.

#### void lily_error_callback_pop(lily_state *s)

Pop the last error callback pushed.

### Stack top operations

These are a pair of miscellaneous operations that operate on the top of the
stack.

#### void lily_stack_drop_top(lily_state *s)

This pops the value on the top of the stack and destroys it. Foreign functions
don't need to drop values they push onto the stack. This is for situations such
as with `Hash.select`. The original pairs of select need to be stored on the
stack so that a new `Hash` isn't built with altered values. To do that,
`Hash.select` pushes the values onto the stack, popping them off if the select
returns 0.

#### lily_value *lily_stack_get_top(lily_state *s)

Returns the value at the top of the stack.

### Message buffer operations

The message buffer (msgbuf) is used for storing messages. It automatically grows
to accommodate new content inserted. The interpreter provides a shared msgbuf
for various tasks (`lily_msgbuf_get`).

Functions that add to the msgbuf will always include a \0 terminator at the end.
Message buffers keep track of the length of what has been sent, but do not keep
track of valid utf-8 status. The latter is because msgbuf can be used to build
`ByteString` or `String` values.

#### lily_msgbuf *lily_new_msgbuf(uint32_t size)

Create a new msgbuf with 'size' starting size (should be a power of 2). Message
buffers live entirely outside of the vm, so the caller is responsible for
destroying the msgbuf.

#### void lily_free_msgbuf(lily_msgbuf *msgbuf)

Free a given msgbuf.

#### void lily_mb_add(lily_msgbuf *msgbuf, const char *source)

Add 'source' to the msgbuf.

#### void lily_mb_add_char(lily_msgbuf *msgbuf, char ch)

Add a single char to the msgbuf.

#### void lily_mb_add_fmt(lily_msgbuf *msgbuf, const char *format, ...)

This adds the arguments passed, using 'format' as a formatting string. The
following format specifiers are allowed:

* `%s`: A `const char *`.
* `%d`: An int value. Not for use with `Integer` values which are int64_t.
* `%p`: A `void *`.
* `%ld`: An int64_t value for `Integer` values.
* `%%`: One `%` sign.
* `^T`: A type. This is means for internal use, and is subject to change.

This function does not include support for widths, padding, etc. Such support
will be added based on need.

#### void lily_mb_add_fmt_va(lily_msgbuf *msgbuf, const char *format, va_list)

Wraps over `lily_mb_add_fmt` for when the caller has a va_list.

#### void lily_mb_add_slice(lily_msgbuf *msgbuf, const char *source, int start, int end)

Add 'source' from between 'start' and 'end'.

#### void lily_mb_add_value(lily_msgbuf *msgbuf, lily_state *s, lily_value *value)

Add 'value' into the msgbuf using the state 's' for class information. The state
given must be the same that created the value.

#### lily_msgbuf *lily_mb_flush(lily_msgbuf *msgbuf)

Resets the msgbuf's internals to be an empty \0 terminated string.

#### const char *lily_mb_raw(lily_msgbuf *msgbuf)

Returns the raw buffer behind the msgbuf. The embedder should not modify or
free the buffer.

#### int lily_mb_pos(lily_msgbuf *msgbuf)

Returns the number of characters inside of the msgbuf.

#### const char *lily_mb_html_escape(lily_msgbuf *msgbuf, const char *input)

This writes 'input' into the msgbuf, but with `&<>` being escaped into html
(`&amp;`, etc.).

#### const char *lily_mb_sprintf(lily_msgbuf *msgbuf, const char *format, ...)

This is equivalent to flushing the msgbuf, writing into it with
`lily_mb_add_fmt`, and returning the raw buffer.

#### lily_msgbuf *lily_msgbuf_get(lily_state *)

Returns the interpreter's shared message buffer. This should only be used in
cases where the buffer size is known to be reasonable. It is important that the
embedder not call into the interpreter with partial data in the msgbuf, as the
data may be erased.

This function flushes the buffer before handing it over. As a result, the
embedder will not need to worry about flushing the data they've placed in it
when they are done.

### Miscellaneous

#### lily_function_val *lily_find_function(lily_state *s, const char *name)

Search for a function called 'name'. The search is done as if 'name' was written
in the first module loaded. On success, returns a valid function for use with
`lily_call`. On failure, NULL is returned.

#### void lily_module_register(lily_state *s, const char *name, const char **dynaload_table, void *loader)

This makes a module called 'name' available to the interpreter. The module is
available anywhere, but still must be explicitly loaded through `import`. This
should be called before any parse/render function.

#### int lily_is_valid_utf8(const char *source)

Returns 1 if a given string is valid utf-8, 0 otherwise. 'source' must be
non-NULL and \0 terminated.

#### void lily_value_tag(lily_state *s, lily_value *value)

Place a gc tag onto 'value'. If there are too many objects with gc tag already,
the gc will be invoked beforehand.

#### uint16_t lily_cid_at(lily_state *s, int index)

Only autogenerated ID_ macros should call this. It's for getting class ids
during dynaload and foreign function execution.

#### define LILY_FOREIGN_HEADER

This header is for use with foreign classes. Binding generation will put this
at the top of structs that are later to be passed as foreign values to the
interpreter. The embedder should not modify the contents the header defines, or
make assumptions about the layout within.

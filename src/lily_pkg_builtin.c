#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "lily_parser.h"
#include "lily_symtab.h"
#include "lily_utf8.h"
#include "lily_move.h"
#include "lily_value_flags.h"

#include "lily_api_alloc.h"
#include "lily_api_embed.h"
#include "lily_api_value.h"

/* When destroying a value with a gc tag, set the tag to this to prevent destroy
   from reentering it. The values are useless, but cannot be 0 or this will be
   optimized as a NULL pointer. */
const lily_gc_entry lily_gc_stopper =
{
    1,
    1,
    {.integer = 1},
    NULL
};

/* This represents a return type of "self", which is special-cased in function
   returns. */
static const lily_class raw_self =
{
    NULL,
    ITEM_TYPE_CLASS,
    0,
    SYM_CLASS_SELF,
    0,
    (lily_type *)&raw_self,
    "self",
    0,
    NULL,
    NULL,
    NULL,
    0,
    0,
    {0},
    0,
    NULL,
    NULL,
};

const lily_class *lily_self_class = &raw_self;

/* Similar to the above, this is the read-only class+type of Unit. */
static const lily_class raw_unit =
{
    NULL,
    ITEM_TYPE_CLASS,
    0,
    SYM_CLASS_UNIT,
    0,
    (lily_type *)&raw_unit,
    "Unit",
    0,
    NULL,
    NULL,
    NULL,
    0,
    0,
    {0},
    0,
    NULL,
    NULL,
};

const lily_type *lily_unit_type = (lily_type *)&raw_unit;

/**
embedded builtin

The builtin package provides the classes, vars, and functions that form the
foundation of Lily.
*/

/**
var stdin: File

Provides a wrapper around the 'stdin' present within C.
*/

/**
var stderr: File

Provides a wrapper around the 'stderr' present within C.
*/

/**
var stdout: File

Provides a wrapper around the 'stdin' present within C. The builtin function
'print' relies on this, and thus might raise `IOError` if 'stdout' is closed or
set to a read-only stream.
*/

/**
define print[A](value: A)

Attempt to write 'value' to 'stdout', including a terminating newline.

Errors:

Raises `IOError` if 'stdout' is closed or not open for writing.
*/

/**
define calltrace: List[String]

Returns a `List` with one `String` for each function that is currently entered.
*/

/**
class Boolean

The `Boolean` class represents a value that is either 'true' or 'false'.
*/

/**
method Boolean.to_i(self: Boolean): Integer

Convert a `Boolean` to an `Integer`. 'true' becomes '1', 'false' becomes '0'.
*/
void lily_builtin_Boolean_to_i(lily_state *s)
{
    lily_return_integer(s, lily_arg_boolean(s, 0));
}

/**
method Boolean.to_s(self: Boolean): String

Convert a `Boolean` to a `String`.
*/
void lily_builtin_Boolean_to_s(lily_state *s)
{
    int input = lily_arg_boolean(s, 0);
    char *to_copy;

    if (input == 0)
        to_copy = "false";
    else
        to_copy = "true";

    lily_return_string(s, lily_new_raw_string(to_copy));
}

/**
class ByteString

The `ByteString` class represents a bag of bytes. A `ByteString` may have '\0'
values embedded within it. It may also have data that is not valid as utf-8.
The `ByteString` class currently does not support any primitive operations.
*/

/**
method ByteString.encode(self: ByteString, encode: *String="error"): Option[String]

Attempt to transform the given `ByteString` into a `String`. The action taken
depends on the value of 'encode'.

If encode is '"error"', then invalid utf-8 or embedded '\0' values within `self`
will result in 'None'.
*/
void lily_builtin_ByteString_encode(lily_state *s)
{
    lily_string_val *input_bytestring = lily_arg_string(s, 0);
    const char *encode_method;

    if (lily_arg_count(s) == 2)
        encode_method = lily_arg_string_raw(s, 1);
    else
        encode_method = "error";

    char *byte_buffer = NULL;

    if (strcmp(encode_method, "error") == 0) {
        byte_buffer = lily_bytestring_get_raw(input_bytestring);
        int byte_buffer_size = lily_bytestring_length(input_bytestring);

        if (lily_is_valid_sized_utf8(byte_buffer, byte_buffer_size) == 0) {
            lily_return_empty_variant(s, lily_get_none(s));
            return;
        }
    }
    else {
        lily_return_empty_variant(s, lily_get_none(s));
        return;
    }

    lily_instance_val *variant = lily_new_some();
    lily_variant_set_string(variant, 0, lily_new_raw_string(byte_buffer));
    lily_return_filled_variant(s, variant);
}

/**
bootstrap DivisionByZeroError(msg: String) < Exception(msg) {}

The `DivisionByZeroError` is a subclass of `Exception` that is raised when
trying to divide or modulo by zero.
*/

/**
class Double

The `Double` class exists as a wrapper over a C double.
*/

/**
method Double.to_i(self: Double): Integer

Convert a `Double` to an `Integer`. This is done internally through a cast from
a C double, to int64_t, the type of `Integer`.
*/
void lily_builtin_Double_to_i(lily_state *s)
{
    int64_t integer_val = (int64_t)lily_arg_double(s, 0);

    lily_return_integer(s, integer_val);
}

/**
class Dynamic


The `Dynamic` class allows defering type checking until runtime. Creation of
`Dynamic` is done through 'Dynamic(<value>)'. Extraction of values is done
through a cast: '.@(type)'. The result of a cast is `Option[<type>]`, with
'Some' on success and 'None' on failure. Finally, casts are not allowed to hold
polymorphic types, such as `List` or `Hash` or `Function`, because Lily's vm
only holds class information at runtime.
*/

/**
constructor Dynamic[A](self: A): Dynamic

Constructs a new `Dynamic` value.

While it is currently possible to place polymorphic values into `Dynamic`, that
ability will be removed in the future, as it is not possible to cast polymorphic
values out of a `Dynamic`.
*/
extern void lily_builtin_Dynamic_new(lily_state *);

/**
enum Either[A, B]
    Left(A)
    Right(B)

The `Either` enum can be usd to represent an operation that may or may not
succeed. Unlike `Option`, `Either` provides `Left` which may be used to hold a
useful error message in the event of a failure.
*/
static void either_is_left_right(lily_state *s, int expect)
{
    lily_instance_val *iv = lily_arg_instance(s, 0);

    lily_return_boolean(s, (iv->instance_id == expect));
}

/**
method Either.is_left[A, B](self: Either[A, B]): Boolean

Return 'true' if 'self' contains a 'Left', 'false' otherwise.
*/
void lily_builtin_Either_is_left(lily_state *s)
{
    either_is_left_right(s, SYM_CLASS_LEFT);
}

/**
method Either.is_right[A, B](self: Either[A, B]): Boolean

Return 'true' if 'self' contains a 'Right', 'false' otherwise.
*/
void lily_builtin_Either_is_right(lily_state *s)
{
    either_is_left_right(s, SYM_CLASS_RIGHT);
}

static void either_optionize_left_right(lily_state *s, int expect)
{
    lily_instance_val *iv = lily_arg_instance(s, 0);

    if (iv->instance_id == expect) {
        lily_instance_val *variant = lily_new_some();
        lily_variant_set_value(variant, 0, lily_instance_value(iv, 0));
        lily_return_filled_variant(s, variant);
    }
    else
        lily_return_empty_variant(s, lily_get_none(s));
}

/**
method Either.left[A, B](self: Either[A, B]):Option[A]

If 'self' contains a 'Left', produces a 'Some(A)'.

If 'self' contains a 'Right', produces 'None'.
*/
void lily_builtin_Either_left(lily_state *s)
{
    either_optionize_left_right(s, SYM_CLASS_LEFT);
}

/**
method Either.right[A, B](self: Either[A, B]): Option[B]

If 'self' contains a 'Left', produces a 'None'.

If 'self' contains a 'Right', produces 'Right(B)'.
*/
void lily_builtin_Either_right(lily_state *s)
{
    either_optionize_left_right(s, SYM_CLASS_RIGHT);
}

/**
bootstrap Exception(m:String){ var @message = m var @traceback: List[String] = [] }

The `Exception` class is the base class of all exceptions. It defines two
properties: A 'message' as `String`, and a 'traceback' as `List[String]`. The
'traceback' field is rewritten whenever an exception instance is raised.
*/

/**
class File

The `File` class provides a wrapper over a C FILE * struct. A `File` is closed
automatically when a scope exits (though not immediately). However, it is also
possible to manually close a `File`.
*/

/**
method File.close(self: File)

Close 'self' if it is open, or do nothing if already closed.

For standard streams, this marks the `File` as closed, but does not actually
close the stream. Embedders, therefore, do not need to worry about standard
streams being altered or closed by Lily.
*/
void lily_builtin_File_close(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);

    if (filev->inner_file != NULL) {
        if (filev->is_builtin == 0)
            fclose(filev->inner_file);
        filev->inner_file = NULL;
    }

    lily_return_unit(s);
}

/**
method File.each_line(self: File, fn: Function(ByteString))

Read each line of text from 'self', passing it down to 'fn' for processing.

Errors:

If 'self' is not open for reading, or is closed, `IOError` is raised.
*/
void lily_builtin_File_each_line(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    lily_msgbuf *vm_buffer = lily_get_msgbuf(s);
    char read_buffer[128];
    int ch = 0, pos = 0;

    lily_file_ensure_readable(s, filev);
    FILE *f = filev->inner_file;

    lily_prepare_call(s, lily_arg_function(s, 1));

    /* This uses fgetc in a loop because fgets may read in \0's, but doesn't
       tell how much was written. */
    while (1) {
        ch = fgetc(f);

        if (ch == EOF)
            break;

        if (pos == sizeof(read_buffer)) {
            lily_mb_add_range(vm_buffer, read_buffer, 0, sizeof(read_buffer));
            pos = 0;
        }

        read_buffer[pos] = (char)ch;

        /* \r is intentionally not checked for, because it's been a very, very
           long time since any os used \r alone for newlines. */
        if (ch == '\n') {
            if (pos != 0) {
                lily_mb_add_range(vm_buffer, read_buffer, 0, pos);
                pos = 0;
            }

            const char *text = lily_mb_get(vm_buffer);

            lily_push_bytestring(s, lily_new_raw_string(text));
            lily_exec_prepared(s, 1);
            lily_mb_flush(vm_buffer);
        }
        else
            pos++;
    }

    lily_return_unit(s);
}

/**
method File.open(path: String, mode: String):File

Attempt to open 'path' using the 'mode' given. 'mode' may be one of the
following:

'"r"' (readonly, must exist)

'"w"' (writeonly)

'"a"' (append, create if not exist)

'"r+"' (read+write, must exist)

'"w+"' (read+write, creates an empty file if needed)

'"a+"' (read+append)

Errors:

If unable to open 'path', or an invalid 'mode' is provided, `IOError` is raised.
*/
void lily_builtin_File_open(lily_state *s)
{
    char *path = lily_arg_string_raw(s, 0);
    char *mode = lily_arg_string_raw(s, 1);

    errno = 0;
    int ok;

    {
        char *mode_ch = mode;
        if (*mode_ch == 'r' || *mode_ch == 'w' || *mode_ch == 'a') {
            mode_ch++;
            if (*mode_ch == 'b')
                mode_ch++;

            if (*mode_ch == '+')
                mode_ch++;

            ok = (*mode_ch == '\0');
        }
        else
            ok = 0;
    }

    if (ok == 0)
        lily_error_fmt(s, SYM_CLASS_IOERROR,
                "Invalid mode '%s' given.", mode);

    FILE *f = fopen(path, mode);
    if (f == NULL) {
        lily_error_fmt(s, SYM_CLASS_IOERROR, "Errno %d: ^R (%s).",
                errno, errno, path);
    }

    lily_file_val *filev = lily_new_file_val(f, mode);

    lily_return_file(s, filev);
}

void lily_builtin_File_write(lily_state *);

/**
method File.print[A](self: File, data: A)

Attempt to write the contents of 'data' to the file provided. 'data' is written
with a newline at the end.

Errors:

If 'self' is closed or is not open for writing, `IOError` is raised.
*/
void lily_builtin_File_print(lily_state *s)
{
    lily_builtin_File_write(s);
    fputc('\n', lily_arg_file_raw(s, 0));
    lily_return_unit(s);
}

/**
method File.read_line(self: File): ByteString

Attempt to read a line of text from 'self'. Currently, this function does not
have a way to signal that the end of the file has been reached. For now, callers
should check the result against 'B""'. This will be fixed in a future release.

Errors:

If 'self' is not open for reading, or is closed, `IOError` is raised.
*/
void lily_builtin_File_read_line(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    lily_msgbuf *vm_buffer = lily_get_msgbuf(s);
    char read_buffer[128];
    int ch = 0, pos = 0, total_pos = 0;

    lily_file_ensure_readable(s, filev);
    FILE *f = filev->inner_file;

    /* This uses fgetc in a loop because fgets may read in \0's, but doesn't
       tell how much was written. */
    while (1) {
        ch = fgetc(f);

        if (ch == EOF)
            break;

        if (pos == sizeof(read_buffer)) {
            lily_mb_add_range(vm_buffer, read_buffer, 0, sizeof(read_buffer));
            total_pos += pos;
            pos = 0;
        }

        read_buffer[pos] = (char)ch;
        pos++;

        /* \r is intentionally not checked for, because it's been a very, very
           long time since any os used \r alone for newlines. */
        if (ch == '\n')
            break;
    }

    if (pos != 0) {
        lily_mb_add_range(vm_buffer, read_buffer, 0, pos);
        total_pos += pos;
    }

    const char *text = lily_mb_get(vm_buffer);
    lily_return_bytestring(s, lily_new_raw_string_sized(text, total_pos));
}

/**
method File.write[A](self: File, data: A)

Attempt to write the contents of 'data' to the file provided.

Errors:

If 'self' is closed or is not open for writing, `IOError` is raised.
*/
void lily_builtin_File_write(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    lily_value *to_write = lily_arg_value(s, 1);

    lily_file_ensure_writeable(s, filev);

    if (to_write->flags & VAL_IS_STRING)
        fputs(to_write->value.string->string, filev->inner_file);
    else {
        lily_msgbuf *msgbuf = s->vm_buffer;
        lily_mb_flush(msgbuf);
        lily_mb_add_value(msgbuf, s, to_write);
        fputs(lily_mb_get(msgbuf), filev->inner_file);
    }

    lily_return_unit(s);
}

/**
class Function

The `Function` class represents a block of code to be called, which may or may
not produce a value. `Function` values are first-class, and can be passed around
as arguments, placed into a `List`, and so on.

The arguments of a `Function` are denoted within parentheses, with an optional
colon at the end to denote the value returned:

`Function(Integer): String` (return `String`).

`Function(String, String)` (no value returned).
*/

/**
class Hash

The `Hash` class provides a mapping between a key and a value. `Hash` values can
be created through '[key1 => value1, key2 => value2, ...]'. When writing a
`Hash`, the key is the first type, and the value is the second.

'[1 => "a", 2 => "b", 3 => "c"]' would therefore be written as
`Hash[Integer, String]`.

Currently, only `Integer` and `String` can be used as keys.
*/

/* Attempt to find 'key' within 'hash_val'. If an element is found, then it is
   returned. If no element is found, then NULL is returned. */
lily_hash_elem *lily_hash_get_elem(lily_state *s, lily_hash_val *hash_val,
        lily_value *key)
{
    uint64_t key_siphash = lily_siphash(s, key);
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    lily_raw_value key_value = key->value;
    int flags = key->flags;
    int ok = 0;

    while (elem_iter) {
        if (elem_iter->key_siphash == key_siphash) {
            lily_raw_value iter_value = elem_iter->elem_key->value;

            if (flags & VAL_IS_INTEGER &&
                iter_value.integer == key_value.integer)
                ok = 1;
            else if (flags & VAL_IS_STRING &&
                    /* strings are immutable, so try a ptr compare first. */
                    ((iter_value.string == key_value.string) ||
                     /* No? Make sure the sizes match, then call for a strcmp.
                        The size check is an easy way to potentially skip a
                        strcmp in case of hash collision. */
                      (iter_value.string->size == key_value.string->size &&
                       strcmp(iter_value.string->string,
                              key_value.string->string) == 0)))
                ok = 1;
            else
                ok = 0;

            if (ok)
                break;
        }
        elem_iter = elem_iter->next;
    }

    return elem_iter;
}

static inline void remove_key_check(lily_state *s, lily_hash_val *hash_val)
{
    if (hash_val->iter_count)
        lily_error(s, SYM_CLASS_RUNTIMEERROR,
                "Cannot remove key from hash during iteration.");
}

/* This adds a new element to the hash, with 'pair_key' and 'pair_value' inside.
   The key and value are not given a refbump, and are not copied over. For that,
   see lily_hash_add_unique. */
static void hash_add_unique_nocopy(lily_state *s, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
    lily_hash_elem *elem = lily_malloc(sizeof(lily_hash_elem));

    elem->key_siphash = lily_siphash(s, pair_key);
    elem->elem_key = pair_key;
    elem->elem_value = pair_value;

    if (hash_val->elem_chain)
        hash_val->elem_chain->prev = elem;

    elem->prev = NULL;
    elem->next = hash_val->elem_chain;
    hash_val->elem_chain = elem;

    hash_val->num_elems++;
}

/* This function will add an element to the hash with 'pair_key' as the key and
   'pair_value' as the value. This should only be used in cases where the
   caller is completely certain that 'pair_key' is not within the hash. If the
   caller is unsure, then lily_hash_set_elem should be used instead. */
void lily_hash_add_unique(lily_state *s, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
    remove_key_check(s, hash_val);

    pair_key = lily_copy_value(pair_key);
    pair_value = lily_copy_value(pair_value);

    hash_add_unique_nocopy(s, hash_val, pair_key, pair_value);
}

/* This attempts to find 'pair_key' within 'hash_val'. If successful, then the
   element's value is assigned to 'pair_value'. If unable to find an element, a
   new element is created using 'pair_key' and 'pair_value'. */
void lily_hash_set_elem(lily_state *s, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
    lily_hash_elem *elem = lily_hash_get_elem(s, hash_val, pair_key);
    if (elem == NULL)
        lily_hash_add_unique(s, hash_val, pair_key, pair_value);
    else
        lily_assign_value(elem->elem_value, pair_value);
}

static void destroy_elem(lily_hash_elem *elem)
{
    lily_deref(elem->elem_key);
    lily_free(elem->elem_key);

    lily_deref(elem->elem_value);
    lily_free(elem->elem_value);

    lily_free(elem);
}

static void destroy_hash_elems(lily_hash_val *hash_val)
{
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    lily_hash_elem *elem_next;

    while (elem_iter) {
        elem_next = elem_iter->next;

        destroy_elem(elem_iter);

        elem_iter = elem_next;
    }
}

void lily_destroy_hash(lily_value *v)
{
    lily_hash_val *hv = v->value.hash;

    destroy_hash_elems(hv);

    lily_free(hv);
}

/**
method Hash.clear[A, B](self: Hash[A, B])

Removes all pairs currently present within 'self'. No error occurs if 'self' is
currently being iterated over.
*/
void lily_builtin_Hash_clear(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    if (hash_val->iter_count != 0)
        lily_error(s, SYM_CLASS_RUNTIMEERROR,
                "Cannot remove key from hash during iteration.");

    destroy_hash_elems(hash_val);

    hash_val->elem_chain = NULL;
    hash_val->num_elems = 0;

    lily_return_unit(s);
}

/**
method Hash.delete[A, B](self: Hash[A, B], key: A)

Attempt to remove 'key' from 'self'. If 'key' is not present within 'self', then
nothing happens.

Errors:

If 'self' is currently being iterated over, `RuntimeError` will be raised.
*/
void lily_builtin_Hash_delete(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_value *key = lily_arg_value(s, 1);

    remove_key_check(s, hash_val);

    lily_hash_elem *hash_elem = lily_hash_get_elem(s, hash_val, key);

    if (hash_elem) {
        if (hash_elem->next)
            hash_elem->next->prev = hash_elem->prev;

        if (hash_elem->prev)
            hash_elem->prev->next = hash_elem->next;

        if (hash_elem == hash_val->elem_chain)
            hash_val->elem_chain = hash_elem->next;

        destroy_elem(hash_elem);
        hash_val->num_elems--;
    }

    lily_return_unit(s);
}

/**
method Hash.each_pair[A, B](self: Hash[A, B], fn: Function(A, B))

Iterate through each pair that is present within 'self'. For each of the pairs,
call 'fn' with the key and value of each pair.
*/
void lily_builtin_Hash_each_pair(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_hash_elem *elem_iter = hash_val->elem_chain;

    lily_prepare_call(s, lily_arg_function(s, 1));

    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(s->raiser);
    if (setjmp(link->jump) == 0) {
        while (elem_iter) {
            lily_push_value(s, elem_iter->elem_key);
            lily_push_value(s, elem_iter->elem_value);
            lily_exec_prepared(s, 2);

            elem_iter = elem_iter->next;
        }

        hash_val->iter_count--;
        lily_release_jump(s->raiser);
    }
    else {
        hash_val->iter_count--;
        lily_jump_back(s->raiser);
    }
}

/**
method Hash.get[A, B](self: Hash[A, B], key: A, default: B): B

Attempt to find 'key' within 'self'. If 'key' is present, then the value
associated with it is returned. If 'key' cannot be found, then 'default' is
returned instead.
*/
void lily_builtin_Hash_get(lily_state *s)
{
    lily_value *input = lily_arg_value(s, 0);
    lily_value *key = lily_arg_value(s, 1);
    lily_value *default_value = lily_arg_value(s, 2);

    lily_hash_elem *hash_elem = lily_hash_get_elem(s, input->value.hash, key);
    lily_value *new_value = hash_elem ? hash_elem->elem_value : default_value;

    lily_return_value(s, new_value);
}

/**
method Hash.has_key[A, B](self: Hash[A, B], key: A):Boolean

Return 'true' if 'key' is present within 'self', 'false' otherwise.
*/
void lily_builtin_Hash_has_key(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_value *key = lily_arg_value(s, 1);

    lily_hash_elem *hash_elem = lily_hash_get_elem(s, hash_val, key);

    lily_return_boolean(s, hash_elem != NULL);
}

/**
method Hash.keys[A, B](self: Hash[A, B]): List[A]

Construct a `List` containing all values that are present within 'self'. There
is no guarantee of the ordering of the resulting `List`.
*/
void lily_builtin_Hash_keys(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_list_val *result_lv = lily_new_list_val_n(hash_val->num_elems);
    int i = 0;

    lily_hash_elem *elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        lily_assign_value(result_lv->elems[i], elem_iter->elem_key);

        i++;
        elem_iter = elem_iter->next;
    }

    lily_return_list(s, result_lv);
}

static lily_hash_val *build_hash(lily_state *s, int count)
{
    int i;
    lily_hash_val *hash_val = lily_new_hash_val();

    for (i = 0;i < count;i++) {
        lily_value *e_value = lily_copy_value(lily_pop_value(s));
        lily_value *e_key = lily_copy_value(lily_pop_value(s));

        hash_add_unique_nocopy(s, hash_val, e_key, e_value);
    }

    return hash_val;
}

/**
method Hash.map_values[A, B, C](self: Hash[A, B], Function(B => C)): Hash[A, C]

This iterates through 'self' and calls 'fn' for each element present. The result
of this function is a newly-made `Hash` where each value is the result of the
call to 'fn'.
*/
void lily_builtin_Hash_map_values(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_prepare_call(s, lily_arg_function(s, 1));
    lily_hash_elem *elem_iter = hash_val->elem_chain;

    int count = 0;

    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(s->raiser);

    if (setjmp(link->jump) == 0) {
        while (elem_iter) {
            lily_value *e_key = elem_iter->elem_key;
            lily_value *e_value = elem_iter->elem_value;

            lily_push_value(s, e_key);
            lily_push_value(s, e_value);

            lily_exec_prepared(s, 1);

            lily_push_value(s, lily_result_value(s));
            elem_iter = elem_iter->next;
            count++;
        }

        lily_hash_val *new_hash = build_hash(s, count);
        hash_val->iter_count--;
        lily_release_jump(s->raiser);
        lily_return_hash(s, new_hash);
    }
    else {
        hash_val->iter_count--;
        lily_jump_back(s->raiser);
    }
}

/**
method Hash.merge[A, B](self: Hash[A, B], others: Hash[A, B]...): Hash[A, B]

Create a new `Hash` that holds the result of 'self' and each `Hash present
within 'others'.

When duplicate elements are found, the value of the right-most `Hash` wins.
*/
void lily_builtin_Hash_merge(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_list_val *to_merge = lily_arg_list(s, 1);

    lily_hash_val *result_hash = lily_new_hash_val();

    /* The existing hash should be entirely unique, so just add the pairs in
       directly. */
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        lily_hash_add_unique(s, result_hash, elem_iter->elem_key,
                elem_iter->elem_value);

        elem_iter = elem_iter->next;
    }

    int i;
    for (i = 0;i < to_merge->num_values;i++) {
        lily_hash_val *merging_hash = to_merge->elems[i]->value.hash;
        elem_iter = merging_hash->elem_chain;
        while (elem_iter) {
            lily_hash_set_elem(s, result_hash, elem_iter->elem_key,
                    elem_iter->elem_value);

            elem_iter = elem_iter->next;
        }
    }

    lily_return_hash(s, result_hash);
}

static void hash_select_reject_common(lily_state *s, int expect)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_prepare_call(s, lily_arg_function(s, 1));
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    int count = 0;

    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(s->raiser);

    if (setjmp(link->jump) == 0) {
        while (elem_iter) {
            lily_value *e_key = elem_iter->elem_key;
            lily_value *e_value = elem_iter->elem_value;

            lily_push_value(s, e_key);
            lily_push_value(s, e_value);

            lily_push_value(s, e_key);
            lily_push_value(s, e_value);

            lily_exec_prepared(s, 2);
            if (lily_result_boolean(s) != expect) {
                lily_drop_value(s);
                lily_drop_value(s);
            }
            else
                count++;

            elem_iter = elem_iter->next;
        }

        lily_hash_val *new_hash = build_hash(s, count);
        hash_val->iter_count--;
        lily_release_jump(s->raiser);
        lily_return_hash(s, new_hash);
    }
    else {
        hash_val->iter_count--;
        lily_jump_back(s->raiser);
    }
}

/**
method Hash.reject[A, B](self: Hash[A, B], fn: Function(A, B => Boolean)): Hash[A, B]

This calls 'fn' for each element present within 'self'. The result of this
function is a newly-made `Hash` containing all values for which 'fn' returns
'false'.
*/
void lily_builtin_Hash_reject(lily_state *s)
{
    hash_select_reject_common(s, 0);
}

/**
method Hash.select[A, B](self: Hash[A, B], fn: Function(A, B => Boolean)): Hash[A, B]

This calls 'fn' for each element present within 'self'. The result of this
function is a newly-made `Hash` containing all values for which 'fn' returns
'true'.
*/
void lily_builtin_Hash_select(lily_state *s)
{
    hash_select_reject_common(s, 1);
}

/**
method Hash.size[A, B](self: Hash[A, B]): Integer

Returns the number of key+value pairs present within 'self'.
*/
void lily_builtin_Hash_size(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_return_integer(s, hash_val->num_elems);
}

/**
bootstrap IndexError(msg: String) < Exception(msg) {}

`IndexError` is a subclass of `Exception` that is raised when an out-of-bounds
access is performed on a `List`.
*/

/**
class Integer

The `Integer` class is Lily's native numeric type. Internally, it is a wrapper
over a C int64_t.
*/

/**
method Integer.to_bool(self: Integer): Boolean

Converts an `Integer` to a `Boolean`.
*/
void lily_builtin_Integer_to_bool(lily_state *s)
{
    /* Use !! or `x == true` will fail. */
    lily_return_boolean(s, !!lily_arg_integer(s, 0));
}

/**
method Integer.to_d(self: Integer): Double

Converts an `Integer` to a `Double`. Internally, this is done by a typecast to
the `Double` type (a raw C double).
*/
void lily_builtin_Integer_to_d(lily_state *s)
{
    double doubleval = (double)lily_arg_integer(s, 0);

    lily_return_double(s, doubleval);
}

/**
method Integer.to_s(self: Integer): String

Convert an `Integer` to a `String` using base-10.
*/
void lily_builtin_Integer_to_s(lily_state *s)
{
    int64_t integer_val = lily_arg_integer(s, 0);

    char buffer[32];
    snprintf(buffer, 32, "%"PRId64, integer_val);

    lily_return_string(s, lily_new_raw_string(buffer));
}

/**
bootstrap IOError(msg: String) < Exception(msg) {}

`IOError` is a subclass of `Exception` that is raised when an IO operation fails
or does not have permission.
*/

/**
bootstrap KeyError(msg: String) < Exception(msg) {}

`KeyError` is a subclass of `Exception` that is raised when trying to get an
item from a `Hash` that does not exist.
*/

/**
class List

The `List` class represents a container of a given type, written as
`List[<inner type>]`. A `List` value can be accessed through a positive index or
a negative one (with negative indexes being an offset from the end). Attempting
to access an invalid index will produce `IndexError`.
*/

/**
method List.clear[A](self: List[A])

Removes all elements present within 'self'. No error is raised if 'self' is
being iterated over.
*/
void lily_builtin_List_clear(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);
    int i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_deref(list_val->elems[i]);
        lily_free(list_val->elems[i]);
    }

    list_val->extra_space += list_val->num_values;
    list_val->num_values = 0;

    lily_return_unit(s);
}

/**
method List.count[A](self: List[A], fn: Function(A => Boolean)): Integer

This calls 'fn' for each element within 'self'. The result of this function is
the number of times that 'fn' returns 'true.
*/
void lily_builtin_List_count(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);
    lily_prepare_call(s, lily_arg_function(s, 1));
    int count = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_push_value(s, list_val->elems[i]);
        lily_exec_prepared(s, 1);

        if (lily_result_boolean(s) == 1)
            count++;
    }

    lily_return_integer(s, count);
}

/* This expands the list value so there's more extra space. Growth is done
   relative to the current size of the list, because why not? */
static void make_extra_space_in_list(lily_list_val *lv)
{
    /* There's probably room for improvement here, later on. */
    int extra = (lv->num_values + 8) >> 2;
    lv->elems = lily_realloc(lv->elems,
            (lv->num_values + extra) * sizeof(lily_value *));
    lv->extra_space = extra;
}

static int64_t get_relative_index(lily_state *s, lily_list_val *list_val,
        int64_t pos)
{
    if (pos < 0) {
        uint64_t unsigned_pos = -(int64_t)pos;
        if (unsigned_pos > list_val->num_values)
            lily_error_fmt(s, SYM_CLASS_INDEXERROR, "Index %d is too small for list (minimum: %d)",
                    pos, -(int64_t)list_val->num_values);

        pos = list_val->num_values - unsigned_pos;
    }
    else if (pos > list_val->num_values)
        lily_error_fmt(s, SYM_CLASS_INDEXERROR, "Index %d is too large for list (maximum: %d)",
                pos, list_val->num_values);

    return pos;
}

/**
method List.delete_at[A](self: List[A], index: Integer)

Attempts to remove index from the List. If index is negative, then it is
considered an offset from the end of the List.

Errors:

Raises `IndexError` if 'index' (after adjustment) is not a valid index.
*/
void lily_builtin_List_delete_at(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);
    int64_t pos = lily_arg_integer(s, 1);

    if (list_val->num_values == 0)
        lily_error(s, SYM_CLASS_INDEXERROR, "Cannot delete from an empty list.");

    pos = get_relative_index(s, list_val, pos);

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    lily_value *to_delete = list_val->elems[pos];
    lily_deref(to_delete);
    lily_free(to_delete);

    /* Shove everything leftward hide the hole from erasing the value. */
    if (pos != list_val->num_values)
        memmove(list_val->elems + pos, list_val->elems + pos + 1,
                (list_val->num_values - pos) * sizeof(lily_value *));

    list_val->num_values--;
    list_val->extra_space++;
}

/**
method List.each[A](self: List[A], fn: Function(A)): List[A]

Calls 'fn' for each element within 'self'. The result of this function is
'self', so that this method can be chained with others.
*/
void lily_builtin_List_each(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);
    lily_prepare_call(s, lily_arg_function(s, 1));
    int i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_push_value(s, list_val->elems[i]);
        lily_exec_prepared(s, 1);
    }

    lily_return_list(s, list_val);
}

/**
method List.each_index[A](self: List[A], fn: Function(Integer)): List[A]

Calls 'fn' for each element within 'self'. Rather than receive the elements of
'self', 'fn' instead receives the index of each element.
*/
void lily_builtin_List_each_index(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);
    lily_prepare_call(s, lily_arg_function(s, 1));

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_push_integer(s, i);
        lily_exec_prepared(s, 1);
    }

    lily_return_list(s, list_val);
}

/**
method List.fill[A](count: Integer, value: A): List[A]

This createa a new `List` that contains 'value' repeated 'count' times.

Errors:

Raises `ValueError` if 'count' is less than 1.
*/
void lily_builtin_List_fill(lily_state *s)
{
    int n = lily_arg_integer(s, 0);
    if (n < 0)
        lily_error_fmt(s, SYM_CLASS_VALUEERROR,
                "Repeat count must be >= 0 (%d given).", n);

    lily_value *to_repeat = lily_arg_value(s, 1);
    lily_list_val *lv = lily_new_list_val_n(n);

    int i;
    for (i = 0;i < n;i++)
        lily_assign_value(lv->elems[i], to_repeat);

    lily_return_list(s, lv);
}

/**
method List.fold[A](self: List[A], start: A, fn: Function(A, A => A)): A

This calls 'fn' for each element present within 'self'. The first value sent to
'fn' is initially 'start', but will later be the result of 'fn'. Therefore, the
value as it accumulates can be found in the first value sent to 'fn'.

The result of this function is the result of doing an accumulation on each
element within 'self'.
*/
void lily_builtin_List_fold(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);
    lily_value *start = lily_arg_value(s, 1);

    if (list_val->num_values == 0)
        lily_return_value(s, start);
    else {
        lily_value *v = NULL;

        lily_prepare_call(s, lily_arg_function(s, 2));
        lily_push_value(s, start);
        int i = 0;
        while (1) {
            lily_push_value(s, list_val->elems[i]);
            lily_exec_prepared(s, 2);
            v = lily_result_value(s);

            if (i == list_val->num_values - 1)
                break;

            lily_push_value(s, v);

            i++;
        }

        lily_return_value(s, v);
    }
}

/**
method List.insert[A](self: List[A], index: Integer, value: A)

Attempt to insert 'value' at 'index' within 'self'. If index is negative, then
it is treated as an offset from the end of 'self'.

Errors:

Raises `IndexError` if 'index' is not within 'self'.
*/
void lily_builtin_List_insert(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);
    int64_t insert_pos = lily_arg_integer(s, 1);
    lily_value *insert_value = lily_arg_value(s, 2);

    insert_pos = get_relative_index(s, list_val, insert_pos);

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    /* Shove everything rightward to make space for the new value. */
    if (insert_pos != list_val->num_values)
        memmove(list_val->elems + insert_pos + 1, list_val->elems + insert_pos,
                (list_val->num_values - insert_pos) * sizeof(lily_value *));

    list_val->elems[insert_pos] = lily_copy_value(insert_value);
    list_val->num_values++;
    list_val->extra_space--;

    lily_return_unit(s);
}

/**
method List.join[A](self: List[A], separator: *String=""): String

Create a `String` consisting of the elements of 'self' interleaved with
'separator'. The elements of self are converted to a `String` as if they were
interpolated. If 'self' is empty, then the result is an empty `String`.
*/
void lily_builtin_List_join(lily_state *s)
{
    lily_list_val *lv = lily_arg_list(s, 0);
    const char *delim = "";
    if (lily_arg_count(s) == 2)
        delim = lily_arg_string_raw(s, 1);

    lily_msgbuf *vm_buffer = s->vm_buffer;
    lily_mb_flush(vm_buffer);

    if (lv->num_values) {
        int i, stop = lv->num_values - 1;
        lily_value **values = lv->elems;
        for (i = 0;i < stop;i++) {
            lily_mb_add_value(vm_buffer, s, values[i]);
            lily_mb_add(vm_buffer, delim);
        }
        if (stop != -1)
            lily_mb_add_value(vm_buffer, s, values[i]);
    }

    lily_return_string(s, lily_new_raw_string(lily_mb_get(vm_buffer)));
}

/**
method List.map[A,B](self: List[A], fn: Function(A => B)): List[B]

This calls 'fn' on each element within 'self'. The result of this function is a
newly-made `List` containing the results of 'fn'.
*/
void lily_builtin_List_map(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);

    lily_prepare_call(s, lily_arg_function(s, 1));

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_value *e = list_val->elems[i];
        lily_push_value(s, e);
        lily_exec_prepared(s, 1);
        lily_push_value(s, lily_result_value(s));
    }

    lily_list_val *result_list = lily_new_list_val_n(i);

    i--;
    for (;i >= 0;i--) {
        s->num_registers--;
        lily_assign_value(result_list->elems[i],
                s->regs_from_main[s->num_registers]);
    }

    lily_return_list(s, result_list);
}

/**
method List.pop[A](self: List[A]): A

Attempt to remove and return the last element within 'self'.

Errors:

Raises `IndexError` if 'self' is empty.
*/
void lily_builtin_List_pop(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);

    if (list_val->num_values == 0)
        lily_error(s, SYM_CLASS_INDEXERROR, "Pop from an empty list.");

    lily_value *source = list_val->elems[list_val->num_values - 1];

    /* This is a special case because the value is moving out of the list, so
       don't let it get a ref increase. */
    lily_return_value_noref(s, source);

    /* For now, free extra values instead of trying to keep reserves around.
       Not the best course of action, perhaps, but certainly the simplest. */
    lily_free(list_val->elems[list_val->num_values - 1]);
    list_val->num_values--;
    list_val->extra_space++;
}

/**
method List.push[A](self: List[A], value: A)

Add 'value' to the end of 'self'.
*/
void lily_builtin_List_push(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);
    lily_value *insert_value = lily_arg_value(s, 1);

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    int value_count = list_val->num_values;

    list_val->elems[value_count] = lily_copy_value(insert_value);
    list_val->num_values++;
    list_val->extra_space--;

    lily_return_unit(s);
}

static void list_select_reject_common(lily_state *s, int expect)
{
    lily_list_val *list_val = lily_arg_list(s, 0);
    lily_prepare_call(s, lily_arg_function(s, 1));

    int n = 0;
    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_push_value(s, list_val->elems[i]);
        lily_exec_prepared(s, 1);

        int ok = lily_result_boolean(s) == expect;

        if (ok) {
            lily_push_value(s, list_val->elems[i]);
            n++;
        }
    }

    lily_list_val *result_list = lily_new_list_val_n(n);

    n--;
    for (;n >= 0;n--) {
        s->num_registers--;
        lily_assign_value(result_list->elems[n],
                s->regs_from_main[s->num_registers]);
    }

    lily_return_list(s, result_list);
}

/**
method List.reject[A](self: List[A], fn: Function(A => Boolean)): List[A]

This calls 'fn' for each element within 'self'. The result is a newly-made
`List` holding each element where 'fn' returns 'false'.
*/
void lily_builtin_List_reject(lily_state *s)
{
    list_select_reject_common(s, 0);
}

/**
method List.select[A](self: List[A], fn: Function(A => Boolean)): List[A]

This calls 'fn' for each element within 'self'. The result is a newly-made
`List` holding each element where 'fn' returns 'true'.
*/
void lily_builtin_List_select(lily_state *s)
{
    list_select_reject_common(s, 1);
}

/**
method List.size[A](self: List[A]): Integer

Returns the number of elements that are within 'self'.
*/
void lily_builtin_List_size(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);

    lily_return_integer(s, list_val->num_values);
}

/**
method List.shift[A](self: List[A]): A

This attempts to remove the last element from 'self' and return it.

Errors:

Raises `ValueError` if 'self' is empty.
*/
void lily_builtin_List_shift(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);

    if (list_val->num_values == 0)
        lily_error(s, SYM_CLASS_INDEXERROR, "Shift on an empty list.");

    lily_value *source = list_val->elems[0];

    /* Similar to List.pop, the value is being taken out so use this custom
       assign to keep the refcount the same. */
    lily_return_value_noref(s, source);

    /* For now, free extra values instead of trying to keep reserves around.
       Not the best course of action, perhaps, but certainly the simplest. */
    lily_free(list_val->elems[0]);

    if (list_val->num_values != 1)
        memmove(list_val->elems, list_val->elems + 1,
                (list_val->num_values - 1) *
                sizeof(lily_value *));

    list_val->num_values--;
    list_val->extra_space++;
}

/**
method List.unshift[A](self: List[A], value: A)

Inserts value at the front of self, moving all other elements to the right.
*/
void lily_builtin_List_unshift(lily_state *s)
{
    lily_list_val *list_val = lily_arg_list(s, 0);
    lily_value *input_reg = lily_arg_value(s, 1);

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    if (list_val->num_values != 0)
        memmove(list_val->elems + 1, list_val->elems,
                list_val->num_values * sizeof(lily_value *));

    list_val->elems[0] = lily_copy_value(input_reg);

    list_val->num_values++;
    list_val->extra_space--;
}

/**
enum Option[A]
    Some(A)
    None

The `Option` type allows a variable to hold either a value of `A` or to hold
'None', with 'None' being valid for any `Option`. The `Option` type thus
presents a way to have a function to fail without raising an exception, among
many other uses.
*/

/**
method Option.and[A, B](self: Option[A], other: Option[B]): Option[B]

If 'self' is a 'Some', this returns 'other'.

Otherwise, this returns 'None'.
*/
void lily_builtin_Option_and(lily_state *s)
{
    lily_instance_val *input = lily_arg_instance(s, 0);

    if (input->instance_id == SYM_CLASS_SOME)
        lily_return_value(s, lily_arg_value(s, 1));
    else
        lily_return_value(s, lily_arg_value(s, 0));
}

/**
method Option.and_then[A, B](self: Option[A], fn: Function(A => Option[B])): Option[B]

If 'self' is a 'Some', this calls 'fn' with the value within the 'Some'. The
result is the result of the `Option` returned by 'fn'.

Otherwise, this returns 'None'.
*/
void lily_builtin_Option_and_then(lily_state *s)
{
    lily_instance_val *optval = lily_arg_instance(s, 0);

    if (optval->instance_id == SYM_CLASS_SOME) {
        lily_push_value(s, lily_instance_value(optval, 0));

        lily_exec_simple(s, lily_arg_function(s, 1), 1);

        lily_return_value(s, lily_result_value(s));
    }
    else
        lily_return_filled_variant(s, optval);
}

static void option_is_some_or_none(lily_state *s, int num_expected)
{
    lily_instance_val *optval = lily_arg_instance(s, 0);
    lily_return_boolean(s, (optval->num_values == num_expected));
}

/**
method Option.is_none[A](self: Option[A]): Boolean

If 'self' is a 'Some', this returns 'false'.

Otherwise, this returns 'true'.
*/
void lily_builtin_Option_is_none(lily_state *s)
{
    option_is_some_or_none(s, 0);
}

/**
method Option.is_some[A](self: Option[A]): Boolean

If 'self' is a 'Some', this returns 'true'.

Otherwise, this returns 'false'.
*/
void lily_builtin_Option_is_some(lily_state *s)
{
    option_is_some_or_none(s, 1);
}

/**
method Option.map[A, B](self: Option[A], fn: Function(A => B)): Option[B]

If 'self' is a 'Some', this returns a 'Some' holding the result of 'fn'.

Otherwise, this returns 'None'.
*/
void lily_builtin_Option_map(lily_state *s)
{
    lily_instance_val *optval = lily_arg_instance(s, 0);

    if (optval->instance_id == SYM_CLASS_SOME) {
        lily_push_value(s, lily_instance_value(optval, 0));

        lily_exec_simple(s, lily_arg_function(s, 1), 1);

        lily_instance_val *variant = lily_new_some();
        lily_variant_set_value(variant, 0, lily_result_value(s));
        lily_return_filled_variant(s, variant);
    }
    else
        lily_return_empty_variant(s, lily_get_none(s));
}

/**
method Option.or[A](self: Option[A], alternate: Option[A]): Option[A]

If 'self' is a 'Some', this returns 'self'.

Otherwise, this returns 'alternate'.
*/
void lily_builtin_Option_or(lily_state *s)
{
    lily_instance_val *optval = lily_arg_instance(s, 0);

    if (optval->instance_id == SYM_CLASS_SOME)
        lily_return_filled_variant(s, optval);
    else
        lily_return_value(s, lily_arg_value(s, 1));
}

/**
method Option.or_else[A](self: Option[A], fn: Function( => Option[A])):Option[A]

If 'self' is a 'Some', this returns 'self'.

Otherwise, this returns the result of calling 'fn'.
*/
void lily_builtin_Option_or_else(lily_state *s)
{
    lily_instance_val *optval = lily_arg_instance(s, 0);

    if (optval->instance_id == SYM_CLASS_SOME)
        lily_return_filled_variant(s, optval);
    else {
        lily_exec_simple(s, lily_arg_function(s, 1), 0);

        lily_result_return(s);
    }
}

/**
method Option.unwrap[A](self: Option[A]): A

If 'self' is a 'Some', this returns the value contained within.

Errors:

Raises `ValueError` if 'self' is a 'None'.
*/
void lily_builtin_Option_unwrap(lily_state *s)
{
    lily_value *opt_reg = lily_arg_value(s, 0);
    lily_instance_val *optval = opt_reg->value.instance;

    if (optval->instance_id == SYM_CLASS_SOME)
        lily_return_value(s, lily_instance_value(optval, 0));
    else
        lily_error(s, SYM_CLASS_VALUEERROR, "unwrap called on None.");
}

/**
method Option.unwrap_or[A](self: Option[A], alternate: A):A

If 'self' is a 'Some', this returns the value with 'self'.

Otherwise, this returns 'alternate'.
*/
void lily_builtin_Option_unwrap_or(lily_state *s)
{
    lily_value *opt_reg = lily_arg_value(s, 0);
    lily_value *fallback_reg = lily_arg_value(s, 1);
    lily_instance_val *optval = opt_reg->value.instance;
    lily_value *source;

    if (optval->instance_id == SYM_CLASS_SOME)
        source = lily_instance_value(optval, 0);
    else
        source = fallback_reg;

    lily_return_value(s, source);
}

/**
method Option.unwrap_or_else[A](self: Option[A], fn: Function( => A)):A

If 'self' is a 'Some', this returns the value with 'self'.

Otherwise, this returns the result of calling 'fn'.
*/
void lily_builtin_Option_unwrap_or_else(lily_state *s)
{
    lily_instance_val *optval = lily_arg_instance(s, 0);

    if (optval->instance_id == SYM_CLASS_SOME)
        lily_return_value(s, lily_instance_value(optval, 0));
    else {
        lily_exec_simple(s, lily_arg_function(s, 1), 0);

        lily_return_value(s, lily_result_value(s));
    }
}

/**
bootstrap RuntimeError(msg: String) < Exception(msg) {}

`RuntimeError` is a subclass of `Exception` that is raised when the recursion
limit is exceeded, or when trying to modify a `Hash` while iterating over it.
*/

/**
class String

The `String` class provides a wrapper over a C char *. The `String` class is
guaranteed to have a single '\0' terminator. Additionally, a `String` is
guaranteed to always be valid utf-8.

The methods on the `String` class treat the underlying `String` as being
immutable, and thus always create a new `String` instead of modifying the
existing one.
*/

/* This table indicates how many more bytes need to be successfully read after
   that particular byte for proper utf-8. -1 = invalid.
   Table copied from lily_lexer.c */
static const char follower_table[256] =
{
     /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/* 0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 1 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 2 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 3 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 4 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 6 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 7 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 8 */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* 9 */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* A */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* B */-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* C */-1,-1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* D */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* E */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
/* F */ 4, 4, 4, 4, 4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

static lily_string_val *make_sv(lily_state *s, int size)
{
    lily_string_val *new_sv = lily_malloc(sizeof(lily_string_val));
    char *new_string = lily_malloc(sizeof(char) * size);

    new_sv->string = new_string;
    new_sv->size = size - 1;
    new_sv->refcount = 0;

    return new_sv;
}

/**
method String.ends_with(self: String, end: String): Boolean

Checks if 'self' ends with 'end'.
*/
void lily_builtin_String_ends_with(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *suffix_arg = lily_arg_value(s, 1);

    char *input_raw_str = input_arg->value.string->string;
    char *suffix_raw_str = suffix_arg->value.string->string;
    int input_size = input_arg->value.string->size;
    int suffix_size = suffix_arg->value.string->size;

    if (suffix_size > input_size) {
        lily_return_boolean(s, 0);
        return;
    }

    int input_i, suffix_i, ok = 1;
    for (input_i = input_size - 1, suffix_i = suffix_size - 1;
         suffix_i >= 0;
         input_i--, suffix_i--) {
        if (input_raw_str[input_i] != suffix_raw_str[suffix_i]) {
            ok = 0;
            break;
        }
    }

    lily_return_boolean(s, ok);
}


/**
method String.find(self: String, needle: String): Option[Integer]

Check for 'needle' being within 'self.

If 'needle' is found, the result is a 'Some' holding the index.

Otherwise, this returns 'None'.
*/
void lily_builtin_String_find(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *find_arg = lily_arg_value(s, 1);

    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    char *find_str = find_arg->value.string->string;
    int find_length = find_arg->value.string->size;

    if (find_length > input_length ||
        find_length == 0) {
        lily_return_empty_variant(s, lily_get_none(s));
        return;
    }

    char find_ch;
    int i, j, k, length_diff, match;

    length_diff = input_length - find_length;
    find_ch = find_str[0];
    match = 0;

    /* This stops at length_diff for two reasons:
       * The inner loop won't have to do a boundary check.
       * Search will stop if there isn't enough length left for a match
         (ex: "abcdef".find("defg")) */
    for (i = 0;i <= length_diff;i++) {
        if (input_str[i] == find_ch) {
            match = 1;
            /* j starts at i + 1 to skip the first match.
               k starts at 1 for the same reason. */
            for (j = i + 1, k = 1;k < find_length;j++, k++) {
                if (input_str[j] != find_str[k]) {
                    match = 0;
                    break;
                }
            }
            if (match == 1)
                break;
        }
    }

    if (match) {
        lily_instance_val *variant = lily_new_some();
        lily_variant_set_integer(variant, 0, i);
        lily_return_filled_variant(s, variant);
    }
    else
        lily_return_empty_variant(s, lily_get_none(s));
}


/* Scan through 'input' in search of html characters to encode. If there are
   any, then s->vm_buffer is updated to contain an html-safe version of the
   input string.
   If no html characters are found, then 0 is returned, and the caller is to use
   the given input buffer directly.
   If html charcters are found, then 1 is returned, and the caller should read
   from s->vm_buffer->message. */
int lily_maybe_html_encode_to_buffer(lily_state *s, lily_value *input)
{
    lily_msgbuf *vm_buffer = lily_get_msgbuf(s);
    int start = 0, stop = 0;
    char *input_str = input->value.string->string;
    char *ch = &input_str[0];

    while (1) {
        if (*ch == '&') {
            stop = (ch - input_str);
            lily_mb_add_range(vm_buffer, input_str, start, stop);
            lily_mb_add(vm_buffer, "&amp;");
            start = stop + 1;
        }
        else if (*ch == '<') {
            stop = (ch - input_str);
            lily_mb_add_range(vm_buffer, input_str, start, stop);
            lily_mb_add(vm_buffer, "&lt;");
            start = stop + 1;
        }
        else if (*ch == '>') {
            stop = (ch - input_str);
            lily_mb_add_range(vm_buffer, input_str, start, stop);
            lily_mb_add(vm_buffer, "&gt;");
            start = stop + 1;
        }
        else if (*ch == '\0')
            break;

        ch++;
    }

    if (start != 0) {
        stop = (ch - input_str);
        lily_mb_add_range(vm_buffer, input_str, start, stop);
    }

    return start;
}

/**
method String.html_encode(self: String): String

Check for one of '"&"', '"<"', or '">"' being within 'self'.

If found, a new `String` is contained with any instance of the above being
replaced by an html-safe value.

If not found, 'self' is returned.
*/
void lily_builtin_String_html_encode(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);

    /* If nothing was escaped, output what was input. */
    if (lily_maybe_html_encode_to_buffer(s, input_arg) == 0)
        lily_return_value(s, input_arg);
    else
        lily_return_string(s, lily_new_raw_string(lily_mb_get(s->vm_buffer)));
}

#define CTYPE_WRAP(WRAP_NAME, WRAPPED_CALL) \
void lily_builtin_String_##WRAP_NAME(lily_state *s) \
{ \
    lily_string_val *input = lily_arg_string(s, 0); \
    int length = lily_string_length(input); \
\
    if (length == 0) { \
        lily_return_boolean(s, 0); \
        return; \
    } \
\
    const char *loop_str = lily_string_get_raw(input); \
    int i = 0; \
    int ok = 1; \
\
    for (i = 0;i < length;i++) { \
        if (WRAPPED_CALL(loop_str[i]) == 0) { \
            ok = 0; \
            break; \
        } \
    } \
\
    lily_return_boolean(s, ok); \
}

/**
method String.is_alnum(self: String):Boolean

Return 'true' if 'self' has only alphanumeric([a-zA-Z0-9]+) characters, 'false'
otherwise.
*/
CTYPE_WRAP(is_alnum, isalnum)

/**
method String.is_alpha(self: String):Boolean

Return 'true' if 'self' has only alphabetical([a-zA-Z]+) characters, 'false'
otherwise.
*/
CTYPE_WRAP(is_alpha, isalpha)

/**
method String.is_digit(self: String):Boolean

Return 'true' if 'self' has only digit([0-9]+) characters, 'false' otherwise.
*/
CTYPE_WRAP(is_digit, isdigit)

/**
method String.is_space(self: String):Boolean

Returns 'true' if 'self' has only space(" \t\r\n") characters, 'false'
otherwise.
*/
CTYPE_WRAP(is_space, isspace)

/**
method String.lower(self: String):String

Checks if any characters within 'self' are within [A-Z]. If so, it creates a new
`String` with [A-Z] replaced by [a-z]. Otherwise, 'self' is returned.
*/
void lily_builtin_String_lower(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);

    int new_size = input_arg->value.string->size + 1;
    lily_string_val *new_sv = make_sv(s, new_size);

    char *new_str = new_sv->string;
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;
    int i;

    for (i = 0;i < input_length;i++) {
        char ch = input_str[i];
        if (isupper(ch))
            new_str[i] = tolower(ch);
        else
            new_str[i] = ch;
    }
    new_str[input_length] = '\0';

    lily_return_string(s, new_sv);
}

/* This is a helper for lstrip wherein input_arg has some utf-8 bits inside. */
static int lstrip_utf8_start(lily_value *input_arg, lily_string_val *strip_sv)
{
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    char *strip_str = strip_sv->string;
    int strip_length = strip_sv->size;
    int i = 0, j = 0, match = 1;

    char ch = strip_str[0];
    if (follower_table[(unsigned char)ch] == strip_length) {
        /* Only a single utf-8 char. This is a bit simpler. */
        char strip_start_ch = ch;
        int char_width = follower_table[(unsigned char)ch];
        while (i < input_length) {
            if (input_str[i] == strip_start_ch) {
                /* j starts at 1 because the first byte was already checked.
                   This compares the inner part of the strip string and the
                   input string to make sure the whole utf-8 chunk matches. */
                for (j = 1;j < char_width;j++) {
                    if (input_str[i + j] != strip_str[j]) {
                        match = 0;
                        break;
                    }
                }
                if (match == 0)
                    break;

                i += char_width;
            }
            else
                break;
        }
    }
    else {
        /* There's at least one utf-8 chunk. There may be ascii bytes to strip
           as well, or more utf-8 chunks. This is the most complicated case. */
        char input_ch;
        int char_width, k;
        while (1) {
            input_ch = input_str[i];
            if (input_ch == strip_str[j]) {
                char_width = follower_table[(unsigned char)strip_str[j]];
                match = 1;
                /* This has to use k, unlike the above loop, because j is being
                   used to hold the current position in strip_str. */
                for (k = 1;k < char_width;k++) {
                    if (input_str[i + k] != strip_str[j + k]) {
                        match = 0;
                        break;
                    }
                }
                if (match == 1) {
                    /* Found a match, so eat the valid utf-8 chunk and start
                       from the beginning of the strip string again. This makes
                       sure that each chunk of the input string is matched to
                       each chunk of the strip string. */
                    i += char_width;
                    if (i >= input_length)
                        break;
                    else {
                        j = 0;
                        continue;
                    }
                }
            }

            /* This assumes that strip_str is valid utf-8. */
            j += follower_table[(unsigned char)strip_str[j]];

            /* If all chunks in the strip str have been checked, then
               everything that can be removed has been removed. */
            if (j == strip_length) {
                match = 0;
                break;
            }
        }
    }

    return i;
}

/* This is a helper for lstrip wherein input_arg does not have utf-8. */
static int lstrip_ascii_start(lily_value *input_arg, lily_string_val *strip_sv)
{
    int i;
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    if (strip_sv->size == 1) {
        /* Strip a single byte really fast. The easiest case. */
        char strip_ch;
        strip_ch = strip_sv->string[0];
        for (i = 0;i < input_length;i++) {
            if (input_str[i] != strip_ch)
                break;
        }
    }
    else {
        /* Strip one of many ascii bytes. A bit tougher, but not much. */
        char *strip_str = strip_sv->string;
        int strip_length = strip_sv->size;
        for (i = 0;i < input_length;i++) {
            char ch = input_str[i];
            int found = 0;
            int j;
            for (j = 0;j < strip_length;j++) {
                if (ch == strip_str[j]) {
                    found = 1;
                    break;
                }
            }
            if (found == 0)
                break;
        }
    }

    return i;
}

/**
method String.lstrip(self: String, to_strip: String):String

This walks through 'self' from left to right, stopping on the first utf-8 chunk
that is not found within 'to_strip'. The result is a newly-made copy of self
without the elements within 'to_strip' at the front.
*/
void lily_builtin_String_lstrip(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *strip_arg = lily_arg_value(s, 1);

    char *strip_str;
    unsigned char ch;
    int copy_from, i, has_multibyte_char, strip_str_len;
    lily_string_val *strip_sv;

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_arg->value.string->size == 0 ||
        strip_arg->value.string->size == 0) {
        lily_return_value(s, input_arg);
        return;
    }

    strip_sv = strip_arg->value.string;
    strip_str = strip_sv->string;
    strip_str_len = strlen(strip_str);
    has_multibyte_char = 0;

    for (i = 0;i < strip_str_len;i++) {
        ch = (unsigned char)strip_str[i];
        if (ch > 127) {
            has_multibyte_char = 1;
            break;
        }
    }

    if (has_multibyte_char == 0)
        copy_from = lstrip_ascii_start(input_arg, strip_sv);
    else
        copy_from = lstrip_utf8_start(input_arg, strip_sv);

    int new_size = (input_arg->value.string->size - copy_from) + 1;
    lily_string_val *new_sv = make_sv(s, new_size);

    strcpy(new_sv->string, input_arg->value.string->string + copy_from);

    lily_return_string(s, new_sv);
}

/**
method String.parse_i(self: String): Option[Integer]

Attempts to convert 'self' into an `Integer`. Currently, 'self' is parsed as a
base-10 encoded value.

If the value is a valid `Integer`, then a 'Some' containing the value is
returned.

Otherwise, 'None' is returned.
*/
void lily_builtin_String_parse_i(lily_state *s)
{
    char *input = lily_arg_string_raw(s, 0);
    uint64_t value = 0;
    int is_negative = 0;
    unsigned int rounds = 0;
    int leading_zeroes = 0;

    if (*input == '-') {
        is_negative = 1;
        ++input;
    }
    else if (*input == '+')
        ++input;

    if (*input == '0') {
        ++input;
        leading_zeroes = 1;
        while (*input == '0')
            ++input;
    }

    /* A signed int64 peaks at 9223372036854775807 (or ...808 for negative).
       The maximum number of reasonable digits is therefore 20 for scanning
       decimal. */
    while (*input >= '0' && *input <= '9' && rounds != 20) {
        value = (value * 10) + (*input - '0');
        ++input;
        rounds++;
    }

    /* These cases check for overflow, trailing junk, and just + or just -. */
    if (value > ((uint64_t)INT64_MAX + is_negative) ||
        *input != '\0' ||
        (rounds == 0 && leading_zeroes == 0)) {
        lily_return_empty_variant(s, lily_get_none(s));
    }
    else {
        int64_t signed_value;

        if (is_negative == 0)
            signed_value = (int64_t)value;
        else
            signed_value = -(int64_t)value;

        lily_instance_val *variant = lily_new_some();
        lily_variant_set_integer(variant, 0, signed_value);
        lily_return_filled_variant(s, variant);
    }
}

/* This is a helper for rstrip when there's no utf-8 in input_arg. */
static int rstrip_ascii_stop(lily_value *input_arg, lily_string_val *strip_sv)
{
    int i;
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    if (strip_sv->size == 1) {
        char strip_ch = strip_sv->string[0];
        for (i = input_length - 1;i >= 0;i--) {
            if (input_str[i] != strip_ch)
                break;
        }
    }
    else {
        char *strip_str = strip_sv->string;
        int strip_length = strip_sv->size;
        for (i = input_length - 1;i >= 0;i--) {
            char ch = input_str[i];
            int found = 0;
            int j;
            for (j = 0;j < strip_length;j++) {
                if (ch == strip_str[j]) {
                    found = 1;
                    break;
                }
            }
            if (found == 0)
                break;
        }
    }

    return i + 1;
}

/* Helper for rstrip, for when there is some utf-8. */
static int rstrip_utf8_stop(lily_value *input_arg, lily_string_val *strip_sv)
{
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    char *strip_str = strip_sv->string;
    int strip_length = strip_sv->size;
    int i, j;

    i = input_length - 1;
    j = 0;
    while (i >= 0) {
        /* First find out how many bytes are in the current chunk. */
        int follow_count = follower_table[(unsigned char)strip_str[j]];
        /* Now get the last byte of this chunk. Since the follower table
           includes the total, offset by -1. */
        char last_strip_byte = strip_str[j + (follow_count - 1)];
        /* Input is going from right to left. See if input matches the last
           byte of the current utf-8 chunk. But also check that there are
           enough chars left to protect against underflow. */
        if (input_str[i] == last_strip_byte &&
            i + 1 >= follow_count) {
            int match = 1;
            int input_i, strip_i, k;
            /* input_i starts at i - 1 to skip the last byte.
               strip_i starts at follow_count so it can stop things. */
            for (input_i = i - 1, strip_i = j + (follow_count - 2), k = 1;
                 k < follow_count;
                 input_i--, strip_i--, k++) {
                if (input_str[input_i] != strip_str[strip_i]) {
                    match = 0;
                    break;
                }
            }

            if (match == 1) {
                i -= follow_count;
                j = 0;
                continue;
            }
        }

        /* Either the first byte or one of the inner bytes didn't match.
           Go to the next chunk and try again. */
        j += follow_count;
        if (j == strip_length)
            break;

        continue;
    }

    return i + 1;
}

/**
method String.rstrip(self: String, to_strip: String):String

This walks through 'self' from right to left, stopping on the first utf-8 chunk
that is not found within 'to_strip'. The result is a newly-made copy of 'self'
without the elements of 'to_strip' at the end.
*/
void lily_builtin_String_rstrip(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *strip_arg = lily_arg_value(s, 1);

    char *strip_str;
    unsigned char ch;
    int copy_to, i, has_multibyte_char, strip_str_len;
    lily_string_val *strip_sv;

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_arg->value.string->size == 0 ||
        strip_arg->value.string->size == 0) {
        lily_return_value(s, input_arg);
        return;
    }

    strip_sv = strip_arg->value.string;
    strip_str = strip_sv->string;
    strip_str_len = strlen(strip_str);
    has_multibyte_char = 0;

    for (i = 0;i < strip_str_len;i++) {
        ch = (unsigned char)strip_str[i];
        if (ch > 127) {
            has_multibyte_char = 1;
            break;
        }
    }

    if (has_multibyte_char == 0)
        copy_to = rstrip_ascii_stop(input_arg, strip_sv);
    else
        copy_to = rstrip_utf8_stop(input_arg, strip_sv);

    int new_size = copy_to + 1;
    lily_string_val *new_sv = make_sv(s, new_size);

    strncpy(new_sv->string, input_arg->value.string->string, copy_to);
    /* This will always copy a partial string, so make sure to add a terminator. */
    new_sv->string[copy_to] = '\0';

    lily_return_string(s, new_sv);
}

static const char move_table[256] =
{
     /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/* 0 */ 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 1 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 2 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 3 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 4 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 6 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 7 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 8 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 9 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* A */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* B */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* C */ 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* D */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* E */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
/* F */ 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static lily_list_val *string_split_by_val(lily_state *s, char *input,
        char *splitby)
{
    char *input_ch = &input[0];
    char *splitby_ch = &splitby[0];
    int values_needed = 0;

    while (move_table[(unsigned char)*input_ch] != 0) {
        if (*input_ch == *splitby_ch) {
            char *restore_ch = input_ch;
            int is_match = 1;
            while (*input_ch == *splitby_ch) {
                splitby_ch++;
                input_ch++;
                if (*splitby_ch == '\0')
                    break;

                if (*input_ch != *splitby_ch) {
                    is_match = 0;
                    input_ch = restore_ch;
                    break;
                }
            }

            splitby_ch = &splitby[0];
            values_needed += is_match;
        }
        else
            input_ch += move_table[(unsigned char)*input_ch];
    }

    values_needed++;
    input_ch = &input[0];
    lily_list_val *list_val = lily_new_list_val_n(values_needed);
    int i = 0;
    char *last_start = input_ch;

    while (1) {
        char *match_start = input_ch;
        int is_match = 0;
        if (*input_ch == *splitby_ch) {
            is_match = 1;
            while (*input_ch == *splitby_ch) {
                splitby_ch++;
                if (*splitby_ch == '\0')
                    break;

                input_ch++;
                if (*input_ch != *splitby_ch) {
                    is_match = 0;
                    input_ch = match_start;
                    break;
                }
            }
            splitby_ch = &splitby[0];
        }

        /* The second check is so that if the last bit of the input string
           matches the split string, an empty string will be made.
           Ex: "1 2 3 ".split(" ") # ["1", "2", "3", ""] */
        if (is_match || *input_ch == '\0') {
            int size = match_start - last_start;
            lily_string_val *sv = lily_new_raw_string_sized(last_start, size);
            lily_list_set_string(list_val, i, sv);

            i++;
            if (*input_ch == '\0')
                break;

            last_start = input_ch + 1;
        }
        else if (*input_ch == '\0')
            break;

        input_ch++;
    }

    return list_val;
}

/**
method String.split(self: String, split_by: *String=" "):List[String]

This attempts to split 'self' using 'split_by', with a default value of a single
space.

Errors:

Raises `ValueError` if 'split_by' is empty.
*/
void lily_builtin_String_split(lily_state *s)
{
    lily_string_val *input_strval = lily_arg_string(s, 0);
    lily_string_val *split_strval;
    lily_string_val fake_sv;

    if (lily_arg_count(s) == 2) {
        split_strval = lily_arg_string(s, 1);
        if (split_strval->size == 0)
            lily_error(s, SYM_CLASS_VALUEERROR,
                    "Cannot split by empty string.");
    }
    else {
        fake_sv.string = " ";
        fake_sv.size = 1;
        split_strval = &fake_sv;
    }

    lily_list_val *lv = string_split_by_val(s, input_strval->string,
            split_strval->string);

    lily_return_list(s, lv);
}

/**
method String.starts_with(self: String, with: String): Boolean

Checks if 'self' starts with 'with'.
*/
void lily_builtin_String_starts_with(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *prefix_arg = lily_arg_value(s, 1);

    char *input_raw_str = input_arg->value.string->string;
    char *prefix_raw_str = prefix_arg->value.string->string;
    int prefix_size = prefix_arg->value.string->size;

    if (input_arg->value.string->size < prefix_size) {
        lily_return_boolean(s, 0);
        return;
    }

    int i, ok = 1;
    for (i = 0;i < prefix_size;i++) {
        if (input_raw_str[i] != prefix_raw_str[i]) {
            ok = 0;
            break;
        }
    }

    lily_return_boolean(s, ok);
}

/**
method String.strip(self: String, to_strip: String):String

This walks through self from right to left, and then from left to right. The
result of this is a newly-made `String` without any elements within 'to_strip'
at either end.
*/
void lily_builtin_String_strip(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *strip_arg = lily_arg_value(s, 1);

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_arg->value.string->size == 0 ||
        strip_arg->value.string->size == 0) {
        lily_return_value(s, input_arg);
        return;
    }

    char ch;
    lily_string_val *strip_sv = strip_arg->value.string;
    char *strip_str = strip_sv->string;
    int strip_str_len = strlen(strip_str);
    int has_multibyte_char = 0;
    int copy_from, copy_to, i;

    for (i = 0;i < strip_str_len;i++) {
        ch = (unsigned char)strip_str[i];
        if (ch > 127) {
            has_multibyte_char = 1;
            break;
        }
    }

    if (has_multibyte_char == 0)
        copy_from = lstrip_ascii_start(input_arg, strip_sv);
    else
        copy_from = lstrip_utf8_start(input_arg, strip_sv);

    if (copy_from != input_arg->value.string->size) {
        if (has_multibyte_char)
            copy_to = rstrip_ascii_stop(input_arg, strip_sv);
        else
            copy_to = rstrip_utf8_stop(input_arg, strip_sv);
    }
    else
        /* The whole string consists of stuff in strip_str. Do this so the
           result is an empty string. */
        copy_to = copy_from;

    int new_size = (copy_to - copy_from) + 1;
    lily_string_val *new_sv = make_sv(s, new_size);

    char *new_str = new_sv->string;
    strncpy(new_str, input_arg->value.string->string + copy_from, new_size - 1);
    new_str[new_size - 1] = '\0';

    lily_return_string(s, new_sv);
}

/**
method String.trim(self: String): String

Checks if 'self' starts or ends with any of '" \t\r\n"'. If it does, then a new
`String` is made with spaces removed from both sides. If it does not, then this
returns 'self'.
*/
void lily_builtin_String_trim(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);

    char fake_buffer[5] = " \t\r\n";
    lily_string_val fake_sv;
    fake_sv.string = fake_buffer;
    fake_sv.size = strlen(fake_buffer);

    int copy_from = lstrip_ascii_start(input_arg, &fake_sv);
    lily_string_val *new_sv;

    if (copy_from != input_arg->value.string->size) {
        int copy_to = rstrip_ascii_stop(input_arg, &fake_sv);
        int new_size = (copy_to - copy_from) + 1;
        new_sv = make_sv(s, new_size);
        char *new_str = new_sv->string;

        strncpy(new_str, input_arg->value.string->string + copy_from, new_size - 1);
        new_str[new_size - 1] = '\0';
    }
    else {
        /* It's all space, so make a new empty string. */
        new_sv = make_sv(s, 1);
        new_sv->string[0] = '\0';
    }

    lily_return_string(s, new_sv);
}

/**
method String.upper(self: String):String

Checks if any characters within self are within [a-z]. If so, it creates a new
`String` with [a-z] replaced by [A-Z]. Otherwise, 'self' is returned.
*/
void lily_builtin_String_upper(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);

    int new_size = input_arg->value.string->size + 1;
    lily_string_val *new_sv = make_sv(s, new_size);

    char *new_str = new_sv->string;
    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;
    int i;

    for (i = 0;i < input_length;i++) {
        char ch = input_str[i];
        if (islower(ch))
            new_str[i] = toupper(ch);
        else
            new_str[i] = ch;
    }
    new_str[input_length] = '\0';

    lily_return_string(s, new_sv);
}

/* This handles a string subscript. The subscript may be negative (in which case
   it is an offset against the end). This must check if the index given by
   'index_reg' is a valid one.
   This moves by utf-8 codepoints, not by bytes. The result is sent to
   'result_reg', unless IndexError is raised. */
void lily_string_subscript(lily_state *s, lily_value *input_reg,
        lily_value *index_reg, lily_value *result_reg)
{
    char *input = input_reg->value.string->string;
    int index = index_reg->value.integer;
    char *ch;

    if (index >= 0) {
        ch = &input[0];
        while (index && move_table[(unsigned char)*ch] != 0) {
            ch += move_table[(unsigned char)*ch];
            index--;
        }
        if (move_table[(unsigned char)*ch] == 0)
            lily_error_fmt(s, SYM_CLASS_INDEXERROR, "Index %d is out of range.",
                    index_reg->value.integer);
    }
    else {
        char *stop = &input[0];
        ch = &input[input_reg->value.string->size];
        while (stop != ch && index != 0) {
            ch--;
            if (move_table[(unsigned char)*ch] != 0)
                index++;
        }
        if (index != 0)
            lily_error_fmt(s, SYM_CLASS_INDEXERROR, "Index %d is out of range.",
                    index_reg->value.integer);
    }

    int to_copy = move_table[(unsigned char)*ch];
    lily_string_val *result = make_sv(s, to_copy + 1);
    char *dest = &result->string[0];
    dest[to_copy] = '\0';

    strncpy(dest, ch, to_copy);

    lily_move_string(result_reg, result);
}

/**
bootstrap Tainted[A](v:A){ var @value = v }

The `Tainted` type represents a wrapper over some data that is considered
unsafe. Data, once inside a `Tainted` value can only be retrieved using the
'Tainted.sanitize' function.
*/

/**
method Tainted.sanitize[A, B](self: Tainted[A], fn: Function(A => B)): B

This calls 'fn' with the value contained within 'self'. 'fn' is assumed to be a
function that can sanitize the data within 'self'.
*/
void lily_builtin_Tainted_sanitize(lily_state *s)
{
    lily_instance_val *instance_val = lily_arg_instance(s, 0);

    lily_push_value(s, lily_instance_value(instance_val, 0));

    lily_exec_simple(s, lily_arg_function(s, 1), 1);

    lily_result_return(s);
}

/**
class Tuple

The `Tuple` class provides a fixed-size container over a set of types. `Tuple`
is ideal for situations where a variety of data is needed, but a class is too
complex.

`Tuple` literals are created by '<[value1, value2, ...]>'. Member of the `Tuple`
class can be accessed through subscripts. Unlike `List`, `Tuple` does not
support negative indexes.

Operations on the `Tuple` class use the types `1` and `2`. These are special
types that match against any number of types. This allows `Tuple` operations to
work on all `Tuple` instances, regardless of the number of elements within the
`Tuple` (sometimes considered its arity).
*/


/**
method Tuple.merge(self: Tuple[1], other: Tuple[2]): Tuple[1, 2]

Build a new `Tuple` composed of the contents of 'self' and the contents of
'other'.
*/
void lily_builtin_Tuple_merge(lily_state *s)
{
    lily_list_val *left_tuple = lily_arg_list(s, 0);
    lily_list_val *right_tuple = lily_arg_list(s, 1);

    int new_count = left_tuple->num_values + right_tuple->num_values;
    lily_list_val *lv = lily_new_list_val_n(new_count);

    int i, j;
    for (i = 0, j = 0;i < left_tuple->num_values;i++, j++)
        lily_assign_value(lv->elems[j], left_tuple->elems[i]);

    for (i = 0;i < right_tuple->num_values;i++, j++)
        lily_assign_value(lv->elems[j], right_tuple->elems[i]);

    lily_return_tuple(s, lv);
}

/**
method Tuple.push[A](self: Tuple[1], other: A): Tuple[1, A]

Build a new `Tuple` composed of the contents of 'self' and 'other'.
*/
void lily_builtin_Tuple_push(lily_state *s)
{
    lily_list_val *left_tuple = lily_arg_list(s, 0);
    lily_value *right = lily_arg_value(s, 1);
    lily_list_val *lv = lily_new_list_val_n(left_tuple->num_values + 1);

    int i, j;
    for (i = 0, j = 0;i < left_tuple->num_values;i++, j++)
        lily_assign_value(lv->elems[j], left_tuple->elems[i]);

    lily_assign_value(lv->elems[j], right);

    lily_return_tuple(s, lv);
}

/**
bootstrap ValueError(msg: String) < Exception(msg) {}

`ValueError` is a subclass of `Exception` that is raised when sending an
improper argument to a function, such as trying to call 'List.fill' with a
negative amount.
*/

/***
 *      ____                    _                 _
 *     |  _ \ _   _ _ __   __ _| | ___   __ _  __| |
 *     | | | | | | | '_ \ / _` | |/ _ \ / _` |/ _` |
 *     | |_| | |_| | | | | (_| | | (_) | (_| | (_| |
 *     |____/ \__, |_| |_|\__,_|_|\___/ \__,_|\__,_|
 *            |___/
 */

static lily_value *new_builtin_file(FILE *source, const char *mode)
{
    lily_file_val *file_val = lily_new_file_val(source, mode);
    file_val->is_builtin = 1;

    return lily_new_value_of_file(file_val);
}

static void *load_var_stdin(lily_options *options, uint16_t *cid_table)
{
    return new_builtin_file(stdin, "r");
}

static void *load_var_stdout(lily_options *options, uint16_t *cid_table)
{
    return new_builtin_file(stdout, "w");
}

static void *load_var_stderr(lily_options *options, uint16_t *cid_table)
{
    return new_builtin_file(stderr, "w");
}

extern void lily_builtin_calltrace(lily_state *);
extern void lily_builtin_print(lily_state *);

#include "extras_builtin.h"
#include "dyna_builtin.h"

static lily_class *build_class(lily_symtab *symtab, const char *name,
        int generic_count, int dyna_start)
{
    lily_class *result = lily_new_class(symtab, name);
    result->dyna_start = dyna_start;
    result->generic_count = generic_count;
    result->flags |= CLS_IS_BUILTIN;

    return result;
}

/* This handles building classes for which no concrete values will ever exist.
   Giving them a sequential id is a waste because the vm will want to eventually
   scoop it up into the class table. So don't do that. */
static lily_class *build_special(lily_symtab *symtab, const char *name,
        int generic_count, int id)
{
    lily_class *result = lily_new_class(symtab, name);
    result->id = id;
    result->generic_count = generic_count;
    result->flags |= CLS_IS_BUILTIN;

    symtab->active_module->class_chain = result->next;
    symtab->next_class_id--;

    result->next = symtab->old_class_chain;
    symtab->old_class_chain = result;

    return result;
}

void lily_register_pkg_builtin(lily_state *s)
{
    lily_register_package(s, "", lily_builtin_dynaload_table,
            lily_builtin_loader);
}

void lily_init_pkg_builtin(lily_symtab *symtab)
{
    symtab->integer_class    = build_class(symtab, "Integer",     0, INTEGER_OFFSET);
    symtab->double_class     = build_class(symtab, "Double",      0, DOUBLE_OFFSET);
    symtab->string_class     = build_class(symtab, "String",      0, STRING_OFFSET);
    symtab->bytestring_class = build_class(symtab, "ByteString",  0, BYTESTRING_OFFSET);
    symtab->boolean_class    = build_class(symtab, "Boolean",     0, BOOLEAN_OFFSET);
    symtab->function_class   = build_class(symtab, "Function",   -1, FUNCTION_OFFSET);
    symtab->dynamic_class    = build_class(symtab, "Dynamic",     0, DYNAMIC_OFFSET);
    symtab->list_class       = build_class(symtab, "List",        1, LIST_OFFSET);
    symtab->hash_class       = build_class(symtab, "Hash",        2, HASH_OFFSET);
    symtab->tuple_class      = build_class(symtab, "Tuple",      -1, TUPLE_OFFSET);
    lily_class *file_class   = build_class(symtab, "File",        0, FILE_OFFSET);

    symtab->question_class = build_special(symtab, "?", 0, SYM_CLASS_QUESTION);
    symtab->optarg_class   = build_special(symtab, "*", 1, SYM_CLASS_OPTARG);
    lily_class *scoop1     = build_special(symtab, "~1", 0, SYM_CLASS_SCOOP_1);
    lily_class *scoop2     = build_special(symtab, "~2", 0, SYM_CLASS_SCOOP_2);

    scoop1->self_type->flags |= TYPE_HAS_SCOOP;
    scoop2->self_type->flags |= TYPE_HAS_SCOOP;

    symtab->integer_class->flags    |= CLS_VALID_OPTARG | CLS_VALID_HASH_KEY;
    symtab->double_class->flags     |= CLS_VALID_OPTARG;
    symtab->string_class->flags     |= CLS_VALID_OPTARG | CLS_VALID_HASH_KEY;
    symtab->bytestring_class->flags |= CLS_VALID_OPTARG;
    symtab->boolean_class->flags    |= CLS_VALID_OPTARG;

    symtab->integer_class->move_flags    = VAL_IS_INTEGER;
    symtab->double_class->move_flags     = VAL_IS_DOUBLE;
    symtab->string_class->move_flags     = VAL_IS_STRING;
    symtab->bytestring_class->move_flags = VAL_IS_BYTESTRING;
    symtab->boolean_class->move_flags    = VAL_IS_BOOLEAN;
    symtab->function_class->move_flags   = VAL_IS_FUNCTION;
    symtab->dynamic_class->move_flags    = VAL_IS_DYNAMIC;
    symtab->list_class->move_flags       = VAL_IS_LIST;
    symtab->hash_class->move_flags       = VAL_IS_HASH;
    symtab->tuple_class->move_flags      = VAL_IS_TUPLE;
    file_class->move_flags               = VAL_IS_FILE;

    /* These need to be set here so type finalization can bubble them up. */
    symtab->question_class->self_type->flags |= TYPE_IS_INCOMPLETE;
    symtab->function_class->flags |= CLS_GC_TAGGED;
    symtab->dynamic_class->flags |= CLS_GC_SPECULATIVE;
    /* HACK: This ensures that there is space to dynaload builtin classes and
       enums into. */
    symtab->next_class_id = START_CLASS_ID;
}

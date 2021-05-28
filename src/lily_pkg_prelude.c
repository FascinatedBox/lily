#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "lily.h"
#include "lily_alloc.h"
#include "lily_lexer.h"
#include "lily_parser.h"
#include "lily_platform.h"
#include "lily_symtab.h"
#include "lily_utf8.h"
#include "lily_value.h"
#define LILY_NO_EXPORT
#include "lily_pkg_prelude_bindings.h"

/* When destroying a value with a gc tag, set the tag to this to prevent destroy
   from reentering it. The values are useless, but cannot be 0 or this will be
   optimized as a NULL pointer. */
const lily_gc_entry *lily_gc_stopper =
&(lily_gc_entry)
{
    1,
    1,
    {.integer = 1},
    NULL
};

#define DEFINE_CONST_CLASS(name, id, shorthash, search_name, flags) \
static const lily_class name = \
{ \
    NULL, \
    ITEM_CLASS_FOREIGN, \
    flags, \
    id, \
    0, \
    (lily_type *)&name, \
    search_name, \
    shorthash, \
    0, \
    UINT16_MAX, \
    0, \
    NULL, \
    NULL, \
    0, \
    0, \
    {0}, \
    0, \
    NULL, \
    NULL, \
}

/* This is used by the type system to represent an incomplete type. */
DEFINE_CONST_CLASS(raw_question, LILY_ID_QUESTION, 0, "?",
                   TYPE_IS_INCOMPLETE | TYPE_TO_BLOCK);

/* This is available to class methods, and allows them to return the value for
   self that was passed. */
DEFINE_CONST_CLASS(raw_self, LILY_ID_SELF, 0, "self", 0);

/* This matches to any type and remembers what was matched. Essential for
   methods like `List.zip` and `String.format`. Foreign modules only. */
DEFINE_CONST_CLASS(raw_scoop, LILY_ID_SCOOP, 0, "$1",
                   TYPE_HAS_SCOOP | TYPE_TO_BLOCK);

/* Mismatched function returns narrow to this. */
DEFINE_CONST_CLASS(raw_unit, LILY_ID_UNIT, 1953066581, "Unit", 0);

/* When a keyarg function has optional argument holes, this type is used to send
   empty arguments to fill those holes. */
DEFINE_CONST_CLASS(raw_unset, LILY_ID_UNSET, 0, "", 0);

#undef DEFINE_CONST_CLASS

const lily_class *lily_scoop_class = &raw_scoop;
const lily_class *lily_self_class = &raw_self;
const lily_type *lily_question_type = (lily_type *)&raw_question;
const lily_type *lily_scoop_type = (lily_type *)&raw_scoop;
lily_type *lily_unit_type = (lily_type *)&raw_unit;
const lily_type *lily_unset_type = (lily_type *)&raw_unset;

static void return_exception(lily_state *s, uint16_t id)
{
    lily_container_val *result = lily_push_super(s, id, 2);

    lily_con_set(result, 0, lily_arg_value(s, 0));
    lily_push_list(s, 0);
    lily_con_set_from_stack(s, result, 1);
    lily_return_super(s);
}

void lily_prelude_Boolean_to_i(lily_state *s)
{
    lily_return_integer(s, lily_arg_boolean(s, 0));
}

void lily_prelude_Boolean_to_s(lily_state *s)
{
    int input = lily_arg_boolean(s, 0);
    char *to_copy;

    if (input == 0)
        to_copy = "false";
    else
        to_copy = "true";

    lily_return_string(s, to_copy);
}

void lily_prelude_Byte_to_i(lily_state *s)
{
    lily_return_integer(s, lily_arg_byte(s, 0));
}

void lily_prelude_ByteString_each_byte(lily_state *s)
{
    lily_bytestring_val *sv = lily_arg_bytestring(s, 0);
    const char *input = lily_bytestring_raw(sv);
    uint32_t len = lily_bytestring_length(sv);
    uint32_t i;

    lily_call_prepare(s, lily_arg_function(s, 1));

    for (i = 0;i < len;i++) {
        lily_push_byte(s, (uint8_t)input[i]);
        lily_call(s, 1);
    }

    lily_return_unit(s);
}

void lily_prelude_ByteString_encode(lily_state *s)
{
    lily_bytestring_val *input_bv = lily_arg_bytestring(s, 0);
    const char *encode_method = "error";

    if (lily_arg_count(s) == 2)
        encode_method = lily_arg_string_raw(s, 1);

    if (strcmp(encode_method, "error") != 0) {
        lily_return_none(s);
        return;
    }

    char *input_bytes = lily_bytestring_raw(input_bv);
    uint32_t input_size = lily_bytestring_length(input_bv);

    if (lily_is_valid_sized_utf8(input_bytes, input_size) == 0) {
        lily_return_none(s);
        return;
    }

    lily_push_string(s, input_bytes);
    lily_return_some_of_top(s);
}

void lily_prelude_ByteString_size(lily_state *s)
{
    lily_bytestring_val *input_bv = lily_arg_bytestring(s, 0);
    uint32_t input_size = lily_bytestring_length(input_bv);

    lily_return_integer(s, input_size);
}

/* This table indicates how many more bytes need to be successfully read after
   that particular byte for proper utf-8. 0 = invalid. */
static const uint8_t follower_table[256] =
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
/* F */ 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

int get_slice_range(lily_state *s, uint32_t max, uint32_t *start,
        uint32_t *stop)
{
    uint16_t count = lily_arg_count(s);
    int64_t raw_start, raw_stop;

    if (count == 1) {
        *start = 0;
        *stop = max;
        return 1;
    }

    if (count == 3) {
        raw_stop = lily_arg_integer(s, 2);

        if (raw_stop < 0)
            raw_stop += max;
    }
    else
        raw_stop = max;

    raw_start = lily_arg_integer(s, 1);

    if (raw_start < 0)
        raw_start += max;

    int ok = 1;

    if (raw_start >= 0 &&
        raw_start < raw_stop &&
        raw_stop <= max) {
        *start = (uint32_t)raw_start;
        *stop = (uint32_t)raw_stop;
    }
    else
        ok = 0;

    return ok;
}

void do_str_slice(lily_state *s, int is_bytestring)
{
    lily_string_val *input_sv = lily_arg_string(s, 0);
    char *input_str = lily_string_raw(input_sv);
    uint32_t input_size = lily_string_length(input_sv);
    uint32_t start, stop;
    int ok = get_slice_range(s, input_size, &start, &stop);

    if (ok == 0) {
        if (is_bytestring == 0)
            lily_push_string(s, "");
        else
            lily_push_bytestring(s, "", 0);

        return;
    }

    if (is_bytestring == 0) {
        if (start != 0 &&
            follower_table[(unsigned char)input_str[start]] == 0)
            ok = 0;
        else if (stop != input_size &&
            follower_table[(unsigned char)input_str[stop]] == 0)
            ok = 0;

        if (ok == 0) {
            lily_push_string(s, "");
            return;
        }
    }

    if (is_bytestring == 0)
        lily_push_string_sized(s, input_str + start, stop - start);
    else
        lily_push_bytestring(s, input_str + start, stop - start);
}

void lily_prelude_ByteString_slice(lily_state *s)
{
    do_str_slice(s, 1);
    lily_return_top(s);
}

void lily_prelude_DivisionByZeroError_new(lily_state *s)
{
    return_exception(s, LILY_ID_DBZERROR);
}

void lily_prelude_Double_to_i(lily_state *s)
{
    int64_t integer_val = (int64_t)lily_arg_double(s, 0);

    lily_return_integer(s, integer_val);
}

void lily_prelude_Exception_new(lily_state *s)
{
    return_exception(s, LILY_ID_EXCEPTION);
}

void lily_prelude_File_close(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);

    if (filev->close_func) {
        filev->close_func(filev->inner_file);
        filev->close_func = NULL;
    }

    lily_return_unit(s);
}

static int read_file_line(lily_msgbuf *msgbuf, FILE *source)
{
    char read_buffer[128];
    int pos = 0;
    int total_pos = 0;

    /* This uses fgetc in a loop because fgets may read in \0's, but doesn't
       tell how much was written. */
    while (1) {
        int ch = fgetc(source);

        if (ch == EOF)
            break;

        if (pos == sizeof(read_buffer)) {
            lily_mb_add_sized(msgbuf, read_buffer, sizeof(read_buffer));
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
        lily_mb_add_sized(msgbuf, read_buffer, pos);
        total_pos += pos;
    }

    return total_pos;
}

void lily_prelude_File_each_line(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    FILE *f = lily_file_for_read(s, filev);
    lily_msgbuf *vm_buffer = lily_msgbuf_get(s);

    lily_call_prepare(s, lily_arg_function(s, 1));

    while (1) {
        int total_bytes = read_file_line(vm_buffer, f);

        if (total_bytes == 0)
            break;

        const char *text = lily_mb_raw(vm_buffer);

        lily_push_bytestring(s, text, total_bytes);
        lily_call(s, 1);
        lily_mb_flush(vm_buffer);
    }

    lily_return_unit(s);
}

void lily_prelude_File_flush(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    FILE *f = lily_file_for_write(s, filev);

    fflush(f);
    lily_return_unit(s);
}

/* This wraps around fclose because fclose returns int. */
static void file_close(FILE *f)
{
    fclose(f);
}

static FILE *open_file(lily_state *s, char *path, char *mode)
{
    errno = 0;

    FILE *result = fopen(path, mode);

    if (result == NULL) {
        char buffer[LILY_STRERROR_BUFFER_SIZE];

        lily_strerror(buffer);
        lily_IOError(s, "Errno %d: %s (%s).", errno, buffer, path);
    }

    return result;
}

void lily_prelude_File_open(lily_state *s)
{
    char *path = lily_arg_string_raw(s, 0);
    char *mode = lily_arg_string_raw(s, 1);
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
        lily_IOError(s, "Invalid mode '%s' given.", mode);

    FILE *f = open_file(s, path, mode);

    lily_push_file(s, f, mode, file_close);
    lily_return_top(s);
}

void lily_prelude_File_write(lily_state *);

void lily_prelude_File_print(lily_state *s)
{
    lily_prelude_File_write(s);
    fputc('\n', lily_file_for_write(s, lily_arg_file(s, 0)));
    lily_return_unit(s);
}

void lily_prelude_File_read(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s,0);
    FILE *raw_file = lily_file_for_read(s, filev);
    int need = -1;

    if (lily_arg_count(s) == 2) {
        need = lily_arg_integer(s, 1);

        /* For simplicity, reduce all negative arguments to -1. */
        if (need < -1)
            need = -1;
    }

    int bufsize = 64;
    char *buffer = lily_malloc(bufsize * sizeof(*buffer));
    int pos = 0;
    int nbuf = bufsize/2;
    int nread;

    while (1) {
        int to_read;
        /* Read either the max possible, or the rest that's needed. */
        if (need == -1 || need > nbuf)
            to_read = nbuf;
        else
            to_read = need;

        nread = fread(buffer+pos, 1, to_read, raw_file);
        pos += nread;

        if (pos >= bufsize) {
            nbuf = bufsize;
            bufsize *= 2;
            buffer = lily_realloc(buffer, bufsize * sizeof(*buffer));
        }

        /* Done if EOF hit (first), or got what was wanted (second). */
        if (nread < to_read || (pos >= need && need != -1)) {
            buffer[pos] = '\0';
            break;
        }
        else if (to_read != -1)
            to_read -= nread;
    }

    lily_push_bytestring(s, buffer, pos);
    lily_free(buffer);
    lily_return_top(s);
}

void lily_prelude_File_read_line(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    lily_msgbuf *vm_buffer = lily_msgbuf_get(s);
    FILE *f = lily_file_for_read(s, filev);
    int byte_count = read_file_line(vm_buffer, f);
    const char *text = lily_mb_raw(vm_buffer);

    lily_push_bytestring(s, text, byte_count);
    lily_return_top(s);
}

void lily_prelude_File_read_to_string(lily_state *s)
{
    char *path = lily_arg_string_raw(s, 0);
    FILE *f = open_file(s, path, "r");
    int bufsize = 64;
    char *buffer = lily_malloc(bufsize * sizeof(*buffer));
    int pos = 0;
    int nbuf = bufsize/2;
    int nread;

    while (1) {
        int to_read = nbuf;

        nread = fread(buffer+pos, 1, to_read, f);
        pos += nread;

        if (pos >= bufsize) {
            nbuf = bufsize;
            bufsize *= 2;
            buffer = lily_realloc(buffer, bufsize * sizeof(*buffer));
        }

        if (nread < to_read) {
            buffer[pos] = '\0';
            break;
        }
    }

    /* This is a convenience method, so gloss over the details. */
    if (lily_is_valid_sized_utf8(buffer, pos) == 0)
        lily_ValueError(s, "File '%s' contains invalid utf-8.", path);

    fclose(f);
    lily_push_string(s, buffer);
    lily_free(buffer);
    lily_return_top(s);
}

void lily_prelude_File_write(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    lily_value *to_write = lily_arg_value(s, 1);
    FILE *inner_file = lily_file_for_write(s, filev);

    if (to_write->flags & V_STRING_FLAG)
        fputs(to_write->value.string->string, inner_file);
    else {
        lily_msgbuf *msgbuf = lily_msgbuf_get(s);

        lily_mb_add_value(msgbuf, s, to_write);
        fputs(lily_mb_raw(msgbuf), inner_file);
    }

    lily_return_unit(s);
}

void lily_prelude_File_write_to_path(lily_state *s)
{
    char *path = lily_arg_string_raw(s, 0);
    FILE *f = open_file(s, path, "w");
    lily_value *to_write = lily_arg_value(s, 1);

    if (to_write->flags & V_STRING_FLAG)
        fputs(to_write->value.string->string, f);
    else {
        lily_msgbuf *msgbuf = lily_msgbuf_get(s);

        lily_mb_add_value(msgbuf, s, to_write);
        fputs(lily_mb_raw(msgbuf), f);
    }

    fclose(f);
    lily_return_unit(s);
}

static inline void remove_key_check(lily_state *s, lily_hash_val *hash_val)
{
    if (hash_val->iter_count)
        lily_RuntimeError(s, "Cannot remove key from hash during iteration.");
}

static void destroy_hash_elems(lily_hash_val *hash_val)
{
    int i;

    for (i = 0;i < hash_val->num_bins;i++) {
        lily_hash_entry *entry = hash_val->bins[i];
        lily_hash_entry *next_entry;

        while (entry) {
            lily_deref(entry->boxed_key);
            lily_free(entry->boxed_key);
            lily_deref(entry->record);
            lily_free(entry->record);

            next_entry = entry->next;
            lily_free(entry);
            entry = next_entry;
        }

        hash_val->bins[i] = NULL;
    }
}

void lily_destroy_hash(lily_value *v)
{
    lily_hash_val *hv = v->value.hash;

    destroy_hash_elems(hv);
    lily_free(hv->bins);
    lily_free(hv);
}

void lily_prelude_Hash_clear(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    remove_key_check(s, hash_val);
    destroy_hash_elems(hash_val);
    hash_val->num_entries = 0;
    lily_return_unit(s);
}

void lily_prelude_Hash_delete(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_value *key = lily_arg_value(s, 1);

    remove_key_check(s, hash_val);

    if (lily_hash_take(s, hash_val, key)) {
        lily_stack_drop_top(s);
        lily_stack_drop_top(s);
    }

    lily_return_unit(s);
}

static void hash_iter_callback(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    hash_val->iter_count--;
}

typedef void (*hash_each_fn)(lily_state *, lily_hash_val *);

static void each_value(lily_state *s, lily_hash_val *hash_val)
{
    int i;

    for (i = 0;i < hash_val->num_bins;i++) {
        lily_hash_entry *entry = hash_val->bins[i];

        while (entry) {
            lily_push_value(s, entry->record);
            lily_call(s, 1);
            entry = entry->next;
        }
    }
}

static void each_pair(lily_state *s, lily_hash_val *hash_val)
{
    int i;

    for (i = 0;i < hash_val->num_bins;i++) {
        lily_hash_entry *entry = hash_val->bins[i];

        while (entry) {
            lily_push_value(s, entry->boxed_key);
            lily_push_value(s, entry->record);
            lily_call(s, 2);
            entry = entry->next;
        }
    }
}

static void hash_each(lily_state *s, hash_each_fn fn)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_error_callback_push(s, hash_iter_callback);
    lily_call_prepare(s, lily_arg_function(s, 1));
    hash_val->iter_count++;
    fn(s, hash_val);
    lily_error_callback_pop(s);
    hash_val->iter_count--;
}

void lily_prelude_Hash_each_value(lily_state *s)
{
    hash_each(s, each_value);
    lily_return_unit(s);
}

void lily_prelude_Hash_each_pair(lily_state *s)
{
    hash_each(s, each_pair);
    lily_return_unit(s);
}

void lily_prelude_Hash_get(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_value *key = lily_arg_value(s, 1);
    lily_value *record = lily_hash_get(s, hash_val, key);

    if (record) {
        lily_push_value(s, record);
        lily_return_some_of_top(s);
    }
    else
        lily_return_none(s);
}

void lily_prelude_Hash_has_key(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_value *key = lily_arg_value(s, 1);
    lily_value *entry = lily_hash_get(s, hash_val, key);

    lily_return_boolean(s, entry != NULL);
}

void lily_prelude_Hash_keys(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    uint32_t size = (uint32_t)hash_val->num_entries;
    lily_container_val *result_lv = lily_push_list(s, size);
    int i;
    uint32_t list_i;

    for (i = 0, list_i = 0;i < hash_val->num_bins;i++) {
        lily_hash_entry *entry = hash_val->bins[i];

        while (entry) {
            lily_con_set(result_lv, list_i, entry->boxed_key);
            list_i++;
            entry = entry->next;
        }
    }

    lily_return_top(s);
}

void lily_prelude_Hash_map_values(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_call_prepare(s, lily_arg_function(s, 1));

    lily_value *result = lily_call_result(s);
    lily_hash_val *h = lily_push_hash(s, hash_val->num_entries);
    int i;

    lily_error_callback_push(s, hash_iter_callback);

    for (i = 0;i < hash_val->num_bins;i++) {
        lily_hash_entry *entry = hash_val->bins[i];

        while (entry) {
            lily_push_value(s, entry->record);
            lily_call(s, 1);
            lily_hash_set(s, h, entry->boxed_key, result);
            entry = entry->next;
        }
    }

    hash_val->iter_count--;
    lily_error_callback_pop(s);
    lily_return_top(s);
}

void lily_prelude_Hash_merge(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_container_val *to_merge = lily_arg_container(s, 1);
    uint32_t hash_size = (uint32_t)hash_val->num_entries;
    uint32_t merge_count = lily_con_size(to_merge);
    lily_hash_val *result_hash = lily_push_hash(s, hash_size);
    int bin_i;
    uint32_t merge_i;
  
    for (bin_i = 0;bin_i < hash_val->num_bins;bin_i++) {
        lily_hash_entry *entry = hash_val->bins[bin_i];

        while (entry) {
            lily_hash_set(s, result_hash, entry->boxed_key, entry->record);
            entry = entry->next;
        }
    }

    for (merge_i = 0;merge_i < merge_count;merge_i++) {
        lily_value *v = lily_con_get(to_merge, merge_i);
        lily_hash_val *merging_hash = lily_as_hash(v);

        for (bin_i = 0;bin_i < merging_hash->num_bins;bin_i++) {
            lily_hash_entry *entry = merging_hash->bins[bin_i];

            while (entry) {
                lily_hash_set(s, result_hash, entry->boxed_key, entry->record);
                entry = entry->next;
            }
        }
    }

    lily_return_top(s);
}

static void hash_select_reject_common(lily_state *s, int expect)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));
    lily_value *result = lily_call_result(s);
    lily_hash_val *h = lily_push_hash(s, hash_val->num_entries);
    int i;

    lily_error_callback_push(s, hash_iter_callback);
    hash_val->iter_count++;

    for (i = 0;i < hash_val->num_bins;i++) {
        lily_hash_entry *entry = hash_val->bins[i];

        while (entry) {
            lily_push_value(s, entry->boxed_key);
            lily_push_value(s, entry->record);
            lily_push_value(s, entry->boxed_key);
            lily_push_value(s, entry->record);
            lily_call(s, 2);

            if (lily_as_boolean(result) != expect) {
                lily_stack_drop_top(s);
                lily_stack_drop_top(s);
            }
            else
                lily_hash_set_from_stack(s, h);

            entry = entry->next;
        }
    }

    hash_val->iter_count--;
    lily_error_callback_pop(s);
}

void lily_prelude_Hash_reject(lily_state *s)
{
    hash_select_reject_common(s, 0);
    lily_return_top(s);
}

void lily_prelude_Hash_select(lily_state *s)
{
    hash_select_reject_common(s, 1);
    lily_return_top(s);
}

void lily_prelude_Hash_size(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_return_integer(s, hash_val->num_entries);
}

void lily_prelude_IndexError_new(lily_state *s)
{
    return_exception(s, LILY_ID_INDEXERROR);
}

void lily_prelude_Integer_to_binary(lily_state *s)
{
    uint64_t uvalue = (uint64_t)lily_arg_integer(s, 0);
    uint64_t mask = ((uint64_t)1) << 63;
    char buffer_start[128];
    char *buffer = buffer_start;
    int i;

    if (uvalue & mask) {
        *buffer = '-';
        buffer++;
        uvalue = ~uvalue + 1;
    }

    *buffer = '0';
    buffer++;
    *buffer = 'b';
    buffer++;

    for (i = 63;i > 0;i--, mask >>= 1) {
        if (uvalue & mask)
            break;
    }

    for (;i > 0;i--, mask >>= 1) {
        uint64_t v = (uvalue & mask) >> i;

        *buffer = '0' + (char)v;
        buffer++;
    }

    *buffer = '0' + (uvalue & 1);
    buffer++;
    *buffer = '\0';

    lily_return_string(s, buffer_start);
}

void lily_prelude_Integer_to_bool(lily_state *s)
{
    /* Use !! or `x == true` will fail. */
    lily_return_boolean(s, !!lily_arg_integer(s, 0));
}

void lily_prelude_Integer_to_byte(lily_state *s)
{
    lily_return_byte(s, lily_arg_integer(s, 0) & 0xFF);
}

void lily_prelude_Integer_to_d(lily_state *s)
{
    double doubleval = (double)lily_arg_integer(s, 0);

    lily_return_double(s, doubleval);
}

static void hex_octal(lily_state *s, int is_hex)
{
    uint64_t value = (uint64_t)lily_arg_integer(s, 0);
    uint64_t negative = ((uint64_t)1) << 63;
    char *sign = "";
    char buffer[32];

    if (value & negative) {
        sign = "-";
        value = ~value + 1;
    }

    if (is_hex)
        sprintf(buffer, "%s0x%"PRIx64, sign, value);
    else
        sprintf(buffer, "%s0c%"PRIo64, sign, value);

    lily_return_string(s, buffer);
}

void lily_prelude_Integer_to_hex(lily_state *s)
{
    hex_octal(s, 1);
}

void lily_prelude_Integer_to_octal(lily_state *s)
{
    hex_octal(s, 0);
}

void lily_prelude_Integer_to_s(lily_state *s)
{
    int64_t integer_val = lily_arg_integer(s, 0);
    char buffer[32];

    snprintf(buffer, 32, "%"PRId64, integer_val);
    lily_return_string(s, buffer);
}

void lily_prelude_IOError_new(lily_state *s)
{
    return_exception(s, LILY_ID_IOERROR);
}

void lily_prelude_KeyError_new(lily_state *s)
{
    return_exception(s, LILY_ID_KEYERROR);
}

static int list_any_all(lily_state *s, int stop_on)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    uint32_t size = lily_con_size(input_list);

    if (size == 0)
        return 1;

    lily_call_prepare(s, lily_arg_function(s, 1));

    lily_value *result = lily_call_result(s);
    uint32_t i;
    int ok = !stop_on;

    for (i = 0;i < lily_con_size(input_list);i++) {
        lily_value *v = lily_con_get(input_list, i);

        lily_push_value(s, v);
        lily_call(s, 1);

        if (lily_as_boolean(result) == stop_on) {
            ok = stop_on;
            break;
        }
    }

    return ok;
}

void lily_prelude_List_accumulate(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    lily_value *output = lily_arg_value(s, 1);
    uint32_t i;
    uint32_t input_size = lily_con_size(input_list);

    lily_call_prepare(s, lily_arg_function(s, 2));

    for (i = 0;i < input_size;i++) {
        lily_value *v = lily_con_get(input_list, i);

        lily_push_value(s, output);
        lily_push_value(s, v);
        lily_call(s, 2);
    }

    lily_return_value(s, output);
}

void lily_prelude_List_all(lily_state *s)
{
    lily_return_boolean(s, list_any_all(s, 0));
}

void lily_prelude_List_any(lily_state *s)
{
    lily_return_boolean(s, list_any_all(s, 1));
}

void lily_prelude_List_clear(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    uint32_t input_size = lily_con_size(input_list);
    uint32_t i;

    for (i = 0;i < input_size;i++) {
        lily_value *v = lily_con_get(input_list, i);

        lily_deref(v);
        lily_free(v);
    }

    input_list->extra_space += input_list->num_values;
    input_list->num_values = 0;
    lily_return_unit(s);
}

void lily_prelude_List_count(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);

    lily_call_prepare(s, lily_arg_function(s, 1));

    lily_value *result = lily_call_result(s);
    uint32_t total = 0;
    uint32_t i;

    for (i = 0;i < lily_con_size(input_list);i++) {
        lily_value *v = lily_con_get(input_list, i);

        lily_push_value(s, v);
        lily_call(s, 1);

        if (lily_as_boolean(result) == 1)
            total++;
    }

    lily_return_integer(s, (int64_t)total);
}

static uint32_t get_relative_index(lily_state *s, lily_container_val *list_val,
        int64_t pos)
{
    uint32_t list_size = lily_con_size(list_val);

    if (pos < 0) {
        int64_t old_pos = pos;

        pos += list_size;

        if (pos < 0 ||
            pos > list_size)
            lily_IndexError(s,
                    "Index %ld is too small for list (minimum: -%d)", old_pos,
                    list_size);
    }
    else if (pos > list_size)
        lily_IndexError(s, "Index %ld is too large for list (maximum: %d)",
                pos, list_size);

    return (uint32_t)pos;
}

void lily_prelude_List_delete_at(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    uint32_t input_size = lily_con_size(input_list);
    int64_t pos = lily_arg_integer(s, 1);

    if (input_size == 0)
        lily_IndexError(s, "Cannot delete from an empty list.");

    uint32_t fixed_pos = get_relative_index(s, input_list, pos);

    lily_list_take(s, input_list, fixed_pos);
    lily_stack_drop_top(s);
    lily_return_unit(s);
}

void lily_prelude_List_each(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    uint32_t i;

    lily_call_prepare(s, lily_arg_function(s, 1));

    for (i = 0;i < lily_con_size(input_list);i++) {
        lily_push_value(s, lily_con_get(input_list, i));
        lily_call(s, 1);
    }

    lily_return_value(s, lily_arg_value(s, 0));
}

void lily_prelude_List_each_index(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    uint32_t i;

    lily_call_prepare(s, lily_arg_function(s, 1));

    for (i = 0;i < lily_con_size(input_list);i++) {
        lily_push_integer(s, i);
        lily_call(s, 1);
    }

    lily_return_value(s, lily_arg_value(s, 0));
}

void lily_prelude_List_fill(lily_state *s)
{
    int64_t raw_stop = lily_arg_integer(s, 0);

    if (raw_stop <= 0 ||
        raw_stop >= (int64_t)UINT32_MAX) {
        lily_push_list(s, 0);
        lily_return_top(s);
        return;
    }

    lily_call_prepare(s, lily_arg_function(s, 1));

    uint32_t stop = (uint32_t)raw_stop;
    lily_container_val *con = lily_push_list(s, stop);
    lily_value *result = lily_call_result(s);
    uint32_t i;

    for (i = 0;i < stop;i++) {
        lily_push_integer(s, i);
        lily_call(s, 1);
        lily_con_set(con, i, result);
    }

    lily_return_top(s);
}

void lily_prelude_List_fold(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    lily_value *start = lily_arg_value(s, 1);

    if (lily_con_size(input_list) == 0) {
        lily_return_value(s, start);
        return;
    }

    lily_call_prepare(s, lily_arg_function(s, 2));

    lily_value *result = lily_call_result(s);
    uint32_t i = 0;

    lily_push_value(s, start);

    while (1) {
        lily_push_value(s, lily_con_get(input_list, i));
        lily_call(s, 2);
        i++;

        if (i >= lily_con_size(input_list))
            break;

        lily_push_value(s, result);
    }

    lily_return_value(s, result);
}


void lily_prelude_List_get(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    int64_t raw_pos = lily_arg_integer(s, 1);
    uint32_t input_size = lily_con_size(input_list);

    /* This does what get_relative_index does, except the error case doesn't
       raise an error. */
    if (raw_pos < 0)
        raw_pos += input_size;

    if (raw_pos >= 0 &&
        raw_pos < input_size) {
        uint32_t fixed_pos = (uint32_t)raw_pos;

        lily_push_value(s, lily_con_get(input_list, fixed_pos));
        lily_return_some_of_top(s);
    }
    else
        lily_return_none(s);
}

void lily_prelude_List_insert(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_container_val *input_list = lily_as_container(input_arg);
    int64_t insert_pos = lily_arg_integer(s, 1);
    lily_value *insert_value = lily_arg_value(s, 2);
    uint32_t fixed_pos = get_relative_index(s, input_list, insert_pos);

    lily_list_insert(input_list, fixed_pos, insert_value);
    lily_return_value(s, input_arg);
}

void lily_prelude_List_join(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    const char *delim = lily_optional_string_raw(s, 1, "");
    uint32_t input_size = lily_con_size(input_list);
    lily_msgbuf *vm_buffer = lily_msgbuf_get(s);

    if (input_size == 0) {
        lily_return_string(s, "");
        return;
    }

    input_size--;

    lily_value *v;
    uint32_t i;

    for (i = 0;i < input_size;i++) {
        v = lily_con_get(input_list, i);

        lily_mb_add_value(vm_buffer, s, v);
        lily_mb_add(vm_buffer, delim);
    }

    v = lily_con_get(input_list, i);
    lily_mb_add_value(vm_buffer, s, v);
    lily_return_string(s, lily_mb_raw(vm_buffer));
}

void lily_prelude_List_map(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);

    lily_call_prepare(s, lily_arg_function(s, 1));

    lily_container_val *result = lily_push_list(s, 0);
    lily_list_reserve(result, lily_con_size(input_list));

    uint32_t i;

    for (i = 0;i < lily_con_size(input_list);i++) {
        lily_value *v = lily_con_get(input_list, i);

        lily_push_value(s, v);
        lily_call(s, 1);
        lily_list_push(result, lily_call_result(s));
    }

    lily_return_top(s);
}

void lily_prelude_List_merge(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    lily_container_val *arg_list = lily_arg_container(s, 1);
    uint32_t input_size = lily_con_size(input_list);
    uint32_t arg_list_size = lily_con_size(arg_list);
    uint32_t result_size = input_size;
    uint32_t i;

    for (i = 0;i < arg_list_size;i++) {
        lily_container_val *arg = lily_as_container(lily_con_get(arg_list, i));
        uint32_t arg_size = lily_con_size(arg);

        result_size += arg_size;
    }

    lily_container_val *result = lily_push_list(s, result_size);
    uint32_t result_i = input_size;

    for (i = 0;i < input_size;i++)
        lily_con_set(result, i, lily_con_get(input_list, i));

    for (i = 0;i < arg_list_size;i++) {
        lily_container_val *arg = lily_as_container(lily_con_get(arg_list, i));
        uint32_t arg_size = lily_con_size(arg);
        uint32_t j;

        for (j = 0;j < arg_size;result_i++, j++)
            lily_con_set(result, result_i, lily_con_get(arg, j));
    }

    lily_return_top(s);
}

void lily_prelude_List_pop(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    uint32_t input_size = lily_con_size(input_list);

    if (input_size == 0)
        lily_IndexError(s, "Pop from an empty list.");

    input_size--;

    lily_list_take(s, input_list, input_size);
    lily_return_top(s);
}

void lily_prelude_List_push(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *insert_value = lily_arg_value(s, 1);
    lily_container_val *input_list = lily_as_container(input_arg);

    lily_list_insert(input_list, lily_con_size(input_list), insert_value);
    lily_return_value(s, input_arg);
}

static void list_select_reject_common(lily_state *s, int expect)
{
    lily_container_val *input_list = lily_arg_container(s, 0);

    lily_call_prepare(s, lily_arg_function(s, 1));

    lily_value *result = lily_call_result(s);
    lily_container_val *con = lily_push_list(s, 0);
    uint32_t i = 0;

    if (lily_con_size(input_list) == 0)
        return;

    while (1) {
        if (i >= lily_con_size(input_list))
            break;

        lily_value *v = lily_con_get(input_list, i);

        lily_push_value(s, v);
        lily_call(s, 1);
        i++;

        if (i > lily_con_size(input_list))
            break;

        if (lily_as_boolean(result) == expect)
            lily_list_push(con, v);
    }
}

void lily_prelude_List_reject(lily_state *s)
{
    list_select_reject_common(s, 0);
    lily_return_top(s);
}

void lily_prelude_List_repeat(lily_state *s)
{
    int64_t raw_count = lily_arg_integer(s, 0);

    if (raw_count < 0)
        lily_ValueError(s, "Repeat count must be >= 0 (%ld given).",
                (int64_t)raw_count);

    /* This ceiling isn't mentioned in the above message since it shouldn't be
       an issue. */
    if (raw_count > (int64_t)UINT32_MAX)
        lily_ValueError(s, "Repeat count is far too large (%ld given).",
                (int64_t)raw_count);

    lily_value *to_repeat = lily_arg_value(s, 1);
    uint32_t count = (uint32_t)raw_count;
    lily_container_val *result = lily_push_list(s, count);
    uint32_t i;

    for (i = 0;i < count;i++)
        lily_con_set(result, i, to_repeat);

    lily_return_top(s);
}

void lily_prelude_List_reverse(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    uint32_t size = lily_con_size(input_list);
    uint32_t end = size;
    uint32_t i = 0;
    lily_container_val *result = lily_push_list(s, size);

    if (end == 0) {
        lily_return_top(s);
        return;
    }

    end--;

    for (;i != size;i++, end--) {
        lily_value *input_v = lily_con_get(input_list, end);

        lily_con_set(result, i, input_v);
    }

    lily_return_top(s);
}

void lily_prelude_List_select(lily_state *s)
{
    list_select_reject_common(s, 1);
    lily_return_top(s);
}

void lily_prelude_List_size(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);

    lily_return_integer(s, lily_con_size(input_list));
}

void lily_prelude_List_shift(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);

    if (lily_con_size(input_list) == 0)
        lily_IndexError(s, "Shift on an empty list.");

    lily_list_take(s, input_list, 0);
    lily_return_top(s);
}

void lily_prelude_List_slice(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    uint32_t input_size = lily_con_size(input_list);
    uint32_t source_i, stop;
    int ok = get_slice_range(s, input_size, &source_i, &stop);

    if (ok == 0) {
        lily_push_list(s, 0);
        lily_return_top(s);
        return;
    }

    uint32_t new_size = stop - source_i;
    lily_container_val *result = lily_push_list(s, new_size);
    uint32_t result_i;

    for (result_i = 0;
         result_i < new_size;
         source_i++, result_i++) {
        lily_value *v = lily_con_get(input_list, source_i);

        lily_con_set(result, result_i, v);
    }

    lily_return_top(s);
}

void lily_prelude_List_unshift(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *unshift_arg = lily_arg_value(s, 1);
    lily_container_val *input_list = lily_as_container(input_arg);

    lily_list_insert(input_list, 0, unshift_arg);
    lily_return_value(s, input_arg);
}

void lily_prelude_List_zip(lily_state *s)
{
    lily_container_val *input_list = lily_arg_container(s, 0);
    lily_container_val *all_others = lily_arg_container(s, 1);
    uint32_t other_list_size = lily_con_size(all_others);
    uint32_t result_size = lily_con_size(input_list);
    uint32_t row_i, column_i;

    /* Since Lily can't have unset values, clamp the result List to the size of
       the smallest List. */
    for (row_i = 0;row_i < other_list_size;row_i++) {
        lily_value *other_value = lily_con_get(all_others, row_i);
        lily_container_val *other_elem = lily_as_container(other_value);
        uint32_t elem_size = lily_con_size(other_elem);

        if (result_size > elem_size)
            result_size = elem_size;
    }

    lily_container_val *result_list = lily_push_list(s, result_size);
    uint32_t result_width = other_list_size + 1;

    for (row_i = 0;row_i < result_size;row_i++) {
        /* For each row, create a Tuple and fill in the columns. */
        lily_container_val *tup = lily_push_tuple(s, result_width);

        lily_con_set(tup, 0, lily_con_get(input_list, row_i));

        for (column_i = 0;column_i < other_list_size;column_i++) {
            /* Take the [column] element from the List at [row]. */
            lily_value *other_value = lily_con_get(all_others, column_i);
            lily_container_val *other_elem = lily_as_container(other_value);

            lily_con_set(tup, column_i + 1, lily_con_get(other_elem, row_i));
        }

        lily_con_set_from_stack(s, result_list, row_i);
    }

    lily_return_top(s);
}

void lily_prelude_Option_and(lily_state *s)
{
    if (lily_arg_is_some(s, 0))
        lily_return_value(s, lily_arg_value(s, 1));
    else
        lily_return_value(s, lily_arg_value(s, 0));
}

void lily_prelude_Option_and_then(lily_state *s)
{
    if (lily_arg_is_some(s, 0)) {
        lily_container_val *con = lily_arg_container(s, 0);

        lily_call_prepare(s, lily_arg_function(s, 1));
        lily_push_value(s, lily_con_get(con, 0));
        lily_call(s, 1);
        lily_return_value(s, lily_call_result(s));
    }
    else
        lily_return_none(s);
}

void lily_prelude_Option_is_none(lily_state *s)
{
    lily_return_boolean(s, lily_arg_is_some(s, 0) == 0);
}

void lily_prelude_Option_is_some(lily_state *s)
{
    lily_return_boolean(s, lily_arg_is_some(s, 0));
}

void lily_prelude_Option_map(lily_state *s)
{
    if (lily_arg_is_some(s, 0)) {
        lily_container_val *con = lily_arg_container(s, 0);

        lily_call_prepare(s, lily_arg_function(s, 1));
        lily_push_value(s, lily_con_get(con, 0));
        lily_call(s, 1);
        lily_push_value(s, lily_call_result(s));
        lily_return_some_of_top(s);
    }
    else
        lily_return_none(s);
}

void lily_prelude_Option_or(lily_state *s)
{
    if (lily_arg_is_some(s, 0))
        lily_return_value(s, lily_arg_value(s, 0));
    else
        lily_return_value(s, lily_arg_value(s, 1));
}

void lily_prelude_Option_or_else(lily_state *s)
{
    if (lily_arg_is_some(s, 0))
        lily_return_value(s, lily_arg_value(s, 0));
    else {
        lily_call_prepare(s, lily_arg_function(s, 1));
        lily_call(s, 0);
        lily_return_value(s, lily_call_result(s));
    }
}

void lily_prelude_Option_unwrap(lily_state *s)
{
    if (lily_arg_is_some(s, 0)) {
        lily_container_val *con = lily_arg_container(s, 0);

        lily_return_value(s, lily_con_get(con, 0));
    }
    else
        lily_ValueError(s, "unwrap called on None.");
}

void lily_prelude_Option_unwrap_or(lily_state *s)
{
    lily_value *source;

    if (lily_arg_is_some(s, 0)) {
        lily_container_val *con = lily_arg_container(s, 0);

        source = lily_con_get(con, 0);
    }
    else
        source = lily_arg_value(s, 1);

    lily_return_value(s, source);
}

void lily_prelude_Option_unwrap_or_else(lily_state *s)
{
    if (lily_arg_is_some(s, 0)) {
        lily_container_val *con = lily_arg_container(s, 0);

        lily_return_value(s, lily_con_get(con, 0));
    }
    else {
        lily_call_prepare(s, lily_arg_function(s, 1));
        lily_call(s, 0);

        lily_return_value(s, lily_call_result(s));
    }
}

static void result_optionize(lily_state *s, int expect)
{
    if (lily_arg_is_success(s, 0) == expect) {
        lily_container_val *con = lily_arg_container(s, 0);
        lily_container_val *variant = lily_push_some(s);

        lily_con_set(variant, 0, lily_con_get(con, 0));
    }
    else
        lily_push_none(s);
}

void lily_prelude_Result_failure(lily_state *s)
{
    result_optionize(s, 0);
    lily_return_top(s);
}

void lily_prelude_Result_is_failure(lily_state *s)
{
    lily_return_boolean(s, lily_arg_is_failure(s, 0));
}

void lily_prelude_Result_is_success(lily_state *s)
{
    lily_return_boolean(s, lily_arg_is_success(s, 0));
}

void lily_prelude_Result_success(lily_state *s)
{
    result_optionize(s, 1);
    lily_return_top(s);
}

void lily_prelude_RuntimeError_new(lily_state *s)
{
    return_exception(s, LILY_ID_RUNTIMEERROR);
}

void lily_prelude_String_format(lily_state *s)
{
    const char *fmt = lily_arg_string_raw(s, 0);
    lily_container_val *args = lily_arg_container(s, 1);
    uint32_t empty_iter_i = 0;
    uint32_t args_size = lily_con_size(args);
    lily_msgbuf *msgbuf = lily_msgbuf_get(s);
    uint32_t len = strlen(fmt);
    uint32_t text_start = 0;
    uint32_t i;

    for (i = 0;i < len;i++) {
        char c = fmt[i];

        if (c == '{') {
            if (i != text_start)
                lily_mb_add_slice(msgbuf, fmt, text_start, i);

            i++;

            if (fmt[i] == '{') {
                lily_mb_add_char(msgbuf, '{');
                text_start = i + 1;
                continue;
            }

            uint32_t start = i;
            uint32_t total = 0;
            char ch;

            while (1) {
                ch = fmt[i];

                if (isdigit(ch) == 0 || total > 99)
                    break;

                total = (total * 10) + (ch - '0');
                i++;
            }

            if (i == start) {
                if (ch == '}') {
                    total = empty_iter_i;
                    empty_iter_i++;
                }
                else if (ch == '\0')
                    lily_ValueError(s, "Format specifier is empty.");
                else
                    lily_ValueError(s, "Format specifier is not numeric.");
            }

            if (total > 99)
                lily_ValueError(s, "Format must be between 0...99.");
            else if (total >= args_size)
                lily_IndexError(s, "Format specifier is too large.");
            else if (fmt[i] != '}')
                lily_ValueError(s, "Format specifier is not closed.");

            lily_value *v = lily_con_get(args, total);

            lily_mb_add_value(msgbuf, s, v);
            text_start = i + 1;
        }
        else if (c == '}') {
            i++;

            if (fmt[i] == '}') {
                lily_mb_add_char(msgbuf, '}');
                text_start = i + 1;
                continue;
            }

            lily_ValueError(s, "Unescaped '}' in format string.");
        }
    }

    if (i != text_start)
        lily_mb_add_slice(msgbuf, fmt, text_start, i);

    lily_return_string(s, lily_mb_raw(msgbuf));
}

void lily_prelude_String_ends_with(lily_state *s)
{
    lily_string_val *input_sv = lily_arg_string(s, 0);
    lily_string_val *suffix_sv = lily_arg_string(s, 1);
    char *input_raw = lily_string_raw(input_sv);
    char *suffix_raw = lily_string_raw(suffix_sv);
    uint32_t input_size = lily_string_length(input_sv);
    uint32_t suffix_size = lily_string_length(suffix_sv);

    if (suffix_size > input_size) {
        lily_return_boolean(s, 0);
        return;
    }

    char *adjusted_input = input_raw + input_size - suffix_size;
    int ok = strcmp(adjusted_input, suffix_raw) == 0;

    lily_return_boolean(s, ok);
}

void lily_prelude_String_find(lily_state *s)
{
    lily_string_val *input_sv = lily_arg_string(s, 0);
    lily_string_val *find_sv = lily_arg_string(s, 1);
    uint32_t input_size = lily_string_length(input_sv);
    uint32_t find_size = lily_string_length(find_sv);
    int64_t raw_start = 0;
    uint32_t start;

    if (lily_arg_count(s) == 2)
        start = 0;
    else {
        raw_start = lily_arg_integer(s, 2);

        if (raw_start < 0)
            raw_start += input_size;

        if (raw_start >= input_size) {
            lily_return_none(s);
            return;
        }

        start = (uint32_t)raw_start;
    }

    if (find_size == 0 ||
        find_size > input_size)
    {
        lily_return_none(s);
        return;
    }

    const char *input_str = lily_string_raw(input_sv);
    const char *find_str = lily_string_raw(find_sv);
    char *result = strstr(input_str + start, find_str);

    if (result == NULL) {
        lily_return_none(s);
        return;
    }

    lily_push_integer(s, (int64_t)(result - input_str));
    lily_return_some_of_top(s);
}

void lily_prelude_String_html_encode(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    const char *raw = lily_as_string_raw(input_arg);
    lily_msgbuf *msgbuf = lily_msgbuf_get(s);

    /* If nothing was escaped, output what was input. */
    if (lily_mb_html_escape(msgbuf, raw) == raw)
        /* The `String` given may be a cached literal, so return the input arg
           instead of making a new `String`. */
        lily_return_value(s, input_arg);
    else
        lily_return_string(s, lily_mb_raw(msgbuf));
}

#define CTYPE_WRAP(WRAP_NAME, WRAPPED_CALL) \
void lily_prelude_String_##WRAP_NAME(lily_state *s) \
{ \
    lily_string_val *input_sv = lily_arg_string(s, 0); \
    uint32_t input_size = lily_string_length(input_sv); \
\
    if (input_size == 0) { \
        lily_return_boolean(s, 0); \
        return; \
    } \
\
    const char *input_str = lily_string_raw(input_sv); \
    int ok = 1; \
\
    while (*input_str) { \
        unsigned char ch = (unsigned char)*input_str; \
\
        if (WRAPPED_CALL(ch) == 0) { \
            ok = 0; \
            break; \
        } \
\
        input_str++; \
    } \
\
    lily_return_boolean(s, ok); \
}

CTYPE_WRAP(is_alnum, isalnum)

CTYPE_WRAP(is_alpha, isalpha)

CTYPE_WRAP(is_digit, isdigit)

CTYPE_WRAP(is_space, isspace)

void lily_prelude_String_lower(lily_state *s)
{
    lily_string_val *input_sv = lily_arg_string(s, 0);
    uint32_t input_size = lily_string_length(input_sv);
    uint32_t i;

    lily_push_string(s, lily_string_raw(input_sv));

    char *raw_out = lily_as_string_raw(lily_stack_get_top(s));

    for (i = 0;i < input_size;i++) {
        int ch = raw_out[i];

        if (isupper(ch))
            raw_out[i] = tolower(ch);
    }

    lily_return_top(s);
}

static int lstrip_utf8_start(lily_string_val *input_sv, const char *strip_str)
{
    const char *input_str = lily_string_raw(input_sv);
    const char *input_end = input_str + lily_string_length(input_sv);
    const char *input_iter = input_str;
    const char *strip_iter = strip_str;

    while (*strip_iter) {
        uint8_t follow_count = follower_table[(unsigned char)*strip_str];
        uint8_t i;

        if ((input_iter + follow_count) > input_end) {
            strip_iter += follow_count;
            continue;
        }

        const char *input_next = input_iter;

        for (i = 0;
             i < follow_count;
             i++, input_next++, strip_iter++) {
            if (*input_next != *strip_iter)
                break;
        }

        if (i != follow_count)
            strip_iter = strip_iter - i + follow_count;
        else {
            strip_iter = strip_str;
            input_iter = input_next;
        }
    }

    int result = (int)(input_iter - input_str);

    return result;
}

static int is_utf8(const char *str)
{
    int result = 0;

    while (*str) {
        unsigned char ch = (unsigned char)*str;

        if (ch > 127) {
            result = 1;
            break;
        }

        str++;
    }

    return result;
}

void lily_prelude_String_lstrip(lily_state *s)
{
    lily_string_val *input_sv = lily_arg_string(s, 0);
    lily_string_val *strip_sv = lily_arg_string(s, 1);
    uint32_t input_size = lily_string_length(input_sv);
    uint32_t strip_size = lily_string_length(strip_sv);

    if (input_size == 0 ||
        strip_size == 0) {
        lily_return_value(s, lily_arg_value(s, 0));
        return;
    }

    const char *input_str = lily_string_raw(input_sv);
    const char *strip_str = lily_string_raw(strip_sv);
    int have_utf8 = is_utf8(strip_str);

    if (have_utf8 == 0)
        input_str += strspn(input_str, strip_str);
    else
        input_str += lstrip_utf8_start(input_sv, strip_str);

    lily_return_string(s, input_str);
}

void lily_prelude_String_parse_i(lily_state *s)
{
    char *input = lily_arg_string_raw(s, 0);
    int ok = 1;
    int64_t result = lily_scan_number(input, &ok);

    if (ok) {
        lily_push_integer(s, result);
        lily_return_some_of_top(s);
    }
    else
        lily_return_none(s);
}

void lily_prelude_String_replace(lily_state *s)
{
    lily_string_val *input_sv = lily_arg_string(s, 0);
    lily_string_val *needle_sv = lily_arg_string(s, 1);
    const char *replace_with = lily_arg_string_raw(s, 2);
    uint32_t source_len = lily_string_length(input_sv);
    uint32_t needle_len = lily_string_length(needle_sv);

    if (needle_len > source_len ||
        needle_len == 0) {
        lily_return_value(s, lily_arg_value(s, 0));
        return;
    }

    lily_msgbuf *msgbuf = lily_msgbuf_get(s);
    const char *source_raw = lily_string_raw(input_sv);
    const char *needle_raw = lily_string_raw(needle_sv);
    const char *input_iter = strstr(source_raw, needle_raw);

    if (input_iter == NULL) {
        lily_return_value(s, lily_arg_value(s, 0));
        return;
    }

    const char *last_iter = source_raw;

    do {
        int offset = (int)(input_iter - last_iter);

        if (offset)
            lily_mb_add_sized(msgbuf, last_iter, offset);

        lily_mb_add(msgbuf, replace_with);
        last_iter = input_iter + needle_len;
        input_iter = strstr(last_iter, needle_raw);
    } while (input_iter);

    lily_mb_add(msgbuf, last_iter);
    lily_return_string(s, lily_mb_raw(msgbuf));
}

/* This is a helper for rstrip when there's no utf-8 in input_arg. */
static int rstrip_ascii_stop(lily_string_val *input_sv, const char *strip_str)
{
    const char *input_str = lily_string_raw(input_sv);
    const char *input_begin = input_str - 1;
    const char *input_end = input_str + lily_string_length(input_sv) - 1;
    const char *input_iter = input_end;
    const char *strip_iter = strip_str;

    while (*strip_iter) {
        if (*strip_iter == *input_iter) {
            input_iter--;

            if (input_iter == input_begin)
                break;

            strip_iter = strip_str;
        }
        else
            strip_iter++;
    }

    int result = (int)(input_iter - input_begin);

    return result;
}

/* Helper for rstrip, for when there is some utf-8. */
static int rstrip_utf8_stop(lily_string_val *input_sv, const char *strip_str)
{
    const char *input_str = lily_string_raw(input_sv);
    const char *input_begin = input_str - 1;
    const char *input_end = input_str + lily_string_length(input_sv) - 1;
    const char *input_iter = input_end;
    const char *strip_iter = strip_str;

    while (*strip_iter) {
        uint8_t follow_count = follower_table[(unsigned char)*strip_str];
        const char *input_next = input_iter - follow_count + 1;
        uint8_t i;

        if ((input_iter - follow_count) < input_begin) {
            strip_iter += follow_count;
            continue;
        }

        for (i = 0;
             i < follow_count;
             i++, input_next++, strip_iter++) {
            if (*input_next != *strip_iter)
                break;
        }

        if (i != follow_count)
            strip_iter = strip_iter - i + follow_count;
        else {
            strip_iter = strip_str;
            input_iter -= follow_count;
        }
    }

    int result = (int)(input_iter - input_begin);

    return result;
}


void lily_prelude_String_rstrip(lily_state *s)
{
    lily_string_val *input_sv = lily_arg_string(s, 0);
    lily_string_val *strip_sv = lily_arg_string(s, 1);
    uint32_t input_size = lily_string_length(input_sv);
    uint32_t strip_size = lily_string_length(strip_sv);

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_size == 0 ||
        strip_size == 0) {
        lily_return_value(s, lily_arg_value(s, 0));
        return;
    }

    const char *strip_str = lily_string_raw(strip_sv);
    int have_utf8 = is_utf8(strip_str);
    int copy_to;

    if (have_utf8 == 0)
        copy_to = rstrip_ascii_stop(input_sv, strip_str);
    else
        copy_to = rstrip_utf8_stop(input_sv, strip_str);

    const char *input_str = lily_string_raw(input_sv);

    lily_push_string_sized(s, input_str, copy_to);
    lily_return_top(s);
}

static uint32_t count_split_elements(const char *input, const char *splitby,
        int split_len, uint32_t max)
{
    uint32_t result = 0;
    uint32_t i = 0;

    while (1) {
        input = strstr(input, splitby);
        result++;

        if (input == NULL || i == max)
            break;

        input = input + split_len;
        i++;
    }

    return result;
}

static void string_split_by_val(lily_state *s, char *input, char *splitby,
        uint32_t max)
{
    int split_len = strlen(splitby);
    uint32_t values_needed = count_split_elements(input, splitby, split_len,
            max);
    lily_container_val *list_val = lily_push_list(s, values_needed);
    uint32_t i = 0;

    while (1) {
        char *input_next = strstr(input, splitby);

        if (input_next == NULL || i == max)
            break;

        int offset = (int)(input_next - input);

        lily_push_string_sized(s, input, offset);
        lily_con_set_from_stack(s, list_val, i);
        i++;
        input = input_next + split_len;
    }

    lily_push_string(s, input);
    lily_con_set_from_stack(s, list_val, i);
}

void lily_prelude_String_size(lily_state *s)
{
    lily_string_val *input_sv = lily_arg_string(s, 0);

    lily_return_integer(s, (int64_t)lily_string_length(input_sv));
}

void lily_prelude_String_slice(lily_state *s)
{
    do_str_slice(s, 0);
    lily_return_top(s);
}

void lily_prelude_String_split(lily_state *s)
{
    lily_string_val *input_strval = lily_arg_string(s, 0);
    lily_string_val *split_strval;
    lily_string_val fake_sv;
    uint32_t max = UINT32_MAX;

    switch (lily_arg_count(s)) {
        case 3: {
            int64_t integer_val = lily_arg_integer(s, 2);

            if (integer_val < 0 ||
                integer_val > (int64_t)UINT32_MAX)
                max = UINT32_MAX;
            else
                max = (uint32_t)integer_val;
        }
        /* Fallthrough to process what to split by. */
        case 2:
            split_strval = lily_arg_string(s, 1);

            if (lily_string_length(split_strval) == 0)
                lily_ValueError(s, "Cannot split by an empty string.");

            break;
        /* Always 1, but 'default' prevents a compiler warning. */
        default:
            fake_sv.string = " ";
            fake_sv.size = 1;
            split_strval = &fake_sv;
            break;
    }

    string_split_by_val(s, input_strval->string, split_strval->string, max);
    lily_return_top(s);
}

void lily_prelude_String_starts_with(lily_state *s)
{
    lily_string_val *input_sv = lily_arg_string(s, 0);
    lily_string_val *prefix_sv = lily_arg_string(s, 1);
    char *input_raw = lily_string_raw(input_sv);
    char *prefix_raw = lily_string_raw(prefix_sv);
    uint32_t input_size = lily_string_length(input_sv);
    uint32_t prefix_size = lily_string_length(prefix_sv);

    if (input_size < prefix_size ||
        *input_raw != *prefix_raw) {
        lily_return_boolean(s, 0);
        return;
    }

    int ok = strncmp(input_raw, prefix_raw, prefix_size) == 0;

    lily_return_boolean(s, ok);
}

void lily_prelude_String_strip(lily_state *s)
{
    lily_string_val *input_sv = lily_arg_string(s, 0);
    lily_string_val *strip_sv = lily_arg_string(s, 1);
    uint32_t input_size = lily_string_length(input_sv);
    uint32_t strip_size = lily_string_length(strip_sv);

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_size == 0 || strip_size == 0) {
        lily_return_value(s, lily_arg_value(s, 0));
        return;
    }

    const char *input_str = lily_string_raw(input_sv);
    const char *strip_str = lily_string_raw(strip_sv);
    int have_utf8 = is_utf8(strip_str);
    int copy_from, copy_to;

    if (have_utf8 == 0)
        copy_from = (int)strspn(input_str, strip_str);
    else
        copy_from = lstrip_utf8_start(input_sv, strip_str);

    if (*(input_str + copy_from) == '\0') {
        lily_return_string(s, "");
        return;
    }

    if (have_utf8 == 0)
        copy_to = rstrip_ascii_stop(input_sv, strip_str);
    else
        copy_to = rstrip_utf8_stop(input_sv, strip_str);

    lily_push_string_sized(s, input_str + copy_from, copy_to - copy_from);
    lily_return_top(s);
}

void lily_prelude_String_to_bytestring(lily_state *s)
{
    /* They currently have the same internal representation. This method is
       provided for the type system. */
    lily_string_val *sv = lily_arg_string(s, 0);

    lily_push_bytestring(s, lily_string_raw(sv), lily_string_length(sv));
    lily_return_top(s);
}

void lily_prelude_String_trim(lily_state *s)
{
    lily_string_val *input_sv = lily_arg_string(s, 0);
    const char *input_str = lily_string_raw(input_sv);
    const char *to_skip = " \t\r\n";
    size_t span = strspn(input_str, to_skip);

    input_str += span;

    if (*input_str == '\0') {
        lily_return_string(s, "");
        return;
    }

    int end = rstrip_ascii_stop(input_sv, to_skip) - (int)span;

    lily_push_string_sized(s, input_str, end);
    lily_return_top(s);
}

void lily_prelude_String_upper(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);

    lily_push_string(s, lily_as_string_raw(input_arg));

    char *raw_out = lily_as_string_raw(lily_stack_get_top(s));

    while (*raw_out) {
        char ch = *raw_out;

        if (islower(ch))
            *raw_out = toupper(ch);

        raw_out++;
    }

    lily_return_top(s);
}

void lily_prelude_ValueError_new(lily_state *s)
{
    return_exception(s, LILY_ID_VALUEERROR);
}

static void file_builtin_close(FILE *f)
{
    (void)f;
}

static void new_builtin_file(lily_state *s, FILE *source, const char *mode)
{
    lily_push_file(s, source, mode, file_builtin_close);
}

void lily_prelude_var_stdin(lily_state *s)
{
    new_builtin_file(s, stdin, "r");
}

void lily_prelude_var_stdout(lily_state *s)
{
    new_builtin_file(s, stdout, "w");
}

void lily_prelude_var_stderr(lily_state *s)
{
    new_builtin_file(s, stderr, "w");
}

static lily_class *build_class(lily_symtab *symtab, const char *name,
        int generic_count, int dyna_start)
{
    lily_class *result = lily_new_class(symtab, name, 0);
    result->item_kind = ITEM_CLASS_FOREIGN;
    result->dyna_start = dyna_start;
    result->generic_count = generic_count;

    return result;
}

/* This handles building classes for which no concrete values will ever exist.
   Giving them a sequential id is a waste because the vm will want to eventually
   scoop it up into the class table. So don't do that. */
static lily_class *build_special(lily_symtab *symtab, const char *name,
        int generic_count, int id, int visible)
{
    lily_class *result = lily_new_class(symtab, name, 0);
    result->item_kind = ITEM_CLASS_FOREIGN;
    result->id = id;
    result->generic_count = generic_count;

    symtab->next_class_id--;

    if (visible == 0) {
        symtab->active_module->class_chain = result->next;
        result->next = symtab->hidden_class_chain;
        symtab->hidden_class_chain = result;
    }

    return result;
}

void lily_init_pkg_prelude(lily_symtab *symtab)
{
    symtab->integer_class    = build_class(symtab, "Integer",     0, Integer_OFFSET);
    symtab->double_class     = build_class(symtab, "Double",      0, Double_OFFSET);
    symtab->string_class     = build_class(symtab, "String",      0, String_OFFSET);
    symtab->byte_class       = build_class(symtab, "Byte",        0, Byte_OFFSET);
    symtab->bytestring_class = build_class(symtab, "ByteString",  0, ByteString_OFFSET);
    symtab->boolean_class    = build_class(symtab, "Boolean",     0, Boolean_OFFSET);
    symtab->function_class   = build_class(symtab, "Function",   -1, Function_OFFSET);
    symtab->list_class       = build_class(symtab, "List",        1, List_OFFSET);
    symtab->hash_class       = build_class(symtab, "Hash",        2, Hash_OFFSET);
    symtab->tuple_class      = build_class(symtab, "Tuple",      -1, Tuple_OFFSET);
                               build_class(symtab, "File",        0, File_OFFSET);

    symtab->optarg_class   = build_special(symtab, "*", 1, LILY_ID_OPTARG, 0);
    lily_class *unit_cls   = build_special(symtab, "Unit", 0, LILY_ID_UNIT, 1);

    /* The `Unit` type is readonly since it's referenced often. A class is
       created for it too so it's visible for searches. It's the only time that
       a monomorphic class isn't the self type for itself. */
    unit_cls->self_type = lily_unit_type;
    /* This must be set here so that it bubbles up in type building. */
    symtab->function_class->flags |= CLS_GC_TAGGED;
    /* HACK: This ensures that there is space to dynaload builtin classes and
       enums into. */
    symtab->next_class_id = START_CLASS_ID;
}

/* The call table expects to find these here, but they're in the vm. */
void lily_prelude__print(lily_state *);
void lily_prelude__calltrace(lily_state *);

LILY_DECLARE_PRELUDE_CALL_TABLE

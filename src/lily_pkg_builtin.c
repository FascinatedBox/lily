#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "lily.h"
#include "lily_alloc.h"
#include "lily_parser.h"
#include "lily_symtab.h"
#include "lily_utf8.h"
#include "lily_value_flags.h"
#include "lily_value_raw.h"
#include "lily_value_structs.h"
#define LILY_NO_EXPORT
#include "lily_pkg_builtin_bindings.h"

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
    (uint16_t)-1, \
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

void lily_builtin_Boolean_to_i(lily_state *s)
{
    lily_return_integer(s, lily_arg_boolean(s, 0));
}

void lily_builtin_Boolean_to_s(lily_state *s)
{
    int input = lily_arg_boolean(s, 0);
    char *to_copy;

    if (input == 0)
        to_copy = "false";
    else
        to_copy = "true";

    lily_push_string(s, to_copy);
    lily_return_top(s);
}

void lily_builtin_Byte_to_i(lily_state *s)
{
    lily_return_integer(s, lily_arg_byte(s, 0));
}

void lily_builtin_ByteString_each_byte(lily_state *s)
{
    lily_bytestring_val *sv = lily_arg_bytestring(s, 0);
    const char *input = lily_bytestring_raw(sv);
    int len = lily_bytestring_length(sv);
    int i;

    lily_call_prepare(s, lily_arg_function(s, 1));

    for (i = 0;i < len;i++) {
        lily_push_byte(s, (uint8_t)input[i]);
        lily_call(s, 1);
    }

    lily_return_unit(s);
}

void lily_builtin_ByteString_encode(lily_state *s)
{
    lily_bytestring_val *input_bytestring = lily_arg_bytestring(s, 0);
    const char *encode_method;

    if (lily_arg_count(s) == 2)
        encode_method = lily_arg_string_raw(s, 1);
    else
        encode_method = "error";

    char *byte_buffer = NULL;

    if (strcmp(encode_method, "error") == 0) {
        byte_buffer = lily_bytestring_raw(input_bytestring);
        int byte_buffer_size = lily_bytestring_length(input_bytestring);

        if (lily_is_valid_sized_utf8(byte_buffer, byte_buffer_size) == 0) {
            lily_return_none(s);
            return;
        }
    }
    else {
        lily_return_none(s);
        return;
    }

    lily_container_val *variant = lily_push_some(s);
    lily_push_string(s, byte_buffer);
    lily_con_set_from_stack(s, variant, 0);
    lily_return_top(s);
}

void lily_builtin_ByteString_size(lily_state *s)
{
    lily_return_integer(s, lily_arg_bytestring(s, 0)->size);
}

/* This table indicates how many more bytes need to be successfully read after
   that particular byte for proper utf-8. 0 = invalid.
   Table copied from lily_lexer.c */
static const uint8_t follower_table[256] =
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
/* 8 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 9 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* A */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* B */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* C */ 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* D */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* E */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
/* F */ 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

void do_str_slice(lily_state *s, int is_bytestring)
{
    lily_string_val *sv = lily_arg_string(s, 0);
    int64_t start = 0;
    int64_t stop = sv->size;

    switch (lily_arg_count(s)) {
        case 3: stop = lily_arg_integer(s, 2);
        case 2: start = lily_arg_integer(s, 1);
    }

    if (stop < 0)
        stop = sv->size + stop;
    if (start < 0)
        start = sv->size + start;

    if (stop > sv->size ||
        start > sv->size ||
        start > stop) {
        if (is_bytestring == 0)
            lily_push_string(s, "");
        else
            lily_push_bytestring(s, "", 0);

        lily_return_top(s);
        return;
    }

    char *raw = lily_string_raw(sv);
    if (is_bytestring == 0) {
        if (follower_table[(unsigned char)raw[start]] == 0 ||
            follower_table[(unsigned char)raw[stop]] == 0) {
            lily_push_string(s, "");
            lily_return_top(s);
            return;
        }
    }

    if (is_bytestring == 0)
        lily_push_string_sized(s, raw + start, stop - start);
    else
        lily_push_bytestring(s, raw + start, stop - start);

    lily_return_top(s);
}

void lily_builtin_ByteString_slice(lily_state *s)
{
    do_str_slice(s, 1);
}

void lily_builtin_DivisionByZeroError_new(lily_state *s)
{
    return_exception(s, LILY_ID_DBZERROR);
}

/* Coroutines are mostly implemented in the vm because much of what they do
   involves using internal vm magic. */


#define CORO_IS(name, to_check) \
void lily_builtin_Coroutine_is_##name(lily_state *s) \
{ \
    lily_coroutine_val *co_val = lily_arg_coroutine(s, 0); \
    lily_return_boolean(s, co_val->status == to_check); \
} \

CORO_IS(done, co_done)

CORO_IS(failed, co_failed)

CORO_IS(waiting, co_waiting)

CORO_IS(running, co_running)




void lily_builtin_Double_to_i(lily_state *s)
{
    int64_t integer_val = (int64_t)lily_arg_double(s, 0);

    lily_return_integer(s, integer_val);
}

void lily_builtin_Exception_new(lily_state *s)
{
    return_exception(s, LILY_ID_EXCEPTION);
}

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

static int read_file_line(lily_msgbuf *msgbuf, FILE *source)
{
    char read_buffer[128];
    int ch = 0, pos = 0, total_pos = 0;

    /* This uses fgetc in a loop because fgets may read in \0's, but doesn't
       tell how much was written. */
    while (1) {
        ch = fgetc(source);

        if (ch == EOF)
            break;

        if (pos == sizeof(read_buffer)) {
            lily_mb_add_slice(msgbuf, read_buffer, 0, sizeof(read_buffer));
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
        lily_mb_add_slice(msgbuf, read_buffer, 0, pos);
        total_pos += pos;
    }

    return total_pos;
}

void lily_builtin_File_each_line(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    lily_msgbuf *vm_buffer = lily_msgbuf_get(s);
    FILE *f = lily_file_for_read(s, filev);

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

void lily_builtin_File_flush(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    FILE *f = lily_file_for_write(s, filev);

    fflush(f);

    lily_return_unit(s);
}

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
        lily_IOError(s, "Invalid mode '%s' given.", mode);

    FILE *f = fopen(path, mode);
    if (f == NULL) {
        /* Assume that the message is of a reasonable sort of size. */
        char buffer[128];
#ifdef _WIN32
        strerror_s(buffer, sizeof(buffer), errno);
#else
        strerror_r(errno, buffer, sizeof(buffer));
#endif
        lily_IOError(s, "Errno %d: %s (%s).", errno, buffer, path);
    }

    lily_push_file(s, f, mode);
    lily_return_top(s);
}

void lily_builtin_File_write(lily_state *);

void lily_builtin_File_print(lily_state *s)
{
    lily_builtin_File_write(s);
    fputc('\n', lily_file_for_write(s, lily_arg_file(s, 0)));
    lily_return_unit(s);
}

void lily_builtin_File_read(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s,0);
    FILE *raw_file = lily_file_for_read(s, filev);
    int need = -1;
    if (lily_arg_count(s) == 2)
        need = lily_arg_integer(s, 1);

    /* For simplicity, reduce all negative arguments to -1. */
    if (need < -1)
        need = -1;

    int bufsize = 64;
    char *buffer = lily_malloc(bufsize * sizeof(*buffer));
    int pos = 0, nread;
    int nbuf = bufsize/2;

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

void lily_builtin_File_read_line(lily_state *s)
{
    lily_file_val *filev = lily_arg_file(s, 0);
    lily_msgbuf *vm_buffer = lily_msgbuf_get(s);
    FILE *f = lily_file_for_read(s, filev);
    int byte_count = read_file_line(vm_buffer, f);
    const char *text = lily_mb_raw(vm_buffer);

    lily_push_bytestring(s, text, byte_count);
    lily_return_top(s);
}

void lily_builtin_File_write(lily_state *s)
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

void lily_builtin_Hash_clear(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    remove_key_check(s, hash_val);
    destroy_hash_elems(hash_val);

    hash_val->num_entries = 0;

    lily_return_unit(s);
}

void lily_builtin_Hash_delete(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    remove_key_check(s, hash_val);

    lily_value *key = lily_arg_value(s, 1);

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

void lily_builtin_Hash_each_pair(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_error_callback_push(s, hash_iter_callback);
    lily_call_prepare(s, lily_arg_function(s, 1));
    hash_val->iter_count++;

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

    lily_error_callback_pop(s);
    hash_val->iter_count--;
    lily_return_unit(s);
}

void lily_builtin_Hash_get(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_value *key = lily_arg_value(s, 1);
    lily_value *record = lily_hash_get(s, hash_val, key);

    if (record) {
        lily_container_val *variant = lily_push_some(s);
        lily_con_set(variant, 0, record);
        lily_return_top(s);
    }
    else
        lily_return_none(s);
}

void lily_builtin_Hash_has_key(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_value *key = lily_arg_value(s, 1);

    lily_value *entry = lily_hash_get(s, hash_val, key);

    lily_return_boolean(s, entry != NULL);
}

void lily_builtin_Hash_keys(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);
    lily_container_val *result_lv = lily_push_list(s, hash_val->num_entries);
    int i, list_i;

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

void lily_builtin_Hash_map_values(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_call_prepare(s, lily_arg_function(s, 1));
    lily_value *result = lily_call_result(s);

    lily_error_callback_push(s, hash_iter_callback);

    lily_hash_val *h = lily_push_hash(s, hash_val->num_entries);

    int i;
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

void lily_builtin_Hash_merge(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_hash_val *result_hash = lily_push_hash(s, hash_val->num_entries);

    uint16_t i;
    int j;
  
    for (i = 0;i < hash_val->num_bins;i++) {
        lily_hash_entry *entry = hash_val->bins[i];
        while (entry) {
            lily_hash_set(s, result_hash, entry->boxed_key,
                    entry->record);
            entry = entry->next;
        }
    }

    lily_container_val *to_merge = lily_arg_container(s, 1);
    for (i = 0;i < to_merge->num_values;i++) {
        lily_hash_val *merging_hash = to_merge->values[i]->value.hash;
        for (j = 0;j < merging_hash->num_bins;j++) {
            lily_hash_entry *entry = merging_hash->bins[j];
            while (entry) {
                lily_hash_set(s, result_hash, entry->boxed_key,
                        entry->record);
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

    lily_error_callback_push(s, hash_iter_callback);

    hash_val->iter_count++;

    int i;
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
    lily_return_top(s);
}

void lily_builtin_Hash_reject(lily_state *s)
{
    hash_select_reject_common(s, 0);
}

void lily_builtin_Hash_select(lily_state *s)
{
    hash_select_reject_common(s, 1);
}

void lily_builtin_Hash_size(lily_state *s)
{
    lily_hash_val *hash_val = lily_arg_hash(s, 0);

    lily_return_integer(s, hash_val->num_entries);
}

void lily_builtin_IndexError_new(lily_state *s)
{
    return_exception(s, LILY_ID_INDEXERROR);
}

void lily_builtin_Integer_to_bool(lily_state *s)
{
    /* Use !! or `x == true` will fail. */
    lily_return_boolean(s, !!lily_arg_integer(s, 0));
}

void lily_builtin_Integer_to_byte(lily_state *s)
{
    lily_return_byte(s, lily_arg_integer(s, 0) & 0xFF);
}

void lily_builtin_Integer_to_d(lily_state *s)
{
    double doubleval = (double)lily_arg_integer(s, 0);

    lily_return_double(s, doubleval);
}

void lily_builtin_Integer_to_s(lily_state *s)
{
    int64_t integer_val = lily_arg_integer(s, 0);

    char buffer[32];
    snprintf(buffer, 32, "%"PRId64, integer_val);

    lily_push_string(s, buffer);
    lily_return_top(s);
}

void lily_builtin_IOError_new(lily_state *s)
{
    return_exception(s, LILY_ID_IOERROR);
}

void lily_builtin_KeyError_new(lily_state *s)
{
    return_exception(s, LILY_ID_KEYERROR);
}

void lily_builtin_List_clear(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    uint32_t i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_deref(list_val->values[i]);
        lily_free(list_val->values[i]);
    }

    list_val->extra_space += list_val->num_values;
    list_val->num_values = 0;

    lily_return_unit(s);
}

void lily_builtin_List_count(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));
    lily_value *result = lily_call_result(s);
    int count = 0;

    uint32_t i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_push_value(s, list_val->values[i]);
        lily_call(s, 1);

        if (lily_as_boolean(result) == 1)
            count++;
    }

    lily_return_integer(s, count);
}

static int64_t get_relative_index(lily_state *s, lily_container_val *list_val,
        int64_t pos)
{
    if (pos < 0) {
        uint64_t unsigned_pos = -(int64_t)pos;
        if (unsigned_pos > list_val->num_values)
            lily_IndexError(s, "Index %ld is too small for list (minimum: %ld)",
                    pos, -(int64_t)list_val->num_values);

        pos = list_val->num_values - unsigned_pos;
    }
    else if (pos > list_val->num_values) {
        lily_IndexError(s, "Index %ld is too large for list (maximum: %ld)",
                pos, (uint64_t)list_val->num_values);
    }

    return pos;
}

void lily_builtin_List_delete_at(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    int64_t pos = lily_arg_integer(s, 1);

    if (list_val->num_values == 0)
        lily_IndexError(s, "Cannot delete from an empty list.");

    pos = get_relative_index(s, list_val, pos);

    lily_list_take(s, list_val, pos);
    lily_return_top(s);
}

void lily_builtin_List_each(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));
    uint32_t i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_push_value(s, lily_con_get(list_val, i));
        lily_call(s, 1);
    }

    lily_return_value(s, lily_arg_value(s, 0));
}

void lily_builtin_List_each_index(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));

    uint32_t i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_push_integer(s, i);
        lily_call(s, 1);
    }

    lily_return_value(s, lily_arg_value(s, 0));
}

void lily_builtin_List_fold(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_value *start = lily_arg_value(s, 1);

    if (list_val->num_values == 0)
        lily_return_value(s, start);
    else {
        lily_call_prepare(s, lily_arg_function(s, 2));
        lily_value *result = lily_call_result(s);
        lily_push_value(s, start);
        uint32_t i = 0;
        while (1) {
            lily_push_value(s, lily_con_get(list_val, i));
            lily_call(s, 2);

            if (i == list_val->num_values - 1)
                break;

            lily_push_value(s, result);

            i++;
        }

        lily_return_value(s, result);
    }
}

void lily_builtin_List_fill(lily_state *s)
{
    int64_t stop = lily_arg_integer(s, 0);

    if (stop <= 0) {
        lily_push_list(s, 0);
        lily_return_top(s);
        return;
    }

    lily_call_prepare(s, lily_arg_function(s, 1));
    lily_container_val *con = lily_push_list(s, stop);
    lily_value *result = lily_call_result(s);
    int64_t i;

    for (i = 0;i < stop;i++) {
        lily_push_integer(s, i);
        lily_call(s, 1);
        lily_con_set(con, i, result);
    }

    lily_return_top(s);
}

void lily_builtin_List_get(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    int64_t pos = lily_arg_integer(s, 1);

    /* This does what get_relative_index does, except the error case doesn't
       raise an error. */
    if (pos < 0) {
        uint64_t unsigned_pos = -(int64_t)pos;
        if (unsigned_pos > list_val->num_values)
            pos = -1;
        else
            pos = list_val->num_values - unsigned_pos;
    }

    if (pos >= list_val->num_values)
        pos = -1;

    if (pos != -1) {
        lily_container_val *variant = lily_push_some(s);
        lily_con_set(variant, 0, lily_con_get(list_val, pos));
        lily_return_top(s);
    }
    else
        lily_return_none(s);
}

void lily_builtin_List_insert(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    int64_t insert_pos = lily_arg_integer(s, 1);
    lily_value *insert_value = lily_arg_value(s, 2);

    insert_pos = get_relative_index(s, list_val, insert_pos);

    lily_list_insert(list_val, insert_pos, insert_value);
    lily_return_unit(s);
}

void lily_builtin_List_join(lily_state *s)
{
    lily_container_val *lv = lily_arg_container(s, 0);
    const char *delim = "";
    if (lily_arg_count(s) == 2)
        delim = lily_arg_string_raw(s, 1);

    lily_msgbuf *vm_buffer = lily_msgbuf_get(s);

    if (lv->num_values) {
        int i, stop = lv->num_values - 1;
        lily_value **values = lv->values;
        for (i = 0;i < stop;i++) {
            lily_mb_add_value(vm_buffer, s, values[i]);
            lily_mb_add(vm_buffer, delim);
        }
        if (stop != -1)
            lily_mb_add_value(vm_buffer, s, values[i]);
    }

    lily_push_string(s, lily_mb_raw(vm_buffer));
    lily_return_top(s);
}

void lily_builtin_List_map(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);

    lily_call_prepare(s, lily_arg_function(s, 1));
    lily_container_val *con = lily_push_list(s, 0);
    lily_list_reserve(con, list_val->num_values);

    uint32_t i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_value *e = list_val->values[i];
        lily_push_value(s, e);
        lily_call(s, 1);
        lily_list_push(con, lily_call_result(s));
    }

    lily_return_top(s);
}

void lily_builtin_List_pop(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);

    if (list_val->num_values == 0)
        lily_IndexError(s, "Pop from an empty list.");

    lily_list_take(s, list_val, lily_con_size(list_val) - 1);
    lily_return_top(s);
}

void lily_builtin_List_push(lily_state *s)
{
    lily_value *list_arg = lily_arg_value(s, 0);
    lily_container_val *list_val = lily_as_container(list_arg);
    lily_value *insert_value = lily_arg_value(s, 1);

    lily_list_insert(list_val, lily_con_size(list_val), insert_value);
    lily_return_value(s, list_arg);
}

static void list_select_reject_common(lily_state *s, int expect)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));
    lily_value *result = lily_call_result(s);
    lily_container_val *con = lily_push_list(s, 0);

    uint32_t i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_push_value(s, list_val->values[i]);
        lily_call(s, 1);

        int ok = lily_as_boolean(result) == expect;

        if (ok)
            lily_list_push(con, list_val->values[i]);
    }

    lily_return_top(s);
}

void lily_builtin_List_reject(lily_state *s)
{
    list_select_reject_common(s, 0);
}

void lily_builtin_List_repeat(lily_state *s)
{
    int n = lily_arg_integer(s, 0);
    if (n < 0)
        lily_ValueError(s, "Repeat count must be >= 0 (%ld given).",
                (int64_t)n);

    lily_value *to_repeat = lily_arg_value(s, 1);
    lily_container_val *lv = lily_push_list(s, n);

    int i;
    for (i = 0;i < n;i++)
        lily_con_set(lv, i, to_repeat);

    lily_return_top(s);
}

void lily_builtin_List_select(lily_state *s)
{
    list_select_reject_common(s, 1);
}

void lily_builtin_List_size(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);

    lily_return_integer(s, list_val->num_values);
}

void lily_builtin_List_shift(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);

    if (lily_con_size(list_val) == 0)
        lily_IndexError(s, "Shift on an empty list.");

    lily_list_take(s, list_val, 0);
    lily_return_top(s);
    return;
}

void lily_builtin_List_slice(lily_state *s)
{
    lily_container_val *lv = lily_arg_container(s, 0);
    int start = 0;
    int size = lily_con_size(lv);
    int stop = size;

    switch (lily_arg_count(s)) {
        case 3: stop = lily_arg_integer(s, 2);
        case 2: start = lily_arg_integer(s, 1);
    }

    if (stop < 0)
        stop = size + stop;
    if (start < 0)
        start = size + start;

    if (stop > size ||
        start > size ||
        start > stop) {
        lily_push_list(s, 0);
        lily_return_top(s);
        return;
    }

    int new_size = (stop - start);
    lily_container_val *new_lv = lily_push_list(s, new_size);
    int i, j;

    for (i = 0, j = start;i < new_size;i++, j++) {
        lily_con_set(new_lv, i, lily_con_get(lv, j));
    }

    lily_return_top(s);
}

void lily_builtin_List_unshift(lily_state *s)
{
    lily_value *list_arg = lily_arg_value(s, 0);
    lily_value *input_arg = lily_arg_value(s, 1);
    lily_container_val *list_val = lily_as_container(list_arg);

    lily_list_insert(list_val, 0, input_arg);
    lily_return_value(s, list_arg);
}

void lily_builtin_List_zip(lily_state *s)
{
    lily_container_val *list_val = lily_arg_container(s, 0);
    lily_container_val *all_others = lily_arg_container(s, 1);
    int other_list_count = lily_con_size(all_others);
    int result_size = lily_con_size(list_val);
    int row_i, column_i;

    /* Since Lily can't have unset values, clamp the result List to the size of
       the smallest List. */
    for (row_i = 0;row_i < other_list_count;row_i++) {
        lily_value *other_value = lily_con_get(all_others, row_i);
        lily_container_val *other_elem = lily_as_container(other_value);
        int elem_size = lily_con_size(other_elem);

        if (result_size > elem_size)
            result_size = elem_size;
    }

    lily_container_val *result_list = lily_push_list(s, result_size);
    int result_width = other_list_count + 1;

    for (row_i = 0;row_i < result_size;row_i++) {
        /* For each row, create a Tuple and fill in the columns. */
        lily_container_val *tup = lily_push_tuple(s, result_width);

        lily_con_set(tup, 0, lily_con_get(list_val, row_i));

        for (column_i = 0;column_i < other_list_count;column_i++) {
            /* Take the [column] element from the List at [row]. To avoid having
               a cache the size of 'others', this re-extracts containers. */
            lily_value *other_value = lily_con_get(all_others, column_i);
            lily_container_val *other_elem = lily_as_container(other_value);
            lily_con_set(tup, column_i + 1, lily_con_get(other_elem, row_i));
        }

        lily_con_set_from_stack(s, result_list, row_i);
    }

    lily_return_top(s);
}

void lily_builtin_Option_and(lily_state *s)
{
    if (lily_arg_is_some(s, 0))
        lily_return_value(s, lily_arg_value(s, 1));
    else
        lily_return_value(s, lily_arg_value(s, 0));
}

void lily_builtin_Option_and_then(lily_state *s)
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

void lily_builtin_Option_is_none(lily_state *s)
{
    lily_return_boolean(s, lily_arg_is_some(s, 0) == 0);
}

void lily_builtin_Option_is_some(lily_state *s)
{
    lily_return_boolean(s, lily_arg_is_some(s, 0));
}

void lily_builtin_Option_map(lily_state *s)
{
    if (lily_arg_is_some(s, 0)) {
        lily_container_val *con = lily_arg_container(s, 0);
        lily_call_prepare(s, lily_arg_function(s, 1));
        lily_push_value(s, lily_con_get(con, 0));
        lily_call(s, 1);

        lily_container_val *variant = lily_push_some(s);
        lily_con_set(variant, 0, lily_call_result(s));
        lily_return_top(s);
    }
    else
        lily_return_none(s);
}

void lily_builtin_Option_or(lily_state *s)
{
    if (lily_arg_is_some(s, 0))
        lily_return_value(s, lily_arg_value(s, 0));
    else
        lily_return_value(s, lily_arg_value(s, 1));
}

void lily_builtin_Option_or_else(lily_state *s)
{
    if (lily_arg_is_some(s, 0))
        lily_return_value(s, lily_arg_value(s, 0));
    else {
        lily_call_prepare(s, lily_arg_function(s, 1));
        lily_call(s, 0);

        lily_return_value(s, lily_call_result(s));
    }
}

void lily_builtin_Option_unwrap(lily_state *s)
{
    if (lily_arg_is_some(s, 0)) {
        lily_container_val *con = lily_arg_container(s, 0);
        lily_return_value(s, lily_con_get(con, 0));
    }
    else
        lily_ValueError(s, "unwrap called on None.");
}

void lily_builtin_Option_unwrap_or(lily_state *s)
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

void lily_builtin_Option_unwrap_or_else(lily_state *s)
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
        lily_return_top(s);
    }
    else
        lily_return_none(s);
}

void lily_builtin_Result_failure(lily_state *s)
{
    result_optionize(s, 0);
}

static void result_is_success_or_failure(lily_state *s, int expect)
{
    lily_return_boolean(s, lily_arg_is_success(s, 0) == expect);
}

void lily_builtin_Result_is_failure(lily_state *s)
{
    result_is_success_or_failure(s, 0);
}

void lily_builtin_Result_is_success(lily_state *s)
{
    result_is_success_or_failure(s, 1);
}

void lily_builtin_Result_success(lily_state *s)
{
    result_optionize(s, 1);
}

void lily_builtin_RuntimeError_new(lily_state *s)
{
    return_exception(s, LILY_ID_RUNTIMEERROR);
}

static int char_index(const char *s, int idx, char ch)
{
    const char *P = strchr(s + idx,ch);
    if (P == NULL)
        return -1;
    else
        return (int)((uintptr_t)P - (uintptr_t)s);
}

void lily_builtin_String_format(lily_state *s)
{
    const char *fmt = lily_arg_string_raw(s, 0);
    lily_container_val *lv = lily_arg_container(s, 1);

    int lsize = lily_con_size(lv);
    lily_msgbuf *msgbuf = lily_msgbuf_get(s);

    int idx, last_idx = 0;

    while (1) {
        idx = char_index(fmt, last_idx, '{');
        if (idx > -1) {
            if (idx > last_idx)
                lily_mb_add_slice(msgbuf, fmt, last_idx, idx);

            char ch;
            int i, total = 0;
            int start = idx + 1;

            /* Ignore any leading zeroes, but cap at 2 digits. */
            do {
                idx++;
                ch = fmt[idx];
            } while (ch == '0');

            for (i = 0;i < 2;i++) {
                if (isdigit(ch) == 0)
                    break;

                total = (total * 10) + (ch - '0');
                idx++;
                ch = fmt[idx];
            }

            if (isdigit(ch))
                lily_ValueError(s, "Format must be between 0...99.");
            else if (start == idx) {
                if (ch == '}' || ch == '\0')
                    lily_ValueError(s, "Format specifier is empty.");
                else
                    lily_ValueError(s, "Format specifier is not numeric.");
            }
            else if (total >= lsize)
                lily_IndexError(s, "Format specifier is too large.");

            idx++;
            last_idx = idx;

            lily_value *v = lily_con_get(lv, total);
            lily_mb_add_value(msgbuf, s, v);
        }
        else {
            lily_mb_add_slice(msgbuf, fmt, last_idx, strlen(fmt));
            break;
        }
    }

    lily_push_string(s, lily_mb_raw(msgbuf));
    lily_return_top(s);
}

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

void lily_builtin_String_find(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *find_arg = lily_arg_value(s, 1);
    int start = 0;
    if (lily_arg_count(s) == 3)
        start = lily_arg_integer(s, 2);

    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    char *find_str = find_arg->value.string->string;
    int find_length = find_arg->value.string->size;

    if (find_length > input_length ||
        find_length == 0 ||
        start > input_length ||
        follower_table[(unsigned char)input_str[start]] == 0) {
        lily_return_none(s);
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
    for (i = start;i <= length_diff;i++) {
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
        lily_container_val *variant = lily_push_some(s);

        lily_push_integer(s, i);
        lily_con_set_from_stack(s, variant, 0);

        lily_return_top(s);
    }
    else
        lily_return_none(s);
}

void lily_builtin_String_html_encode(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    const char *raw = lily_as_string_raw(input_arg);
    lily_msgbuf *msgbuf = lily_msgbuf_get(s);

    /* If nothing was escaped, output what was input. */
    if (lily_mb_html_escape(msgbuf, raw) == raw)
        /* The `String` given may be a cached literal, so return the input arg
           instead of making a new `String`. */
        lily_return_value(s, input_arg);
    else {
        lily_push_string(s, lily_mb_raw(msgbuf));
        lily_return_top(s);
    }
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
    const char *loop_str = lily_string_raw(input); \
    int i = 0; \
    int ok = 1; \
\
    for (i = 0;i < length;i++) { \
        unsigned char ch = (unsigned char)loop_str[i]; \
        if (WRAPPED_CALL(ch) == 0) { \
            ok = 0; \
            break; \
        } \
    } \
\
    lily_return_boolean(s, ok); \
}

CTYPE_WRAP(is_alnum, isalnum)

CTYPE_WRAP(is_alpha, isalpha)

CTYPE_WRAP(is_digit, isdigit)

CTYPE_WRAP(is_space, isspace)

void lily_builtin_String_lower(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    uint32_t input_length = input_arg->value.string->size;
    uint32_t i;

    lily_push_string(s, lily_as_string_raw(input_arg));

    char *raw_out = lily_as_string_raw(lily_stack_get_top(s));

    for (i = 0;i < input_length;i++) {
        int ch = raw_out[i];
        if (isupper(ch))
            raw_out[i] = tolower(ch);
    }

    lily_return_top(s);
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
static uint32_t lstrip_ascii_start(lily_value *input_arg,
        lily_string_val *strip_sv)
{
    char *input_str = input_arg->value.string->string;
    uint32_t i;
    uint32_t input_length = input_arg->value.string->size;

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
        uint32_t strip_length = strip_sv->size;
        for (i = 0;i < input_length;i++) {
            char ch = input_str[i];
            int found = 0;
            uint32_t j;
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

void lily_builtin_String_lstrip(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *strip_arg = lily_arg_value(s, 1);

    char *strip_str;
    unsigned char ch;
    uint32_t copy_from, i, strip_str_len;
    lily_string_val *strip_sv;
    int has_multibyte_char = 0;

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_arg->value.string->size == 0 ||
        strip_arg->value.string->size == 0) {
        lily_return_value(s, input_arg);
        return;
    }

    strip_sv = strip_arg->value.string;
    strip_str = strip_sv->string;
    strip_str_len = (uint32_t)strlen(strip_str);
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

    const char *raw = input_arg->value.string->string + copy_from;
    int size = input_arg->value.string->size;

    lily_push_string_sized(s, raw, size - copy_from);
    lily_return_top(s);
}

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
        lily_return_none(s);
    }
    else {
        int64_t signed_value;

        if (is_negative == 0)
            signed_value = (int64_t)value;
        else
            signed_value = -(int64_t)value;

        lily_container_val *variant = lily_push_some(s);

        lily_push_integer(s, signed_value);
        lily_con_set_from_stack(s, variant, 0);

        lily_return_top(s);
    }
}

void lily_builtin_String_replace(lily_state *s)
{
    lily_string_val *source_sv = lily_arg_string(s, 0);
    lily_string_val *needle_sv = lily_arg_string(s, 1);
    int source_len = lily_string_length(source_sv);
    int needle_len = lily_string_length(needle_sv);

    if (needle_len > source_len) {
        lily_return_value(s, lily_arg_value(s, 0));
        return;
    }

    lily_msgbuf *msgbuf = lily_msgbuf_get(s);
    char *source = lily_string_raw(source_sv);
    char *needle = lily_string_raw(needle_sv);
    char *replace_with = lily_arg_string_raw(s, 2);
    char needle_first = *needle;
    char ch;
    int start = 0;
    int i;

    for (i = 0;i < source_len;i++) {
        ch = source[i];
        if (ch == needle_first &&
            (i + needle_len) <= source_len) {
            int match = 1;
            int j;
            for (j = 1;j < needle_len;j++) {
                if (needle[j] != source[i + j])
                    match = 0;
            }

            if (match) {
                if (i != start)
                    lily_mb_add_slice(msgbuf, source, start, i);

                lily_mb_add(msgbuf, replace_with);
                i += needle_len - 1;
                start = i + 1;
            }
        }
    }

    if (i != start)
        lily_mb_add_slice(msgbuf, source, start, i);

    lily_push_string(s, lily_mb_raw(msgbuf));
    lily_return_top(s);
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

void lily_builtin_String_rstrip(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *strip_arg = lily_arg_value(s, 1);

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_arg->value.string->size == 0 ||
        strip_arg->value.string->size == 0) {
        lily_return_value(s, input_arg);
        return;
    }

    lily_string_val *strip_sv = strip_arg->value.string;
    char *strip_str = strip_sv->string;
    uint32_t strip_str_len = (uint32_t)strlen(strip_str);
    int has_multibyte_char = 0;
    unsigned char ch;
    uint32_t copy_to, i;

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

    const char *raw = input_arg->value.string->string;

    lily_push_string_sized(s, raw, copy_to);
    lily_return_top(s);
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

static void string_split_by_val(lily_state *s, char *input, char *splitby)
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
                    input_ch = restore_ch + 1;
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
    lily_container_val *list_val = lily_push_list(s, values_needed);
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
            lily_push_string_sized(s, last_start, size);
            lily_con_set_from_stack(s, list_val, i);

            i++;
            if (*input_ch == '\0')
                break;

            last_start = input_ch + 1;
        }

        input_ch++;
    }
}

void lily_builtin_String_size(lily_state *s)
{
    lily_return_integer(s, lily_arg_string(s, 0)->size);
}

void lily_builtin_String_slice(lily_state *s)
{
    do_str_slice(s, 0);
}

void lily_builtin_String_split(lily_state *s)
{
    lily_string_val *input_strval = lily_arg_string(s, 0);
    lily_string_val *split_strval;
    lily_string_val fake_sv;

    if (lily_arg_count(s) == 2) {
        split_strval = lily_arg_string(s, 1);
        if (split_strval->size == 0)
            lily_ValueError(s, "Cannot split by empty string.");
    }
    else {
        fake_sv.string = " ";
        fake_sv.size = 1;
        split_strval = &fake_sv;
    }

    string_split_by_val(s, input_strval->string, split_strval->string);
    lily_return_top(s);
}

void lily_builtin_String_starts_with(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    lily_value *prefix_arg = lily_arg_value(s, 1);

    char *input_raw_str = input_arg->value.string->string;
    char *prefix_raw_str = prefix_arg->value.string->string;
    int ok = 1;
    uint32_t prefix_size = prefix_arg->value.string->size;

    uint32_t i;

    if (input_arg->value.string->size < prefix_size) {
        lily_return_boolean(s, 0);
        return;
    }


    for (i = 0;i < prefix_size;i++) {
        if (input_raw_str[i] != prefix_raw_str[i]) {
            ok = 0;
            break;
        }
    }

    lily_return_boolean(s, ok);
}

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

    unsigned char ch;
    lily_string_val *strip_sv = strip_arg->value.string;
    char *strip_str = strip_sv->string;
    int has_multibyte_char = 0;
    uint32_t strip_str_len = (uint32_t)strlen(strip_str);
    uint32_t copy_from, copy_to, i;

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

    const char *raw = input_arg->value.string->string + copy_from;

    lily_push_string_sized(s, raw, copy_to - copy_from);
    lily_return_top(s);
}

void lily_builtin_String_to_bytestring(lily_state *s)
{
    /* They currently have the same internal representation. This method is
       provided for the type system. */
    lily_string_val *sv = lily_arg_string(s, 0);

    lily_push_bytestring(s, lily_string_raw(sv), lily_string_length(sv));
    lily_return_top(s);
}

void lily_builtin_String_trim(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    char fake_buffer[5] = " \t\r\n";
    lily_string_val fake_sv;

    fake_sv.string = fake_buffer;
    fake_sv.size = (uint32_t)strlen(fake_buffer);

    uint32_t copy_from = lstrip_ascii_start(input_arg, &fake_sv);

    if (copy_from != input_arg->value.string->size) {
        const char *raw = input_arg->value.string->string;
        uint32_t copy_to = rstrip_ascii_stop(input_arg, &fake_sv);

        lily_push_string_sized(s, raw + copy_from, copy_to - copy_from);
    }
    else {
        /* It's all space, so make a new empty string. */
        lily_push_string(s, "");
    }

    lily_return_top(s);
}

void lily_builtin_String_upper(lily_state *s)
{
    lily_value *input_arg = lily_arg_value(s, 0);
    int input_length = input_arg->value.string->size;
    int i;

    lily_push_string(s, lily_as_string_raw(input_arg));
    char *raw_out = lily_as_string_raw(lily_stack_get_top(s));

    for (i = 0;i < input_length;i++) {
        char ch = raw_out[i];
        if (islower(ch))
            raw_out[i] = toupper(ch);
    }

    lily_return_top(s);
}

void lily_builtin_ValueError_new(lily_state *s)
{
    return_exception(s, LILY_ID_VALUEERROR);
}

static void new_builtin_file(lily_state *s, FILE *source, const char *mode)
{
    lily_push_file(s, source, mode);
    lily_file_val *file_val = lily_as_file(lily_stack_get_top(s));
    file_val->is_builtin = 1;
}

void lily_builtin_var_stdin(lily_state *s)
{
    new_builtin_file(s, stdin, "r");
}

void lily_builtin_var_stdout(lily_state *s)
{
    new_builtin_file(s, stdout, "w");
}

void lily_builtin_var_stderr(lily_state *s)
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

void lily_init_pkg_builtin(lily_symtab *symtab)
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
void lily_builtin__print(lily_state *);
void lily_builtin__calltrace(lily_state *);

LILY_DECLARE_BUILTIN_CALL_TABLE

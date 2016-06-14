#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "lily_parser.h"
#include "lily_symtab.h"
#include "lily_utf8.h"

#include "lily_api_alloc.h"
#include "lily_api_value_ops.h"

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

/***
 *      ____              _                  
 *     | __ )  ___   ___ | | ___  __ _ _ __  
 *     |  _ \ / _ \ / _ \| |/ _ \/ _` | '_ \ 
 *     | |_) | (_) | (_) | |  __/ (_| | | | |
 *     |____/ \___/ \___/|_|\___|\__,_|_| |_|
 *                                          
 */

void lily_boolean_to_i(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_reg = vm_regs[code[1]];
    lily_value *result_reg = vm_regs[code[0]];

    lily_move_integer(result_reg, input_reg->value.integer);
}

void lily_boolean_to_s(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int64_t input = vm_regs[code[1]]->value.integer;
    lily_value *result_reg = vm_regs[code[0]];
    char *to_copy;

    if (input == 0)
        to_copy = "false";
    else
        to_copy = "true";

    lily_move_string(result_reg, lily_new_raw_string(to_copy));
}

/***
 *      ____        _       ____  _        _             
 *     | __ ) _   _| |_ ___/ ___|| |_ _ __(_)_ __   __ _ 
 *     |  _ \| | | | __/ _ \___ \| __| '__| | '_ \ / _` |
 *     | |_) | |_| | ||  __/___) | |_| |  | | | | | (_| |
 *     |____/ \__, |\__\___|____/ \__|_|  |_|_| |_|\__, |
 *            |___/                                |___/ 
 */

void lily_bytestring_encode(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_string_val *input_bytestring = vm_regs[code[1]]->value.string;
    const char *encode_method =
            (argc == 2) ? vm_regs[code[2]]->value.string->string : "error";
    lily_value *result = vm_regs[code[0]];
    char *byte_buffer = NULL;

    if (strcmp(encode_method, "error") == 0) {
        byte_buffer = input_bytestring->string;
        int byte_buffer_size = input_bytestring->size;

        if (lily_is_valid_sized_utf8(byte_buffer, byte_buffer_size) == 0) {
            lily_move_enum_f(MOVE_SHARED_NO_GC, result, lily_get_none(vm));
            return;
        }
    }
    else {
        lily_move_enum_f(MOVE_SHARED_NO_GC, result, lily_get_none(vm));
        return;
    }

    lily_value *v = lily_new_string(byte_buffer);
    lily_move_enum_f(MOVE_DEREF_NO_GC, result, lily_new_some(v));
}

/***
 *      ____              _     _      
 *     |  _ \  ___  _   _| |__ | | ___ 
 *     | | | |/ _ \| | | | '_ \| |/ _ \
 *     | |_| | (_) | |_| | |_) | |  __/
 *     |____/ \___/ \__,_|_.__/|_|\___|
 *                                     
 */

void lily_double_to_i(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int64_t integer_val = (int64_t)vm_regs[code[1]]->value.doubleval;
    lily_value *result_reg = vm_regs[code[0]];

    lily_move_integer(result_reg, integer_val);
}

/***
 *      ____                              _      
 *     |  _ \ _   _ _ __   __ _ _ __ ___ (_) ___ 
 *     | | | | | | | '_ \ / _` | '_ ` _ \| |/ __|
 *     | |_| | |_| | | | | (_| | | | | | | | (__ 
 *     |____/ \__, |_| |_|\__,_|_| |_| |_|_|\___|
 *            |___/                              
 */

void lily_dynamic_new(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result = vm_regs[code[0]];
    lily_value *input = vm_regs[code[1]];

    if (input->flags & VAL_IS_DEREFABLE)
        input->value.generic->refcount++;

    lily_dynamic_val *dynamic_val = lily_new_dynamic_val();

    *(dynamic_val->inner_value) = *input;
    lily_move_dynamic(result, dynamic_val);
    lily_tag_value(vm, result);
}

/***
 *      _____ _ _   _               
 *     | ____(_) |_| |__   ___ _ __ 
 *     |  _| | | __| '_ \ / _ \ '__|
 *     | |___| | |_| | | |  __/ |   
 *     |_____|_|\__|_| |_|\___|_|   
 *                                  
 */

static void either_is_left_right(lily_vm_state *vm, uint16_t *code, int expect)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_instance_val *iv = vm_regs[code[1]]->value.instance;
    lily_value *result_reg = vm_regs[code[0]];

    lily_move_boolean(result_reg, (iv->variant_id == expect));
}

void lily_either_is_left(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    either_is_left_right(vm, code, LEFT_VARIANT_ID);
}

void lily_either_is_right(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    either_is_left_right(vm, code, RIGHT_VARIANT_ID);
}

static void either_optionize_left_right(lily_vm_state *vm, uint16_t *code, int expect)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_instance_val *iv = vm_regs[code[1]]->value.instance;
    lily_value *result_reg = vm_regs[code[0]];

    if (iv->variant_id == expect)
        lily_move_enum_f(MOVE_DEREF_SPECULATIVE, result_reg,
                lily_new_some(lily_copy_value(iv->values[0])));
    else
        lily_move_enum_f(MOVE_DEREF_NO_GC, result_reg, lily_get_none(vm));
}

void lily_either_left(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    either_optionize_left_right(vm, code, LEFT_VARIANT_ID);
}

void lily_either_right(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    either_optionize_left_right(vm, code, RIGHT_VARIANT_ID);
}

/***
 *      _____ _ _      
 *     |  ___(_) | ___ 
 *     | |_  | | |/ _ \
 *     |  _| | | |  __/
 *     |_|   |_|_|\___|
 *                     
 */

static void write_check(lily_vm_state *vm, lily_file_val *filev)
{
    if (filev->inner_file == NULL)
        lily_raise(vm->raiser, lily_IOError, "IO operation on closed file.\n");

    if (filev->write_ok == 0)
        lily_raise(vm->raiser, lily_IOError, "File not open for writing.\n");
}

static void read_check(lily_vm_state *vm, lily_file_val *filev)
{
    if (filev->inner_file == NULL)
        lily_raise(vm->raiser, lily_IOError, "IO operation on closed file.\n");

    if (filev->read_ok == 0)
        lily_raise(vm->raiser, lily_IOError, "File not open for reading.\n");
}

void lily_file_close(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_file_val *filev = vm_regs[code[1]]->value.file;

    if (filev->inner_file != NULL) {
        if (filev->is_builtin == 0)
            fclose(filev->inner_file);
        filev->inner_file = NULL;
    }
}

void lily_file_open(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    char *path = vm_regs[code[1]]->value.string->string;
    char *mode = vm_regs[code[2]]->value.string->string;
    lily_value *result_reg = vm_regs[code[0]];

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
        lily_raise(vm->raiser, lily_IOError,
                "Invalid mode '%s' given.\n", mode);

    FILE *f = fopen(path, mode);
    if (f == NULL) {
        lily_raise(vm->raiser, lily_IOError, "Errno %d: ^R (%s).\n",
                errno, errno, path);
    }

    lily_file_val *filev = lily_new_file_val(f, mode);

    lily_move_file(result_reg, filev);
}

void lily_file_write(lily_vm_state *, uint16_t, uint16_t *);

void lily_file_print(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_file_write(vm, argc, code);
    fputc('\n', vm->vm_regs[code[1]]->value.file->inner_file);
}

void lily_file_read_line(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_file_val *filev = vm_regs[code[1]]->value.file;
    lily_value *result_reg = vm_regs[code[0]];
    lily_msgbuf *vm_buffer = vm->vm_buffer;
    lily_msgbuf_flush(vm_buffer);

    /* This ensures that the buffer will always have space for the \0 at the
       very end. */
    int buffer_size = vm_buffer->size - 1;
    char *buffer = vm_buffer->message;

    int ch = 0;
    int pos = 0;

    read_check(vm, filev);
    FILE *f = filev->inner_file;

    /* This uses fgetc in a loop because fgets may read in \0's, but doesn't
       tell how much was written. */
    while (1) {
        ch = fgetc(f);

        if (ch == EOF)
            break;

        buffer[pos] = (char)ch;

        if (pos == buffer_size) {
            lily_msgbuf_grow(vm_buffer);
            buffer = vm_buffer->message;
            buffer_size = vm_buffer->size - 1;
        }

        pos++;

        /* \r is intentionally not checked for, because it's been a very, very
           long time since any os used \r alone for newlines. */
        if (ch == '\n')
            break;
    }

    lily_move_string(result_reg, lily_new_raw_string_sized(buffer, pos));
}

void lily_file_write(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_file_val *filev = vm_regs[code[1]]->value.file;
    lily_value *to_write = vm_regs[code[2]];

    write_check(vm, filev);

    if (to_write->flags & VAL_IS_STRING)
        fputs(to_write->value.string->string, filev->inner_file);
    else {
        lily_msgbuf *msgbuf = vm->vm_buffer;
        lily_msgbuf_flush(msgbuf);
        lily_vm_add_value_to_msgbuf(vm, msgbuf, to_write);
        fputs(msgbuf->message, filev->inner_file);
    }
}

/***
 *      _   _           _     
 *     | | | | __ _ ___| |__  
 *     | |_| |/ _` / __| '_ \ 
 *     |  _  | (_| \__ \ | | |
 *     |_| |_|\__,_|___/_| |_|
 *                            
 */

/* Attempt to find 'key' within 'hash_val'. If an element is found, then it is
   returned. If no element is found, then NULL is returned. */
lily_hash_elem *lily_hash_get_elem(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *key)
{
    uint64_t key_siphash = lily_siphash(vm, key);
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

static inline void remove_key_check(lily_vm_state *vm, lily_hash_val *hash_val)
{
    if (hash_val->iter_count)
        lily_raise(vm->raiser, lily_RuntimeError,
                "Cannot remove key from hash during iteration.\n");
}

/* This adds a new element to the hash, with 'pair_key' and 'pair_value' inside.
   The key and value are not given a refbump, and are not copied over. For that,
   see lily_hash_add_unique. */
static void hash_add_unique_nocopy(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
    lily_hash_elem *elem = lily_malloc(sizeof(lily_hash_elem));

    elem->key_siphash = lily_siphash(vm, pair_key);
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
void lily_hash_add_unique(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
    remove_key_check(vm, hash_val);

    pair_key = lily_copy_value(pair_key);
    pair_value = lily_copy_value(pair_value);

    hash_add_unique_nocopy(vm, hash_val, pair_key, pair_value);
}

/* This attempts to find 'pair_key' within 'hash_val'. If successful, then the
   element's value is assigned to 'pair_value'. If unable to find an element, a
   new element is created using 'pair_key' and 'pair_value'. */
void lily_hash_set_elem(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
    lily_hash_elem *elem = lily_hash_get_elem(vm, hash_val, pair_key);
    if (elem == NULL)
        lily_hash_add_unique(vm, hash_val, pair_key, pair_value);
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

void lily_hash_clear(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;

    if (hash_val->iter_count != 0)
        lily_raise(vm->raiser, lily_RuntimeError,
                "Cannot remove key from hash during iteration.\n");

    destroy_hash_elems(hash_val);

    hash_val->elem_chain = NULL;
    hash_val->num_elems = 0;
}

void lily_hash_get(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input = vm_regs[code[1]];
    lily_value *key = vm_regs[code[2]];
    lily_value *default_value = vm_regs[code[3]];
    lily_value *result = vm_regs[code[0]];

    lily_hash_elem *hash_elem = lily_hash_get_elem(vm, input->value.hash, key);
    lily_value *new_value = hash_elem ? hash_elem->elem_value : default_value;

    lily_assign_value(result, new_value);
}

void lily_hash_keys(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *result_reg = vm_regs[code[0]];

    int num_elems = hash_val->num_elems;

    lily_list_val *result_lv = lily_new_list_val();
    result_lv->num_values = num_elems;
    result_lv->elems = lily_malloc(num_elems * sizeof(lily_value *));

    int i = 0;

    lily_hash_elem *elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        result_lv->elems[i] = lily_copy_value(elem_iter->elem_key);

        i++;
        elem_iter = elem_iter->next;
    }

    lily_move_list_f(MOVE_DEREF_SPECULATIVE, result_reg, result_lv);
}

void lily_hash_delete(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *key = vm_regs[code[2]];

    remove_key_check(vm, hash_val);

    lily_hash_elem *hash_elem = lily_hash_get_elem(vm, hash_val, key);

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
}

void lily_hash_each_pair(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *function_reg = vm_regs[code[2]];
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    int cached = 0;

    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(vm->raiser);
    if (setjmp(link->jump) == 0) {
        while (elem_iter) {
            lily_value *e_key = elem_iter->elem_key;
            lily_value *e_value = elem_iter->elem_value;

            lily_foreign_call(vm, &cached, 0, function_reg, 2, e_key,
                    e_value);

            elem_iter = elem_iter->next;
        }

        hash_val->iter_count--;
        lily_release_jump(vm->raiser);
    }
    else {
        hash_val->iter_count--;
        lily_jump_back(vm->raiser);
    }
}

void lily_hash_has_key(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *key = vm_regs[code[2]];

    lily_hash_elem *hash_elem = lily_hash_get_elem(vm, hash_val, key);

    lily_move_integer(vm_regs[code[0]], hash_elem != NULL);
}

static void build_hash_from_vm_list(lily_vm_state *vm, int start,
        lily_value *result_reg)
{
    int stop = vm->vm_list->pos;
    int i;
    lily_hash_val *hash_val = lily_new_hash_val();
    lily_value **values = vm->vm_list->values;

    for (i = start;i < stop;i += 2) {
        lily_value *e_key = values[i];
        lily_value *e_value = values[i + 1];

        hash_add_unique_nocopy(vm, hash_val, e_key, e_value);
    }

    vm->vm_list->pos = start;

    lily_move_hash_f(MOVE_DEREF_SPECULATIVE, result_reg, hash_val);
}

void lily_hash_map_values(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];

    lily_hash_elem *elem_iter = hash_val->elem_chain;
    lily_vm_list *vm_list = vm->vm_list;
    int cached = 0;
    int vm_list_start = vm->vm_list->pos;

    lily_vm_list_ensure(vm, hash_val->num_elems * 2);

    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(vm->raiser);

    if (setjmp(link->jump) == 0) {
        while (elem_iter) {
            lily_value *e_value = elem_iter->elem_value;

            lily_value *new_value = lily_foreign_call(vm, &cached, 1,
                    function_reg, 1, e_value);

            vm_list->values[vm_list->pos] = lily_copy_value(elem_iter->elem_key);
            vm_list->values[vm_list->pos+1] = lily_copy_value(new_value);
            vm_list->pos += 2;

            elem_iter = elem_iter->next;
        }

        build_hash_from_vm_list(vm, vm_list_start, result_reg);
        hash_val->iter_count--;
        lily_release_jump(vm->raiser);
    }
    else {
        hash_val->iter_count--;
        lily_jump_back(vm->raiser);
    }
}

void lily_hash_merge(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_list_val *to_merge = vm_regs[code[2]]->value.list;
    lily_value *result_reg = vm_regs[code[0]];

    lily_hash_val *result_hash = lily_new_hash_val();

    /* The existing hash should be entirely unique, so just add the pairs in
       directly. */
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        lily_hash_add_unique(vm, result_hash, elem_iter->elem_key,
                elem_iter->elem_value);

        elem_iter = elem_iter->next;
    }

    int i;
    for (i = 0;i < to_merge->num_values;i++) {
        lily_hash_val *merging_hash = to_merge->elems[i]->value.hash;
        elem_iter = merging_hash->elem_chain;
        while (elem_iter) {
            lily_hash_set_elem(vm, result_hash, elem_iter->elem_key,
                    elem_iter->elem_value);

            elem_iter = elem_iter->next;
        }
    }

    lily_move_hash_f(MOVE_DEREF_SPECULATIVE, result_reg, result_hash);
}

static void hash_select_reject_common(lily_vm_state *vm, uint16_t argc,
        uint16_t *code, int expect)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];

    lily_hash_elem *elem_iter = hash_val->elem_chain;
    lily_vm_list *vm_list = vm->vm_list;
    int cached = 0;
    int vm_list_start = vm->vm_list->pos;

    lily_vm_list_ensure(vm, hash_val->num_elems * 2);

    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(vm->raiser);

    if (setjmp(link->jump) == 0) {
        while (elem_iter) {
            lily_value *e_key = elem_iter->elem_key;
            lily_value *e_value = elem_iter->elem_value;

            lily_value *result = lily_foreign_call(vm, &cached, 1,
                    function_reg, 2, e_key, e_value);

            if (result->value.integer == expect) {
                vm_list->values[vm_list->pos] = lily_copy_value(e_key);
                vm_list->values[vm_list->pos+1] = lily_copy_value(e_value);
                vm_list->pos += 2;
            }

            elem_iter = elem_iter->next;
        }

        build_hash_from_vm_list(vm, vm_list_start, result_reg);
        hash_val->iter_count--;
        lily_release_jump(vm->raiser);
    }
    else {
        hash_val->iter_count--;
        lily_jump_back(vm->raiser);
    }
}

void lily_hash_reject(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    hash_select_reject_common(vm, argc, code, 0);
}

void lily_hash_select(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    hash_select_reject_common(vm, argc, code, 1);
}

void lily_hash_size(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;

    lily_move_integer(vm_regs[code[0]], hash_val->num_elems);
}

/***
 *      ___       _                       
 *     |_ _|_ __ | |_ ___  __ _  ___ _ __ 
 *      | || '_ \| __/ _ \/ _` |/ _ \ '__|
 *      | || | | | ||  __/ (_| |  __/ |   
 *     |___|_| |_|\__\___|\__, |\___|_|   
 *                        |___/           
 */

void lily_integer_to_d(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm_regs[code[0]];
    double doubleval = (double)vm_regs[code[1]]->value.integer;

    lily_move_double(result_reg, doubleval);
}

void lily_integer_to_s(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int64_t integer_val = vm_regs[code[1]]->value.integer;
    lily_value *result_reg = vm_regs[code[0]];

    char buffer[32];
    snprintf(buffer, 32, "%"PRId64, integer_val);

    lily_move_string(result_reg, lily_new_raw_string(buffer));
}

/***
 *      _     _     _   
 *     | |   (_)___| |_ 
 *     | |   | / __| __|
 *     | |___| \__ \ |_ 
 *     |_____|_|___/\__|
 *                      
 */

void lily_list_size(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *ret_reg = vm_regs[code[0]];

    lily_move_integer(ret_reg, list_val->num_values);
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

void lily_list_push(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *insert_value = vm_regs[code[2]];

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    int value_count = list_val->num_values;

    list_val->elems[value_count] = lily_copy_value(insert_value);
    list_val->num_values++;
    list_val->extra_space--;
}

void lily_list_pop(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *result_reg = vm_regs[code[0]];

    if (list_val->num_values == 0)
        lily_raise(vm->raiser, lily_IndexError, "Pop from an empty list.\n");

    lily_value *source = list_val->elems[list_val->num_values - 1];

    /* This is a special case: The value must be moved, but there will be no
       net increase to refcount. Use assign (because it will copy over flags)
       but not the regular one or the refcount will be wrong. */
    lily_assign_value_noref(result_reg, source);

    /* For now, free extra values instead of trying to keep reserves around.
       Not the best course of action, perhaps, but certainly the simplest. */
    lily_free(list_val->elems[list_val->num_values - 1]);
    list_val->num_values--;
    list_val->extra_space++;
}

static int64_t get_relative_index(lily_vm_state *vm, lily_list_val *list_val,
        int64_t pos)
{
    if (pos < 0) {
        uint64_t unsigned_pos = -(int64_t)pos;
        if (unsigned_pos > list_val->num_values)
            lily_raise(vm->raiser, lily_IndexError, "Index %d is too small for list (minimum: %d)\n",
                    pos, -(int64_t)list_val->num_values);

        pos = list_val->num_values - unsigned_pos;
    }
    else if (pos > list_val->num_values)
        lily_raise(vm->raiser, lily_IndexError, "Index %d is too large for list (maximum: %d)\n",
                pos, list_val->num_values);

    return pos;
}

void lily_list_insert(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    int64_t insert_pos = vm_regs[code[2]]->value.integer;
    lily_value *insert_value = vm_regs[code[3]];

    insert_pos = get_relative_index(vm, list_val, insert_pos);

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    /* Shove everything rightward to make space for the new value. */
    if (insert_pos != list_val->num_values)
        memmove(list_val->elems + insert_pos + 1, list_val->elems + insert_pos,
                (list_val->num_values - insert_pos) * sizeof(lily_value *));

    list_val->elems[insert_pos] = lily_copy_value(insert_value);
    list_val->num_values++;
    list_val->extra_space--;
}

void lily_list_delete_at(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    int64_t pos = vm_regs[code[2]]->value.integer;

    if (list_val->num_values == 0)
        lily_raise(vm->raiser, lily_IndexError, "Cannot delete from an empty list.\n");

    pos = get_relative_index(vm, list_val, pos);

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

void lily_list_clear(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    int i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_deref(list_val->elems[i]);
        lily_free(list_val->elems[i]);
    }

    list_val->extra_space += list_val->num_values;
    list_val->num_values = 0;
}

void lily_list_each(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *list_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_list_val *list_val = list_reg->value.list;
    lily_value *result_reg = vm_regs[code[0]];
    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++)
        lily_foreign_call(vm, &cached, 1, function_reg, 1,
                list_val->elems[i]);

    lily_assign_value(result_reg, list_reg);
}

void lily_list_each_index(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *list_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_list_val *list_val = list_reg->value.list;
    lily_value *result_reg = vm_regs[code[0]];
    lily_value fake_reg;

    fake_reg.value.integer = 0;
    fake_reg.flags = VAL_IS_INTEGER;

    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++, fake_reg.value.integer++)
        lily_foreign_call(vm, &cached, 0, function_reg, 1, &fake_reg);

    lily_assign_value(result_reg, list_reg);
}

void lily_list_fill(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int n = vm_regs[code[1]]->value.integer;
    if (n < 0)
        lily_raise(vm->raiser, lily_ValueError,
                "Repeat count must be >= 0 (%d given).\n", n);

    lily_value *to_repeat = vm_regs[code[2]];
    lily_value *result = vm_regs[code[0]];
    lily_list_val *lv = lily_new_list_val();

    lily_move_list_f(MOVE_DEREF_SPECULATIVE, result, lv);

    lily_value **elems = lily_malloc(sizeof(lily_value *) * n);
    lv->elems = elems;

    int i;
    for (i = 0;i < n;i++)
        elems[i] = lily_copy_value(to_repeat);

    lv->num_values = n;
}

/* This function will take 'vm_list->pos - vm_list_start' elements out of the
   vm's vm_list and move them into a newly-made list. vm_list->pos is then
   rewound to vm_list_start.
   This function assumes that values which are put into vm_list are copied (and
   thus receive a refcount bump). This allows the new list to simply take
   ownership of the values in the vm_list. */
static void slice_vm_list(lily_vm_state *vm, int vm_list_start,
        lily_value *result_reg)
{
    lily_vm_list *vm_list = vm->vm_list;
    lily_list_val *result_list = lily_new_list_val();
    int num_values = vm_list->pos - vm_list_start;

    result_list->num_values = num_values;
    result_list->elems = lily_malloc(sizeof(lily_value *) * num_values);

    int i;
    for (i = 0;i < num_values;i++)
        result_list->elems[i] = vm_list->values[vm_list_start + i];

    vm_list->pos = vm_list_start;

    lily_move_list_f(MOVE_DEREF_SPECULATIVE, result_reg, result_list);
}

static void list_select_reject_common(lily_vm_state *vm, uint16_t argc,
        uint16_t *code, int expect)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *function_reg = vm_regs[code[2]];

    lily_vm_list *vm_list = vm->vm_list;
    int vm_list_start = vm_list->pos;
    int cached = 0;

    lily_vm_list_ensure(vm, list_val->num_values);

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_value *result = lily_foreign_call(vm, &cached, 1,
                function_reg, 1, list_val->elems[i]);

        if (result->value.integer == expect) {
            vm_list->values[vm_list->pos] = lily_copy_value(list_val->elems[i]);
            vm_list->pos++;
        }
    }

    slice_vm_list(vm, vm_list_start, result_reg);
}

void lily_list_count(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *function_reg = vm_regs[code[2]];
    int count = 0;

    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_value *result = lily_foreign_call(vm, &cached, 1,
                function_reg, 1, list_val->elems[i]);

        if (result->value.integer == 1)
            count++;
    }

    lily_move_integer(result_reg, count);
}

void lily_list_join(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm_regs[code[0]];
    lily_list_val *lv = vm_regs[code[1]]->value.list;
    const char *delim = "";
    if (argc == 2)
        delim = vm_regs[code[2]]->value.string->string;

    lily_msgbuf *vm_buffer = vm->vm_buffer;
    lily_msgbuf_flush(vm_buffer);

    if (lv->num_values) {
        int i, stop = lv->num_values - 1;
        lily_value **values = lv->elems;
        for (i = 0;i < stop;i++) {
            lily_vm_add_value_to_msgbuf(vm, vm_buffer, values[i]);
            lily_msgbuf_add(vm_buffer, delim);
        }
        if (stop != -1)
            lily_vm_add_value_to_msgbuf(vm, vm_buffer, values[i]);
    }

    lily_move_string(result_reg, lily_new_raw_string(vm_buffer->message));
}

void lily_list_select(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    list_select_reject_common(vm, argc, code, 1);
}

void lily_list_reject(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    list_select_reject_common(vm, argc, code, 0);
}

void lily_list_map(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *function_reg = vm_regs[code[2]];

    lily_vm_list *vm_list = vm->vm_list;
    int vm_list_start = vm_list->pos;
    int cached = 0;

    lily_vm_list_ensure(vm, list_val->num_values);

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        lily_value *result = lily_foreign_call(vm, &cached, 1,
                function_reg, 1, list_val->elems[i]);

        vm_list->values[vm_list->pos] = lily_copy_value(result);
        vm_list->pos++;
    }

    slice_vm_list(vm, vm_list_start, result_reg);
}

void lily_list_shift(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *result_reg = vm_regs[code[0]];

    if (list_val->num_values == 0)
        lily_raise(vm->raiser, lily_IndexError, "Shift on an empty list.\n");

    lily_value *source = list_val->elems[0];

    /* Similar to List.pop, the value is being taken out so use this custom
       assign to keep the refcount the same. */
    lily_assign_value_noref(result_reg, source);

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

void lily_list_unshift(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *input_reg = vm_regs[code[2]];

    if (list_val->extra_space == 0)
        make_extra_space_in_list(list_val);

    if (list_val->num_values != 0)
        memmove(list_val->elems + 1, list_val->elems,
                list_val->num_values * sizeof(lily_value *));

    list_val->elems[0] = lily_copy_value(input_reg);

    list_val->num_values++;
    list_val->extra_space--;
}

void lily_list_fold(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm->vm_regs[code[0]];
    lily_list_val *list_val = vm_regs[code[1]]->value.list;
    lily_value *starting_reg = vm_regs[code[2]];
    lily_value *function_reg = vm_regs[code[3]];
    lily_value *current = starting_reg;
    int cached = 0;

    int i;
    for (i = 0;i < list_val->num_values;i++) {
        current = lily_foreign_call(vm, &cached, 1, function_reg, 2, current,
                list_val->elems[i]);
    }

    lily_assign_value(result_reg, current);
}

/***
 *       ___        _   _             
 *      / _ \ _ __ | |_(_) ___  _ __  
 *     | | | | '_ \| __| |/ _ \| '_ \ 
 *     | |_| | |_) | |_| | (_) | | | |
 *      \___/| .__/ \__|_|\___/|_| |_|
 *           |_|                      
 */

void lily_option_and(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *and_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_value *source;

    if (opt_reg->value.instance->variant_id == SOME_VARIANT_ID)
        source = and_reg;
    else
        source = opt_reg;

    lily_assign_value(result_reg, source);
}

void lily_option_and_then(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_value *source;
    int cached = 0;

    if (optval->variant_id == SOME_VARIANT_ID) {
        lily_value *output = lily_foreign_call(vm, &cached, 1,
                function_reg, 1, optval->values[0]);

        source = output;
    }
    else
        source = opt_reg;

    lily_assign_value(result_reg, source);
}

static void option_is_some_or_none(lily_vm_state *vm, uint16_t argc,
        uint16_t *code, int num_expected)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_instance_val *optval = vm_regs[code[1]]->value.instance;
    lily_value *result_reg = vm_regs[code[0]];

    lily_move_boolean(result_reg, (optval->num_values == num_expected));
}

void lily_option_map(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_instance_val *source;
    int cached = 0;

    if (optval->variant_id == SOME_VARIANT_ID) {
        lily_value *output = lily_foreign_call(vm, &cached, 1,
                function_reg, 1, optval->values[0]);

        source = lily_new_some(lily_copy_value(output));
        lily_move_enum_f(MOVE_DEREF_SPECULATIVE, result_reg, source);
    }
    else {
        source = lily_get_none(vm);
        lily_move_enum_f(MOVE_SHARED_SPECULATIVE, result_reg, source);
    }
}

void lily_option_is_some(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    option_is_some_or_none(vm, argc, code, 1);
}

void lily_option_is_none(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    option_is_some_or_none(vm, argc, code, 0);
}

void lily_option_or(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *or_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_value *source;

    if (opt_reg->value.instance->variant_id == SOME_VARIANT_ID)
        source = opt_reg;
    else
        source = or_reg;

    lily_assign_value(result_reg, source);
}

void lily_option_unwrap(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_value *result_reg = vm_regs[code[0]];

    if (optval->variant_id == SOME_VARIANT_ID)
        lily_assign_value(result_reg, opt_reg->value.instance->values[0]);
    else
        lily_raise(vm->raiser, lily_ValueError, "unwrap called on None.\n");
}

void lily_option_unwrap_or(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *fallback_reg = vm_regs[code[2]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_value *result_reg = vm_regs[code[0]];
    lily_value *source;

    if (optval->variant_id == SOME_VARIANT_ID)
        source = opt_reg->value.instance->values[0];
    else
        source = fallback_reg;

    lily_assign_value(result_reg, source);
}

void lily_option_or_else(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_value *source;
    int cached = 0;

    if (optval->variant_id == SOME_VARIANT_ID)
        source = opt_reg;
    else
        source = lily_foreign_call(vm, &cached, 1, function_reg, 0);

    lily_assign_value(result_reg, source);
}

void lily_option_unwrap_or_else(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_value *source;
    int cached = 0;

    if (optval->variant_id == SOME_VARIANT_ID)
        source = opt_reg->value.instance->values[0];
    else
        source = lily_foreign_call(vm, &cached, 1, function_reg, 0);

    lily_assign_value(result_reg, source);
}

/***
 *      ____  _        _             
 *     / ___|| |_ _ __(_)_ __   __ _ 
 *     \___ \| __| '__| | '_ \ / _` |
 *      ___) | |_| |  | | | | | (_| |
 *     |____/ \__|_|  |_|_| |_|\__, |
 *                             |___/ 
 */

static lily_string_val *make_sv(lily_vm_state *vm, int size)
{
    lily_string_val *new_sv = lily_malloc(sizeof(lily_string_val));
    char *new_string = lily_malloc(sizeof(char) * size);

    new_sv->string = new_string;
    new_sv->size = size - 1;
    new_sv->refcount = 1;

    return new_sv;
}

#define CTYPE_WRAP(WRAP_NAME, WRAPPED_CALL) \
void WRAP_NAME(lily_vm_state *vm, uint16_t argc, uint16_t *code) \
{ \
    lily_value **vm_regs = vm->vm_regs; \
    lily_value *ret_arg = vm_regs[code[0]]; \
    lily_value *input_arg = vm_regs[code[1]]; \
\
    if (input_arg->value.string->size == 0) { \
        lily_move_integer(ret_arg, 0); \
        return; \
    } \
\
    char *loop_str = input_arg->value.string->string; \
    int i = 0; \
\
    lily_move_integer(ret_arg, 1); \
    for (i = 0;i < input_arg->value.string->size;i++) { \
        if (WRAPPED_CALL(loop_str[i]) == 0) { \
            ret_arg->value.integer = 0; \
            break; \
        } \
    } \
}

CTYPE_WRAP(lily_string_is_digit, isdigit)
CTYPE_WRAP(lily_string_is_alpha, isalpha)
CTYPE_WRAP(lily_string_is_space, isspace)
CTYPE_WRAP(lily_string_is_alnum, isalnum)

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

void lily_string_ends_with(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[1]];
    lily_value *suffix_arg = vm_regs[code[2]];
    lily_value *result_arg = vm_regs[code[0]];

    char *input_raw_str = input_arg->value.string->string;
    char *suffix_raw_str = suffix_arg->value.string->string;
    int input_size = input_arg->value.string->size;
    int suffix_size = suffix_arg->value.string->size;

    if (suffix_size > input_size) {
        lily_move_boolean(result_arg, 0);
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

    lily_move_boolean(result_arg, ok);
}

void lily_string_find(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[1]];
    lily_value *find_arg = vm_regs[code[2]];
    lily_value *result_arg = vm_regs[code[0]];

    char *input_str = input_arg->value.string->string;
    int input_length = input_arg->value.string->size;

    char *find_str = find_arg->value.string->string;
    int find_length = find_arg->value.string->size;

    if (find_length > input_length ||
        find_length == 0) {
        lily_move_enum_f(MOVE_SHARED_NO_GC, result_arg, lily_get_none(vm));
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
        lily_value *v = lily_new_empty_value();
        lily_move_integer(v, i);
        lily_move_enum_f(MOVE_DEREF_NO_GC, result_arg, lily_new_some(v));
    }
    else
        lily_move_enum_f(MOVE_SHARED_SPECULATIVE, result_arg,
                lily_get_none(vm));
}

/* Scan through 'input' in search of html characters to encode. If there are
   any, then vm->vm_buffer is updated to contain an html-safe version of the
   input string.
   If no html characters are found, then 0 is returned, and the caller is to use
   the given input buffer directly.
   If html charcters are found, then 1 is returned, and the caller should read
   from vm->vm_buffer->message. */
int lily_maybe_html_encode_to_buffer(lily_vm_state *vm, lily_value *input)
{
    lily_msgbuf *vm_buffer = vm->vm_buffer;
    lily_msgbuf_flush(vm_buffer);
    int start = 0, stop = 0;
    char *input_str = input->value.string->string;
    char *ch = &input_str[0];

    while (1) {
        if (*ch == '&') {
            stop = (ch - input_str);
            lily_msgbuf_add_text_range(vm_buffer, input_str, start, stop);
            lily_msgbuf_add(vm_buffer, "&amp;");
            start = stop + 1;
        }
        else if (*ch == '<') {
            stop = (ch - input_str);
            lily_msgbuf_add_text_range(vm_buffer, input_str, start, stop);
            lily_msgbuf_add(vm_buffer, "&lt;");
            start = stop + 1;
        }
        else if (*ch == '>') {
            stop = (ch - input_str);
            lily_msgbuf_add_text_range(vm_buffer, input_str, start, stop);
            lily_msgbuf_add(vm_buffer, "&gt;");
            start = stop + 1;
        }
        else if (*ch == '\0')
            break;

        ch++;
    }

    if (start != 0) {
        stop = (ch - input_str);
        lily_msgbuf_add_text_range(vm_buffer, input_str, start, stop);
    }

    return start;
}

void lily_string_html_encode(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[1]];
    lily_value *result_arg = vm_regs[code[0]];

    /* If nothing was escaped, output what was input. */
    if (lily_maybe_html_encode_to_buffer(vm, input_arg) == 0)
        lily_assign_value(result_arg, input_arg);
    else {
        char *source = vm->vm_buffer->message;
        lily_move_string(result_arg, lily_new_raw_string(source));
    }
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

void lily_string_lstrip(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[1]];
    lily_value *strip_arg = vm_regs[code[2]];
    lily_value *result_arg = vm_regs[code[0]];

    char *strip_str;
    unsigned char ch;
    int copy_from, i, has_multibyte_char, strip_str_len;
    lily_string_val *strip_sv;

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_arg->value.string->size == 0 ||
        strip_arg->value.string->size == 0) {
        lily_assign_value(result_arg, input_arg);
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
    lily_string_val *new_sv = make_sv(vm, new_size);

    strcpy(new_sv->string, input_arg->value.string->string + copy_from);

    lily_move_string(result_arg, new_sv);
}

void lily_string_lower(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[1]];
    lily_value *result_arg = vm_regs[code[0]];

    int new_size = input_arg->value.string->size + 1;
    lily_string_val *new_sv = make_sv(vm, new_size);

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

    lily_move_string(result_arg, new_sv);
}

void lily_string_parse_i(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg = vm_regs[code[0]];
    char *input = vm_regs[code[1]]->value.string->string;
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
        lily_move_enum_f(MOVE_SHARED_NO_GC, result_reg, lily_get_none(vm));
    }
    else {
        int64_t signed_value;

        if (is_negative == 0)
            signed_value = (int64_t)value;
        else
            signed_value = -(int64_t)value;

        lily_value *v = lily_new_empty_value();
        lily_move_integer(v, signed_value);
        lily_move_enum_f(MOVE_DEREF_NO_GC, result_reg, lily_new_some(v));
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

void lily_string_rstrip(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[1]];
    lily_value *strip_arg = vm_regs[code[2]];
    lily_value *result_arg = vm_regs[code[0]];

    char *strip_str;
    unsigned char ch;
    int copy_to, i, has_multibyte_char, strip_str_len;
    lily_string_val *strip_sv;

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_arg->value.string->size == 0 ||
        strip_arg->value.string->size == 0) {
        lily_assign_value(result_arg, input_arg);
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
    lily_string_val *new_sv = make_sv(vm, new_size);

    strncpy(new_sv->string, input_arg->value.string->string, copy_to);
    /* This will always copy a partial string, so make sure to add a terminator. */
    new_sv->string[copy_to] = '\0';

    lily_move_string(result_arg, new_sv);
}

void lily_string_starts_with(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[1]];
    lily_value *prefix_arg = vm_regs[code[2]];
    lily_value *result_arg = vm_regs[code[0]];

    char *input_raw_str = input_arg->value.string->string;
    char *prefix_raw_str = prefix_arg->value.string->string;
    int prefix_size = prefix_arg->value.string->size;

    if (input_arg->value.string->size < prefix_size) {
        lily_move_integer(result_arg, 0);
        return;
    }

    int i, ok = 1;
    for (i = 0;i < prefix_size;i++) {
        if (input_raw_str[i] != prefix_raw_str[i]) {
            ok = 0;
            break;
        }
    }

    lily_move_integer(result_arg, ok);
}

void lily_string_strip(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[1]];
    lily_value *strip_arg = vm_regs[code[2]];
    lily_value *result_arg = vm_regs[code[0]];

    /* Either there is nothing to strip (1st), or stripping nothing (2nd). */
    if (input_arg->value.string->size == 0 ||
        strip_arg->value.string->size == 0) {
        lily_assign_value(result_arg, input_arg);
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
    lily_string_val *new_sv = make_sv(vm, new_size);

    char *new_str = new_sv->string;
    strncpy(new_str, input_arg->value.string->string + copy_from, new_size - 1);
    new_str[new_size - 1] = '\0';

    lily_move_string(result_arg, new_sv);
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

static void string_split_by_val(lily_vm_state *vm, char *input, char *splitby,
        lily_list_val *dest)
{
    char *input_ch = &input[0];
    char *splitby_ch = &splitby[0];
    int values_needed = 0;
    lily_value **elems;

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
    elems = lily_malloc(sizeof(lily_value *) * values_needed);
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
            int sv_size = match_start - last_start;

            elems[i] = lily_new_string_ncpy(last_start, sv_size);
            i++;
            if (*input_ch == '\0')
                break;

            last_start = input_ch + 1;
        }
        else if (*input_ch == '\0')
            break;

        input_ch++;
    }

    dest->elems = elems;
    dest->num_values = values_needed;
}

void lily_string_split(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_string_val *input_strval = vm_regs[code[1]]->value.string;
    lily_string_val *split_strval;
    if (argc == 2)
        split_strval = vm_regs[code[2]]->value.string;
    else {
        lily_string_val fake_sv;
        fake_sv.string = " ";
        fake_sv.size = 1;
        split_strval = &fake_sv;
    }

    lily_value *result_reg = vm_regs[code[0]];

    if (split_strval->size == 0)
        lily_raise(vm->raiser, lily_ValueError, "Cannot split by empty string.\n");

    lily_list_val *lv = lily_new_list_val();

    string_split_by_val(vm, input_strval->string, split_strval->string, lv);

    lily_move_list_f(MOVE_DEREF_NO_GC, result_reg, lv);
}

void lily_string_trim(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[1]];
    lily_value *result_arg = vm_regs[code[0]];

    char fake_buffer[5] = " \t\r\n";
    lily_string_val fake_sv;
    fake_sv.string = fake_buffer;
    fake_sv.size = strlen(fake_buffer);

    int copy_from = lstrip_ascii_start(input_arg, &fake_sv);
    lily_string_val *new_sv;

    if (copy_from != input_arg->value.string->size) {
        int copy_to = rstrip_ascii_stop(input_arg, &fake_sv);
        int new_size = (copy_to - copy_from) + 1;
        new_sv = make_sv(vm, new_size);
        char *new_str = new_sv->string;

        strncpy(new_str, input_arg->value.string->string + copy_from, new_size - 1);
        new_str[new_size - 1] = '\0';
    }
    else {
        /* It's all space, so make a new empty string. */
        new_sv = make_sv(vm, 1);
        new_sv->string[0] = '\0';
    }

    lily_move_string(result_arg, new_sv);
}

void lily_string_upper(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[1]];
    lily_value *result_arg = vm_regs[code[0]];

    int new_size = input_arg->value.string->size + 1;
    lily_string_val *new_sv = make_sv(vm, new_size);

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

    lily_move_string(result_arg, new_sv);
}

/* This handles a string subscript. The subscript may be negative (in which case
   it is an offset against the end). This must check if the index given by
   'index_reg' is a valid one.
   This moves by utf-8 codepoints, not by bytes. The result is sent to
   'result_reg', unless IndexError is raised. */
void lily_string_subscript(lily_vm_state *vm, lily_value *input_reg,
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
            lily_raise(vm->raiser, lily_IndexError, "Index %d is out of range.\n",
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
            lily_raise(vm->raiser, lily_IndexError, "Index %d is out of range.\n",
                    index_reg->value.integer);
    }

    int to_copy = move_table[(unsigned char)*ch];
    lily_string_val *result = make_sv(vm, to_copy + 1);
    char *dest = &result->string[0];
    dest[to_copy] = '\0';

    strncpy(dest, ch, to_copy);

    lily_move_string(result_reg, result);
}

/***
 *      _____     _       _           _ 
 *     |_   _|_ _(_)_ __ | |_ ___  __| |
 *       | |/ _` | | '_ \| __/ _ \/ _` |
 *       | | (_| | | | | | ||  __/ (_| |
 *       |_|\__,_|_|_| |_|\__\___|\__,_|
 *                                      
 */

void lily_tainted_sanitize(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_instance_val *iv = vm_regs[code[1]]->value.instance;
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    int cached = 0;

    lily_value *v = lily_foreign_call(vm, &cached, 1, function_reg, 1,
            iv->values[0]);

    lily_assign_value(result_reg, v);
}

/***
 *      _____            _
 *     |_   _|   _ _ __ | | ___
 *       | || | | | '_ \| |/ _ \
 *       | || |_| | |_) | |  __/
 *       |_| \__,_| .__/|_|\___|
 *                |_|
 */

void lily_tuple_merge(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *left_tuple = vm_regs[code[1]]->value.list;
    lily_list_val *right_tuple = vm_regs[code[2]]->value.list;
    lily_value *result_reg = vm_regs[code[0]];

    lily_list_val *lv = lily_new_list_val();
    int new_count = left_tuple->num_values + right_tuple->num_values;
    lv->elems = lily_malloc(sizeof(lily_value *) * new_count);
    lv->num_values = new_count;

    int i, j;
    for (i = 0, j = 0;i < left_tuple->num_values;i++, j++)
        lv->elems[j] = lily_copy_value(left_tuple->elems[i]);

    for (i = 0;i < right_tuple->num_values;i++, j++)
        lv->elems[j] = lily_copy_value(right_tuple->elems[i]);

    lily_move_tuple_f(MOVE_DEREF_SPECULATIVE, result_reg, lv);
}

void lily_tuple_push(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *left_tuple = vm_regs[code[1]]->value.list;
    lily_value *right = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];

    lily_list_val *lv = lily_new_list_val();
    int new_count = left_tuple->num_values + 1;
    lv->elems = lily_malloc(sizeof(lily_value *) * new_count);
    lv->num_values = new_count;

    int i, j;
    for (i = 0, j = 0;i < left_tuple->num_values;i++, j++)
        lv->elems[j] = lily_copy_value(left_tuple->elems[i]);

    lv->elems[j] = lily_copy_value(right);

    lily_move_tuple_f(MOVE_DEREF_SPECULATIVE, result_reg, lv);
}

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
    lily_value *result = lily_new_empty_value();
    lily_file_val *file_val = lily_new_file_val(source, mode);
    file_val->is_builtin = 1;

    lily_move_file(result, file_val);
    return result;
}

#define INTEGER_OFFSET     2
#define DOUBLE_OFFSET      5
#define STRING_OFFSET      7
#define BYTESTRING_OFFSET 24
#define BOOLEAN_OFFSET    26
#define DYNAMIC_OFFSET    30
#define LIST_OFFSET       32
#define HASH_OFFSET       50
#define TUPLE_OFFSET      62
#define FILE_OFFSET       65
#define OPTION_OFFSET     73
#define EITHER_OFFSET     86
#define TAINTED_OFFSET   101
#define MISC_OFFSET      102

extern void lily_builtin_calltrace(lily_vm_state *, uint16_t, uint16_t *);
extern void lily_builtin_print(lily_vm_state *, uint16_t, uint16_t *);

void *lily_builtin_loader(lily_options *options, uint16_t *cid_table, int id)
{
    switch(id) {
        case INTEGER_OFFSET + 0:  return lily_integer_to_d;
        case INTEGER_OFFSET + 1:  return lily_integer_to_s;

        case DOUBLE_OFFSET + 0:  return lily_double_to_i;

        case STRING_OFFSET + 0:  return lily_string_ends_with;
        case STRING_OFFSET + 1:  return lily_string_find;
        case STRING_OFFSET + 2:  return lily_string_html_encode;
        case STRING_OFFSET + 3:  return lily_string_is_alpha;
        case STRING_OFFSET + 4:  return lily_string_is_digit;
        case STRING_OFFSET + 5:  return lily_string_is_alnum;
        case STRING_OFFSET + 6:  return lily_string_is_space;
        case STRING_OFFSET + 7:  return lily_string_lstrip;
        case STRING_OFFSET + 8:  return lily_string_lower;
        case STRING_OFFSET + 9:  return lily_string_parse_i;
        case STRING_OFFSET + 10: return lily_string_rstrip;
        case STRING_OFFSET + 11: return lily_string_starts_with;
        case STRING_OFFSET + 12: return lily_string_split;
        case STRING_OFFSET + 13: return lily_string_strip;
        case STRING_OFFSET + 14: return lily_string_trim;
        case STRING_OFFSET + 15: return lily_string_upper;

        case BYTESTRING_OFFSET + 0: return lily_bytestring_encode;

        case BOOLEAN_OFFSET + 0: return lily_boolean_to_i;
        case BOOLEAN_OFFSET + 1: return lily_boolean_to_s;
        
        case DYNAMIC_OFFSET + 0: return lily_dynamic_new;

        case LIST_OFFSET +  0: return lily_list_clear;
        case LIST_OFFSET +  1: return lily_list_count;
        case LIST_OFFSET +  2: return lily_list_delete_at;
        case LIST_OFFSET +  3: return lily_list_each;
        case LIST_OFFSET +  4: return lily_list_each_index;
        case LIST_OFFSET +  5: return lily_list_fill;
        case LIST_OFFSET +  6: return lily_list_fold;
        case LIST_OFFSET +  7: return lily_list_insert;
        case LIST_OFFSET +  8: return lily_list_join;
        case LIST_OFFSET +  9: return lily_list_map;
        case LIST_OFFSET + 10: return lily_list_pop;
        case LIST_OFFSET + 11: return lily_list_push;
        case LIST_OFFSET + 12: return lily_list_reject;
        case LIST_OFFSET + 13: return lily_list_select;
        case LIST_OFFSET + 14: return lily_list_size;
        case LIST_OFFSET + 15: return lily_list_shift;
        case LIST_OFFSET + 16: return lily_list_unshift;

        case HASH_OFFSET +  0: return lily_hash_clear;
        case HASH_OFFSET +  1: return lily_hash_delete;
        case HASH_OFFSET +  2: return lily_hash_each_pair;
        case HASH_OFFSET +  3: return lily_hash_has_key;
        case HASH_OFFSET +  4: return lily_hash_keys;
        case HASH_OFFSET +  5: return lily_hash_get;
        case HASH_OFFSET +  6: return lily_hash_map_values;
        case HASH_OFFSET +  7: return lily_hash_merge;
        case HASH_OFFSET +  8: return lily_hash_reject;
        case HASH_OFFSET +  9: return lily_hash_select;
        case HASH_OFFSET + 10: return lily_hash_size;

        case TUPLE_OFFSET + 0: return lily_tuple_merge;
        case TUPLE_OFFSET + 1: return lily_tuple_push;

        case FILE_OFFSET + 0: return lily_file_close;
        case FILE_OFFSET + 1: return lily_file_open;
        case FILE_OFFSET + 2: return lily_file_print;
        case FILE_OFFSET + 3: return lily_file_read_line;
        case FILE_OFFSET + 4: return lily_file_write;

        case OPTION_OFFSET + 0: return lily_option_and;
        case OPTION_OFFSET + 1: return lily_option_and_then;
        case OPTION_OFFSET + 2: return lily_option_is_none;
        case OPTION_OFFSET + 3: return lily_option_is_some;
        case OPTION_OFFSET + 4: return lily_option_map;
        case OPTION_OFFSET + 5: return lily_option_or;
        case OPTION_OFFSET + 6: return lily_option_or_else;
        case OPTION_OFFSET + 7: return lily_option_unwrap;
        case OPTION_OFFSET + 8: return lily_option_unwrap_or;
        case OPTION_OFFSET + 9: return lily_option_unwrap_or_else;

        case EITHER_OFFSET + 0: return lily_either_is_left;
        case EITHER_OFFSET + 1: return lily_either_is_right;
        case EITHER_OFFSET + 2: return lily_either_left;
        case EITHER_OFFSET + 3: return lily_either_right;

        case TAINTED_OFFSET + 0: return lily_tainted_sanitize;

        case MISC_OFFSET + 0: return lily_builtin_calltrace;
        case MISC_OFFSET + 1: return lily_builtin_print;
        case MISC_OFFSET + 2: return new_builtin_file(stdin, "r");
        case MISC_OFFSET + 3: return new_builtin_file(stdout, "w");
        case MISC_OFFSET + 4: return new_builtin_file(stderr, "w");

        default: return NULL;
    }
}

const char *dynaload_table[] =
{
    "\0"
    ,"!\002Integer"
    ,"m:to_d\0(Integer):Double"
    ,"m:to_s\0(Integer):String"

    ,"!\001Double"
    ,"m:to_i\0(Double):Integer"

    ,"!\020String"
    ,"m:ends_with\0(String,String):Boolean"
    ,"m:find\0(String,String):Option[Integer]"
    ,"m:html_encode\0(String):String"
    ,"m:is_alpha\0(String):Boolean"
    ,"m:is_digit\0(String):Boolean"
    ,"m:is_alnum\0(String):Boolean"
    ,"m:is_space\0(String):Boolean"
    ,"m:lstrip\0(String,String):String"
    ,"m:lower\0(String):String"
    ,"m:parse_i\0(String):Option[Integer]"
    ,"m:rstrip\0(String,String):String"
    ,"m:starts_with\0(String,String):Boolean"
    ,"m:split\0(String,*String):List[String]"
    ,"m:strip\0(String,String):String"
    ,"m:trim\0(String):String"
    ,"m:upper\0(String):String"

    ,"!\001ByteString"
    ,"m:encode\0(ByteString, *String):Option[String]"

    ,"!\002Boolean"
    ,"m:to_i\0(Boolean):Double"
    ,"m:to_s\0(Boolean):String"

    ,"!\000Function"

    ,"!\001Dynamic"
    ,"m:new\0[A](A):Dynamic"

    ,"!\021List"
    ,"m:clear\0[A](List[A])"
    ,"m:count\0[A](List[A], Function(A => Boolean)):Integer"
    ,"m:delete_at\0[A](List[A], Integer)"
    ,"m:each\0[A](List[A], Function(A)):List[A]"
    ,"m:each_index\0[A](List[A], Function(Integer)):List[A]"
    ,"m:fill\0[A](Integer, A):List[A]"
    ,"m:fold\0[A](List[A], A, Function(A, A => A)):A"
    ,"m:insert\0[A](List[A], Integer, A)"
    ,"m:join\0[A](List[A], *String):String"
    ,"m:map\0[A,B](List[A], Function(A => B)):List[B]"
    ,"m:pop\0[A](List[A]):A"
    ,"m:push\0[A](List[A], A)"
    ,"m:reject\0[A](List[A], Function(A => Boolean)):List[A]"
    ,"m:select\0[A](List[A], Function(A => Boolean)):List[A]"
    ,"m:size\0[A](List[A]):Integer"
    ,"m:shift\0[A](List[A]):A"
    ,"m:unshift\0[A](List[A], A)"

    ,"!\013Hash"
    ,"m:clear\0[A, B](Hash[A, B])"
    ,"m:delete\0[A, B](Hash[A, B], A)"
    ,"m:each_pair\0[A, B](Hash[A, B], Function(A, B))"
    ,"m:has_key\0[A, B](Hash[A, B], A):Boolean"
    ,"m:keys\0[A, B](Hash[A, B]):List[A]"
    ,"m:get\0[A, B](Hash[A, B], A, B):B"
    ,"m:map_values\0[A, B, C](Hash[A, B], Function(B => C)):Hash[A, C]"
    ,"m:merge\0[A, B](Hash[A, B], Hash[A, B]...):Hash[A, B]"
    ,"m:reject\0[A, B](Hash[A, B], Function(A, B => Boolean)):Hash[A, B]"
    ,"m:select\0[A, B](Hash[A, B], Function(A, B => Boolean)):Hash[A, B]"
    ,"m:size\0[A, B](Hash[A, B]):Integer"

    ,"!\002Tuple"
    ,"m:merge\0(Tuple[1], Tuple[2]):Tuple[1, 2]"
    ,"m:push\0[A](Tuple[1], A):Tuple[1, A]"

    ,"!\005File"
    ,"m:close\0(File)"
    ,"m:open\0(String, String):File"
    ,"m:print\0[A](File, A)"
    ,"m:read_line\0(File):ByteString"
    ,"m:write\0[A](File, A)"

    ,"!\000A"
    ,"!\000*"

    ,"E\012Option\0[A]\0"
    ,"m:and\0[A,B](Option[A],Option[B]):Option[B]"
    ,"m:and_then\0[A,B](Option[A],Function(A => Option[B])):Option[B]"
    ,"m:is_none\0[A](Option[A]):Boolean"
    ,"m:is_some\0[A](Option[A]):Boolean"
    ,"m:map\0[A,B](Option[A],Function(A => B)):Option[B]"
    ,"m:or\0[A](Option[A],Option[A]):Option[A]"
    ,"m:or_else\0[A](Option[A],Function( => Option[A])):Option[A]"
    ,"m:unwrap\0[A](Option[A]):A"
    ,"m:unwrap_or\0[A](Option[A],A):A"
    ,"m:unwrap_or_else\0[A](Option[A], Function( => A)):A"
    ,"V\000Some\0(A)"
    ,"V\000None\0"

    ,"E\004Either\0[A, B]"
    ,"m:is_left\0[A,B](Either[A,B]):Boolean"
    ,"m:is_right\0[A,B](Either[A,B]):Boolean"
    ,"m:left\0[A,B](Either[A,B]):Option[A]"
    ,"m:right\0[A,B](Either[A,B]):Option[B]"
    ,"V\000Right\0(B)"
    ,"V\000Left\0(A)"

    ,"B\000Exception\0(msg:String){ var @message = msg var @traceback: List[String] = [] }"

    ,"B\000IOError\0(m:String) < Exception(m) {  }"
    ,"B\000FormatError\0(m:String) < Exception(m) {  }"
    ,"B\000KeyError\0(m:String) < Exception(m) {  }"
    ,"B\000RuntimeError\0(m:String) < Exception(m) {  }"
    ,"B\000ValueError\0(m:String) < Exception(m) {  }"
    ,"B\000IndexError\0(m:String) < Exception(m) {  }"
    ,"B\000DivisionByZeroError\0(m:String) < Exception(m) {  }"

    ,"B\001Tainted\0[A](v:A){ var @value = v }"
    ,"m:sanitize\0[A,B](Tainted[A], Function(A => B)):B"

    ,"F\000calltrace\0:List[String]"
    ,"F\000print\0[A](A)"

    ,"R\000stdin\0File\0"
    ,"R\000stdout\0File\0"
    ,"R\000stderr\0File\0"
    ,"Z"
};

static void make_default_type_for(lily_class *cls)
{
    lily_type *t = lily_malloc(sizeof(lily_type));
    t->cls = cls;
    t->flags = 0;
    t->generic_pos = 0;
    t->subtype_count = 0;
    t->subtypes = NULL;
    t->next = NULL;
    cls->type = t;
    cls->all_subtypes = t;
}

static lily_class *build_class(lily_symtab *symtab, const char *name,
        int *dyna_start, int generic_count)
{
    lily_class *result = lily_new_class(symtab, name);
    result->dyna_start = *dyna_start + 1;
    result->generic_count = generic_count;
    result->is_builtin = 1;

    if (generic_count == 0)
        make_default_type_for(result);

    *dyna_start += ((unsigned char) dynaload_table[*dyna_start][1]) + 1;
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
    result->is_builtin = 1;

    symtab->active_module->class_chain = result->next;
    symtab->next_class_id--;

    result->next = symtab->old_class_chain;
    symtab->old_class_chain = result;

    if (generic_count == 0)
        make_default_type_for(result);

    return result;
}

void lily_init_builtin_package(lily_symtab *symtab, lily_module_entry *builtin)
{
    builtin->dynaload_table = dynaload_table;
    builtin->loader = lily_builtin_loader;

    int i = 1;

    symtab->integer_class    = build_class(symtab, "Integer",    &i,  0);
    symtab->double_class     = build_class(symtab, "Double",     &i,  0);
    symtab->string_class     = build_class(symtab, "String",     &i,  0);
    symtab->bytestring_class = build_class(symtab, "ByteString", &i,  0);
    symtab->boolean_class    = build_class(symtab, "Boolean",    &i,  0);
    symtab->function_class   = build_class(symtab, "Function",   &i, -1);
    symtab->dynamic_class    = build_class(symtab, "Dynamic",    &i,  0);
    symtab->list_class       = build_class(symtab, "List",       &i,  1);
    symtab->hash_class       = build_class(symtab, "Hash",       &i,  2);
    symtab->tuple_class      = build_class(symtab, "Tuple",      &i, -1);
    lily_class *file_class   = build_class(symtab, "File",       &i,  0);
    symtab->generic_class    = build_class(symtab, "",           &i,  0);
    symtab->question_class   = build_class(symtab, "?",          &i,  0);

    symtab->optarg_class    = build_special(symtab, "*", 1, SYM_CLASS_OPTARG);
    lily_class *scoop1 = build_special(symtab, "~1", 0, SYM_CLASS_SCOOP_1);
    lily_class *scoop2 = build_special(symtab, "~2", 0, SYM_CLASS_SCOOP_2);

    scoop1->type->flags |= TYPE_HAS_SCOOP;
    scoop2->type->flags |= TYPE_HAS_SCOOP;

    symtab->integer_class->is_refcounted = 0;
    symtab->double_class->is_refcounted = 0;
    symtab->boolean_class->is_refcounted = 0;

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
    symtab->generic_class->type->flags |= TYPE_IS_UNRESOLVED;
    symtab->question_class->type->flags |= TYPE_IS_INCOMPLETE;
    symtab->function_class->flags |= CLS_GC_TAGGED;
    symtab->dynamic_class->flags |= CLS_GC_SPECULATIVE;
    /* HACK: This ensures that there is space to dynaload builtin classes and
       enums into. */
    symtab->next_class_id = START_CLASS_ID;
}

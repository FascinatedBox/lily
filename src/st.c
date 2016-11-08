/* This is based on the public domain general purpose hash table package written
   by Peter Moore @ UCB. */

#include <stdio.h>
#include <string.h>

#include "lily_core_types.h"
#include "lily_value_structs.h"

#include "lily_api_alloc.h"
#include "lily_api_value.h"

static long primes[] =
{
    8 + 3,
    16 + 3,
    32 + 5,
    64 + 3,
    128 + 3,
    256 + 27,
    512 + 9,
    1024 + 9,
    2048 + 5,
    4096 + 3,
    8192 + 27,
    16384 + 43,
    32768 + 3,
    65536 + 45,
    131072 + 29,
    262144 + 3,
    524288 + 21,
    1048576 + 7,
    2097152 + 17,
    4194304 + 15,
    8388608 + 9,
    16777216 + 43,
    33554432 + 35,
    67108864 + 15,
    134217728 + 29,
    268435456 + 3,
    536870912 + 11,
    1073741824 + 85,
    0
};

#define MINSIZE 8
#define ST_DEFAULT_MAX_DENSITY 5
#define ST_DEFAULT_INIT_TABLE_SIZE 11
#define do_hash(key,table) (unsigned int)(*(table)->hash_fn)((key))
#define do_hash_bin(key,table) (do_hash(key, table)%(table)->num_bins)

#define EQUAL(table,x,y) ((x)==(y) || (*table->compare_fn)((x),(y)) == 0)
#define PTR_NOT_EQUAL(table, ptr, hash_val, key) \
((ptr) != 0 && (ptr->hash != (hash_val) || !EQUAL((table), (key), (ptr)->raw_key)))

#define ADD_DIRECT(table, key_box, key_raw, value, hash_val, bin_pos)\
{\
    lily_hash_entry *entry;\
    if (table->num_entries/(table->num_bins) > ST_DEFAULT_MAX_DENSITY) {\
        rehash(table);\
        bin_pos = hash_val % table->num_bins;\
    }\
    \
    entry = lily_malloc(sizeof(lily_hash_entry));\
    \
    entry->boxed_key = lily_value_copy(key_box); \
    entry->raw_key = key_raw; \
    entry->hash = hash_val;\
    entry->record = lily_value_copy(value);\
    entry->next = table->bins[bin_pos];\
    table->bins[bin_pos] = entry;\
    table->num_entries++;\
}

#define FIND_ENTRY(table, ptr, hash_val, bin_pos) \
bin_pos = hash_val%(table)->num_bins;\
ptr = (table)->bins[bin_pos];\
if (PTR_NOT_EQUAL(table, ptr, hash_val, key)) {\
    while (PTR_NOT_EQUAL(table, ptr->next, hash_val, key)) {\
        ptr = ptr->next;\
    }\
    ptr = ptr->next;\
}

static int new_size(int size)
{
    int i, newsize;

    for (i = 0, newsize = MINSIZE;
         i < sizeof(primes)/sizeof(primes[0]);
         i++, newsize <<= 1)
    {
        if (newsize > size)
            return primes[i];
    }

    /* Out of primes. You'll probably hit out of memory before this. */
    return -1;
}

static lily_hash_val *new_table_sized(int size, int (*compare_fn)(),
        int (*hash_fn)())
{
    lily_hash_val *tbl;

    size = new_size(size); /* round up to prime number */

    tbl = lily_malloc(sizeof(lily_hash_val));
    tbl->refcount = 0;
    tbl->iter_count = 0;
    tbl->compare_fn = compare_fn;
    tbl->hash_fn = hash_fn;
    tbl->num_entries = 0;
    tbl->num_bins = size;
    tbl->bins =
            (lily_hash_entry **)lily_malloc(size * sizeof(lily_hash_entry *));
    memset(tbl->bins, 0, size * sizeof(lily_hash_entry *));

    return tbl;
}

static lily_hash_val *new_table(int (*compare_fn)(), int (*hash_fn)())
{
    return new_table_sized(0, compare_fn, hash_fn);
}

static int strhash(char *string)
{
    register int c;
    register int val = 0;

    while ((c = *string++) != '\0')
        val = val * 997 + c;

    return val + (val >> 5);
}

static int numcmp(int x, int y)
{
    return x != y;
}

static int numhash(int n)
{
    return n;
}

lily_hash_val *lily_new_hash_numtable(void)
{
    return new_table(numcmp, numhash);
}

lily_hash_val *lily_new_hash_numtable_sized(int size)
{
    return new_table_sized(size, numcmp, numhash);
}

lily_hash_val *lily_new_hash_strtable(void)
{
    return new_table(strcmp, strhash);
}

lily_hash_val *lily_new_hash_strtable_sized(int size)
{
    return new_table_sized(size, strcmp, strhash);
}

lily_hash_val *lily_new_hash_like_sized(lily_hash_val *other, int size)
{
    return new_table_sized(size, other->compare_fn, other->hash_fn);
}

static void rehash(lily_hash_val *table)
{
    lily_hash_entry *ptr, *next, **new_bins;
    int i, old_num_bins = table->num_bins, new_num_bins;
    unsigned int hash_val;

    new_num_bins = new_size(old_num_bins+1);
    new_bins = (lily_hash_entry **)lily_malloc(
            new_num_bins * sizeof(lily_hash_entry *));
    memset(new_bins, 0, new_num_bins * sizeof(lily_hash_entry *));

    for(i = 0; i < old_num_bins; i++) {
        ptr = table->bins[i];
        while (ptr != 0) {
            next = ptr->next;
            hash_val = ptr->hash % new_num_bins;
            ptr->next = new_bins[hash_val];
            new_bins[hash_val] = ptr;
            ptr = next;
        }
    }
    lily_free(table->bins);
    table->num_bins = new_num_bins;
    table->bins = new_bins;
}

int lily_hash_delete(lily_hash_val *table, lily_value **boxed_key,
        lily_value **record)
{
    unsigned int hash_val;
    lily_hash_entry *tmp, *ptr;
    char *raw_key;

    if (table->compare_fn == numcmp)
        raw_key = (char *)(*boxed_key)->value.integer;
    else
        raw_key = (*boxed_key)->value.string->string;

    hash_val = do_hash_bin(raw_key, table);
    ptr = table->bins[hash_val];

    if (ptr == 0) {
        if (record != 0)
            *record = 0;
        return 0;
    }

    if (EQUAL(table, raw_key, ptr->raw_key)) {
        table->bins[hash_val] = ptr->next;
        table->num_entries--;
        if (record != 0)
            *record = ptr->record;

        *boxed_key = ptr->boxed_key;
        lily_free(ptr);
        return 1;
    }

    for(; ptr->next != 0; ptr = ptr->next) {
        if (EQUAL(table, ptr->next->raw_key, raw_key)) {
            tmp = ptr->next;
            ptr->next = ptr->next->next;
            table->num_entries--;
            if (record != 0)
                *record = tmp->record;

            *boxed_key = tmp->boxed_key;
            lily_free(tmp);
            return 1;
        }
    }

    return 0;
}

void lily_hash_insert_value(register lily_hash_val *table,
        lily_value *boxed_key, lily_value *record)
{
    unsigned int hash_val, bin_pos;
    register lily_hash_entry *ptr;
    register char *key;

    if (table->compare_fn == numcmp)
        key = (char *)boxed_key->value.integer;
    else
        key = boxed_key->value.string->string;

    hash_val = do_hash(key, table);
    FIND_ENTRY(table, ptr, hash_val, bin_pos);
    if (ptr == 0) {
        ADD_DIRECT(table, boxed_key, key, record, hash_val, bin_pos);
    }
    else {
        lily_value_assign(ptr->record, record);
        lily_value_assign(ptr->boxed_key, boxed_key);
    }
}

void lily_hash_insert_str(register lily_hash_val *table, lily_string_val *key,
        lily_value *record)
{
    lily_value boxed_key;
    boxed_key.flags = LILY_STRING_ID;
    boxed_key.value.string = key;

    lily_hash_insert_value(table, &boxed_key, record);
}

lily_value *lily_hash_find_value(lily_hash_val *table, lily_value *boxed_key)
{
    unsigned int hash_val, bin_pos;
    register lily_hash_entry *ptr;
    char *key;

    if (table->compare_fn == numcmp)
        key = (char *)boxed_key->value.integer;
    else
        key = boxed_key->value.string->string;

    hash_val = do_hash(key, table);
    FIND_ENTRY(table, ptr, hash_val, bin_pos);

    if (ptr)
        return ptr->record;
    else
        return NULL;
}

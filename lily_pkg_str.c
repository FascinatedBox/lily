#include <ctype.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_value.h"
#include "lily_vm.h"

void lily_str_concat(lily_vm_state *vm, uintptr_t *code, int num_args)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_str_val *ret, *arg1, *arg2;
    lily_value *ret_reg;
    ret_reg = vm_regs[code[2]];
    ret = vm_regs[code[2]]->value.str;
    arg1 = vm_regs[code[0]]->value.str;
    arg2 = vm_regs[code[1]]->value.str;

    int newsize = arg1->size + arg2->size + 1;

        /* Create a str if there isn't one. */
    if ((ret_reg->flags & VAL_IS_NIL_OR_PROTECTED) ||
        /* ...or to preserve immutability. */
        ret == arg1 || ret == arg2) {
        lily_str_val *new_sv = lily_malloc(sizeof(lily_str_val));
        char *new_str = lily_malloc(sizeof(char) * newsize);
        if (new_sv == NULL || new_str == NULL) {
            lily_free(new_sv);
            lily_free(new_str);
            return;
        }

        new_sv->str = new_str;
        new_sv->refcount = 1;
        new_sv->size = newsize;

        strcpy(new_sv->str, arg1->str);
        strcat(new_sv->str, arg2->str);

        if ((ret_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
            ret_reg->value.generic->refcount--;

        ret = new_sv;
        ret_reg->flags &= ~VAL_IS_PROTECTED;
    }
    else if (ret->size < newsize) {
        char *newstr;
        newstr = lily_realloc(ret->str, sizeof(char) * newsize);
        if (newstr == NULL)
            return;

        ret->str = newstr;
        strcpy(ret->str, arg1->str);
        strcat(ret->str, arg2->str);
    }

    vm_regs[code[2]]->value.str = ret;
    vm_regs[code[2]]->flags &= ~VAL_IS_NIL;
}

#define CTYPE_WRAP(WRAP_NAME, WRAPPED_CALL) \
void WRAP_NAME(lily_vm_state *vm, uintptr_t *code, int num_args) \
{ \
    lily_value **vm_regs = vm->vm_regs; \
    lily_value *ret_arg = vm_regs[code[1]]; \
    lily_value *input_arg = vm_regs[code[0]]; \
\
    if (input_arg->flags & VAL_IS_NIL || \
        input_arg->value.str->size == 0) { \
        ret_arg->value.integer = 0; \
        ret_arg->flags = 0; \
        return; \
    } \
\
    char *loop_str = input_arg->value.str->str; \
    int i = 0; \
\
    ret_arg->value.integer = 1; \
    ret_arg->flags = 0; \
    for (i = 0;i < input_arg->value.str->size;i++) { \
        if (WRAPPED_CALL(loop_str[i]) == 0) { \
            ret_arg->value.integer = 0; \
            break; \
        } \
    } \
}

CTYPE_WRAP(lily_str_isdigit, isdigit)
CTYPE_WRAP(lily_str_isalpha, isalpha)
CTYPE_WRAP(lily_str_isspace, isspace)
CTYPE_WRAP(lily_str_isalnum, isalnum)

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

/*  lstrip_utf8_start
    This is a helper for lstrip where input_arg's string has been checked to
    hold at least one utf8 chunk. */
static int lstrip_utf8_start(lily_value *input_arg, lily_value *strip_arg)
{
    char *input_str = input_arg->value.str->str;
    int input_length = input_arg->value.str->size;

    char *strip_str = strip_arg->value.str->str;
    int strip_length = strip_arg->value.str->size;
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

/*  lstrip_ascii_start
    This is a helper for lstrip where input_arg's string has been checked to
    hold no utf8 chunks. This does byte stripping, which is simpler than utf8
    chunk check+strip. */
static int lstrip_ascii_start(lily_value *input_arg, lily_value *strip_arg)
{
    int i;
    char *input_str = input_arg->value.str->str;
    int input_length = input_arg->value.str->size;

    if (strip_arg->value.str->size == 1) {
        /* Strip a single byte really fast. The easiest case. */
        char strip_ch;
        strip_ch = strip_arg->value.str->str[0];
        for (i = 0;i < input_length;i++) {
            if (input_str[i] != strip_ch)
                break;
        }
    }
    else {
        /* Strip one of many ascii bytes. A bit tougher, but not much. */
        char *strip_str = strip_arg->value.str->str;
        int strip_length = strip_arg->value.str->size;
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

void lily_str_lstrip(lily_vm_state *vm, uintptr_t *code, int num_args)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[0]];
    lily_value *strip_arg = vm_regs[code[1]];
    lily_value *result_arg = vm_regs[code[2]];

    char *strip_str;
    unsigned char ch;
    int copy_from, i, has_multibyte_char, strip_str_len;

    if (input_arg->flags & VAL_IS_NIL)
        lily_raise(vm->raiser, lily_ErrBadValue, "Input string is nil.\n");

    if (input_arg->value.str->size == 0) {
        lily_assign_value(vm, result_arg, input_arg);
        return;
    }

    if (strip_arg->flags & VAL_IS_NIL)
        lily_raise(vm->raiser, lily_ErrBadValue, "Cannot strip nil value.\n");

    if (strip_arg->value.str->size == 0) {
        lily_assign_value(vm, result_arg, strip_arg);
        return;
    }

    strip_str = strip_arg->value.str->str;
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
        copy_from = lstrip_ascii_start(input_arg, strip_arg);
    else
        copy_from = lstrip_utf8_start(input_arg, strip_arg);

    lily_str_val *new_sv = lily_malloc(sizeof(lily_str_val));
    char *sv_str = lily_malloc((input_arg->value.str->size - copy_from) + 1);

    if (new_sv == NULL || sv_str == NULL) {
        lily_free(new_sv);
        lily_free(sv_str);
        lily_raise_nomem(vm->raiser);
    }

    new_sv->str = sv_str;
    new_sv->size = (input_arg->value.str->size - copy_from);
    new_sv->refcount = 1;
    strcpy(sv_str, input_arg->value.str->str + copy_from);
    if ((result_arg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
        lily_deref_str_val(result_arg->value.str);

    result_arg->flags = 0;
    result_arg->value.str = new_sv;
}

void lily_str_startswith(lily_vm_state *vm, uintptr_t *code, int num_args)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_arg = vm_regs[code[0]];
    lily_value *prefix_arg = vm_regs[code[1]];
    lily_value *result_arg = vm_regs[code[2]];

    if (input_arg->flags & VAL_IS_NIL)
        lily_raise(vm->raiser, lily_ErrBadValue, "Input is nil.\n");

    if (prefix_arg->flags & VAL_IS_NIL)
        lily_raise(vm->raiser, lily_ErrBadValue, "Prefix is nil.\n");

    char *input_raw_str = input_arg->value.str->str;
    char *prefix_raw_str = prefix_arg->value.str->str;
    int prefix_size = prefix_arg->value.str->size;

    if (input_arg->value.str->size < prefix_size) {
        result_arg->value.integer = 0;
        result_arg->flags = 0;
        return;
    }

    int i, ok = 1;
    for (i = 0;i < prefix_size;i++) {
        if (input_raw_str[i] != prefix_raw_str[i]) {
            ok = 0;
            break;
        }
    }

    result_arg->flags = 0;
    result_arg->value.integer = ok;
}

static const lily_func_seed startswith =
    {"startswith", lily_str_startswith, NULL,
        {SYM_CLASS_FUNCTION, 3, 0, SYM_CLASS_INTEGER, SYM_CLASS_STR, SYM_CLASS_STR}};

static const lily_func_seed lstrip =
    {"lstrip", lily_str_lstrip, &startswith,
        {SYM_CLASS_FUNCTION, 3, 0, SYM_CLASS_STR, SYM_CLASS_STR, SYM_CLASS_STR}};

static const lily_func_seed isalnum_fn =
    {"isalnum", lily_str_isalnum, &lstrip,
        {SYM_CLASS_FUNCTION, 2, 0, SYM_CLASS_INTEGER, SYM_CLASS_STR}};

static const lily_func_seed isdigit_fn =
    {"isdigit", lily_str_isdigit, &isalnum_fn,
        {SYM_CLASS_FUNCTION, 2, 0, SYM_CLASS_INTEGER, SYM_CLASS_STR}};

static const lily_func_seed isalpha_fn =
    {"isalpha", lily_str_isalpha, &isdigit_fn,
        {SYM_CLASS_FUNCTION, 2, 0, SYM_CLASS_INTEGER, SYM_CLASS_STR}};

static const lily_func_seed isspace_fn =
    {"isspace", lily_str_isspace, &isalpha_fn,
        {SYM_CLASS_FUNCTION, 2, 0, SYM_CLASS_INTEGER, SYM_CLASS_STR}};

static const lily_func_seed concat =
    {"concat", lily_str_concat, &isspace_fn,
        {SYM_CLASS_FUNCTION, 3, 0, SYM_CLASS_STR, SYM_CLASS_STR, SYM_CLASS_STR}};

#define SEED_START concat

int lily_str_setup(lily_class *cls)
{
    cls->seed_table = &SEED_START;
    return 1;
}

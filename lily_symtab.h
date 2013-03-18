#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include <stdint.h>
# include "lily_raiser.h"
# include "lily_opcode.h"

/* This is where the raw data gets stored. Anything not an integer or a number
   is stored in ptr and cast appropriately. */
typedef union {
    int integer;
    double number;
    struct lily_method_val_t *method;
    struct lily_str_val_t *str;
    struct lily_list_val_t *list;
    struct lily_generic_val_t *generic;
    void *ptr;
} lily_value;

typedef struct lily_str_val_t {
    int refcount;
    char *str;
    int size;
} lily_str_val;

typedef struct lily_method_val_t {
    int refcount;
    uintptr_t *code;
    struct lily_var_t *first_arg;
    struct lily_var_t *last_arg;
    int pos;
    int len;
} lily_method_val;

typedef struct lily_list_val_t {
    int refcount;
    struct lily_sig_t *elem_sig;
    lily_value *values;
    int num_values;
} lily_list_val;

/* Every ref'd value is a superset of the 'generic' value (refcount
   comes first). This allows the vm to make refs/derefs a bit easier. */
typedef struct lily_generic_val_t {
    int refcount;
} lily_generic_val;

/* Indicates what kind of value is being stored. */
typedef struct lily_class_t {
    char *name;
    int id;
    int is_refcounted;
    struct lily_storage_t *storage;
    struct lily_sig_t *sig;
    struct lily_var_t *call_start;
    struct lily_var_t *call_top;
} lily_class;

typedef struct lily_call_sig_t {
    struct lily_sig_t *ret;
    struct lily_sig_t **args;
    int num_args;
    int is_varargs;
} lily_call_sig;

typedef struct lily_sig_t {
    lily_class *cls;
    /* This refcount was created so that complex list signatures could
       be shared without worry of the inner signature being deleted.
       Refcount is incremented even for basic signatures. */
    int refcount;
    union {
        lily_call_sig *call;
        struct lily_sig_t *value_sig;
    } node;
} lily_sig;

#define VAR_SYM      0x01
#define LITERAL_SYM  0x02
#define STORAGE_SYM  0x04
#define S_IS_NIL     0x10

/* All symbols have at least these fields. The vm and debugging functions use
   this to cast and grab common info. */
typedef struct lily_sym_t {
    int id;
    int flags;
    lily_sig *sig;
    lily_value value;
} lily_sym;

/* Literals are syms created to hold values that the lexer finds. These syms
   always have a value set. */
typedef struct lily_literal_t {
    int id;
    int flags;
    lily_sig *sig;
    lily_value value;
    struct lily_literal_t *next;
} lily_literal;

/* These are created by keywords (initialization), or by user declaration. They
   always have a class, but don't have a value at parse-time. Builtin symbols
   have line_num == 0. */
typedef struct lily_var_t {
    int id;
    int flags;
    lily_sig *sig;
    lily_value value;
    char *name;
    int line_num;
    lily_class *parent;
    struct lily_var_t *next;
} lily_var;

/* These hold the results of a vm op. An usage example would be holding the
   result of a plus operation. Each expression has a different id (expr_num) to
   keep the same storage from being used twice in an expression. Storages are
   circularly-linked, and held in the class that they are of (integer storages
   are in the integer class). */
typedef struct lily_storage_t {
    int id;
    int flags;
    lily_sig *sig;
    lily_value value;
    int expr_num;
    struct lily_storage_t *next;
} lily_storage;

typedef struct {
    /* The first symbol in the table (for itering from). This is also @main,
       since @main is the first symbol. */
    lily_var *var_start;
    /* The last symbol (for adding to). */
    lily_var *var_top;
    lily_var *old_var_start;
    lily_var *old_var_top;
    lily_class **classes;
    lily_literal *lit_start;
    lily_literal *lit_top;
    int next_var_id;
    int next_lit_id;
    int next_storage_id;
    int *lex_linenum;
    lily_raiser *raiser;
} lily_symtab;

/* Sync with keywords in lily_seed_symtab.h. */
#define KEY_IF      0
#define KEY_ELIF    1
#define KEY_ELSE    2
#define KEY_RETURN  3

#define KEY_LAST_ID 3

/* Sync with classname_seeds in lily_seed_symtab.h. */
#define SYM_CLASS_INTEGER  0
#define SYM_CLASS_NUMBER   1
#define SYM_CLASS_STR      2
#define SYM_CLASS_FUNCTION 3
#define SYM_CLASS_OBJECT   4
#define SYM_CLASS_METHOD   5
#define SYM_CLASS_LIST     6

#define SYM_LAST_CLASS     6

lily_class *lily_class_by_id(lily_symtab *, int);
lily_class *lily_class_by_name(lily_symtab *, char *);
lily_var *lily_find_class_callable(lily_class *, char *);
void lily_free_symtab(lily_symtab *);
int lily_keyword_by_name(char *);
lily_literal *lily_new_literal(lily_symtab *, lily_class *, lily_value);
lily_symtab *lily_new_symtab(lily_raiser *);
lily_var *lily_try_new_var(lily_symtab *, lily_class *, char *);
lily_var *lily_var_by_name(lily_symtab *, char *);
int lily_try_add_storage(lily_symtab *, lily_class *);
lily_method_val *lily_try_new_method_val();
void lily_deref_sig(lily_sig *);
void lily_deref_method_val(lily_method_val *);
void lily_deref_str_val(lily_str_val *);
void lily_deref_list_val(lily_sig *, lily_list_val *);
int lily_drop_block_vars(lily_symtab *, lily_var *);
#endif

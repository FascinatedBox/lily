#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include "lily_error.h"

/* There's no struct for storing floats or ints. They're stored raw (just cast
   the pointer). */

typedef struct {
    char *str;
    int size;
} lily_strval;

typedef struct {
    void *values;
    int *val_types;
    int val_count;
    int val_size;
} lily_listval;

typedef struct {
    int *code;
    int len;
    int pos;
} lily_code_data;

/* The following four types are the most important in lily. */

/* This is where the raw data gets stored. Anything not an integer or a number
   is stored in ptr and cast appropriately. */
typedef union {
    int integer;
    double number;
    void *ptr;
} lily_value;

/* Indicates what kind of value is being stored. */
typedef struct {
    char *name;
    int id;
} lily_class;

/* Objects are divided into three categories:
 * Fixed: Any number, integer, or string that's collected by the lexer (such as
 *        15, 1.0, or "hello, world."). These are called fixed because their
 *        class and value will not change. Their sym is NULL, because they were
 *        never declared.
 * Storage: Used for storing the results of opcodes in an intermediate location.
 *          Their class is set at parse-time, but the value changes. The sym is
 *          also NULL.
 * Vars: These hold the value of a symbol that was properly declared. Their
 *       class is set, but the value isn't. The sym points to the sym that they
 *       are associated with (so the VM can grab info on qualified symbols). */
#define OB_SYM     0x01
#define OB_FIXED   0x02

typedef struct lily_object_t {
    struct lily_object_t *next;
    /* For debugging. */
    int id;
    /* Also for debugging. */
    int flags;
    lily_class *cls;
    lily_value value;
    struct lily_symbol_t *sym;
} lily_object;

#define isafunc(s) (s->sym_class->id == SYM_CLASS_FUNCTION)

typedef struct lily_symbol_t {
    struct lily_symbol_t *next;
    char *name;
    int id;
    int line_num;
    int num_args;
    lily_object *object;
    lily_class *sym_class;
    lily_code_data *code_data;
} lily_symbol;

typedef struct {
    /* The first symbol in the table (for itering from). */
    lily_symbol *sym_start;
    /* The last symbol (for adding to). */
    lily_symbol *sym_top;
    lily_class **classes;
    /* The function containing commands outside of functions. */
    lily_symbol *main;
    lily_object *obj_start;
    lily_object *obj_top;
    int next_sym_id;
    int next_obj_id;
    int *lex_linenum;
    lily_excep_data *error;
} lily_symtab;

/* Sync with classnames in lily_symtab.c */
#define SYM_CLASS_FUNCTION 0
#define SYM_CLASS_STR      1
#define SYM_CLASS_INTEGER  2
#define SYM_CLASS_NUMBER   3

lily_class *lily_class_by_id(lily_symtab *, int);
lily_class *lily_class_by_name(lily_symtab *, char *);
void lily_free_symtab(lily_symtab *);
lily_object *lily_new_fixed(lily_symtab *, lily_class *);
lily_symtab *lily_new_symtab(lily_excep_data *);
lily_symbol *lily_new_var(lily_symtab *, lily_class *, char *);
lily_symbol *lily_sym_by_name(lily_symtab *, char *);

#endif

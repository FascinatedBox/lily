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

typedef union {
    int integer;
    double number;
    void *ptr;
} lily_value;

#define isafunc(s) (s->sym_class->id == SYM_CLASS_FUNCTION)

typedef struct {
    char *name;
    int id;
} lily_class;

typedef struct lily_symbol_t {
    struct lily_symbol_t *next;
    char *name;
    int id;
    int line_num;
    int num_args;
    void *value;
    lily_class *sym_class;
    lily_code_data *code_data;
} lily_symbol;

typedef struct {
    /* The first symbol in the table (for itering from). */
    lily_symbol *start;
    /* The last symbol (for adding to). */
    lily_symbol *top;
    lily_class **classes;
    /* The function containing commands outside of functions. */
    lily_symbol *main;
    int new_sym_id;
    int *lex_linenum;
    lily_excep_data *error;
} lily_symtab;

/* Sync with classnames in lily_symtab.c */
#define SYM_CLASS_FUNCTION 0
#define SYM_CLASS_STR      1
#define SYM_CLASS_INTEGER  2
#define SYM_CLASS_NUMBER   3

void lily_free_symtab(lily_symtab *);
lily_symtab *lily_new_symtab(lily_excep_data *);
lily_symbol *lily_sym_by_name(lily_symtab *, char *);
lily_class *lily_class_by_id(lily_symtab *, int);
lily_class *lily_class_by_name(lily_symtab *, char *);
lily_symbol *lily_new_var(lily_symtab *, lily_class *, char *);
lily_symbol *lily_new_temp(lily_symtab *, lily_class *, lily_value);

#endif

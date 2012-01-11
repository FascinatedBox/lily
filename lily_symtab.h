#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include "lily_error.h"
# include "lily_opcode.h"
# include "lily_expr_op.h"
# include "lily_builtins.h"

typedef struct {
    char *str;
    int size;
} lily_strval;

/* This is where the raw data gets stored. Anything not an integer or a number
   is stored in ptr and cast appropriately. */
typedef union {
    int integer;
    double number;
    void *ptr;
} lily_value;

/* Indicates what kind of value is being stored. */
typedef struct lily_class_t {
    char *name;
    int id;
    struct lily_storage_t *storage;
} lily_class;

#define VAR_SYM      0x01
#define LITERAL_SYM  0x02
#define STORAGE_SYM  0x04
#define S_IS_NIL     0x10

#define isafunc(s) (s->cls->id == SYM_CLASS_FUNCTION)

/* All symbols have at least these fields. The vm and debugging functions use
   this to cast and grab common info. */
typedef struct lily_sym_t {
    int id;
    int flags;
    lily_class *cls;
    lily_value value;
} lily_sym;

/* Literals are syms created to hold values that the lexer finds. These syms
   always have a value set. */
typedef struct lily_literal_t {
    int id;
    int flags;
    lily_class *cls;
    lily_value value;
    struct lily_literal_t *next;
} lily_literal;

typedef struct {
    lily_fast_func func;
    lily_class **args;
    int num_args;
    int *code;
    int len;
    int pos;
} lily_func_prop;

/* These are created by keywords (initialization), or by user declaration. They
   always have a class, but don't have a value at parse-time. Builtin symbols
   have line_num == 0. */
typedef struct lily_var_t {
    int id;
    int flags;
    lily_class *cls;
    lily_value value;
    char *name;
    int line_num;
    /* This stores extra information for the class. Currently, it's used to hold
       lily_func_prop's for functions. */
    void *properties;
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
    lily_class *cls;
    lily_value value;
    int expr_num;
    struct lily_storage_t *next;
} lily_storage;

typedef struct {
    /* The first symbol in the table (for itering from). */
    lily_var *var_start;
    /* The last symbol (for adding to). */
    lily_var *var_top;
    lily_class **classes;
    /* The function containing commands outside of functions. */
    lily_var *main;
    lily_literal *lit_start;
    lily_literal *lit_top;
    int next_var_id;
    int next_lit_id;
    int next_storage_id;
    int *lex_linenum;
    lily_excep_data *error;
} lily_symtab;

/* Sync with classname_seeds in lily_seed_symtab.h. */
#define SYM_CLASS_INTEGER  0
#define SYM_CLASS_NUMBER   1
#define SYM_CLASS_STR      2
#define SYM_CLASS_FUNCTION 3
#define SYM_CLASS_OBJECT   4

lily_class *lily_class_by_id(lily_symtab *, int);
lily_class *lily_class_by_name(lily_symtab *, char *);
void lily_free_symtab(lily_symtab *);
lily_literal *lily_new_literal(lily_symtab *, lily_class *);
lily_symtab *lily_new_symtab(lily_excep_data *);
lily_var *lily_new_var(lily_symtab *, lily_class *, char *);
lily_var *lily_var_by_name(lily_symtab *, char *);
void lily_add_storage(lily_symtab *, lily_storage *);
void lily_reset_main(lily_symtab *);
#endif

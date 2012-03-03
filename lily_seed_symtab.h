#ifndef LILY_SEED_SYMTAB_H
# define LILY_SEED_SYMTAB_H

# include "lily_symtab.h"
# include "lily_builtins.h"

/* Sync name order with SYM_CLASS_* #defines in lily_symtab.h */
char *class_seeds[] = {
    "integer",
    "number",
    "str",
    "function",
    "object",
    "method",
};

static const char *keywords[] = {
    "if",
    "elif",
    "else"
};

typedef const struct {
    char *name;
    int num_args;
    lily_fast_func func;
    int arg_ids[];
} func_entry;

static func_entry print = {"print", 1, lily_builtin_print, {SYM_CLASS_STR}};
/* All code outside of functions is stuffed here, and at the end of parsing,
   this function is called. */
static func_entry at_main = {"@main", 0, NULL, {-1}};
static func_entry *func_seeds[] = {&print, &at_main};

#endif

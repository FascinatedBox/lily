#ifndef LILY_SEED_SYMTAB_H
# define LILY_SEED_SYMTAB_H

# include "lily_vm.h"

/* Note: If CLS_VALID_HASH_KEY is added to other classes, the vm will need to be
         updated to hash those classes right. It will also need KeyError
         printing to be touched up for that. Other things may also need updating
         too. */

void lily_builtin_print(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_show(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_printfmt(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_calltrace(lily_vm_state *, lily_function_val *, uint16_t *);

static const lily_func_seed calltrace =
    {"calltrace", "function calltrace( => list[tuple[string, string, integer]])", lily_builtin_calltrace, NULL};
static const lily_func_seed show =
    {"show", "function show[A](A)", lily_builtin_show, &calltrace};
static const lily_func_seed print =
    {"print", "function print(string)", lily_builtin_print, &show};
static const lily_func_seed printfmt =
    {"printfmt", "function printfmt(string, list[any]...)", lily_builtin_printfmt, &print};

/* This must always be set to the last func seed defined here. */
#define GLOBAL_SEED_START printfmt

#endif

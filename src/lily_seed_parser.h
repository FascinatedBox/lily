#ifndef LILY_SEED_PARSER_H
# define LILY_SEED_PARSER_H

# include "lily_vm.h"
# include "lily_seed.h"

void lily_builtin_print(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_show(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_printfmt(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_calltrace(lily_vm_state *, lily_function_val *, uint16_t *);

static const lily_func_seed calltrace =
    {NULL, "calltrace", dyna_function, "function calltrace( => list[tuple[string, string, integer]])", lily_builtin_calltrace};
static const lily_func_seed show =
    {&calltrace, "show", dyna_function, "function show[A](A)", lily_builtin_show};
static const lily_func_seed print =
    {&show, "print", dyna_function, "function print(string)", lily_builtin_print};
static const lily_func_seed printfmt =
    {&print, "printfmt", dyna_function, "function printfmt(string, list[any]...)", lily_builtin_printfmt};

# define PARSER_SEED_START &printfmt
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_value.h"

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

void lily_file_open(lily_vm_state *vm, lily_function_val *self, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    char *path = vm_regs[code[0]]->value.string->string;
    char *mode = vm_regs[code[1]]->value.string->string;
    lily_value *result_reg = vm_regs[code[2]];

    errno = 0;
    char mode_ch = mode[0];
    if (mode_ch != 'a' &&
        mode_ch != 'w' &&
        mode_ch != 'r')
        lily_raise(vm->raiser, lily_IOError,
                "Mode must start with one of 'arw', but got '%c'.\n", mode_ch);

    FILE *f = fopen(path, mode);
    if (f == NULL) {
        lily_raise(vm->raiser, lily_IOError, "Errno %d: ^R (%s).\n",
                errno, errno, path);
    }

    lily_file_val *filev = lily_new_file_val(f, mode_ch);
    lily_raw_value v = {.file = filev};

    filev->inner_file = f;
    filev->is_open = 1;
    filev->refcount = 1;
    lily_move_raw_value(vm, result_reg, 0, v);
}

void lily_file_close(lily_vm_state *vm, lily_function_val *self, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_file_val *filev = vm_regs[code[0]]->value.file;

    if (filev->inner_file != NULL) {
        fclose(filev->inner_file);
        filev->inner_file = NULL;
        filev->is_open = 0;
    }
}

void lily_file_write(lily_vm_state *vm, lily_function_val *self, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_file_val *filev = vm_regs[code[0]]->value.file;
    char *to_write = vm_regs[code[1]]->value.string->string;

    write_check(vm, filev);

    fputs(to_write, filev->inner_file);
}

void lily_file_readline(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_file_val *filev = vm_regs[code[0]]->value.file;
    lily_value *result_reg = vm_regs[code[1]];
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

        if (ch == EOF) {
            buffer[pos] = '\0';
            break;
        }

        buffer[pos] = (char)ch;

        /* \r is intentionally not checked for, because it's been a very, very
           long time since any os used \r alone for newlines. */
        if (ch == '\n') {
            buffer[pos + 1] = '\0';
            pos++;
            break;
        }

        if (pos == buffer_size) {
            lily_msgbuf_grow(vm_buffer);
            buffer = vm_buffer->message;
            buffer_size = vm_buffer->size - 1;
        }

        pos++;
    }

    lily_string_val *new_sv = lily_malloc(sizeof(lily_string_val));
    char *sv_buffer = lily_malloc(pos + 1);

    /* Use memcpy, in case there are embedded \0 values somewhere. */
    memcpy(sv_buffer, buffer, pos + 1);

    new_sv->string = sv_buffer;
    new_sv->refcount = 1;
    new_sv->size = pos;

    lily_raw_value v = {.string = new_sv};
    lily_move_raw_value(vm, result_reg, 0, v);
}

static const lily_func_seed file_readline =
    {"readline", "function readline(file => bytestring)", lily_file_readline, NULL};

static const lily_func_seed file_write =
    {"write", "function write(file, string)", lily_file_write, &file_readline};

static const lily_func_seed file_close =
    {"close", "function close(file)", lily_file_close, &file_write};

static const lily_func_seed file_open =
    {"open", "function open(string, string => file)", lily_file_open, &file_close};

int lily_file_setup(lily_symtab *symtab, lily_class *file_cls)
{
    {
        lily_var *stdout_var = lily_new_var(symtab, file_cls->type, "stdout", 0);
        lily_file_val *filev = lily_new_file_val(stdout, 'w');
        lily_value v;
        v.type = file_cls->type;
        v.flags = 0;
        v.value.file = filev;
        lily_tie_value(symtab, stdout_var, &v);
    }

    {
        lily_var *stderr_var = lily_new_var(symtab, file_cls->type, "stderr", 0);
        lily_file_val *filev = lily_new_file_val(stderr, 'w');
        lily_value v;
        v.type = file_cls->type;
        v.flags = 0;
        v.value.file = filev;
        lily_tie_value(symtab, stderr_var, &v);
    }

    file_cls->seed_table = &file_open;
    return 1;
}

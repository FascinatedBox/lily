#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_seed.h"

#include "lily_api_value.h"

void lily_destroy_file(lily_value *v)
{
    lily_file_val *filev = v->value.file;

    if (filev->inner_file && filev->is_builtin == 0)
        fclose(filev->inner_file);

    lily_free(filev);
}

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

void lily_file_close(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_file_val *filev = vm_regs[code[1]]->value.file;

    if (filev->inner_file != NULL) {
        if (filev->is_builtin == 0)
            fclose(filev->inner_file);
        filev->inner_file = NULL;
    }
}

void lily_file_open(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    char *path = vm_regs[code[1]]->value.string->string;
    char *mode = vm_regs[code[2]]->value.string->string;
    lily_value *result_reg = vm_regs[code[0]];

    errno = 0;
    int ok;

    {
        char *mode_ch = mode;
        if (*mode_ch == 'r' || *mode_ch == 'w' || *mode_ch == 'a') {
            mode_ch++;
            if (*mode_ch == 'b')
                mode_ch++;

            if (*mode_ch == '+')
                mode_ch++;

            ok = (*mode_ch == '\0');
        }
        else
            ok = 0;
    }

    if (ok == 0)
        lily_raise(vm->raiser, lily_IOError,
                "Invalid mode '%s' given.\n", mode);

    FILE *f = fopen(path, mode);
    if (f == NULL) {
        lily_raise(vm->raiser, lily_IOError, "Errno %d: ^R (%s).\n",
                errno, errno, path);
    }

    lily_file_val *filev = lily_new_file_val(f, mode);

    lily_move_file(result_reg, filev);
}

void lily_file_write(lily_vm_state *, uint16_t, uint16_t *);

void lily_file_print(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_file_write(vm, argc, code);
    fputc('\n', vm->vm_regs[code[1]]->value.file->inner_file);
}

void lily_file_read_line(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_file_val *filev = vm_regs[code[1]]->value.file;
    lily_value *result_reg = vm_regs[code[0]];
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

        if (ch == EOF)
            break;

        buffer[pos] = (char)ch;

        if (pos == buffer_size) {
            lily_msgbuf_grow(vm_buffer);
            buffer = vm_buffer->message;
            buffer_size = vm_buffer->size - 1;
        }

        pos++;

        /* \r is intentionally not checked for, because it's been a very, very
           long time since any os used \r alone for newlines. */
        if (ch == '\n')
            break;
    }

    lily_move_string(result_reg, lily_new_raw_string_sized(buffer, pos));
}

void lily_file_write(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_file_val *filev = vm_regs[code[1]]->value.file;
    lily_value *to_write = vm_regs[code[2]];

    write_check(vm, filev);

    if (to_write->flags & VAL_IS_STRING)
        fputs(to_write->value.string->string, filev->inner_file);
    else {
        lily_msgbuf *msgbuf = vm->vm_buffer;
        lily_msgbuf_flush(msgbuf);
        lily_vm_add_value_to_msgbuf(vm, msgbuf, to_write);
        fputs(msgbuf->message, filev->inner_file);
    }
}

static const lily_func_seed file_close =
    {NULL, "close", dyna_function, "(File)", lily_file_close};

static const lily_func_seed file_open =
    {&file_close, "open", dyna_function, "(String, String):File", lily_file_open};

static const lily_func_seed file_print =
    {&file_open, "print", dyna_function, "[A](File, A)", lily_file_print};

static const lily_func_seed file_read_line =
    {&file_print, "read_line", dyna_function, "(File):ByteString", lily_file_read_line};

static const lily_func_seed dynaload_start =
    {&file_read_line, "write", dyna_function, "[A](File, A)", lily_file_write};


static const lily_class_seed file_seed =
{
    NULL,             /* next */
    "File",           /* name */
    dyna_class,       /* load_type */
    1,                /* is_refcounted */
    0,                /* generic_count */
    &dynaload_start   /* dynaload_table */
};

lily_class *lily_file_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &file_seed);
}

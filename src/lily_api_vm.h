#ifndef LILY_API_VM_H
# define LILY_API_VM_H

typedef struct lily_msgbuf_ lily_msgbuf;
typedef struct lily_vm_state_ lily_vm_state;

void lily_vm_raise(lily_vm_state *, uint8_t, const char *);
void lily_vm_raise_fmt(lily_vm_state *, uint8_t, const char *, ...);

lily_msgbuf *lily_vm_msgbuf(lily_vm_state *);
uint16_t *lily_vm_cid_table(lily_vm_state *);

#endif

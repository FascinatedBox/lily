#include <stdio.h>

typedef struct {
    FILE *lex_file;
    char *lex_buffer;
    int *lex_bufsize;
    int *lex_bufpos;
} lily_page_data;

void lily_page_scanner(void);
void lily_page_scanner_init(char *);

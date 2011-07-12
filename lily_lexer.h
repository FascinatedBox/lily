#include <stdio.h>

typedef struct {
    FILE *lex_file;
    char *lex_buffer;
    int *lex_bufsize;
    int *lex_bufpos;
} lily_lexer_data;

void lily_init_lexer(lily_lexer_data *);

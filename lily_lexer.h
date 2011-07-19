#include <stdio.h>

typedef enum {
    tk_invalid,
    tk_word
} lily_tok_type;

typedef struct {
    lily_tok_type tok_type;
    char *word_buffer;
} lily_token;

lily_token *lily_lexer_token();
void lily_init_lexer(char *);
void lily_lexer(void);

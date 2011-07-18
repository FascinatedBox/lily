#include "lily_lexer.h"
#include "lily_parser.h"

static lily_token *tok;

static void next_token()
{
    
}

void lily_init_parser(lily_parser_data *d)
{

}

void lily_parser(void)
{
    tok = lily_lexer_token();
}

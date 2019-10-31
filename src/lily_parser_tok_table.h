#ifndef LILY_PARSER_TOK_TABLE_H
# define LILY_PARSER_TOK_TABLE_H

/* This file creates a table of token information for the parser that's easy to
   understand and add to. */
typedef struct {
    /* The token that this block of info goes to. This token table can be sanity
       checked by ensuring that entries go up to tk_eof and that each entry's
       represented token goes up by 1. */
    lily_token tok;
    /* 0 or 1: If 1, this token is a value, or a token that is used for ending
               expressions. This is used to help determine if expression is
               done. */
    int val_or_end;
    /* The binary op of this token, or -1 if this token doesn't have an
       associated binary op. */
    lily_expr_op expr_op;
} lily_tok_info;

static const lily_tok_info parser_tok_table[] =
{
    /* token name       | val_or_end | expr_op */
    {tk_right_parenth,    0          , -1},
    {tk_comma,            1          , -1},
    {tk_left_curly,       0          , -1},
    {tk_right_curly,      1          , -1},
    {tk_left_bracket,     0          , -1},
    {tk_colon,            1          , -1},
    {tk_tilde,            0          , -1},
    {tk_bitwise_xor,      0          , expr_bitwise_xor},
    {tk_bitwise_xor_eq,   0          , expr_bitwise_xor_assign},
    {tk_not,              0          , -1},
    {tk_not_eq,           0          , expr_not_eq},
    {tk_modulo,           0          , expr_modulo},
    {tk_modulo_eq,        0          , expr_modulo_assign},
    {tk_multiply,         0          , expr_multiply},
    {tk_multiply_eq,      0          , expr_mul_assign},
    {tk_divide,           0          , expr_divide},
    {tk_divide_eq,        0          , expr_div_assign},
    {tk_plus,             0          , expr_plus},
    {tk_plus_plus,        0          , expr_plus_plus},
    {tk_plus_eq,          0          , expr_plus_assign},
    {tk_minus,            0          , expr_minus},
    {tk_minus_eq,         0          , expr_minus_assign},
    {tk_lt,               0          , expr_lt},
    {tk_lt_eq,            0          , expr_lt_eq},
    {tk_left_shift,       0          , expr_left_shift},
    {tk_left_shift_eq,    0          , expr_left_shift_assign},
    {tk_gt,               0          , expr_gr},
    {tk_gt_eq,            0          , expr_gr_eq},
    {tk_right_shift,      0          , expr_right_shift},
    {tk_right_shift_eq,   0          , expr_right_shift_assign},
    {tk_equal,            0          , expr_assign},
    {tk_eq_eq,            0          , expr_eq_eq},
    {tk_left_parenth,     0          , -1},
    {tk_lambda,           1          , -1},
    {tk_tuple_open,       0          , -1},
    {tk_tuple_close,      0          , -1},
    {tk_right_bracket,    0          , -1},
    {tk_arrow,            0          , -1},
    {tk_word,             1          , -1},
    {tk_prop_word,        1          , -1},
    {tk_double_quote,     1          , -1},
    {tk_bytestring,       1          , -1},
    {tk_byte,             1          , -1},
    /* These next two may or may not be a simple value: '1+1' is seen by the
       lexer as two tokens: '1 and +1'. In a binary context, '+1' should
       actually be '+ and 1'. Otherwise 'x = 1+1' fails, which is rather
       silly. */
    {tk_integer,          0          , -1},
    {tk_double,           0          , -1},
    {tk_docblock,         0          , -1},
    {tk_keyword_arg,      0          , -1},
    {tk_dot,              0          , -1},
    {tk_bitwise_and,      0          , expr_bitwise_and},
    {tk_bitwise_and_eq,   0          , expr_bitwise_and_assign},
    {tk_logical_and,      0          , expr_logical_and},
    {tk_bitwise_or,       0          , expr_bitwise_or},
    {tk_bitwise_or_eq,    0          , expr_bitwise_or_assign},
    {tk_logical_or,       0          , expr_logical_or},
    {tk_typecast_parenth, 0          , -1},
    {tk_three_dots,       1          , -1},
    {tk_func_pipe,        0          , expr_func_pipe},
    {tk_scoop,            0          , -1},
    {tk_invalid,          0          , -1},
    {tk_end_lambda,       1          , -1},
    {tk_end_tag,          1          , -1},
    {tk_eof,              1          , -1}
};

#endif

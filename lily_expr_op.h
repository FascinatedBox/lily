#ifndef LILY_EXPR_OP_H
# define LILY_EXPR_OP_H

typedef enum {
    expr_plus,
    expr_minus,
    expr_eq_eq,
    expr_lt,
    expr_lt_eq,
    expr_gr,
    expr_gr_eq,
    expr_not_eq,
    expr_multiply,
    expr_divide,
    expr_unary_not,
    expr_unary_minus,
    expr_logical_and,
    expr_logical_or,
    expr_assign,
    expr_mul_assign,
    expr_div_assign
} lily_expr_op;

#endif

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
    expr_unary_minus,
    expr_assign,
} lily_expr_op;

#endif

#ifndef LILY_PARSER_DATA_H
# define LILY_PARSER_DATA_H

/* Generated by scripts/parser_data.lily. */

typedef struct {
    const char *name;
    uint64_t shorthash;
} keyword_entry;

typedef void (expr_handler)(lily_parse_state *, uint16_t *);
typedef void (keyword_handler)(lily_parse_state *);

keyword_entry constants[] =
{
    {"true", 1702195828},
    {"self", 1718379891},
    {"unit", 1953066613},
    {"false", 435728179558},
    {"__dir__", 26845067786608479},
    {"__file__", 6872323072689856351},
    {"__line__", 6872323081280184159},
    {"__function__", 7598807797348065119},
};

# define CONST_TRUE 0
# define CONST_SELF 1
# define CONST_UNIT 2
# define CONST_FALSE 3
# define CONST___DIR__ 4
# define CONST___FILE__ 5
# define CONST___LINE__ 6
# define CONST___FUNCTION__ 7
# define CONST_BAD_ID 8

keyword_entry keywords[] =
{
    {"if", 26217},
    {"do", 28516},
    {"var", 7496054},
    {"for", 7499622},
    {"try", 7959156},
    {"case", 1702060387},
    {"else", 1702063205},
    {"elif", 1718185061},
    {"with", 1752459639},
    {"enum", 1836412517},
    {"while", 435610544247},
    {"raise", 435727982962},
    {"match", 448345170285},
    {"break", 461195539042},
    {"class", 495857003619},
    {"public", 109304441107824},
    {"static", 109304575259763},
    {"scoped", 110386840822643},
    {"define", 111524889126244},
    {"return", 121437875889522},
    {"except", 128026086176869},
    {"import", 128034844732777},
    {"forward", 28273260612448102},
    {"private", 28556934595048048},
    {"protected", 7310577382525465200},
    {"continue", 7310870969309884259},
    {"constant", 8389750308618530659},
};

# define KEY_IF 0
# define KEY_DO 1
# define KEY_VAR 2
# define KEY_FOR 3
# define KEY_TRY 4
# define KEY_CASE 5
# define KEY_ELSE 6
# define KEY_ELIF 7
# define KEY_WITH 8
# define KEY_ENUM 9
# define KEY_WHILE 10
# define KEY_RAISE 11
# define KEY_MATCH 12
# define KEY_BREAK 13
# define KEY_CLASS 14
# define KEY_PUBLIC 15
# define KEY_STATIC 16
# define KEY_SCOPED 17
# define KEY_DEFINE 18
# define KEY_RETURN 19
# define KEY_EXCEPT 20
# define KEY_IMPORT 21
# define KEY_FORWARD 22
# define KEY_PRIVATE 23
# define KEY_PROTECTED 24
# define KEY_CONTINUE 25
# define KEY_CONSTANT 26
# define KEY_BAD_ID 27

static void expr_arrow(lily_parse_state *, uint16_t *);
static void expr_binary(lily_parse_state *, uint16_t *);
static void expr_byte(lily_parse_state *, uint16_t *);
static void expr_bytestring(lily_parse_state *, uint16_t *);
static void expr_close_token(lily_parse_state *, uint16_t *);
static void expr_comma(lily_parse_state *, uint16_t *);
static void expr_dot(lily_parse_state *, uint16_t *);
static void expr_double(lily_parse_state *, uint16_t *);
static void expr_double_quote(lily_parse_state *, uint16_t *);
static void expr_integer(lily_parse_state *, uint16_t *);
static void expr_invalid(lily_parse_state *, uint16_t *);
static void expr_keyword_arg(lily_parse_state *, uint16_t *);
static void expr_lambda(lily_parse_state *, uint16_t *);
static void expr_left_bracket(lily_parse_state *, uint16_t *);
static void expr_left_parenth(lily_parse_state *, uint16_t *);
static void expr_minus(lily_parse_state *, uint16_t *);
static void expr_prop_word(lily_parse_state *, uint16_t *);
static void expr_tuple_open(lily_parse_state *, uint16_t *);
static void expr_unary(lily_parse_state *, uint16_t *);
static void expr_word(lily_parse_state *, uint16_t *);

static expr_handler *expr_handlers[] =
{
    [tk_right_parenth] = expr_close_token,
    [tk_comma] = expr_comma,
    [tk_left_curly] = expr_close_token,
    [tk_right_curly] = expr_close_token,
    [tk_left_bracket] = expr_left_bracket,
    [tk_colon] = expr_close_token,
    [tk_tilde] = expr_unary,
    [tk_bitwise_xor] = expr_binary,
    [tk_bitwise_xor_eq] = expr_binary,
    [tk_not] = expr_unary,
    [tk_not_eq] = expr_binary,
    [tk_modulo] = expr_binary,
    [tk_modulo_eq] = expr_binary,
    [tk_multiply] = expr_binary,
    [tk_multiply_eq] = expr_binary,
    [tk_divide] = expr_binary,
    [tk_divide_eq] = expr_binary,
    [tk_plus] = expr_binary,
    [tk_plus_plus] = expr_binary,
    [tk_plus_eq] = expr_binary,
    [tk_minus] = expr_minus,
    [tk_minus_eq] = expr_binary,
    [tk_lt] = expr_binary,
    [tk_lt_eq] = expr_binary,
    [tk_left_shift] = expr_binary,
    [tk_left_shift_eq] = expr_binary,
    [tk_gt] = expr_binary,
    [tk_gt_eq] = expr_binary,
    [tk_right_shift] = expr_binary,
    [tk_right_shift_eq] = expr_binary,
    [tk_equal] = expr_binary,
    [tk_eq_eq] = expr_binary,
    [tk_left_parenth] = expr_left_parenth,
    [tk_lambda] = expr_lambda,
    [tk_tuple_open] = expr_tuple_open,
    [tk_tuple_close] = expr_close_token,
    [tk_right_bracket] = expr_close_token,
    [tk_arrow] = expr_arrow,
    [tk_word] = expr_word,
    [tk_prop_word] = expr_prop_word,
    [tk_double_quote] = expr_double_quote,
    [tk_bytestring] = expr_bytestring,
    [tk_byte] = expr_byte,
    [tk_integer] = expr_integer,
    [tk_double] = expr_double,
    [tk_docblock] = expr_close_token,
    [tk_keyword_arg] = expr_keyword_arg,
    [tk_dot] = expr_dot,
    [tk_bitwise_and] = expr_binary,
    [tk_bitwise_and_eq] = expr_binary,
    [tk_logical_and] = expr_binary,
    [tk_bitwise_or] = expr_binary,
    [tk_bitwise_or_eq] = expr_binary,
    [tk_logical_or] = expr_binary,
    [tk_typecast_parenth] = expr_invalid,
    [tk_three_dots] = expr_close_token,
    [tk_func_pipe] = expr_binary,
    [tk_scoop] = expr_invalid,
    [tk_invalid] = expr_invalid,
    [tk_end_lambda] = expr_close_token,
    [tk_end_tag] = expr_close_token,
    [tk_eof] = expr_close_token,
};

static void keyword_if(lily_parse_state *);
static void keyword_do(lily_parse_state *);
static void keyword_var(lily_parse_state *);
static void keyword_for(lily_parse_state *);
static void keyword_try(lily_parse_state *);
static void keyword_case(lily_parse_state *);
static void keyword_else(lily_parse_state *);
static void keyword_elif(lily_parse_state *);
static void keyword_with(lily_parse_state *);
static void keyword_enum(lily_parse_state *);
static void keyword_while(lily_parse_state *);
static void keyword_raise(lily_parse_state *);
static void keyword_match(lily_parse_state *);
static void keyword_break(lily_parse_state *);
static void keyword_class(lily_parse_state *);
static void keyword_public(lily_parse_state *);
static void keyword_static(lily_parse_state *);
static void keyword_scoped(lily_parse_state *);
static void keyword_define(lily_parse_state *);
static void keyword_return(lily_parse_state *);
static void keyword_except(lily_parse_state *);
static void keyword_import(lily_parse_state *);
static void keyword_forward(lily_parse_state *);
static void keyword_private(lily_parse_state *);
static void keyword_protected(lily_parse_state *);
static void keyword_continue(lily_parse_state *);
static void keyword_constant(lily_parse_state *);

static keyword_handler *handlers[] =
{
    keyword_if,
    keyword_do,
    keyword_var,
    keyword_for,
    keyword_try,
    keyword_case,
    keyword_else,
    keyword_elif,
    keyword_with,
    keyword_enum,
    keyword_while,
    keyword_raise,
    keyword_match,
    keyword_break,
    keyword_class,
    keyword_public,
    keyword_static,
    keyword_scoped,
    keyword_define,
    keyword_return,
    keyword_except,
    keyword_import,
    keyword_forward,
    keyword_private,
    keyword_protected,
    keyword_continue,
    keyword_constant,
};

static const int valid_docblock_table[28] = {
    0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1,
    0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 1,
};

#endif

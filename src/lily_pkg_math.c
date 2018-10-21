/**
library math

The math package provides access to some useful math functions
and constants.
*/

#define _USE_MATH_DEFINES //I think this is needed?

#include <math.h>
#include <stdlib.h>

#include "lily.h"

/** Begin autogen section. **/
const char *lily_math_info_table[] = {
    "\0\0"
    ,"F\0abs\0(Integer): Integer"
    ,"F\0acos\0(Double): Double"
    ,"F\0asin\0(Double): Double"
    ,"F\0atan\0(Double): Double"
    ,"F\0ceil\0(Double): Double"
    ,"F\0cos\0(Double): Double"
    ,"F\0cosh\0(Double): Double"
    ,"F\0exp\0(Double): Double"
    ,"F\0fabs\0(Double): Double"
    ,"F\0floor\0(Double): Double"
    ,"F\0fmod\0(Double,Double): Double"
    ,"F\0is_infinity\0(Double): Boolean"
    ,"F\0is_nan\0(Double): Boolean"
    ,"F\0ldexp\0(Double,Integer): Double"
    ,"F\0log\0(Double): Double"
    ,"F\0log10\0(Double): Double"
    ,"F\0modf\0(Double): Tuple[Double,Double]"
    ,"F\0pow\0(Double,Double): Double"
    ,"F\0sin\0(Double): Double"
    ,"F\0sinh\0(Double): Double"
    ,"F\0sqrt\0(Double): Double"
    ,"F\0tan\0(Double): Double"
    ,"F\0tanh\0(Double): Double"
    ,"F\0to_deg\0(Double): Double"
    ,"F\0to_rad\0(Double): Double"
    ,"R\0huge\0Double"
    ,"R\0infinity\0Double"
    ,"R\0nan\0Double"
    ,"R\0pi\0Double"
    ,"Z"
};
#define toplevel_OFFSET 1
void lily_math__abs(lily_state *);
void lily_math__acos(lily_state *);
void lily_math__asin(lily_state *);
void lily_math__atan(lily_state *);
void lily_math__ceil(lily_state *);
void lily_math__cos(lily_state *);
void lily_math__cosh(lily_state *);
void lily_math__exp(lily_state *);
void lily_math__fabs(lily_state *);
void lily_math__floor(lily_state *);
void lily_math__fmod(lily_state *);
void lily_math__is_infinity(lily_state *);
void lily_math__is_nan(lily_state *);
void lily_math__ldexp(lily_state *);
void lily_math__log(lily_state *);
void lily_math__log10(lily_state *);
void lily_math__modf(lily_state *);
void lily_math__pow(lily_state *);
void lily_math__sin(lily_state *);
void lily_math__sinh(lily_state *);
void lily_math__sqrt(lily_state *);
void lily_math__tan(lily_state *);
void lily_math__tanh(lily_state *);
void lily_math__to_deg(lily_state *);
void lily_math__to_rad(lily_state *);
void lily_math_var_huge(lily_state *);
void lily_math_var_infinity(lily_state *);
void lily_math_var_nan(lily_state *);
void lily_math_var_pi(lily_state *);
void (*lily_math_call_table[])(lily_state *s) = {
    NULL,
    lily_math__abs,
    lily_math__acos,
    lily_math__asin,
    lily_math__atan,
    lily_math__ceil,
    lily_math__cos,
    lily_math__cosh,
    lily_math__exp,
    lily_math__fabs,
    lily_math__floor,
    lily_math__fmod,
    lily_math__is_infinity,
    lily_math__is_nan,
    lily_math__ldexp,
    lily_math__log,
    lily_math__log10,
    lily_math__modf,
    lily_math__pow,
    lily_math__sin,
    lily_math__sinh,
    lily_math__sqrt,
    lily_math__tan,
    lily_math__tanh,
    lily_math__to_deg,
    lily_math__to_rad,
    lily_math_var_huge,
    lily_math_var_infinity,
    lily_math_var_nan,
    lily_math_var_pi,
};
/** End autogen section. **/

/**
define abs(x: Integer): Integer

Calculates the absolute value of an integer.
*/
void lily_math__abs(lily_state *s)
{
    int64_t x = lily_arg_integer(s, 0);

    lily_return_integer(s, abs(x));
}

/**
define acos(x: Double): Double

Calculates the arc cosine of a double in radians.
*/
void lily_math__acos(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, acos(x));
}

/**
define asin(x: Double): Double

Calculates the arc sine of a double in radians.
*/
void lily_math__asin(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, asin(x));
}

/**
define atan(x: Double): Double

Calculates the arc tangent of a double in radians.
*/
void lily_math__atan(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, atan(x));
}

/**
define ceil(x: Double): Double

Round a double up to the nearest integer.
*/
void lily_math__ceil(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, ceil(x));
}

/**
define cos(x: Double): Double

Calculate the cosine of a double in radians.
*/
void lily_math__cos(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, cos(x));
}

/**
define cosh(x: Double): Double

Calculate the hyperbolic cosine of a double in radians.
*/
void lily_math__cosh(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, cosh(x));
}

/**
define exp(x: Double): Double

Calculate e^x.
*/
void lily_math__exp(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, exp(x));
}

/**
define fabs(x: Double): Double

Calculates the absolute value of a double.
*/
void lily_math__fabs(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, fabs(x));
}

/**
define floor(x: Double): Double

Round a double down to the nearest integer.
*/
void lily_math__floor(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, floor(x));
}

/**
define fmod(x: Double, y: Double): Double

Calculate the remainder of x/y.
*/
void lily_math__fmod(lily_state *s)
{
    double x = lily_arg_double(s, 0);
    double y = lily_arg_double(s, 1);

    lily_return_double(s, fmod(x, y));
}

/**
define is_infinity(x: Double): Boolean

Check if a number is infinity
*/
void lily_math__is_infinity(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_boolean(s, isinf(x));
}

/**
define is_nan(x: Double): Boolean

Check if a number is nan since if x = nan, then x == nan is false
*/
void lily_math__is_nan(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_boolean(s, isnan(x));
}

/**
define ldexp(x: Double, y: Integer): Double

Calculate x * 2^y.
*/
void lily_math__ldexp(lily_state *s)
{
    double x = lily_arg_double(s, 0);
    double y = lily_arg_integer(s, 1);

    lily_return_double(s, ldexp(x, y));
}

/**
define log(x: Double): Double

Calculate the log of a double with base e.
*/
void lily_math__log(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, log(x));
}

/**
define log10(x: Double): Double

Calculate the log of a double with base 10.
*/
void lily_math__log10(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, log10(x));
}

/**
define modf(x: Double): Tuple[Double, Double]

Split a double into an integer and a fractional part <[ipart, fpart]>.
*/
void lily_math__modf(lily_state *s)
{
    double i, f;
    double x = lily_arg_double(s, 0);

    f = modf(x, &i);

    lily_container_val *tpl = lily_push_tuple(s, 2);
    lily_push_double(s, i);
    lily_con_set_from_stack(s, tpl, 0);
    lily_push_double(s, f);
    lily_con_set_from_stack(s, tpl, 1);

    lily_return_top(s);
}

/**
define pow(x: Double, y: Double): Double

Calculate x^y.
*/
void lily_math__pow(lily_state *s)
{
    double x = lily_arg_double(s, 0);
    double y = lily_arg_double(s, 1);

    lily_return_double(s, pow(x, y));
}

/**
define sin(x: Double): Double

Calculate the sine of a double in radians.
*/
void lily_math__sin(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, sin(x));
}

/**
define sinh(x: Double): Double

Calculate the hyperbolic sine of a double in radians.
*/
void lily_math__sinh(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, sinh(x));
}

/**
define sqrt(x: Double): Double

Calculate the square root of a double.
*/
void lily_math__sqrt(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, sqrt(x));
}

/**
define tan(x: Double): Double

Calculate the tangent of a double in radians.
*/
void lily_math__tan(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, tan(x));
}

/**
define tanh(x: Double): Double

Calculate the hyperbolic tangent of a double in radians.
*/
void lily_math__tanh(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, tanh(x));
}

/**
define to_deg(x: Double): Double

Convert a double in radians to degrees.
*/
void lily_math__to_deg(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, x * (180 / M_PI));
}

/**
define to_rad(x: Double): Double

Convert a double in degrees to radians.
*/
void lily_math__to_rad(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, x * (M_PI / 180));
}

//--------------------Variables--------------------

/**
var huge: Double

Value used as an error returned by math functions.
+infinity on systems supporting IEEE Std 754-1985.
*/
void lily_math_var_huge(lily_state *s)
{
    lily_push_double(s, HUGE_VAL);
}

/**
var infinity: Double

Value representing unsigned infinity
*/
void lily_math_var_infinity(lily_state *s)
{
    lily_push_double(s, INFINITY);
}

/**
var nan: Double

Value representing not a number
*/
void lily_math_var_nan(lily_state *s)
{
    lily_push_double(s, NAN);
}

/**
var pi: Double

Value of pi
*/
void lily_math_var_pi(lily_state *s)
{
    lily_push_double(s, M_PI);
}

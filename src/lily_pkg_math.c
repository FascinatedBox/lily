#define _USE_MATH_DEFINES //I think this is needed?

#include <math.h>
#include <stdlib.h>

#include "lily.h"
#define LILY_NO_EXPORT
#include "lily_pkg_math_bindings.h"

void lily_math__abs(lily_state *s)
{
    int64_t x = lily_arg_integer(s, 0);

    lily_return_integer(s, llabs(x));
}

void lily_math__acos(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, acos(x));
}

void lily_math__asin(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, asin(x));
}

void lily_math__atan(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, atan(x));
}

void lily_math__ceil(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, ceil(x));
}

void lily_math__cos(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, cos(x));
}

void lily_math__cosh(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, cosh(x));
}

void lily_math__exp(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, exp(x));
}

void lily_math__fabs(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, fabs(x));
}

void lily_math__floor(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, floor(x));
}

void lily_math__fmod(lily_state *s)
{
    double x = lily_arg_double(s, 0);
    double y = lily_arg_double(s, 1);

    lily_return_double(s, fmod(x, y));
}

void lily_math__is_infinity(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_boolean(s, isinf(x));
}

void lily_math__is_nan(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_boolean(s, isnan(x));
}

void lily_math__ldexp(lily_state *s)
{
    double x = lily_arg_double(s, 0);
    int y = (int)lily_arg_integer(s, 1);

    lily_return_double(s, ldexp(x, y));
}

void lily_math__log(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, log(x));
}

void lily_math__log10(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, log10(x));
}

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

void lily_math__pow(lily_state *s)
{
    double x = lily_arg_double(s, 0);
    double y = lily_arg_double(s, 1);

    lily_return_double(s, pow(x, y));
}

void lily_math__sin(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, sin(x));
}

void lily_math__sinh(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, sinh(x));
}

void lily_math__sqrt(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, sqrt(x));
}

void lily_math__tan(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, tan(x));
}

void lily_math__tanh(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, tanh(x));
}

void lily_math__to_deg(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, x * (180 / M_PI));
}

void lily_math__to_rad(lily_state *s)
{
    double x = lily_arg_double(s, 0);

    lily_return_double(s, x * (M_PI / 180));
}

void lily_math_var_huge(lily_state *s)
{
    lily_push_double(s, HUGE_VAL);
}

void lily_math_var_infinity(lily_state *s)
{
    lily_push_double(s, INFINITY);
}

void lily_math_var_nan(lily_state *s)
{
    lily_push_double(s, NAN);
}

void lily_math_var_pi(lily_state *s)
{
    lily_push_double(s, M_PI);
}

LILY_DECLARE_MATH_CALL_TABLE

#include "lily.h"
#include "lily_farm_bindings.h"

void lily_farm_var_carrot_count(lily_state *s)
{
    lily_push_integer(s, 100);
}

void lily_farm_constant_a(lily_state *s)
{
    lily_push_integer(s, 10);
}

void lily_farm_constant_b(lily_state *s)
{
    lily_push_double(s, 5.5);
}

void lily_farm_constant_c(lily_state *s)
{
    lily_push_string(s, "test");
}

LILY_DECLARE_FARM_CALL_TABLE

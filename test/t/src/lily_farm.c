#include "lily.h"
#include "lily_farm_bindings.h"

void lily_farm_var_carrot_count(lily_state *s)
{
    lily_push_integer(s, 100);
}

LILY_DECLARE_FARM_CALL_TABLE

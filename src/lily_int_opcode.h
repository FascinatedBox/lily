#ifndef LILY_INT_OPCODE_H
# define LILY_INT_OPCODE_H

/* These are Lily's opcodes. The format is unlikely to change, so the comments
   should thus stay valid. For those wanting to get a better idea of how the
   opcodes are laid out, lily_code_iter.c is a good reference.
   The int prefix of this file means that it is internal, and thus may change
   (whereas an api file tends to be more stable). */

typedef enum {
    /* Perform an assignment, but do not alter refcount. */
    o_fast_assign,

    /* General purpose assign; right refcount increased, left decreased. */
    o_assign,

    /* Fast-path integer-only operations. */
    o_integer_add,
    o_integer_minus,
    o_modulo,
    o_integer_mul,
    o_integer_div,
    o_left_shift,
    o_right_shift,
    o_bitwise_and,
    o_bitwise_or,
    o_bitwise_xor,

    /* Integer/Double ops, that aren't as fast as the above ops. */
    o_double_add,
    o_double_minus,
    o_double_mul,
    o_double_div,

    /* Comparison operations are general purpose, and work for any two values of
       the same underlying class. */
    o_is_equal,
    o_not_eq,
    o_less,
    o_less_eq,
    o_greater,
    o_greater_eq,

    /* Simple unary operations. */
    o_unary_not,
    o_unary_minus,

    /* An absolute jump. A motion is done relative to the current position. The
       value is usually positive, but can be negative for backward jumps. */
    o_jump,
    /* Check a condition. If the condition matches the check value, then the
       jump provided is taken. Otherwise, control moves to after this condition.
       Like o_jump, this may be a negative jump. */
    o_jump_if,
    /* Check if an input symbol belongs to an input class. If yes, fall.
       Otherwise, follow the jump. */
    o_jump_if_not_class,

    /* Perform a single step of a for loop. This may jump out of the loop, or it
       may only increment and continue on. */
    o_integer_for,
    /* Prepare a for loop for entry by establishing the starting counter, and
       verifying that the increment is non-zero. */
    o_for_setup,

    /* Perform a call that has been guaranteed at emit-time to target a foreign
       function. The function to be called is provided as an index into the vm's
       readonly table. */
    o_foreign_call,
    /* This is a call to a function that is always native. The value of the
       function is given as an index into the vm's readonly table. */
    o_native_call,
    /* Perform a general call: It could be either a native function or a foreign
       one. The source is a register. This checks which path to use, and follows
       either o_foreign_call or o_native call. */
    o_function_call,

    /* Return to the caller and push a value back. */
    o_return_val,
    /* Return a Unit value to the caller. */
    o_return_unit,

    /* Build a new List. This includes a count, and registers to use as source
       values. */
    o_build_list,
    /* Build a new Tuple. This includes a count, and registers to use as source
       values. */
    o_build_tuple,
    /* Build a new Hash. This includes a count, which is the total number of
       values sent. The values are divided into key, value, key, value pairs. */
    o_build_hash,
    /* Build a new enum, which will never be empty. This includes the variant
       class id, a count, and the values. */
    o_build_enum,

    /* Try to get a value from one of: (Hash, List, Tuple, String). */
    o_get_item,
    /* Try to set a value into one of: (Hash, List, Tuple). */
    o_set_item,

    /* Get a value where the target index is an index to __main__'s globals. */
    o_get_global,
    /* Set a value where the target index is an index to __main__'s globals. */
    o_set_global,

    /* Get an interned value from vm's readonly table (empty variants, String,
       or ByteString). */
    o_get_readonly,
    /* Load an Integer value that is in the range of a 16-bit SIGNED value. */
    o_get_integer,
    /* Load a boolean value. */
    o_get_boolean,
    /* Load a byte value. */
    o_get_byte,
    /* Load an empty variant through the class id. */
    o_get_empty_variant,

    /* Create a new class that the gc can completely ignore. */
    o_new_instance_basic,
    /* Create a new class that might have tagged values inside. */
    o_new_instance_speculative,
    /* Create a new class and tag it. */
    o_new_instance_tagged,
    /* This is like o_get_item, except the index is an integer in the bytecode
       instead of a register pointing to an integer. The index is pre-checked.
       This is used for class member access. */
    o_get_property,
    /* This is like o_get_item, except the index is an integer in the bytecode
       instead of a register pointing to an integer. The index is pre-checked.
       This is used for class member access. */
    o_set_property,

    /* Register the current position as having code that can catch. */
    o_push_try,
    /* Unregister the last position as having code that can catch. */
    o_pop_try,
    /* Attempt to catch some class id. This includes a jump to the next catch
       declared in the same try (or 0 if it's the last one). */
    o_catch,
    /* Store the exception that was just caught by o_catch into a given
       register spot. */
    o_store_exception,
    /* Raise a value that emit-time has verified to be an exception. */
    o_raise,

    /* This provides a series of jumps, and a class identity. The enum given has
       the given id subtracted from it. The value left determines which jump
       should be taken. An exhaustive set of jumps is provided. */
    o_match_dispatch,
    /* This takes a variant, a count, and a number of output registers. Each
       value within the variant is dumped into an output register. */
    o_variant_decompose,

    /* Get a value where the target index is an upvalue index. */
    o_get_upvalue,
    /* Set a value where the target index is an upvalue index. */
    o_set_upvalue,
    /* Create the initial closure that will be handed out to subsequent closure
       operations at different levels. Termed 'backing closure'. */
    o_create_closure,
    /* This creates a copy of a given function that has upvalues from the
       backing closure. */
    o_create_function,
    /* Attempt to extract a value from a Dynamic. The wanted class is given as
       an id into the vm's class table. Success yields a Some(class), whereas
       failure provides a None. */
    o_dynamic_cast,
    /* This joins a series of values together into a String, eventually
       returning that String. */
    o_interpolation,
    /* This gets a stop register, and checks toward the front to find the last
       register that was given a value. Based on that, a jump is returned for
       code to go to. The emitter writes code between the jumps so that optional
       arguments are fixed up in a similar manner to assignments down a switch
       statement where there are no breaks. */
    o_optarg_dispatch,

    /* Exit the vm. This is written at the end of __main__ to make it leave the
       vm exec function. It is also spoofed when entering foreign functions, so
       that they leave the vm exec function properly. */
    o_return_from_vm
} lily_opcode;

#endif

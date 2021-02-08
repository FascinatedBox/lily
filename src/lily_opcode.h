#ifndef LILY_OPCODE_H
# define LILY_OPCODE_H

/* Here are the interpreter's opcodes. There are comments here explaining what
   these opcodes do.

   The interpreter uses a register-based vm. When an opcode is written, the
   values it is interested in are written behind it. This means that some
   opcodes (specifically calls) have a variable length.

   These opcodes have comments that document how they work. All opcode follow a
   common format (see code iter's header). This common format allows code iter
   to present opcode data in a uniform way for other services to consume the
   information. */
typedef enum {
    /* Perform an assignment and increase refcount. */
    o_assign,

    /* o_assign, but without refcount checking on the right side. */
    o_assign_noref,

    /* Integer-only operations. */
    o_int_add,
    o_int_minus,
    o_int_modulo,
    o_int_multiply,
    o_int_divide,
    o_int_left_shift,
    o_int_right_shift,
    o_int_bitwise_and,
    o_int_bitwise_or,
    o_int_bitwise_xor,

    /* Operations where a Double may be on one or both sides. */
    o_number_add,
    o_number_minus,
    o_number_multiply,
    o_number_divide,

    /* Comparisons. Less and less equal are missing because the emitter swaps
       their sides and writes greater and greater equal instead. */
    o_compare_eq,
    o_compare_not_eq,
    o_compare_greater,
    o_compare_greater_eq,

    /* Simple unary operations. */
    o_unary_not,
    o_unary_minus,
    o_unary_bitwise_not,

    /* Do a relative move of the instruction pointer by N spots. N may be
       negative (such as if this was written for `continue`). */
    o_jump,
    /* This is given a check bit (0 or 1), a value, and a distance to move.
       check bit == 0: The jump is taken if the value is falsey.
       check bit == 1: The jump is taken if the value is truthy.
       Like o_jump, the distance can be negative. */
    o_jump_if,
    /* This is given a class id, a value, and a distance to move.
       Check if 'value' has the class id given. Jump if it doesn't. This is used
       to implement `match`. */
    o_jump_if_not_class,
    /* This is given a value and a distance to move.
       Check if 'value' is set. If it is, then follow the jump provided.
       For optional arguments only. */
    o_jump_if_set,

    /* Perform a single step of a `for i in a...b` loop. */
    o_for_integer,
    /* Does setup work needed by `o_for_integer`. */
    o_for_setup,

    /* Perform a call. The target is known to be a foreign function. */
    o_call_foreign,
    /* Perform a call. The target is a native Lily function. */
    o_call_native,
    /* Perform a call. The source is a register that may have a native or
       foreign function. */
    o_call_register,

    /* Returns the value given to the caller. */
    o_return_value,
    /* Returns a Unit value to the caller. This is slightly faster than
       o_return_value because no register is loaded to get a Unit value. */
    o_return_unit,

    /* Build a List of a given size and values. */
    o_build_list,
    /* Build a Tuple of a given size and values. */
    o_build_tuple,
    /* Build a Hash of a given size and key+value pairs. The size provided is
       the total number of registers sent, not the number of pairs. */
    o_build_hash,
    /* Build a variant of a given size and values. Size should be at least 1.
       Use o_load_empty_variant to load empty variant values. */
    o_build_variant,

    /* Perform a subscript access (index is a value). */
    o_subscript_get,
    /* Perform a subscript assignment (index is a value). */
    o_subscript_set,

    /* Get a global value. An index is written in the bytecode. */
    o_global_get,
    /* Get a global value. An index is written in the bytecode. */
    o_global_set,

    /* Load a literal from vm's readonly_table. */
    o_load_readonly,
    /* Load an Integer from bytecode (it's loaded as a 16-bit SIGNED value). */
    o_load_integer,
    /* Load a Boolean from bytecode. */
    o_load_boolean,
    /* Load a Byte value from bytecode. */
    o_load_byte,
    /* Load an empty variant. The bytecode supplies a class id. */
    o_load_empty_variant,

    /* Create a new instance of some class id, or return the one being built if
       in a superclass. */
    o_instance_new,
    /* o_subscript_get, except that the index is non-negative, checked, and in
       bytecode instead of a register. This can be used on anything that is a
       container. This is used to implement class property access and
       decomposition of enums. */
    o_property_get,
    /* Like above, except for setting instead of getting. */
    o_property_set,

    /* Push a catch entry. It's expected that this will be followed by
       o_exception_catch. */
    o_catch_push,
    /* Pop a catch entry. Written when the catch isn't used or when exiting from
       a `try` block. */
    o_catch_pop,
    /* This is given the class id of some exception-based class, and a jump
       position. If the catch succeeds, then control returns to just after this
       opcode.
       For every `except` but the last, the jump points to the next `except`.
       The last jump will have a distance of 0 (which is invalid). */
    o_exception_catch,
    /* This is written after o_exception_catch if the `except` clause specified
       a var to store the exception into. The traceback is copied from the stack
       and stored into that var. */
    o_exception_store,
    /* Raises an exception using the value of the register given. */
    o_exception_raise,

    /* Get a closure upvalue. An index is written in the bytecode. */
    o_closure_get,
    /* Set a closure upvalue. An index is written in the bytecode. */
    o_closure_set,
    /* Given a native function and a size, this creates a closure with 'size'
       number of empty closure spots. */
    o_closure_new,
    /* Create a copy of a function using upvalues from the backing closure. The
       cells that are local to the function copy will be cleared out so they are
       fresh. */
    o_closure_function,

    /* This takes a series of values and shoves them into a msgbuf. The content
       of the msgbuf (a String) is deposited into a register. */
    o_interpolation,

    /* Leave the vm execution function. When a foreign function calls into the
       interpreter, the foreign function has this written for its code. When the
       interpreter is done with the native function and enters the code for the
       foreign frame, it sees this. That causes execution to return to the
       foreign function.
       This is also written at the end of __main__ for the same effect. */
    o_vm_exit,
} lily_opcode;

#endif

#ifndef LILY_OPCODE_H
# define LILY_OPCODE_H

/* This file defines the opcodes used by the Lily VM. This file is shared by
   emitter, debug, and (obviously) the vm. These codes, along with the operands,
   are written to the code array (uintptr_t *). There are a few rules to make
   sure things run smoothly though.
   * New opcodes must be showable by debug. There is no exception to this, and
     there never will be. This is the most important rule.
   * Most, but not all opcodes will have a line number. This will be written
     exactly after the opcode, so that code[opcode_pos+1] shall always yield the
     line number when a line number is needed.
   * If there is a result that can be the right side of an assignment, it shall
     always be the last value written.
   * If what an opcode does is not completely obvious, there will be an
     explanation of how it works here.
   Legend:
   * int: Describes an int value written to the code array.
   * addr: The given value is an address, rather than an index to something.
           This is necessary for o_get_const.
   * reg: An integer index that maps to a local register, unless otherwise
          specified. Each native function has its own set of registers to work
          with.
   * reg(x): Describes an index to a local register that is guaranteed by the
             emitter to be of a given type. Negative qualifiers are okay (ex:
             !any), as well as specifying anything of a basic type (ex:
             list[*] to denote a list that may contain any type).
   * reg...: Indicates a series of indexes to registers that will be used as
             arguments. An argument count is provided beforehand.
   * T: A type that could be anything. This is used to indicate that two syms
        share a type. Ex:
        * A reg(list[T])
        * B reg(T)
        This indicates that A has a list of some type, and that B is a value of
        that same type. List-related ops use this.

   Additionally, 'right' is used in place of 'result' where there is no true
   result.*/
typedef enum {
    /* Assignments: int lineno, reg left, reg right. */
    o_assign,
    /* Any assignment:
       * int lineno
       * reg(any) left
       * reg(*) right
       This makes sure that anys can be assigned any value. Updates the
       any's value sig and the value. Also does ref/deref. */
    o_any_assign,

    /* Ref assign handles assignments where left and right may need a ref/deref.
       string, list, and hash are examples of this. */
    o_ref_assign,

    /* Integer binary ops:
       * int lineno
       * reg(integer) left
       * reg(integer) right
       * reg(integer) result
       Opcodes that start with o_integer_ are integer-only versions of
       operations for which there is a more flexible numeric version.
       Those here that don't (like o_modulo, o_left_shift, etc.) do not have
       numeric versions. */
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

    /* Numeric binary ops:
       * int lineno
       * reg(number/integer) left
       * reg(number/integer) right
       * reg(number) result
       These are the slower arith ops, because they have to handle different
       type combinations. */
    o_number_add,
    o_number_minus,
    o_number_mul,
    o_number_div,

    /* Binary comparison ops:
       * int lineno
       * reg(integer/number/string) left
       * reg(typeof(left)) right
       * reg(integer) result */
    o_is_equal,
    o_not_eq,
    o_less,
    o_less_eq,
    o_greater,
    o_greater_eq,

    /* jump:
       * int jump
       This specifies a jump to a future position in the code. Emitter is
       responsible for ensuring the jump is valid. */
    o_jump,

    /* jump_if:
       * int jump_on
       * reg left
       * int jump
       If jump_on is 1, move to jump if left is TRUE.
       If jump_on is 0, move to jump if left is FALSE.
       This is called 'jump if false' or 'jump if true' by debug depending on
       if 0 or 1 is set. However, it is implemented as one op.
       Emitter is responsible for ensuring the jump is valid. */
    o_jump_if,

    /* Function calls:
       * int lineno
       * reg(func *) input
       * int num_args
       * reg args...
       * reg result
       Input -must- be a reg, or passing functions as arguments will fail. */
    o_function_call,

    /* return val:
       * int lineno
       * reg result
       Pushes the result to the storage that the caller reserved for it. The
       lineno is added for debug. */
    o_return_val,

    /* return noval:
       * int lineno
       Returns from the current call but doesn't push a value. Lineno is
       strictly for debug. */
    o_return_noval,

    /* Unary operations:
       * int lineno
       * int left
       * int result */
    o_unary_not,
    o_unary_minus,

    /* Build list:
       * int lineno
       * int num_args
       * reg args...
       * reg result
       This creates a new list. Emitter has already set the sig of result, and
       that is the type the elements are assumed to be. Emitter also guarantees
       that all elements are of the same type. */
    o_build_list,

    /* Build hash:
       * int lineno
       * int num_values
       * reg values...
       * reg result
       This creates a new hash, and is fairly similar to o_build_list in that
       the sig of the result is already set. 'values' is a series of key and
       value pairs. It should be noted that num_values is the number of values,
       NOT the number of pairs. So there are (num_values / 2) pairs to create.
       This was done intentionally to follow calls and o_build_list in that the
       count always precedes the exact number of values. */
    o_build_hash,

    /* Any typecast:
       * int lineno
       * reg(any) left
       * reg(!any) result
       This checks that the value contained by left is the same type as result.
       If it is, left's held value is set to 'right'. This is not checked by
       emitter (because what anys actually contain cannot be known at
       emit-time), and may raise ErrBadCast if the types do not match.
       This can be thought of as the converse of o_any_assign. */
    o_any_typecast,

    /* Integer<->Number typecast:
       * int lineno
       * reg(integer OR number) left
       * reg(integer OR number) right
       This handles conversion from integer to number, and vice versa. Left is
       the opposite type of right, and the appropriate conversion is made. */
    o_intnum_typecast,

    /* Show:
       * int lineno
       * int is_global
       * reg value
       This shows detailed information about the value within the given
       register. If is_global is 1, then the register given is a global
       register. Otherwise, the given register is a local. This is done so that
       globals don't need to be loaded into a local register, since that would
       make show much less helpful. */
    o_show,

    /* Return expected:
       * int lineno
       This is written at the end of every native function that has a non-nil
       return value. This raises ErrReturnExpected within the vm. This has
       lineno included because the vm expects that any opcode that raises has
       a line number after it. */
    o_return_expected,

    /* for (integer range):
       * int lineno
       * reg(integer) user loop var
       * reg(integer) start
       * reg(integer) end
       * reg(integer) step
       * int jump
       This implements a for loop over an integer range. This increments start
       by step until end is reached. This sets user loop var to start on each
       pass. This is done so that user modifications of user loop var do not
       cause the loop to exit early.
       If start == end, then control jumps to the given jump. */
    o_integer_for,

    /* for setup:
       * int lineno
       * reg(integer) user loop var
       * reg(integer) start
       * reg(integer) end
       * reg(integer) step
       * int setup step
       This is run before entering a for loop, and acts as a quick sanity check
       before entering the loop. Both start and end are checked for being nil
       here. If setup setp is 1, then the step will be calculated as -1 or +1.
       This sets user loop var to start, so that it has a proper initial value
       before entering the loop. */
    o_for_setup,

    /* Get Item:
       * int lineno
       * reg(?) left
       * reg(?) index
       * reg(?) right

       o_get_item handles getting a value at the given index and placing it
       into 'right'. Any creation and ref/deref are handled automatically.

       Lists:
       * left is the list to take a value from.
       * index is what element to grab. It's always an integer.
       * right is the value. The type is of the list's value.
       Hashes:
       * left is the hash to take a value from.
       * index is the hash key. The type is the key of the hash.
       * right is the value. The type is the value of the hash.  */
    o_get_item,

    /* Set Item:
       * int lineno
       * reg(?) left
       * reg(?) index
       * reg(?) right
       o_set_item handles setting an item for a complex type. Any creation and
       ref/deref are done as well.

       Lists:
       * left is the list to take a value from.
       * index is what element to grab. It's always an integer.
       * right is the value. The type is of the list's value.
       Hashes:
       * left is the hash to take a value from.
       * index is the hash key. The type is the key of the hash.
       * right is the value. The type is the value of the hash.  */
    o_set_item,

    /* get global:
       * int lineno
       * reg global_reg
       * reg local_reg
       This handles loading a global value from __main__'s registers, and
       putting it into a local register of the current function. This also
       handles ref/deref if necessary. */
    o_get_global,

    /* set global:
       * int lineno
       * reg local_reg
       * reg global_reg
       This sets a global value in one of __main__'s registers. Like get global,
       this will do ref/deref if necessary. */
    o_set_global,

    /* get const:
       * int lineno
       * addr lit
       * reg result
       This loads a literal value from the given address, and stores the value
       into the given register. */
    o_get_const,

    o_package_set,
    o_package_set_deep,
    o_package_get,
    o_package_get_deep,

    /* isnil:
       * int lineno
       * int is_global
       * reg value
       * reg result
       This checks for the nil flag being set on the given value, storing the
       result in the result register. If 'value' is an any, an additional check
       is done to see if the any contains a nil value. */
    o_isnil,

    /* Return from vm:
       This is a special opcode used to leave the vm. It does not take any
       values. This is written at the end of __main__. */
    o_return_from_vm
} lily_opcode;

#endif

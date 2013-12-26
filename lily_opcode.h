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
   * sym: Describes a symbol that could be of any type.
   * sym(x): Describes a symbol that is guaranteed by the emitter to be of a
             given type. Negative qualifiers are okay (ex: !object), as well
             as specifying anything of a basic type (ex: list[*] to denote a
             list that may contain any type).
   * sym...: Indicates a series of arguments. Typically, a num_args is given
             beforehand to indicate how many syms are coming.
   * T: A type that could be anything. This is used to indicate that two syms
        share a type. Ex:
        * A sym(list[T])
        * B sym(T)
        This indicates that A has a list of some type, and that B is a value of
        that same type. List-related ops use this.
        
   Additionally, 'right' is used in place of 'result' where there is no true
   result.*/
typedef enum {
    /* Assignments: int lineno, sym left, sym right. */
    o_assign,
    /* Object assignment:
       * int lineno
       * sym(object) left
       * sym(*) right
       This makes sure that objects can be assigned any value. Updates the
       object's value sig and the value. Also does ref/deref. */
    o_obj_assign,

    /* Ref assign handles assignments where left and right may need a ref/deref.
       str, method, and function are examples of this. This does not handle
       objects. */
    o_ref_assign,

    /* Subscript assignment:
       * int lineno
       * sym(list[T]) left
       * sym(integer) index
       * sym(T) right
       Subscript assignment is special cased so that the list holding the value
       is updated. Subscript and then assign also wouldn't work for primitives
       (the assign would be targeting an integer storage, not the list value).
       This handles objects and ref/deref situations. */
    o_sub_assign,

    /* Integer binary ops:
       * int lineno
       * sym(integer) left
       * sym(integer) right
       * sym(integer) result
       These are the integer-only fast version of the number arith ops. */
    o_integer_add,
    o_integer_minus,
    o_integer_mul,
    o_integer_div,
    o_left_shift,
    o_right_shift,

    /* Numeric binary ops:
       * int lineno
       * sym(number/integer) left
       * sym(number/integer) right
       * sym(number) result
       These are the slower arith ops, because they have to handle different
       type combinations. */
    o_number_add,
    o_number_minus,
    o_number_mul,
    o_number_div,

    /* Binary comparison ops:
       * int lineno
       * sym(integer/number/str) left
       * sym(typeof(left)) right
       * sym(integer) result */
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
       * sym left
       * int jump
       If jump_on is 1, move to jump if left is TRUE.
       If jump_on is 0, move to jump if left is FALSE.
       This is called 'jump if false' or 'jump if true' by debug depending on
       if 0 or 1 is set. However, it is implemented as one op.
       Emitter is responsible for ensuring the jump is valid. */
    o_jump_if,

    /* Function and method calls are treated differently because they do their
       args differently. Method wants the varargs packed into a list, and that
       would be a waste of time for functions. This may be changed in the
       future.
       Both are: 
       * int lineno
       * sym(method * / func *) input
       * int num_args
       * sym args...
       * sym result
       Input -must- be a sym, or passing methods/functions as arguments will
       fail. */
    o_func_call,
    o_method_call,

    /* return val:
       * int lineno
       * sym result
       Pushes the result to the storage that the caller reserved for it. The
       lineno is added for debug. */
    o_return_val,

    /* return noval:
       * int lineno
       Returns from the current call but doesn't push a value. Lineno is
       strictly for debug. */
    o_return_noval,

    /* Save:
       * int num_args
       * sym(*) args...
       This is used to save locals and parameters (and occasionally storages)
       before doing a method call. */
    o_save,

    /* Restore:
       * int num_args
       The converse of o_save. This restores the symbols saved, and is done
       after a method call. */
    o_restore,

    /* Unary operations:
       * int lineno
       * int left
       * int result */
    o_unary_not,
    o_unary_minus,

    /* Build list:
       * int lineno
       * int num_args
       * sym args...
       * sym result
       This creates a new list. Emitter has already set the sig of result, and
       that is the type the elements are assumed to be. Emitter also guarantees
       that all elements are of the same type. */
    o_build_list,

    /* Subscript: 
       * int lineno
       * sym(list[T]) list
       * sym(integer) index
       * sym(T) right
       This is used to take a value from a list and place it in a storage
       indicated by 'right'. This handles ref/deref and objects. */
    o_subscript,

    /* Object typecast:
       * int lineno
       * sym(object) left
       * sym(!object) result
       This checks that the value contained by left is the same type as result.
       If it is, left's held value is set to 'right'. This is not checked by
       emitter (because what objects actually contain cannot be known at
       emit-time), and may raise ErrBadCast if the types do not match.
       This can be thought of as the converse of o_obj_assign. */
    o_obj_typecast,

    /* Show:
       * int lineno
       * sym value
       This displays information about the given value. */
    o_show,

    /* Return expected:
       * int lineno
       This is written at the end of every method that has a non-nil return
       value. This raises ErrReturnExpected within the vm. This has lineno
       included because the vm expects that any opcode that raises has a line
       number after it. */
    o_return_expected,

    /* for (integer range):
       * int lineno
       * sym(integer) user loop var
       * sym(integer) start
       * sym(integer) end
       * sym(integer) step
       * int jump
       This implements a for loop over an integer range. This increments start
       by step until end is reached. This sets user loop var to start on each
       pass. This is done so that user modifications of user loop var do not
       cause the loop to exit early.
       If start == end, then control jumps to the given jump. */
    o_integer_for,

    /* for setup:
       * int lineno
       * sym(integer) user loop var
       * sym(integer) start
       * sym(integer) end
       * sym(integer) step
       * int setup step
       This is run before entering a for loop, and acts as a quick sanity check
       before entering the loop. Both start and end are checked for being nil
       here. If setup setp is 1, then the step will be calculated as -1 or +1.
       This sets user loop var to start, so that it has a proper initial value
       before entering the loop. */
    o_for_setup,

    /* Return from vm:
       This is a special opcode used to leave the vm. It does not take any
       values. This is written at the end of @main. */
    o_return_from_vm
} lily_opcode;

#endif

#ifndef LILY_OPCODE_TABLE_H
# define LILY_OPCODE_TABLE_H

extern const int opcode_table[][8];

/* C_LINENO:          This position contains the line number upon which the
                      opcode is executed. If this exists, it is always right
                      after the opcode. */
# define C_LINENO           0
/* C_INPUT:           This specifies a symbol that is being read. */
# define C_INPUT            1
/* C_OUTPUT:          The opcode's result will be written to this place. */
# define C_OUTPUT           2
/* C_NOP:             This position does not do anything. */
# define C_NOP              3
/* C_JUMP:            This position contains a jump to a future location. The
                      position is an int, not a sym. */
# define C_JUMP             4
/* C_JUMP_ON:         This contains 1 or 0. This is used to determine if
                      o_jump_if should jump on truth or false value. */
# define C_JUMP_ON          5
/* C_COUNT:           This specifies a number of arguments or values to come.
                      This value is stored but not shown. */
# define C_COUNT            8
/* C_COUNT_LIST:      This specifies the start of an argument list, using the
                      value recorded by C_COUNT. */
# define C_COUNT_LIST       9
/* C_INT_VAL:         Show a value that's just an integer. This is used by
                      o_for_setup to determine if it should init the step value
                      or not. */
# define C_INT_VAL         10
/* C_LIT_INPUT:       The input is a position in the vm's table of literals. */
# define C_READONLY_INPUT  11
/* C_GLOBAL_INPUT:    The INput is the address of a global register. */
# define C_GLOBAL_INPUT    12
/* C_GLOBAL_OUTPUT:   The OUTput is the address of a global register. */
# define C_GLOBAL_OUTPUT   13
/* C_CALL_TYPE:       This is used by calls to determine how the call is stored:
                      0: The input is a readonly var.
                      1: The input is a local register. */
# define C_CALL_TYPE       14
/* C_CALL_INPUT:      Input to a function call. This is shown according to what
                      C_CALL_INPUT_TYPE picked up. */
# define C_CALL_INPUT      15
/* C_COUNT_JUMPS:     This specifies the start of a series of jumps, using the
                      value recorded by C_COUNT. */
# define C_COUNT_JUMPS     16
/* C_MATCH_INPUT:     This is a special case for the input value to a match
                      expression. It allows C_COUNT_JUMPS to specify what
                      classes map to which locations. */
# define C_MATCH_INPUT     17
/* C_COUNT_OUTPUTS:   This specifies the start of a series of outputs, using
                      the value recorded by C_COUNT. */
# define C_COUNT_OUTPUTS   18
/* C_COUNT_OPTARGS:   This specifies a series of var spot + literal spot pairs,
                      It's the number of values, not the number of pairs though,
                      so that it's consistent with everything else. */
# define C_COUNT_OPTARGS   19

#endif

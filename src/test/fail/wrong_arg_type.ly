###
SyntaxError: Function f, argument #1 is invalid:
Expected Type: integer
Received Type: string
Where: File "test/fail/wrong_arg_type.ly" at line 8
###

define f(integer a) {} f("a")

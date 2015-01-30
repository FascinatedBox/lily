###
SyntaxError: Function f, argument #1 is invalid:
Expected Type: function ()
Received Type: integer
Where: File "test/fail/wrong_arg_type_empty_func.ly" at line 10
###

define f(g: function()) {  }

f(10)

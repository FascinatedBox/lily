###
SyntaxError: Cannot nest an assignment within an expression.
Where: File "test/fail/assign_nest.ly" at line 7
###

integer a = 10
integer b = (a = 10)

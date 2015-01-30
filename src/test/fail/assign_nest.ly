#[
SyntaxError: Cannot nest an assignment within an expression.
Where: File "test/fail/assign_nest.ly" at line 7
]#

var a = 10
var b = (a = 10)

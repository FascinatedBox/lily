###
SyntaxError: Function g arg #0 expects type 'A' but got type 'B'.
Where: File "test/fail/wrong_generic_type.ly" at line 9
###

# This isn't allowed because A and B may be incompatible types.
# Ex: A being an integer, B as a string.
function f[A, B](function g(A), B value) {
	g(value)
}

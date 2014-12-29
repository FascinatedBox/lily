###
SyntaxError: Function g, argument #1 is invalid:
Expected Type: A
Received Type: B
Where: File "test/fail/wrong_generic_type.ly" at line 11
###

# This isn't allowed because A and B may be incompatible types.
# Ex: A being an integer, B as a string.
function f[A, B](function g(A), B value) {
	g(value)
}

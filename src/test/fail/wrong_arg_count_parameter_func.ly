###
SyntaxError: Function g expects 1 args, but got 3.
Where: File "test/fail/wrong_arg_count_parameter_func.ly" at line 7
###

define f(function g(integer)) {
	g(1, 2, 3)
}

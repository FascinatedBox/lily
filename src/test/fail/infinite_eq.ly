#[
RecursionError: Infinite loop in comparison.
Traceback:
    Function __main__ at line 9
]#

var lso: list[any] = [1, 2, 3]
lso[0] = lso
if lso == lso:
	print("This will never happen.\n")

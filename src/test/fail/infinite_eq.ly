###
RecursionError: Infinite loop in comparison.
Traceback:
    Function __main__ at line 9
###

list[any] lso = [1, 2, 3]
lso[0] = lso
if lso == lso:
	print("This will never happen.\n")

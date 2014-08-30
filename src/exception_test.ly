<@lily

# This file was made to test exceptions. I'll merge it into sanity testing
# once exceptions are more complete.

# Test 1: Catch an error from dividing by zero.
try: {
	integer a = 1 / 0
except DivisionByZeroError:
	print("DBZError successfully caught!\n")
}

# Test 2: Catch a different error.
try: {
	list[integer] lsi = [1]
	integer v = lsi[1000]
except IndexError:
	print ("IndexError successfully caught!\n")
}

# Test 3: If a catching exception is a base of a raised exception, allow it.
#         This is useful for catching any of a group of exceptions.
try: {
	printfmt("%s%s%s%s%s", "")
except Exception:
	print("Successfully caught FormatError as Exception!\n")
}

function g( => integer) {
	return 10 / 0
}

function f() {
	integer a1, b, c, d, e = 10
	integer z = g()
}

# Test 4: Attempt to catch an exception triggered in a function. This forces
#         the vm to unwind the stack back into __main__.
try: {
	f()
except DivisionByZeroError:
	print("Caught a nested exception!\n")
}

# Test 5: Nested catching. The first try fails to catch it, but the second one
#         has the right class.
try: {
	try: {
		integer a = 1 / 0
	except ValueError:
		print("Incorrect exception caught!!!\n")
	}
except DivisionByZeroError:
	print("Caught an error missed by an inner block!")
}

# Test 6: Put exception information into a var. For now, use show to force a
#         simple dump of the exception's properties.
try: {
	list[integer] lsi = [1]
	integer a = lsi[3]
except IndexError as e:
	show e
}

@>

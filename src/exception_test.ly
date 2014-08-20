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
except RangeError:
	print ("RangeError successfully caught!\n")
}

# Test 3: If a catching exception is a base of a raised exception, allow it.
#         This is useful for catching any of a group of exceptions.
try: {
	printfmt("%s%s%s%s%s", "")
except Exception:
	print("Successfully caught FormatError as Exception!\n")
}

@>

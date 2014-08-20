<@lily

# This file was made to test exceptions. I'll merge it into sanity testing
# once exceptions are more complete.

# Test 1: Catch an error from dividing by zero.
try: {
	integer a = 1 / 0
except DivisionByZeroError:
	print("Test 1 (catch division by zero) passed!")
}

show __main__

@>

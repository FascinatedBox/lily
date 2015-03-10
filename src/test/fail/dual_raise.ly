#[
DivisionByZeroError: Attempt to divide by zero.
Traceback:
    Function __main__ at line 10
]#

try:
    raise ValueError::new("10")
except ValueError:
    var v = 1 / 0

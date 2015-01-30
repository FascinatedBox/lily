#[
SyntaxError: Conditional expression has no value.
Where: File "test/fail/assign_in_condition.ly" at line 8
]#

var a = 10

if a = 10:
    print("Failed!\n")

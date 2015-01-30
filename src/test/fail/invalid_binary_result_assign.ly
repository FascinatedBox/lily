#[
SyntaxError: Left side of = is not assignable.
Where: File "test/fail/invalid_binary_result_assign.ly" at line 8
]#

var a = 1
if 1:
    (a + a) = 1

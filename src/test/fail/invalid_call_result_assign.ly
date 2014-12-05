###
SyntaxError: Left side of = is not assignable.
Where: File "test/fail/invalid_call_result_assign.ly" at line 10
###

function f( => list[integer]) {
    return [1]
}

f()[0] = 1

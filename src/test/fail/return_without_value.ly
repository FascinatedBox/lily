###
SyntaxError: 'return' expression has no value.
Where: File "test/fail/return_without_value.ly" at line 6
###

function f() {  } function g( => integer) { return f() }

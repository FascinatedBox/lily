#[
SyntaxError: 'return' expression has no value.
Where: File "test/fail/return_without_value.ly" at line 6
]#

define f() {  } define g( => integer) { return f() }

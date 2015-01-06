###
SyntaxError: Conditional expression has no value.
Where: File "test/fail/condition_without_value.ly" at line 7
###

define f() {}
if f():
    f()

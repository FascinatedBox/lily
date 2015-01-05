###
SyntaxError: Function f, argument #2 is invalid:
Expected Type: integer
Received Type: string
Where: File "test/fail/generics_default_only_resolved.ly" at line 14
###

function f[A](A value1, A value2) {  }
function g[A](A value1 => A) {  }

# g should not assume any type info for the argument it takes because the
# A given isn't resolved. If it's pulled in, then the error message is somewhat
# confusing (it's assumed a bare A is wanted).
f(10, g("10"))

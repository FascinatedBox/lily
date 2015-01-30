#[
SyntaxError: Function f, argument #2 is invalid:
Expected Type: integer
Received Type: string
Where: File "test/fail/generics_default_only_resolved.ly" at line 14
]#

define f[A](value1: A, value2: A) {  }
define g[A](value1: A => A) {  }

# g should not assume any type info for the argument it takes because the
# A given isn't resolved. If it's pulled in, then the error message is somewhat
# confusing (it's assumed a bare A is wanted).
f(10, g("10"))

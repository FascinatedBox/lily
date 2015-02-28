#[
SyntaxError: Cannot infer type of 'a'.
Where: File "test/fail/lambda_argument_default.ly" at line 10
]#

define f[A](g: function(A => A)) {

}

f({|a| a})

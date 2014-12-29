###
SyntaxError: Function f, argument #3 is invalid:
Expected Type: list[A]
Received Type: list[list[integer]]
Where: File "test/fail/double_resolve_proof.ly" at line 28
###

function f[A, B](A one, B two, B three) {  }

function g[A](A one) {
    # Here's what's supposed to happen:
    # The first argument to f is processed as list[any].
    # f: A = list[any]
    # The second argument is list[A]
    # f: B = list[A]
    # The third argument is list[integer].

    # For the third argument, list build will attempt to resolve what B is and
    # find that B = list[A].
    # Correct:
    # [[1]], expect B.       B = list[list[A]]
    #  [1]   expect list[A]. However, the expected signature is resolved.

    # Incorrect:
    # [[1]], expect B.       B = list[list[A]]
    #  [1],  expect list[A]. A = any  (double resolve!)

    f([10.@(any)], [one], [[1]])
}

# This ensures that the interpreter can handle a lambda wherein the body is an
# expression that does not yield a value.

function noret() {}

function f[A](A value) {
}

f({|| noret()})

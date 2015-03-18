define f[A, B, C, D](value: A,
                     g: function(A => B),
                     h: function(A => C),
                     i: function(A => D)
                     => tuple[A, B, C, D]) {
    return <[value, g(value), h(value), i(value)]>
}

define a(value: integer => double) { return 1.0 }
define b(value: integer => string) { return "1" }
define c(value: integer => list[integer]) { return [1] }

f(1, a, b, c)

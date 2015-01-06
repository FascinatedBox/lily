define f[A, B, C, D](A value,
                     function g(A => B),
                     function h(A => C),
                     function i(A => D)
                     => tuple[A, B, C, D]) {
    return <[value, g(value), h(value), i(value)]>
}

define a(integer value => double) { return 1.0 }
define b(integer value => string) { return "1" }
define c(integer value => list[integer]) { return [1] }

f(1, a, b, c)

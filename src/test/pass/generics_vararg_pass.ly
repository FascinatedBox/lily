define f[A, B, C, D, E, F](values: list[F]...) {}
define k[A](v1: A, v2: A) { f(v1, v2) }
k(1, 2)

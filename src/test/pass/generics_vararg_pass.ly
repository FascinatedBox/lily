function f[A, B, C, D, E, F](list[F] values...) {}
function k[A](A v1, A v2) { f(v1, v2) }
k(1, 2)

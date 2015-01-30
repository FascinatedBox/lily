enum class Option[A] {
    Some(A),
    None
}

define f[A](values: list[Option[A]]...) {
	
}

f(None, None, None)
f(None, Some(10), None)
f(Some(10), Some(10))

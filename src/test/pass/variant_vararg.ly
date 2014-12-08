enum class Option[A] {
    Some(A),
    None
}

function f[A](list[Option[A]] values...) {
	
}

f(None, None, None)
f(None, Some(10), None)
f(Some(10), Some(10))

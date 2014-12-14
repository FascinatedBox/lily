enum class Option[A] {
	Some(A),
	None
}

tuple[Option[integer]] t = <[Some(10)]>
tuple[any] t2 = <[None]>
tuple[any] t3 = <[5]>

tuple[any] t4 = <[ [t, t2, t3] ]>

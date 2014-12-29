enum class Option[A] {
	::Some(A),
	::None
}

list[Option[integer]] opt = [Option::Some(10), Option::Some(12), Option::None]

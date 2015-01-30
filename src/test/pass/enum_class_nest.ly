enum class Option[A] {
	Some(A),
	None
}

# This test ensures that if a function says that bare variants cannot resolve
# a generic type until they're first put into an enum class with whatever info
# they have at the time.
var t: Option[Option[integer]] = Some(Some(10))

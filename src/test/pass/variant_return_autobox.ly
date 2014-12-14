enum class Option[A] {
	Some(A),
	None
}

function f( => Option[integer]) {
	return Some(10)
}

function g( => Option[integer]) {
	return None
}

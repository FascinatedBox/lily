enum class Option[A] {
	Some(A)
	None
}

var a: list[Option[any]] = [None, None, None]
var b: list[Option[integer]] = [Some(10), None, None]
var c: hash[integer, Option[integer]] = [1=>None, 2=>Some(10), 3=>Some(11)]

enum class Option[A] {
	Some(A)
	None
}

list[Option[any]] a = [None, None, None]
list[Option[integer]] b = [Some(10), None, None]
hash[integer, Option[integer]] c = [1=>None, 2=>Some(10), 3=>Some(11)]

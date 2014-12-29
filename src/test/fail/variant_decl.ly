###
SyntaxError: Variant types not allowed in a declaration.
Where: File "test/fail/variant_decl.ly" at line 11
###

enum class Option[A] {
	Some(A),
	None
}

Some[integer] = Some(10)

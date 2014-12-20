###
SyntaxError: An enum class must have at least two variants.
Where: File "test/fail/enum_too_few_variants.ly" at line 8
###

enum class Option[A] {
	Some(A)
}

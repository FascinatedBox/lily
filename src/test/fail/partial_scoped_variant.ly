#[
SyntaxError: Expected '::', not a label.
Where: File "test/fail/partial_scoped_variant.ly" at line 8
]#

enum class Option[A] {
	::Some(A),
	None
}

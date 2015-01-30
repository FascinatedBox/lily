#[
SyntaxError: Some has not been declared.
Where: File "test/fail/scoped_variant_direct.ly" at line 11
]#

enum class Option[A] {
	::Some(A),
	::None
}

var opt: Option[integer] = Some(10)

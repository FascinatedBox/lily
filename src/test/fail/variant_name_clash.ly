#[
SyntaxError: A class with the name 'Some' already exists.
Where: File "test/fail/variant_name_clash.ly" at line 8
]#

enum class Option[A] {
    Some(A),
    Some(A)
}

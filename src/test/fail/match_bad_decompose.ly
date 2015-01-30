#[
SyntaxError: Expected '(', not :.
Where: File "test/fail/match_bad_decompose.ly" at line 14
]#

enum class Option[A] {
    Some(A),
    None
}

var v = None

match v: {
    case Some:
    case None:
}

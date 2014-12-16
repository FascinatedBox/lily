###
SyntaxError: Expected ':', not (.
Where: File "test/fail/match_bad_decompose_attempt.ly" at line 14
###

enum class Option[A] {
    Some(A),
    None
}

var v = None

match v: {
    case None(a):
    case Some(s):
}

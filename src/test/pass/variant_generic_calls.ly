enum class Option[A] {
    Some(A),
    None
}

function f[A](Option[A] first, Option[A] second) {

}

f(None, None)
f(Some(10), Some(10))
f(Some("10"), None)
f(None, Some(10))

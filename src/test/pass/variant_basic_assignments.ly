enum class Option[A] {
    Some(A),
    None
}

Option[integer] i = Some(10)
i = None

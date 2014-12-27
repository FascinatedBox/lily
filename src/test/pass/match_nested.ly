enum class Option[A] {
    Some(A),
    None
}

Option[Option[integer]] opt = Some(Some(10))

match opt: {
    case Some(s):
        match s: {
            case Some(s2):
            case None:
                print("Failed.\n")
        }
    case None:
        print("Failed.\n")
}

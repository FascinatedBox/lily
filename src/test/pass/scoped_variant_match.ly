enum class Option[A] {
	::Some(A),
	::None
}

Option[integer] opt = Option::Some(10)

# Since the interpreter knows that 'opt' MUST be of type Option, it does not
# require Option::<x> for each case.
# This has an added benefit: Making an enum class scoped that wasn't scoped
# will not break match code that uses that enum class.

match opt: {
	case Some(s):
	case None:
		print("Failed.\n")
}

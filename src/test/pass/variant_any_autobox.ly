enum class Option[A] {
	Some(A),
	None
}

any a = Some(10)

if a != Some(10):
	print("Failed!\n")

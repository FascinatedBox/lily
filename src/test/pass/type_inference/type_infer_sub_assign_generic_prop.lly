class AA[A](value: A) {
	var @container = [[value]]
}

var test: list[AA[any]] = [AA::new(AA::new(10))]

# This should be allowed to proceed since 'container' will be list[list[any]]
# The subscript means that the left type expects list[any]
# This information should go to the right side, and cause the 11 to be autocast
# to type any.
test[0].container[0] = [11]

var test = [1, 2, 3]
test.append(4)

define toapply(a: integer => integer) { return a * 2 }

test.apply(toapply)

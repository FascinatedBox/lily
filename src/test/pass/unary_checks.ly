function unary_helper( => integer)
{
    return 10
}

function unary_call_checker(integer a => integer)
{
    return 10
}

integer a = 0, b = 0, c = 0

a = unary_call_checker(!!0 + !!0)
b = -10 + --10 + -a + --a + unary_helper() + -unary_helper()
b = b + !!0

list[integer] lsi = [10]
c = !lsi[0]

if (b == 0 && c == 0) == 0:
    print("Failed.\n")

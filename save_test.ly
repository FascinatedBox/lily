<html>
<head>
<title>Lily Save Test</title>
</head>
<body>
<@lily

integer a = 1

method test_callee():integer
{
    return 1
}

method m():str
{
    return "str"
}

method fib(integer n):integer
{
    integer ret
    if n == 0: {
        ret = 0
    elif n == 1:
        ret = 1
    else:
        integer fibn1 = fib(n-1)
        integer fibn2 = fib(n-2)
        ret = fibn1 + fibn2
    }

    return ret
}

method test_caller():integer
{
    integer asdf

    m()
    integer b = 10
    printfmt("test %i, %s.\n", 1+1, m())
    test_callee()
    return 1
}

method test_fib():integer
{
    integer fib0 = fib(0)
    integer fib1 = fib(1)
    integer fib2 = fib(2)
    integer fib3 = fib(3)
    integer fib4 = fib(4)
    integer fib5 = fib(5)
    integer fib6 = fib(6)
    integer fib7 = fib(7)
    integer fib8 = fib(8)
    integer fib9 = fib(9)

    printfmt("fib 0..9 is %i, %i, %i, %i, %i, %i, %i, %i, %i, %i.\n", fib0, 
             fib1, fib2, fib3, fib4, fib5, fib6, fib7, fib8, fib9)
    print("Should be   0, 1, 1, 2, 3, 5, 8, 13, 21, 34.\n")
    return 1
}

test_caller()
test_fib()
@>
</body>
</html>

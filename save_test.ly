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

method test_caller():integer
{
    integer asdf

    m()
    integer b = 10
    printfmt("test %i, %s.\n", 1+1, m())
    test_callee()
    return 1
}

test_caller()

@>
</body>
</html>

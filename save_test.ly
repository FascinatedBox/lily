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

method test_caller():integer
{
    integer b
    test_callee()
    return 1
}

test_caller()

@>
</body>
</html>

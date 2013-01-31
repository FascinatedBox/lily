<html>
<head>
<title>Lily Hello world</title>
</head>
<body>
<@lily

method m(method m2(integer, integer):nil, integer a, integer b):nil
{
    m2(a, b)
}

method m3(integer c, integer d):nil
{
    print("Method called as argument!\n")
}

m(m3, 10, 11)

print("Hello, world.")@>
</body>
</html>

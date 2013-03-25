<html>
<head>
<title>Lily Hello world</title>
</head>
<body>
<@lily
integer a
number b
str c
object d
list[integer] e
list[list[integer]] f, g
list[list[list[list[list[list[number]]]]]] h

method i():nil
{
    return
}

list[method ():nil] j

method k(integer L, integer m, integer n, integer o, integer p):integer
{
}

method q(integer r):nil
{

}

method r(integer s):method(integer):nil
{
    
}

method test_mval(integer mval_int):integer
{
    return 10
}

method test_mval_2(integer mval_int):integer
{
    return 20
}

method test_mval_3(integer mval_int):integer
{
    return 30
}

list[integer] y
list[str] y2
list[method (integer):integer] y3

y = [1, 2, 3, 4, 5, 6, 7, 8, 9]
y2 = ["a", "b", "c", "d"]
y3 = [test_mval, test_mval_2, test_mval_3]

@>
</body>
</html>

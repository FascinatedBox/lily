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
list[list[str]] y4
str y5
y = [1, 2, 3, 4, 5, 6, 7, 8, 9]
y2 = ["a", "b", "c", "d"]
y3 = [test_mval, test_mval_2, test_mval_3]
y4 = [["a"], ["b"]]
y5 = y2[0]
y5 = y2[0]
y2 = y4[0]
y5 = y4[1][0]
# What does this do, you may ask?
# [[0,1,2][1]]
# Create a list of 0,1,2
# Pick element 1 of it.
# Create a list (L) based off of that element.
# [[0,1,2][0]]
# Create a list of 0,1,2
# Pick element 0 of it.
# Use that as a subscript off of list (L).
integer test_i = [[1,2,3][1]] [[0,1,2][0]]

@>
</body>
</html>

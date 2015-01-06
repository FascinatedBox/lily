define fib(integer n => integer)
{
    if n == 0:
        return 0
    elif n == 1:
        return 1
    else:
        return fib(n - 1) + fib(n - 2)
}

list[integer] values = []

for i in 0...9: {
    values.append(fib(i))
}

if values != [0, 1, 1, 2, 3, 5, 8, 13, 21, 34]:
    print("Failed.\n")

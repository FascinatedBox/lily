<html>
<head>
<title>Lily Hello world</title>
</head>
<body>
<@lily
integer which_test = 1
# Test complex methods
method test_method_arg(method method_arg(integer, integer):nil,
                       integer a, integer b):nil
{
    method_arg(a, b)
}

method method_for_arg(integer c, integer d):nil
{
    c = d
}

print("\n\nRunning test_complex.ly tests...\n")
printfmt("#%i: Testing named method as argument...\n", which_test)
which_test = which_test+1
test_method_arg(method_for_arg, 10, 11)


# Test lists
list[integer] x
list[list[integer]]y

printfmt("#%i: Testing list[integer] static assignment...\n", which_test)
which_test = which_test+1
x = [1, 2, 3, 4, 5, 6]

printfmt("#%i: Testing list[list[integer]] static assignment...\n", which_test)
which_test = which_test+1
y = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]

print("Done.")
@>
</body>
</html>

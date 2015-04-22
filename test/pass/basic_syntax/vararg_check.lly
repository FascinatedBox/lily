# function varargs must always be a list. Any extra args are packed into a
# list for functions (instead of flattened like for functions).
# The type that must be passed is the type of the list. So this first function
# will take any extra integers and pack them into a list.
define va_1(abc: list[integer]...) {    }

# Since this one takes anys, any extra args also get converted to anys as
# needed. The list part of that isn't tested yet, because there is no list
# comparison utility.
define va_2(format: string, args: list[any]... => integer) {
    var ok = 0
    # This next statement helped to uncover about 3 bugs. Leave it be.
    if args[0].@(integer) == 1 &&
       args[1].@(double) == 1.1 &&
       args[2].@(string) == "1":
        ok = 1

    return 1
}
define va_3(abc: integer, args: list[list[integer]]...) {    }

var a: any = va_2
# Check it with a typecast too.
var vx: integer = a.@(function(string, list[any] ... => integer))("abc", 1, 1.1, "1")

var lmtd: list[function(string, list[any] ... => integer)] = [va_2, va_2, va_2]

va_1(1,2,3,4,5)
va_2("abc", 1, 1.1, "1", [1])
va_3(1, [1], [2], [3])

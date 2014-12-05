# function varargs must always be a list. Any extra args are packed into a
# list for functions (instead of flattened like for functions).
# The type that must be passed is the type of the list. So this first function
# will take any extra integers and pack them into a list.
function va_1(list[integer] abc...) {    }

# Since this one takes anys, any extra args also get converted to anys as
# needed. The list part of that isn't tested yet, because there is no list
# comparison utility.
function va_2(string format, list[any] args... => integer) {
    integer ok = 0
    # This next statement helped to uncover about 3 bugs. Leave it be.
    if args[0].@(integer) == 1 &&
       args[1].@(double) == 1.1 &&
       args[2].@(string) == "1":
        ok = 1

    return 1
}
function va_3(integer abc, list[list[integer]] args...) {    }

any a = va_2
# Check it with a typecast too.
integer vx = a.@(function(string, list[any] ... => integer))("abc", 1, 1.1, "1")

list[function(string, list[any] ... => integer)] lmtd = [va_2, va_2, va_2]

va_1(1,2,3,4,5)
va_2("abc", 1, 1.1, "1", [1])
va_3(1, [1], [2], [3])

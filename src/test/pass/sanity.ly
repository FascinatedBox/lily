<html>
<head>
<title>Lily Sanity Test</title>
</head>
<body>
<@lily
# This is Lily's main sanity test. This ensures that the interpreter is working
# correctly. New features should add to the tests here, to help prevent
# accidental regressions.

integer test_id = 1
integer fail_count = 0

function test_basic_assignments()
{
    # Begin by testing that basic assignments for common types work.

    # integers are 64-bit signed values.
    integer a = 10

    # Doubles are C doubles, a more precise type than integer.
    double b = 1.1

    # string is Lily's string class.
    string c = "11"

    # Lists are collections of values, and need a type specified.
    # Additionally, static lists, such as [1, 2, 3] automatically guess their
    # resulting type based on what they contain.
    list[integer] d = [1, 2, 3]
    # Lists can also include other lists.
    # This is a static list containing only lists of integers, so it the
    # interpreter sees that it's valid.
    list[list[integer]] e = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
    e[0] = [4, 5, 6]

    # hash is the associative array type. Hashes require two inner type: the
    # key, and the value.
    hash[string, integer] f = ["a" => 1, "b" => 2, "c" => 3]
    # Items can be added through subscript assignment. Lists can't do that
    # though.
    f["d"] = 4

    # Function is a callable block of Lily code. These are different than
    # functions, which are builtin callable blocks.

    # Function can be declared in other functions, but upvalues aren't supported
    # quite yet. The part after the : defines the return value of the function.
    # nil is a special word that is accepted that means the function does not
    # return a value.
    function g() {
        integer g_1 = 10
    }

    # Finally, anys. Anys are containers that can hold any value.
    any h = 10
    h = "1"
    h = 1.1
    h = [1, 2, 3]
    h = g
    h = ["a" => 1, "b" => 2]

    # Now, for something more interesting...

    function ret_10( => integer) { return 10 }
    function ret_20( => integer) { return 20 }
    function ret_30( => integer) { return 30 }

    list[function( => integer)] function_list = [ret_10, ret_20, ret_30]

    # Subscript out the function, then call it. Returns 10.
    integer function_list_ret = function_list[0]()

    # Shuffle the function list around a bit.
    function_list[2] = function_list[0]
    function_list[0] = function_list[1]

    hash[integer, list[function( => integer)]] super_hash = [1 => function_list]

    # printfmt is a function that works like C's printf. It takes in a
    # variable amount of anys as values.
    # Since anything can be an any, this converts test_id to any before
    # doing the call.
    # %1 is for integers.
    printfmt("#%i: Testing basic assignments...ok.\n", test_id)
    test_id = test_id + 1
}

function test_conditions()
{
    printfmt("#%i: Testing conditions...", test_id)
    test_id = test_id + 1

    integer ok = 0
    integer a = 10

    # Lily has two kind of if statements: single-line, and multi-line.
    # A single-line statement only allows one line before needing either a
    # close or an elif/else.

    if a == 10:
        a = 11
    elif a == 11:
        ok = 0
    else:
        ok = 0

    # Multi-line ifs begin by putting a { after the : of the if.
    # Unlike most curly brace languages, the } is only required
    # at the end of the if.
    if a == 11: {
        # Multi-line ifs can contain single-line ifs.
        a = 12
        # This is a single-line if. It closes after 'ok = 0'
        if a == 12:
            a = 13
        else:
            ok = 0
        ok = 1
    # Lily deduces that this else is multi-line, because it hasn't found
    # a } for the {.
    else:
        # Lily deduces that this else is multi-line
        ok = 0
        ok = 0
        ok = 0
    }

    # Now, if with some different things used for the condition.
    if 1.1 != 1.1:
        ok = 0

    if "1" != "1":
        ok = 0

    if ok == 1: {
        print("ok.\n")
    else:
        print("failed.\n")
        fail_count += 1
    }
}

function test_printfmt()
{
    integer a = 1
    double b = 12.34
    string c = "abcd"

    printfmt("#%i: Testing printfmt:...(check results)\n", test_id)
    test_id = test_id + 1

    printfmt("    integer a (1)     is %i.\n", a)
    printfmt("    double  b (12.34) is %d.\n", b)
    printfmt("    string  c (abcd)  is %s.\n", c)
    # Make sure varargs calls are taking the extra ones too.
    printfmt("    a, b, c are %i, %n, %s.\n", a, b, c)
}

function test_any_defaulting()
{
    printfmt("#%i: Testing any defaulting...", test_id)
    test_id = test_id + 1

    function m() {}

    any other_any = 10
    # Any can accept any value
    any a
    a = 1
    a = 1.1
    a = "10"
    a = [1]
    a = ["1" => 10]
    a = m
    a = printfmt
    a = other_any

    # If a static list contains various types, they should default to any.
    list[any] any_list = [1, 1.1, "10", [1], ["1" => 10], m, printfmt,
                             other_any]

    # Ensure that subscript assign for lists autoconverts to any.
    any_list[0] = 1
    any_list[0] = 1.1
    any_list[0] = "10"
    any_list[0] = [1]
    any_list[0] = ["1" => 10]
    any_list[0] = m
    any_list[0] = printfmt
    any_list[0] = other_any

    hash[string, any] any_hash = ["integer" => 1,
                                  "double" => 1.1,
                                  "str" => "10",
                                  "list" => [1],
                                  "hash" => ["1" => 10],
                                  "function" => printfmt,
                                  "any" => other_any,
                                  "test" => 1]

    any_hash["test"] = 1
    any_hash["test"] = 1.1
    any_hash["test"] = "10"
    any_hash["test"] = [1]
    any_hash["test"] = ["1" => 10]
    any_hash["test"] = printfmt
    any_hash["test"] = other_any

    function test(any x) {}

    test(1)
    test(1.1)
    test("10")
    test([1])
    test(["1" => 10])
    test(m)
    test(printfmt)
    test(other_any)

    print("ok.\n")
}

function test_oo()
{
    string abc = "abc", rlt
    integer i, ok
    any abc_any = "abc"
    list[string] abc_list = ["abc"]

    printfmt("#%i: Testing oo calls...(sub tests follow).\n", test_id)
    test_id = test_id + 1
    ok = 1

    rlt = abc.concat("def")
    print("    \"abc\".concat(\"def\")             == \"abcdef\"...")
    if rlt == "abcdef": {
        print("ok.\n")
    else:
        ok = 0
        print("failed.\n")
    }

    rlt = (((abc))).concat("def")
    print("    (((\"abc\"))).concat(\"def\")       == \"abcdef\"...")
    if rlt == "abcdef": {
        print("ok.\n")
    else:
        ok = 0
        print("failed.\n")
    }

    rlt = abc_any.@(string).concat("def")
    print("    abc_any.@(string).concat(\"def\")   == \"abcdef\"...")
    if rlt == "abcdef": {
        print("ok.\n")
    else:
        ok = 0
        print("failed.\n")
    }

    rlt = ["abc", "def"][0].concat("def")
    print("    [\"abc\", \"def\"][0].concat(\"def\") == \"abcdef\"...")
    if rlt == "abcdef": {
        print("ok.\n")
    else:
        ok = 0
        print("failed.\n")
    }

    function return_abc( => string) { return "abc" }

    rlt = return_abc().concat("def")
    print("    return_abc().concat(\"def\")      == \"abcdef\"...")
    if rlt == "abcdef": {
        print("ok.\n")
    else:
        ok = 0
        print("failed.\n")
    }

    # Make sure this works with binary ops.
    i = abc.concat("def") == "abcdef"
    print("    abc.concat(\"def\")               == \"abcdef\"...")
    if i: {
        print("ok.\n")
    else:
        ok = 0
        print("failed.\n")
    }

    if ok == 0:
        fail_count = fail_count + 1
}

function test_utf8()
{
    string h3llö = "hello"
    string ustr = "á"

    printfmt("#%i: Testing concat with utf8...", test_id)
    test_id  = test_id + 1

    if ustr.concat("á") == "áá": {
        print("ok.\n")
    else:
        print("failed.\n")
        fail_count = fail_count + 1
    }
}

function test_escapes()
{
    string s = "\\a\\b\\\\c\\d\\"
    string s2 = "Hello, world.\n"
    string s3 = ""

    printfmt("#%i: Testing escape string...%s.\n", test_id, s)
    test_id = test_id + 1
}

function fib(integer n => integer)
{
    if n == 0:
        return 0
    elif n == 1:
        return 1
    else:
        return fib(n - 1) + fib(n - 2)
}

function test_fib()
{
    printfmt("#%i: Testing fibonacci...(check results)\n", test_id)
    test_id = test_id + 1

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

    printfmt("     fib 0..9 is %i, %i, %i, %i, %i, %i, %i, %i, %i, %i.\n",
             fib0, fib1, fib2, fib3, fib4, fib5, fib6, fib7, fib8, fib9)
    print("     Should be   0, 1, 1, 2, 3, 5, 8, 13, 21, 34.\n")
}

function unary_helper( => integer)
{
    return 10
}

function unary_call_checker(integer a => integer)
{
    return 10
}

function test_unary()
{
    printfmt("#%i: Testing unary ops...", test_id)
    test_id = test_id + 1

    integer a, b, c

    a = unary_call_checker(!!0 + !!0)
    b = -10 + --10 + -a + --a + unary_helper() + -unary_helper()
    b = b + !!0

    list[integer] lsi = [10]
    c = !lsi[0]

    if b == 0 && c == 0: {
        print("ok.\n")
    else:
        print("failed.\n")
        fail_count = fail_count + 1
    }
}

function test_andor()
{
    printfmt("#%i: Testing and/or conditions...(sub tests follow).\n", test_id)
    test_id = test_id + 1
    integer ok = 1

    if 1 || 1: {
        print("     1 || 1 == 1 ok.\n")
    else:
        print("     1 || 1 == 0 failed.\n")
        ok = 0
    }

    if 0 || 1: {
        print("     0 || 1 == 1 ok.\n")
    else:
        print("     0 || 1 == 0 failed.\n")
        ok = 0
    }

    if 0 || 0: {
        print("     0 || 0 == 1 failed.\n")
        ok = 0
    else:
        print("     0 || 0 == 0 ok.\n")
    }

    if 0 || 0 || 0 || 0: {
        print("     0 || 0 || 0 || 0 == 1 failed.\n")
        ok = 0
    else:
        print("     0 || 0 || 0 || 0 == 0 ok.\n")
    }

    if 1 || 1 || 1 || 1: {
        print("     1 || 1 || 1 || 1 == 1 ok.\n")
    else:
        print("     1 || 1 || 1 || 1 == 0 failed.\n")
        ok = 0
    }


    if 1 && 1: {
        print("     1 && 1 == 1 ok.\n")
    else:
        print("     1 && 1 == 0 failed.\n")
        ok = 0
    }

    if 0 && 1: {
        print("     0 && 1 == 1 failed.\n")
        ok = 0
    else:
        print("     0 && 1 == 0 ok.\n")
    }

    if 0 && 0: {
        print("     0 && 0 == 1 failed.\n")
        ok = 0
    else:
        print("     0 && 0 == 0 ok.\n")
    }

    if 0 && 0 && 0 && 0: {
        print("     0 && 0 && 0 && 0 == 1 failed.\n")
        ok = 0
    else:
        print("     0 && 0 && 0 && 0 == 0 ok.\n")
    }

    if 1 && 1 && 1 && 1: {
        print("     1 && 1 && 1 && 1 == 1 ok.\n")
    else:
        print("     1 && 1 && 1 && 1 == 0 failed.\n")
        ok = 0
    }

    function return_1( => integer) {
        return 1
    }

    if 0 + 1 || return_1(): {
        print("     0 + 1 || return_1() == 1 ok.\n")
    else:
        print("     0 + 1 || return_1() == 0 failed.\n")
        ok = 0
    }

    if 0 + 1 && return_1(): {
        print("     0 + 1 && return_1() == 1 ok.\n")
    else:
        print("     0 + 1 && return_1() == 0 failed.\n")
        ok = 0
    }

    if ok == 0:
        fail_count = fail_count + 1
}

function test_parenth()
{
    printfmt("#%i: Testing parenth expressions...", test_id)
    test_id = test_id + 1

    integer a = 1
    integer ok = 1
    string s = "a"

    if (((a))) != 1:
        ok = 0

    if (a != 1):
        ok = 0

    if (s).concat("a") != "aa":
        ok = 0

    if (s.concat("a")).concat("a") != "aaa":
        ok = 0

    if ok == 0: {
        fail_count = fail_count + 1
        print("failed.\n")
    else:
        print("ok.\n")
    }
}

function test_arith()
{
    printfmt("#%i: Testing +, +=, -, -=, *, *=, /, /=, etc...", test_id)
    test_id = test_id + 1

    integer i = 2
    i *= i
    i = i * i
    i /= i
    i = i / i
    i += i
    i = i + i

    i -= i
    i = i - 1

    i = 1 * 1
    i = 2 / 2

    i = 1 << 1 >> 1
    i <<= 1
    i >>= 1
    i <<= 1

    double n = 1.0
    n *= n
    n = n * n
    n /= n
    n = n / n
    n += n
    n = n + n
    n -= n
    n = n - n
    n = 2.0 - 2
    n = 2.0 + 2
    n = 2.0 / 2
    n = 2.0 * 2
    n = 2 * 2.0
    n = 2.0 * 2

    print("ok.\n")
}

function test_sub_assign()
{
    printfmt("#%i: Testing subs assign...(sub tests follow).\n", test_id)
    test_id = test_id + 1
    integer ok = 1

    list[integer] list_integer = [1]
    list_integer[0] = 2

    if list_integer[0] == 2: {
        print("     list[integer] ok.\n")
    else:
        print("     list[integer] failed.\n")
        ok = 0
    }

    list[double] list_double = [1.0]
    list_double[0] = 2.0

    if list_double[0] == 2.0: {
        print("     list[double] ok.\n")
    else:
        print("     list[double] failed.\n")
        ok = 0
    }

    list[string] list_string = ["1"]
    list_string[0] = "2"

    if list_string[0] == "2": {
        print("     list[string] ok.\n")
    else:
        print("     list[string] failed.\n")
        ok = 0
    }

    list[list[string]] list_list_string = [["1"]]
    list_list_string[0][0] = "2"

    function ret_10( => integer) { return 10 }
    function ret_20( => integer) { return 20 }
    function ret_30( => integer) { return 30 }

    list[any] lso = [10, 1.1]
    lso[0] = 10
    lso[0] = 1.1
    lso[0] = "11"
    lso[0] = lso
    lso[0] = lso.@(any)
    lso[0] = [lso.@(any), lso.@(any), lso.@(any)]
    lso[0] = lso[1]
    lso[0] = print
    lso[0] = list_list_string

    print("     list[any] ok.\n")

    print("     list[function] ")

    list[function( => integer)] list_function = [ret_10, ret_20, ret_30]
    list_function[0] = ret_30
    integer call_ret = list_function[0]()
    if call_ret == 30: {
        print("ok.\n")
    else:
        ok = 0
        print("failed.\n")
    }

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

    if test_i == 2: {
        print("     [[1,2,3][1]] [[0,1,2][0]] is 2. ok.\n")
    else:
        ok = 0
        print("     [[1,2,3][1]] [[0,1,2][0]] is not 2. failed.\n")
    }

    # Test declaration list
    list[list[integer]] dlist1, dlist2, dlist3
    # Test a deep list
    list[list[list[list[list[list[double]]]]]] deep_list
    # Check that list[string] doesn't crash if the string is nil.
    string nil_s
    list[string] test_nil_s = [nil_s]
    test_nil_s[0] = "test"

    if ok == 0:
        fail_count = fail_count + 1
}

# Sigs is short for signatures. A complex signature is a signature that contains
# more signature information inside of it. Lists and functions are good examples
# of this. This tests that complex signatures are not leaked.
function test_complex_sigs()
{
    printfmt("#%i: Testing complex sigs...", test_id)
    test_id = test_id + 1
    integer ok = 1

    function lv1_function(  integer arg  ){}
    function lv2_function(  function lv1(  integer  )  ) {}
    function lv3_function(  function lv2(  function(integer)  )  ) {}

    # These won't do anything. This is more to test that parser is doing what
    # it should when passing args.
    lv1_function(ok)
    lv2_function(lv1_function)
    lv3_function(lv2_function)

    list[function(integer)] list_function_n1

    list[
        function(
            function(integer)
        )
    ] list_function_n2

    list[
        function( 
            list[
                function(integer)
            ]
            => list[integer]
        )
    ] list_function_n3

    function mval_1( => integer) { return 10 }
    function mval_2( => integer) { return 20 }
    function mval_3( => integer) { return 30 }
    list[
        function( => integer)
    ] list_function_n4 = [mval_1, mval_2, mval_3]

    function mval_4( => list[integer]) { return [10] }
    function mval_5( => any) { return [10] }

    print("ok.\n")
}

function test_typecasts()
{
    printfmt("#%i: Testing typecasts from any...", test_id)
    test_id = test_id + 1

    any a
    integer intval
    a = 10
    intval = a.@(integer)

    double dblval
    a = 10.0
    dblval = a.@(double)

    string strval
    a = "10"
    strval = a.@(string)

    list[integer] list_intval
    a = [1]
    list_intval = a.@(list[integer])

    list[any] list_anyval
    any aval_1 = 10
    any aval_2 = 1.1
    list_anyval = [aval_1, aval_2]

    function mval_10( => integer) { return 10 }
    function mval_20( => integer) { return 20 }
    function mval_30( => integer) { return 30 }
    function ret_any( => any) { any a2 = 10 return a2 }

    list[function( => integer)] list_mval = [mval_10]
    a = mval_20
    list_mval[0] = a.@(function( => integer))

    intval = list_mval[0]()
    intval = a.@(function( => integer))() +
             a.@(function( => integer))() +
             a.@(function( => integer))() +
             a.@(function( => integer))()

    intval = a.@(function( => integer))()

    a = list_intval
    intval = a.@(list[integer])[0]
    intval = ret_any().@(integer)
    intval = list_anyval[0].@(integer)

    print("ok.\n")
}

function test_circular_ref_checks()
{
    printfmt("#%i: Testing circular reference checks...", test_id)
    test_id = test_id + 1

    # This used to be really important because circular references were tracked
    # by fiddling with refcounts, marking lists, and crossing fingers.
    # It was horrible.
    # Lily has a gc now, so this is important but not as fragile as it used to
    # be. Hurray!

    list[any] a = [1.1, 2]
    a[0] = a
    a[1] = a
    a[1] = 1

    any b = a[0]
    any c = a[1]

    list[any] d = [1, 1]
    list[any] e = [1, 1]
    d[0] = e
    e[0] = d

    any f = c
    any g = d[0]
    any h = g.@(list[any])[0]

    list[list[any]] i = [a, a, a, a]
    i[0][0] = i[0]
    i[0] = i[0]

    list[any] j = ["1", "1"]
    list[any] k = [1.1, 1.1]
    k[0] = j
    j[0] = k
    j[1] = k
    k[1] = j
    j = [1, 1.1]
    k = [1, 1.1]

    any l = 10
    list[list[any]] m = [[l]]
    m[0][0] = m

    any n = [10]
    any o = [n]
    any p = [o, o, o, o]
    n = p
    p = 1

    list[list[any]] q = [[1, 1.1]]
    list[any] r = [1, 1.1, 1.1, 1.1]

    r[0] = q
    r[1] = [q, q, q, q, [[q]], 1.1, 5]
    q[0] = r

    list[any] s = [1, 2.1, 3]
    any t = s
    s[0] = t

    list[any] u = [1, 2.2]
    list[list[any]] v = [[1, 2.2]]
    u[0] = [v, v, v]
    v[0] = u
    u = [1, 1.1]
    v = [[1, 1.1]]

    any w
    list[any] x = [w, w, w]
    x[0] = x

    # Test circular refs with nil.
    any y
    list[any] listany = [y]
    y = listany[0]

    print("ok.\n")
}

function test_function_varargs()
{
    printfmt("#%i: Testing function varargs...", test_id)
    test_id = test_id + 1

    # function varargs must always be a list. Any extra args are packed into a
    # list for functions (instead of flattened like for functions).
    # The type that must be passed is the type of the list. So this first function
    # will take any extra integers and pack them into a list.
    function va_1(list[integer] abc...) {    }

    # Since this one takes anys, any extra args also get converted to anys as
    # needed. The list part of that isn't tested yet, because there is no list
    # comparison utility.
    function va_2(string format, list[any] args... => integer) {
        integer ok
        # This next statement helped to uncover about 3 bugs. Leave it be.
        if args[0].@(integer) == 1 &&
           args[1].@(double) == 1.1 &&
           args[2].@(string) == "1":
            ok = 1
        else:
            ok = 0

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

    print("ok.\n")
}

function test_multiline_strs()
{
    printfmt("#%i: Testing multiline strings...(sub tests follow).", test_id)
    test_id = test_id + 1
    integer ok = 1

    # Ensure that the text is being collected the same in multi-line as it is
    # for single-line.

    # Empty strs
    print("\n     \"\" == \"\"\"\"\"\"...")
    if "" == """""": {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    # Single-line, no escape
    print("     \"abc\" == \"\"\"abc\"\"\"...")
    if "abc" == """abc""": {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    # Single-line, start with escape
    print("     \"\\aabc\" == \"\"\"\\aabc\"\"\"...")
    if "\aabc" == """\aabc""": {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    # Single-line, escape in middle
    print("     \"abc\\adef\" == \"\"\"abc\\adef\"\"\"...")
    if "abc\adef" == """abc\adef""": {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    # Single-line, escape at end
    print("     \"abcdef\\a\" == \"\"\"abcdef\\a\"\"\"...")
    if "abcdef\a" == """abcdef\a""": {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    # Single-line, escape at start+mid
    print("     \"\\aabc\\adef\" == \"\"\"\\aabc\\adef\"\"\"...")
    if "\aabc\adef" == """\aabc\adef""": {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    # Single-line, escape at start+end
    print("     \"\\aabcdef\\a\" == \"\"\"\\aabcdef\\a\"\"\"...")
    if "\aabcdef\a" == """\aabcdef\a""": {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    # Single-line, escape at mid+end
    print("     \"abc\\adef\\a\" == \"\"\"abc\\adef\\a\"\"\"...")
    if "abc\adef\a" == """abc\adef\a""": {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    # Finish off with some multiline strings with escapes in interesting areas:
    string s1 = """abc\n
"""
    string s2 = """abc\n
\n"""
    string s3 = """\aabc\a
\aabc\a"""

    if ok == 0:
        fail_count = fail_count + 1
}

function test_digit_collection()
{
    printfmt("#%i: Testing digit collection...(sub tests follow).\n", test_id)
    test_id = test_id + 1
    integer ok = 1

    # Integers are 64-bit signed integers. Maximum values are:
    integer binary_max, binary_min
    binary_max = +0b111111111111111111111111111111111111111111111111111111111111111
    binary_min = -0b1000000000000000000000000000000000000000000000000000000000000000
    integer octal_max   = +0c777777777777777777777
    integer octal_min   = -0c1000000000000000000000
    integer decimal_max = +9223372036854775807
    integer decimal_min = -9223372036854775808
    integer hex_max     = +0x7fffffffffffffff
    integer hex_min     = -0x8000000000000000

    # Binary tests

    print("     0b0110 == 6...")
    if 0b0110 == 6: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     0b000110 == 6...")
    if 0b000110 == 6: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     binary_max == decimal_max...")
    if binary_max == decimal_max: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     binary_min == decimal_min...")
    if binary_min == decimal_min: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    # Octal tests

    print("     0c1234567 == 342391...")
    if 0c1234567 == 342391: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     octal_max == decimal_max...")
    if octal_max == decimal_max: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     octal_min == decimal_min...")
    if octal_min == decimal_min: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    # Hex tests

    print("     0x1234567890 == 78187493520...")
    if 0x1234567890 == 78187493520: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     0xabcdef == 11259375...")
    if 0xabcdef == 11259375: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     0xABCDEF == 11259375...")
    if 0xABCDEF == 11259375: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     hex_max == decimal_max...")
    if hex_max == decimal_max: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     hex_min == decimal_min...")
    if hex_min == decimal_min: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    # Finally, some integer exponent tests.
    print("     1e-1 == 0.1...")
    if 1e-1 == 0.1: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     1e+1 == 10...")
    if 1e+1 == 10: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     1e1 == 10...")
    if 1e1 == 10: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     .1 == 0.1...")
    if .1 == 0.1: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }
}

function test_loops()
{
    printfmt("#%i: Testing loops (while, for in, break, continue)...\n", test_id)
    test_id = test_id + 1

    integer i, ok
    i = 10
    ok = 1

    while i != 0: {
        i = i - 1
    }

    list[integer] lsi = [0, 1, 2, 3]

    i = 0

    while i < 3: {
        i = i + 1
        if lsi[i] == 2:
            continue
        elif lsi[i] == 3:
            break
    }

    i = 0

    while i < 3: {
        break
    }

    while i < 3: {
        i = i + 1
        continue
    }

    print("     While loops ok.\n")

    for i in 1..10: { }
    print("     Checking that 1 .. 10 ends at 10...")
    if i == 10: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    for i in 10..1: { }
    print("     Checking that 10 .. 1 ends at 1...")
    if i == 1: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    print("     Checking that 1 .. 5 by 2 == 1, 3, 5...")
    list[integer] intlist = [0, 0, 0, 0, 0, 0]
    for i in 1..5 by 2: {
        intlist[i] = 1
    }

    if intlist[1] == 1 && intlist[3] == 1 && intlist[5] == 1: {
        print("ok.\n")
    else:
        print("failed.\n")
        ok = 0
    }

    if ok == 0:
        fail_count += 1
}

function test_assign_chain()
{
    printfmt("#%i: Testing assignment chains...(sub tests follow).\n", test_id)
    test_id = test_id + 1
    integer ok = 1
    integer a = 10, b = 20, c = 30
    string result

    a = b = c

    print("     a = 10, b = 20, c = 30\n")
    print("     a = b = c...")
    if a != 30 || b != 30 || c != 30: {
        print("failed.\n")
        ok = 0
    else:
        print("ok (30, 30, 30).\n")
    }

    a = 10
    b = 20
    c = 30

    a *= b *= c
    print("\n     a = 10, b = 20, c = 30\n")
    print("     a *= b *= c...")
    if a != 6000 || b != 600 || c != 30: {
        print("failed.\n")
        ok = 0
    else:
        print("ok (6000, 600, 30).\n")
    }
    if ok == 0:
        fail_count = fail_count + 1

    a = 1000
    b = 100
    c = 10
    a /= b /= c
    print("\n     a = 1000, b = 100, c = 10\n")
    print("     a /= b /= c...")
    if a != 100 || b != 10 || c != 10: {
        print("failed.\n")
        ok = 0
    else:
        print("ok (100, 10, 10).\n")
    }

    a = 10
    list[integer] d = [20]
    a += d[0] += a

    print("\n     a = 10, d[0] = 20\n")
    print("     a += d[0] += a...")
    if a != 40 || d[0] != 30: {
        print("failed.\n")
        ok = 0
    else:
        print("ok (40, 30, 40).\n")
    }

    if ok == 0:
        fail_count = fail_count + 1
}

function test_multiline_comment()
{
    # Comments now work as follows:
    # If there is a #, but not three, it's single-line:
    # This is a single-line comment
    ## This is too.
    # Test that ### start checking doesn't go past end of line:
    #
    ##
    # If it's three #'s, it's a multi-line comment that ends on the next
    # three #'s.
    ### Multi-line comment. This is for consistency with multi-line strings
        which start with """ and end with """ ###
    # Single-line comments take precedence over multi-line since they skip the
    # current line they are working on.
    # ### (Single-line, valid)
    # ##### (Single-line, valid)
    # Multi-line comments can be empty:
    ######
    # Once inside a multi-line comment, it swallows #'s:
    # Test that ### checking doesn't go past end of line:
    ### Test 1:#
    ###
    ### Test 2:##
    ###
    # Single-line comments can come after multi-line ones:
    ### test #### integer ?

    integer######v######=######10

# Make sure it works when the sign is the first char of the line too.

###

###
    printfmt("#%i: Testing multi-line comments...ok.\n", test_id)
    test_id = test_id + 1
}

function test_intnum_cast()
{
    printfmt("#%i: Testing integer to double casting...", test_id)
    test_id = test_id + 1

    integer int1, int2
    double num1, num2

    ###
    This works against anys as well so that a user doesn't have to cast the
    cast away from any.

    Without:
        double n = int_any.@(integer).@(double)
    With:
        double n = int_any.@(double)
    ###

    any int_any = -5
    any num_any = 10.5

    int1 = 10.5 .@(integer)
    num1 = -5 .@(double)

    int2 = num_any.@(integer)
    num2 = int_any.@(double)

    if int1 == 10 && int2 == 10 && num1 == -5.0 && num2 == -5.0: {
        print("ok.\n")
    else:
        print("failed.\n")
        fail_count = fail_count + 1
    }
}

function test_hashes()
{
    printfmt("#%i: Testing hashes...(sub tests follow).\n", test_id)
    test_id = test_id + 1
    integer ok = 1

    # First, that hashes create values on-demand.
    hash[string, string] config_dict
    config_dict["aaa"] = "bbb"
    config_dict["bbb"] = "ccc"

    # Second, that hashes can use different keys.
    hash[integer, string] int_str_map
    int_str_map[10] = "10"
    int_str_map[5000] = "11"
    int_str_map[0x10] = "12"

    # Doubles as keys, with some exponential stuff too.
    hash[double, string] num_str_map
    num_str_map[5.5] = "10"
    num_str_map[1e1] = "12"

    # static hash creation
    hash[string, string] str_str_map = ["a" => "b", "c" => "d", "e" => "f"]
    # Again, but some of the keys repeat. In this case, the right-most key
    # gets the value.
    hash[string, string] str_str_map_two = ["a" => "a", "a" => "b", "a" => "c",
        "d" => "e"]

    # Test for any defaulting with duplicate keys.
    hash[string, any] str_any_map = ["a" => "1", "b" => 2, "c" => 2,
        "a" => 1]

    any nil_any
    hash[string, any] str_any_map_2 = ["a" => nil_any, "b" => 11,
        "b" => nil_any]
}

function test_eq()
{
    printfmt("#%i: Testing == for complex values...", test_id)
    test_id = test_id + 1

    integer ok = 1

    if [1 => "a", 2 => "b"] != [2 => "b", 1 => "a"]:
        ok = 0

    if [[1]] == [[2]]:
        ok = 0

    if test_eq == test_hashes:
        ok = 0

    if printfmt != printfmt:
        ok = 0

    if [1, 1.1, "3"] != [1, 1.1, "3"]:
        ok = 0

    if <[1, "1", [1]]> != <[1, "1", [1]]>:
        ok = 0

    if ok: {
        print("ok.\n")
    else:
        print("failed.\n")
        fail_count = fail_count + 1
    }
}

function test_tuples()
{
    printfmt("#%i: Testing tuples...", test_id)
    integer ok = 1

    # First a test of tuple literals.
    tuple[integer, string, list[integer]] t = <[1, "1", [1]]>

    # The first and final default to any.
    tuple[integer, integer, any] t2 = <[11, 21, 31]>

    # Make sure that t[1] yields the proper sig at emit time by forcing a call
    # to string::concat after it.
    string s = t[1].concat("1")

    # Check tuple assignment too...
    t2[0] = 12
    if t2[0] != 12:
        ok = 0

    if ok: {
        print("ok.\n")
    else:
        print("failed.\n")
        fail_count = fail_count + 1
    }
}

function test_misc()
{
    printfmt("#%i: Miscellaneous features...(sub tests follow).\n", test_id)
    test_id = test_id + 1
    integer ok = 1

    print("     Call of returned function: m()()...")
    function m2( => function( => integer)) {
        function m1( => integer)
        {
            return 10
        }
        return m1
    }

    function m3() {  }
    ###
    This checks for a parser bug where the parser thought that the return was
    the same as itself. This would cause an infinite loop when trying to raise
    an error when the return type wasn't what was expected.
    ###
    function m4( => function()) { return m3 }

    integer i = m2()()
    if i == 10: {
        print("ok.\n")
    else:
        ok = 0
        print("failed.\n")
    }

    # This checks that parser properly handles () from a dot call routed
    # through a ] check.
    integer i2 = [1].size()
    # This is a test for GH #20, where unary wasn't handling deep subscripts
    # and calls being merged right

    function ret10( => integer) { return 10 }
    list[list[function( => integer)]] llm = [[ret10]]
    list[list[integer]] llsi = [[1]]
    i2 = !llsi[0][0]
    i2 = !llm[0][0]()

    # Issue #36: '1+1' does not work because parser sees it as '1, +1' (two
    # integer literals) instead of breaking it down to '1, +, 1'.
    integer test_36 = 1+1

    # Bug (no issue): Introducing list::apply created a bug in type matching
    # where passing values of a simple type caused an error.
    # This wasn't caught because list::apply uses a complex type. However,
    # list::append does, so test that.
    list[integer] bug_check = [1, 2, 3]
    bug_check.append(4)

    if ok == 0:
        fail_count = fail_count + 1
}

list[function()] function_list =
[
    test_basic_assignments,
    test_conditions,
    test_printfmt,
    test_any_defaulting,
    test_oo,
    test_utf8,
    test_escapes,
    test_fib,
    test_unary,
    test_andor,
    test_parenth,
    test_arith,
    test_sub_assign,
    test_complex_sigs,
    test_typecasts,
    test_circular_ref_checks,
    test_function_varargs,
    test_multiline_strs,
    test_digit_collection,
    test_loops,
    test_assign_chain,
    test_multiline_comment,
    test_intnum_cast,
    test_hashes,
    test_eq,
    test_tuples,
    test_misc
]

for i in 0..function_list.size() - 1: {
    function_list[i]()
}

test_id = test_id - 1

printfmt("Tests passed: %i / %i.", test_id , test_id + fail_count)
@>
</body>
</html>

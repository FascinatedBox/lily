<html>
<head>
<title>Lily Sanity Test</title>
</head>
<body>
<@lily
integer test_id = 1
integer fail_count = 0

method test_basic_assignments():nil
{
    method method1():integer {
        method method2():integer {
            method method3():integer {
                method method4():integer {
                    method method5():integer {
                        return 1
                    }
                    return 1
                }
                return 1
            }
            return 1
        }
        return 1
    }
    # Test valid assignments.
    integer a = 1
    number b = 2.0
    str c = "c"
    object o = "o"

    # Decl list test.
    integer dl1, dl2, dl3, dl4

    # Objects can be assigned any value.
    o = 1
    o = 2.0
    # Make sure that strings don't reference leak.
    c = "c"
    c = "cc"
    o = "o"
    o = "oo"

    printfmt("#%i: Testing basic assignments...ok.\n", test_id)
    test_id = test_id + 1
}

method test_jumps():nil
{
    integer a, ok

    a = 1
    printfmt("#%i: Testing jumps...", test_id)
    test_id = test_id + 1

    if a == a: {
        print("ok.\n")
    else:
        print("failed.\n")
        # Make sure jumps aren't miswired...again.
        fail_count = fail_count + 1
    }
}

method test_manyargs (integer a, integer b, integer c, integer d,
    integer e, integer f):integer
{
    printfmt("#%i: 8-arg call...ok.\n", test_id)
    test_id = test_id + 1

    return a
}

method test_printfmt():nil
{
    integer a = 1
    number b = 12.34
    str c = "abcd"

    printfmt("#%i: Testing printfmt:...(check results)\n", test_id)
    test_id = test_id + 1

    printfmt("    integer a (1)     is %i.\n", a)
    printfmt("    number  b (12.34) is %n.\n", b)
    printfmt("    str     c (abcd)  is %s.\n", c)
    # Make sure varargs calls are taking the extra ones too.
    printfmt("    a, b, c are %i, %n, %s.\n", a, b, c)
}

method test_obj_call():nil
{
    object o

    method sample_ocall(object a, object b):integer
    {
        return 1
    }

    sample_ocall("a", "a")
    sample_ocall(1, 1)
    sample_ocall(1.1, 1.1)
    sample_ocall(o, o)

    printfmt("#%i: Testing autocast of object calls...ok.\n", test_id)
    test_id = test_id + 1
}

method test_oo():nil
{
    str abc = "abc", rlt
    integer i, ok
    object abc_obj = "abc"
    list[str] abc_list = ["abc"]

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

    rlt = @(str: abc_obj).concat("def")
    print("    @(str: abc_obj).concat(\"def\")   == \"abcdef\"...")
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

    method return_abc():str { return "abc" }

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

method test_utf8():nil
{
    str h3llö = "hello"
    str ustr = "á"

    printfmt("#%i: Testing concat with utf8...", test_id)
    test_id  = test_id + 1

    if ustr.concat("á") == "áá": {
        print("ok.\n")
    else:
        print("failed.\n")
        fail_count = fail_count + 1
    }
}

method test_escapes():nil
{
    str s = "\\a\\b\\\\c\\d\\"
    str s2 = "Hello, world.\n"
    str s3 = ""

    printfmt("#%i: Testing escape string...%s.\n", test_id, s)
    test_id = test_id + 1
}

method test_nested_if():nil
{
    integer a, ok = 0
    str s = "a"
    a = 1
    printfmt("#%i: Testing nested if...", test_id)
    test_id = test_id + 1

    if a == 1: {
        if a == 1: {
            if a == 1: {
                if a == 1: {
    if a == 1: {
        if a == 1: {
            if a == 1: {
                if a == 1: {
    if a == 1: {
        if a == 1: {
            if a == 1: {
                if a == 1: {
                    ok = 1
                }
            }
        }
    }
                }
            }
        }
    }
                }
            }
        }
    }

    # In Lily, braces are only needed for the start and end of an if. This is a
    # multi-line if test.
    if a == 1: {
        a = 2
        a = 3
    elif a == 2:
        a = 4
        a = 5
    else:
        a = 6
        a = 7
    }

    if ok == 1: {
        print("ok.\n")
    else:
        print("failed.\n")
        fail_count = fail_count + 1
    }
}

method test_add():nil
{
    # This tests ast merging.
    integer i1, ok
    str s1, s2

    i1 = "1" >= "1"
    i1 = "1" > "1"
    i1 = "1" < "1"
    i1 = "1" <= "1"
    i1 = "1" == "1"

    method add(integer a, integer b):integer
    {
        return a + b
    }

    i1 = add(1, 1) + add(1, 1)
    printfmt("#%i: Testing add(1, 1) + add(1, 1)...", test_id)
    test_id = test_id + 1

    if i1 == 4: {
        print("ok.\n")
    else:
        print("failed.\n")
        fail_count = fail_count + 1
    }
}

method test_assign_decl_list():nil
{
    printfmt("#%i: Testing assigning to decl list...", test_id)
    test_id = test_id + 1

    # First, basic stuff...
    integer a = 1, b = 2, c = 3

    # Now, with binary.
    integer d = a + b, e = a + b

    method add(integer add_a, integer add_b):integer
    {
        return a + b
    }

    # Finally, with a call.
    integer f = add(a, b), g = 4

    if f == 3: {
        print("ok.\n")
    else:
        print("failed.\n")
        fail_count = fail_count + 1
    }
}

method oneline_helper():integer
{
    # oneline conditions go through a different path than multiline ones. This
    # tests the single-line path to make sure everything is ok.
    integer a = 1
    if a == 2:
        return 0
    elif a == 3:
        return 0

    if a == 1:
        a = 1
    elif a == 2:
        a = 2
    else:
        a = a

    # Test transitioning from single to multi.
    if a == 1: {
        a = 1
    }

    if "a" == "a":
        a = 1
    else:
        return 0

    return 1
}

method test_oneline_if():nil
{
    printfmt("#%i: Testing one-line conditions...", test_id)
    test_id = test_id + 1

    if oneline_helper() == 1: {
        print("ok.\n")
    else:
        print("failed.\n")
        fail_count = fail_count + 1
    }
}

method fib(integer n):integer
{
    if n == 0:
        return 0
    elif n == 1:
        return 1
    else:
        return fib(n - 1) + fib(n - 2)
}

method test_fib():nil
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

method unary_helper():integer
{
    return 10
}

method unary_call_checker(integer a):integer
{
    return 10
}

method test_unary():nil
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

method test_andor():nil
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

    method return_1():integer {
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

method test_parenth():nil
{
    printfmt("#%i: Testing parenth expressions...", test_id)
    test_id = test_id + 1

    integer a = 1
    integer ok = 1
    str s = "a"

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

method test_arith():nil
{
    printfmt("#%i: Testing +, +=, -, -=, *, *=, /, and /=...", test_id)
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

    number n = 1.0
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

method test_sub_assign():nil
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

    list[number] list_number = [1.0]
    list_number[0] = 2.0

    if list_number[0] == 2.0: {
        print("     list[number] ok.\n")
    else:
        print("     list[number] failed.\n")
        ok = 0
    }

    list[str] list_str = ["1"]
    list_str[0] = "2"

    if list_str[0] == "2": {
        print("     list[str] ok.\n")
    else:
        print("     list[str] failed.\n")
        ok = 0
    }

    list[list[str]] list_list_str = [["1"]]
    list_list_str[0][0] = "2"

    method ret_10():integer { return 10 }
    method ret_20():integer { return 20 }
    method ret_30():integer { return 30 }

    list[object] lso = [10, 1.1]
    lso[0] = 10
    lso[0] = 1.1
    lso[0] = "11"
    lso[0] = lso
    lso[0] = @(object: lso)
    lso[0] = lso[1]
    lso[0] = print
    lso[0] = list_list_str

    print("     list[object] ok.\n")

    # Can't test functions yet because they all have different signatures (value
    # testing can't be checked).
    print("     list[function] ???.\n")

    print("     list[method] ")

    list[method():integer] list_method = [ret_10, ret_20, ret_30]
    list_method[0] = ret_30
    integer call_ret = list_method[0]()
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
    list[list[list[list[list[list[number]]]]]] deep_list
    # Check that list[str] doesn't crash if the str is nil.
    str nil_s
    list[str] test_nil_s = [nil_s]
    test_nil_s[0] = "test"

    if ok == 0:
        fail_count = fail_count + 1
}

# Sigs is short for signatures. A complex signature is a signature that contains
# more signature information inside of it. Lists and methods are good examples
# of this. This tests that complex signatures are not leaked.
method test_complex_sigs():nil
{
    printfmt("#%i: Testing complex sigs...", test_id)
    test_id = test_id + 1
    integer ok = 1

    method lv1_method(  integer arg  ):nil{}
    method lv2_method(  method lv1(  integer  ):nil  ):nil {}
    method lv3_method(  method lv2(  method(integer):nil  ):nil  ):nil {}

    # These won't do anything. This is more to test that parser is doing what
    # it should when passing args.
    lv1_method(ok)
    lv2_method(lv1_method)
    lv3_method(lv2_method)

    list[method(integer):nil] list_method_n1

    list[
        method(
            method(integer):nil
        ):nil
    ] list_method_n2

    list[
        method( 
            list[
                method(integer):nil
            ]
        ):list[integer]
    ] list_method_n3

    method mval_1():integer { return 10 }
    method mval_2():integer { return 20 }
    method mval_3():integer { return 30 }
    list[
        method():integer
    ] list_method_n4 = [mval_1, mval_2, mval_3]

    method mval_4():list[integer] { return [10] }
    method mval_5():object { return [10] }

    print("ok.\n")
}

method test_typecasts():nil
{
    printfmt("#%i: Testing typecasts from object...", test_id)
    test_id = test_id + 1

    object o
    integer intval
    o = 10
    intval = @(integer: o)

    number numval
    o = 10.0
    numval = @(number: o)

    str strval
    o = "10"
    strval = @(str: o)

    list[integer] list_intval
    o = [1]
    list_intval = @(list[integer]: o)

    list[object] list_objval
    object oval_1 = 10
    object oval_2 = 1.1
    list_objval = [oval_1, oval_2]

    method mval_10():integer { return 10 }
    method mval_20():integer { return 20 }
    method mval_30():integer { return 30 }
    method ret_obj():object { object o2 = 10 return o2 }

    list[method():integer] list_mval = [mval_10]
    o = mval_20
    list_mval[0] = @(method():integer: o)

    intval = list_mval[0]()
    intval = @(method():integer: o)() +
             @(method():integer: o)() +
             @(method():integer: o)() +
             @(method():integer: o)()

    intval = @(method():integer: o)()

    o = list_intval
    intval = @(list[integer]: o)[0]
    intval = @(integer: ret_obj())
    intval = @(integer: list_objval[0])

    print("ok.\n")
}

method test_list_autocast():nil
{
    printfmt("#%i: Testing list autocasts...", test_id)
    test_id = test_id + 1

    # This tests that lists of varying types are autoconverted to list[object].
    list[object] o1 = [1, 1.1, "1"]
    list[object] o2 = [2, 2.2, "2", o1[0]]
    object o3 = o1[0]
    list[object] o4 = [o1[0], o2[2], o3]
    object o5 = o4[0]
    integer i = @(integer: o5)

    print("ok.\n")
}

method test_circular_ref_checks():nil
{
    printfmt("#%i: Testing circular reference checks...", test_id)
    test_id = test_id + 1

    # Catching circular references and making sure they get deref'd properly is
    # rather tricky. Additions to this code are welcome.
    # These are random tests, and they might be incomplete. This test should
    # actually be verified by valgrind.

    list[object] a = [1.1, 2]
    a[0] = a
    a[1] = a
    a[1] = 1

    object b = a[0]
    object c = a[1]

    list[object] d = [1, 1.1]
    list[object] e = [1, 1.1]
    d[0] = e
    e[0] = d

    object f = c
    object g = d[0]
    object h = @(list[object]: g)[0]

    list[list[object]] i = [a, a, a, a]
    i[0][0] = i[0]
    i[0] = i[0]

    list[object] j = [1, 1.1]
    list[object] k = [1, 1.1]
    k[0] = j
    j[0] = k
    j[1] = k
    k[1] = j
    j = [1, 1.1]
    k = [1, 1.1]

    object l = 10
    list[list[object]] m = [[l]]
    m[0][0] = m

    object n = [10]
    object o = [n]
    object p = [o, o, o, o]
    n = p
    p = 1

    # Test circular refs with nil.
    object obj
    list[object] listobj = [obj]
    obj = listobj[0]

    print("ok.\n")
}

method test_method_varargs():nil
{
    printfmt("#%i: Testing method varargs...", test_id)
    test_id = test_id + 1

    # Method varargs must always be a list. Any extra args are packed into a
    # list for methods (instead of flattened like for functions).
    # The type that must be passed is the type of the list. So this first method
    # will take any extra integers and pack them into a list.
    method va_1(list[integer] abc...):nil {    }

    # Since this one takes objects, any extra args also get converted to objects
    # as needed. The list part of that isn't tested yet, because there is no
    # list comparison utility.
    method va_2(str format, list[object] args...):integer {
        integer ok
        # This next statement helped to uncover about 3 bugs. Leave it be.
        if @(integer: args[0]) == 1 &&
           @(number: args[1]) == 1.1 &&
           @(str: args[2]) == "1":
            ok = 1
        else:
            ok = 0

        return 1
    }
    method va_3(integer abc, list[list[integer]] args...):nil {    }

    object o = va_2
    # Check it with a typecast too.
    integer vx = @(method(str, list[object] ...):integer: o)("abc", 1, 1.1, "1")

    list[method(str, list[object] ...):integer] lmtd = [va_2, va_2, va_2]

    va_1(1,2,3,4,5)
    va_2("abc", 1, 1.1, "1", [1])
    va_3(1, [1], [2], [3])

    print("ok.\n")
}

method test_multiline_strs():nil
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
    str s1 = """abc\n
"""
    str s2 = """abc\n
\n"""
    str s3 = """\aabc\a
\aabc\a"""

    if ok == 0:
        fail_count = fail_count + 1
}

method test_digit_collection():nil
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

method test_while():nil
{
    printfmt("#%i: Testing while, break, and continue...", test_id)
    test_id = test_id + 1

    integer i = 10

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

    print("ok.\n")
}

method test_assign_chain():nil
{
    printfmt("#%i: Testing assignment chains...(sub tests follow).\n", test_id)
    test_id = test_id + 1
    integer ok = 1
    integer a = 10, b = 20, c = 30
    str result

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

method test_multiline_comment():nil
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

    printfmt("#%i: Testing multi-line comments...ok.\n", test_id)
    test_id = test_id + 1
}

method test_misc():nil
{
    printfmt("#%i: Miscellaneous features...(sub tests follow).\n", test_id)
    test_id = test_id + 1
    integer ok = 1

    print("     Call of returned method: m()()...")
    method m1(): integer { return 10 }
    method m2(): method():integer { return m1 }

    integer i = m2()()
    if i == 10: {
        print("ok.\n")
    else:
        ok = 0
        print("failed.\n")
    }

    if ok == 0:
        fail_count = fail_count + 1
}

test_basic_assignments()
test_jumps()
test_manyargs(1,2,3,4,5,6)
test_printfmt()
test_obj_call()
test_oo()
test_utf8()
test_escapes()
test_nested_if()
test_add()
test_assign_decl_list()
test_oneline_if()
test_fib()
test_unary()
test_andor()
test_parenth()
test_arith()
test_sub_assign()
test_complex_sigs()
test_typecasts()
test_list_autocast()
test_circular_ref_checks()
test_method_varargs()
test_multiline_strs()
test_digit_collection()
test_while()
test_assign_chain()
test_multiline_comment()
test_misc()

test_id = test_id - 1

printfmt("Tests passed: %i / %i.", test_id , test_id + fail_count)
@>
</body>
</html>

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
    str a = "a"
    integer i

    printfmt("#%i: Testing oo calls...", test_id)
    test_id = test_id + 1

    if a.concat("a") == "aa": {
        # Check that oo works with regular binary ops.
        i = a.concat("a").concat("a") < "bb"
        print("ok.\n")
    else:
        fail_count = fail_count + 1
        print("failed.\n")
    }
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
        return fib(n-1) + fib(n-2)
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

    integer a, b

    a = unary_call_checker(!!0 + !!0)
    b = -10 + --10 + -a + --a + unary_helper() + -unary_helper()
    b = b + !!0
    if b == 0: {
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

method test_mul_div():nil
{
    printfmt("#%i: Testing *, *=, /, and /=...", test_id)
    test_id = test_id + 1

    integer i = 2
    i *= i
    i /= i
    i = 1 * 1
    i = 2 / 2

    number n = 1.0
    n *= n
    n *= 1
    n /= n
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

    # Objects can't be cast to anything, so there's no way to check them yet.
    print("     list[object] ???.\n")

    # Can't test functions yet because they all have different signatures (value
    # testing can't be checked).
    print("     list[function] ???.\n")

    # Can't test methods yet because no way to call a subscript result.
    list[method(integer):integer] list_method = [ret_10, ret_20, ret_30]
    list_method[0] = ret_30
    print("     list[method] ???.\n")

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

    print("ok.\n")
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
test_mul_div()
test_sub_assign()
test_complex_sigs()

test_id = test_id - 1

printfmt("Tests passed: %i / %i.", test_id , test_id + fail_count)
@>
</body>
</html>

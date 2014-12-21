###
SyntaxError: Function f arg #2 expects type 'B' but got type 'list[any]'.
Where: File "test/fail/ensure_call_generics_resolve_once.ly" at line 17
###

function f[A, B](A value, B value2, B value3) {
    
}

function g[A, B](A value, B value2) {
    any a = 10

    # f's A is first resolved as 'any'.
    # B is then resolved as list[A] but it's g's A.
    # The last part fails: B is a list of g's A (which is quasi-known) but is
    # given a list of any.
    f(a, [value], [a])
}

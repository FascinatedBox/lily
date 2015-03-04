#[
SyntaxError: v has not been declared.
Where: File "test/fail/do_while_hide_vars.ly" at line 9
]#

do: {
    var v = 10
    continue
} while v == 10

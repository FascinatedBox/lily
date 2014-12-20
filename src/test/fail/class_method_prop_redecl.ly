###
SyntaxError: A property in class 'Test' already has the name 'abc'.
Where: File "test/fail/class_method_prop_redecl.ly" at line 8
###

class Test() {
    var @abc = 10
    function abc() {

    }
}

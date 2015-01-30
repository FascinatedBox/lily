#[
SyntaxError: A method in class 'Test' already has the name 'abc'.
Where: File "test/fail/class_prop_method_redecl.ly" at line 10
]#

class Test() {
    define abc() {

    }
    var @abc = 10
}

#[
BadTypecastError: Cannot cast any containing type 'integer' to type 'string'.
Traceback:
    Function f at line 10
    Function __main__ at line 14
]#

define f() {
    var a: any = 10
    var s = a.@(string)
}

var function_list: list[function()] = [f, f, f]
function_list[0]()

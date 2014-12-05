###
BadTypecastError: Cannot cast any containing type 'integer' to type 'string'.
Traceback:
    Function f at line 10
    Function __main__ at line 14
###

function f() {
    any a = 10
    string s = a.@(string)
}

list[function()] function_list = [f, f, f]
function_list[0]()

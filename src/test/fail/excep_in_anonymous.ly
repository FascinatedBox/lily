<?lily 
function f() {
    any a = 10
    string s = a.@(string)
}

list[function()] function_list = [f, f, f]
function_list[0]()
?>

<@lily 
method m():nil {
    object o = 10
    string s = o.@(string)
}

list[method():nil] method_list = [m, m, m]
method_list[0]()
@>

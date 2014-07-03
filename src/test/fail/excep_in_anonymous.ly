<@lily 
method m():nil {
    object o = 10
    str s = @(str:o)
}

list[method():nil] method_list = [m, m, m]
method_list[0]()
@>

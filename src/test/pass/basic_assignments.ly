    integer a = 10
    double b = 1.1
    string c = "11"

    list[integer] d = [1, 2, 3]
    list[list[integer]] e = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
    e[0] = [4, 5, 6]

    hash[string, integer] f = ["a" => 1, "b" => 2, "c" => 3]
    f["d"] = 4

    function g() {
        integer g_1 = 10
    }

    any h = 10
    h = "1"
    h = 1.1
    h = [1, 2, 3]
    h = g
    h = ["a" => 1, "b" => 2]

    function ret_10( => integer) { return 10 }
    function ret_20( => integer) { return 20 }
    function ret_30( => integer) { return 30 }

    list[function( => integer)] function_list = [ret_10, ret_20, ret_30]

    integer function_list_ret = function_list[0]()

    function_list[2] = function_list[0]
    function_list[0] = function_list[1]

    hash[integer, list[function( => integer)]] super_hash = [1 => function_list]

var a = 10
var b = 1.1
var c = "11"

var d: list[integer] = [1, 2, 3]
var e: list[list[integer]] = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
e[0] = [4, 5, 6]

var f: hash[string, integer] = ["a" => 1, "b" => 2, "c" => 3]
f["d"] = 4

define g() {
    var g_1: integer = 10
}

var h: any = 10
h = "1"
h = 1.1
h = [1, 2, 3]
h = g
h = ["a" => 1, "b" => 2]

define ret_10( => integer) { return 10 }
define ret_20( => integer) { return 20 }
define ret_30( => integer) { return 30 }

var function_list: list[function( => integer)] = [ret_10, ret_20, ret_30]

var function_list_ret: integer = function_list[0]()

function_list[2] = function_list[0]
function_list[0] = function_list[1]

var super_hash: hash[integer, list[function( => integer)]] = [1 => function_list]

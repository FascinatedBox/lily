var a: any = 10
var intval = 0
a = 10
intval = a.@(integer)

var dblval = 0.0
a = 10.0
dblval = a.@(double)

var strval = ""
a = "10"
strval = a.@(string)

var list_intval: list[integer] = []
a = [1]
list_intval = a.@(list[integer])

var list_anyval: list[any] = []
var aval_1: any = 10
var aval_2: any = 1.1
list_anyval = [aval_1, aval_2]

define mval_10( => integer) { return 10 }
define mval_20( => integer) { return 20 }
define mval_30( => integer) { return 30 }
define ret_any( => any) { var a2: any = 10 return a2 }

var list_mval = [mval_10]
a = mval_20
list_mval[0] = a.@(function( => integer))

intval = list_mval[0]()
intval = a.@(function( => integer))() +
         a.@(function( => integer))() +
         a.@(function( => integer))() +
         a.@(function( => integer))()

intval = a.@(function( => integer))()

a = list_intval
intval = a.@(list[integer])[0]
intval = ret_any().@(integer)
intval = list_anyval[0].@(integer)

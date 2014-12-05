any a = 10
integer intval = 0
a = 10
intval = a.@(integer)

double dblval = 0.0
a = 10.0
dblval = a.@(double)

string strval = ""
a = "10"
strval = a.@(string)

list[integer] list_intval = []
a = [1]
list_intval = a.@(list[integer])

list[any] list_anyval = []
any aval_1 = 10
any aval_2 = 1.1
list_anyval = [aval_1, aval_2]

function mval_10( => integer) { return 10 }
function mval_20( => integer) { return 20 }
function mval_30( => integer) { return 30 }
function ret_any( => any) { any a2 = 10 return a2 }

list[function( => integer)] list_mval = [mval_10]
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

define lv1_function(  arg: integer  ){}
define lv2_function(  lv1: function(  integer  )  ) {}
define lv3_function(  lv2: function(  function(integer)  )  ) {}

# These won't do anything. This is more to test that parser is doing what
# it should when passing args.
lv1_function(10)
lv2_function(lv1_function)
lv3_function(lv2_function)

var list_function_n1: list[function(integer)] = []

var list_function_n2:
list[
    function(
        function(integer)
    )
] = []

var list_function_n3:
list[
    function( 
        list[
            function(integer)
        ]
        => list[integer]
    )
] = []

define mval_1( => integer) { return 10 }
define mval_2( => integer) { return 20 }
define mval_3( => integer) { return 30 }
var list_function_n4: list[
    function( => integer)
] = [mval_1, mval_2, mval_3]

define mval_4( => list[integer]) { return [10] }
define mval_5( => any) { return [10] }

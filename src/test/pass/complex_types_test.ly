function lv1_function(  integer arg  ){}
function lv2_function(  function lv1(  integer  )  ) {}
function lv3_function(  function lv2(  function(integer)  )  ) {}

# These won't do anything. This is more to test that parser is doing what
# it should when passing args.
lv1_function(10)
lv2_function(lv1_function)
lv3_function(lv2_function)

list[function(integer)] list_function_n1 = []

list[
    function(
        function(integer)
    )
] list_function_n2 = []

list[
    function( 
        list[
            function(integer)
        ]
        => list[integer]
    )
] list_function_n3 = []

function mval_1( => integer) { return 10 }
function mval_2( => integer) { return 20 }
function mval_3( => integer) { return 30 }
list[
    function( => integer)
] list_function_n4 = [mval_1, mval_2, mval_3]

function mval_4( => list[integer]) { return [10] }
function mval_5( => any) { return [10] }

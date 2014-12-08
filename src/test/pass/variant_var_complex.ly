enum class Test[A, B, C] {
    First(A, B, C),
    Second(A, B),
    Third
}

var v1 = First(1, "2", [3])
var v2 = Second(1, ["2" => 3])
var v3 = Third

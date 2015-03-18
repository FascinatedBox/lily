define f() {}

var other_any: any = 10
var a: any = 1
a = 1.1
a = "10"
a = [1]
a = ["1" => 10]
a = f
a = printfmt
a = a

# If a static list contains various types, they should default to any.
var any_list: list[any] = [1, 1.1, "10", [1], ["1" => 10], f, printfmt,
                         other_any]

any_list[0] = 1
any_list[0] = 1.1
any_list[0] = "10"
any_list[0] = [1]
any_list[0] = ["1" => 10]
any_list[0] = f
any_list[0] = printfmt
any_list[0] = other_any

var any_hash: hash[string, any] = ["integer" => 1,
                              "double" => 1.1,
                              "str" => "10",
                              "list" => [1],
                              "hash" => ["1" => 10],
                              "function" => printfmt,
                              "any" => other_any,
                              "test" => 1]


any_hash["test"] = 1
any_hash["test"] = 1.1
any_hash["test"] = "10"
any_hash["test"] = [1]
any_hash["test"] = ["1" => 10]
any_hash["test"] = printfmt
any_hash["test"] = other_any

define test(x: any) {}

test(1)
test(1.1)
test("10")
test([1])
test(["1" => 10])
test(f)
test(printfmt)
test(other_any)

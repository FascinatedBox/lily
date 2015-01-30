define f(value: integer, g: function(integer => integer) => integer) {
    return g(value)
}

var value: integer = f(10, {|a| a * a})

if value != 100:
    print("Failed.\n")

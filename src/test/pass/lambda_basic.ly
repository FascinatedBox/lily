define f(integer value, function g(integer => integer) => integer) {
    return g(value)
}

integer value = f(10, {|a| a * a})

if value != 100:
    print("Failed.\n")

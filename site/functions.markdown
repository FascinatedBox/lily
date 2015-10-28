Functions
=========

What's a language without the ability to define a function?

```
define say_hi
{
    print("Hi\n")
}
```

This creates a function that does not accept any arguments coming in, and also does not return anything. A function that has been defined can be used as a value, but it can't be assigned to. For now, let's just adjust the above function so it can greet people.

```
define greet(name: string)
{
    printfmt("Hello, %s.\n", name)
}
```

The above function can be written to return the greeting, instead of printing it out

```
define get_greet_string(name: string) : string
{
    return string::format("Hello, %s.\n", name)
}
```

If you'd like to create a function that just returns a value, then skip the parentheses.

```
define return_ten : integer
{
    return 10
}
```

Functions can also take other functions as arguments. In this case, `apply` takes a function `f` that takes one integer and returns that same integer. However, the result of `apply` is also an integer. The return type of the function being declared is always delimited by a colon
outside of the parentheses of the arguments it takes.

```
define apply(v: integer, f: function(integer => integer)) : integer
{
    return f(v)
}

define add_one(x: integer) { return x + 1 }

apply(10, add_one) # 11
```

Functions can also be marked as accepting a variable number of arguments. The extra arguments will be placed inside of a list for the caller to work with.

```
define sum(values: integer...) : integer
{
    var total = 0
    for i in 0...values.size() - 1:
        total += i

    return total
}

sum(1, 2, 3) # 6
sum() # 0
```

Sometimes it would be nice for a function parameter to have a default value. Currently, the types `boolean`, `integer`, `double`, `string`, and `bytestring` can have default values. Default values must be literals, not function calls or even expressions. This is to keep them simple. Finally, arguments with default values must always come at the end.

```
define say_greeting(name: string, greet: *string="Hello")
{
    printfmt("%s, %s.\n", greet, name)
}

say_greeting("John", "Bonjour") # Bonjor, John.
say_greeting("Jesse") # Hello, Jesse.
```

Functions can be defined within other functions. By doing so, the outer-most function will automatically become a closure. Here's a basic example of what can be done with this:

```
define create_increment(start: integer) : function( => integer))
{
    var i = 0

    define increment : integer
    {
        var result = i
        i += 1

        return result
    }

    return increment
}

var inc = create_increment(10)
inc() # 10
inc() # 11
inc() # 12
# etc.
```

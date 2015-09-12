Lambdas
=======

Sometimes you need a function, but it's going to be very short and only serve a single use. You could `define` the function anyway, but that means you have one more named function hanging around. Lambdas are a shorthand way of defining a new function. The simplest form of a lambda looks like this:

```
# Inferred type: function( => integer)
var v = {|| 10}
```

You can now call 'v' with no arguments and receive an integer. Since there was no `define`, you can assign 'v' to another function, and the current one will eventually be garbage collected. 

```
define example[A](left: A, f: function(A => A) => A)
{
    return f(left)
}

example(10, {|a| a * a})                      # 100
example("abc", {|hello| hello.concat("def")}) # "abcdef"
# Invalid operation: list[integer] / integer
# example([1, 2, 3], {|lst| lst / 10})
```

Lambdas, like normal functions, are fully able to participate in type inference. In the first call, the lambda infers that it will have one parameter of type A. Since type A has been solved as integer, then the variable a must therefore have the type integer. The second time, it's solved as type string. Proof that this is type inference (and not the interpreter happening to allow something that works) can be found in the last usage, which generates a type error if run.

Lambdas are currently limited in that they cannot have their types annotated, and thus rely entirely on inference for determining their parameters. Lambdas are also unable to have default arguments, or variable arguments. Those are limitations that may be lifted later.

However, it is still possible to do some neat things with lambdas and generics:

``
define transform[A, B](input: A, f: function(A => B) => B)
{
    return f(input)
}

# Result: 3
transform(transform(10, {|a| [a, a, a]}), {|b| b.size()})
```

If a function requires only one argument, and that argument is a function, you can pass a lambda without parentheses.

```
define f(g: function( => integer) => integer) {
    return g()
}

# Value: 20
f{|| 10 + 10}
```

any
===

The any type is a container that allows any value to be placed inside of itself. It is commonly used as a fallback type, such as when trying to build a list or a hash where the values are of mixed types.

Plain values of type any can only be created by annotating a variable before assigning it `var some_any: any = 10`.

To recover values from type any, it is necessary to do a typecast like so: `some_any.@(integer)`. If this typecast happens to be incorrect, Lily will raise `BadTypecastError`.

If a function parameter specifies type any, then parameters sent to it will be coerced into type any automatically. However, if the function takes, say, `list[any]` and is given `list[integer]`, that is a syntax error (it cannot be guaranteed that the function will not append in type any into the list of integers).

# Operations

Binary: `!=` `==`

any equality is done by deep equality. If a value of type any contains a circular reference, then a comparison will raise `RuntimeError`.

# Methods

This class has no methods.

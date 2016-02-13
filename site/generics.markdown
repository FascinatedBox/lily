Generics
========

Generics offer a way to define a single function or class that is able to handle various kinds of data types (as long as they are consistent. Lily's generics are done through 'parametric polymorphism'. Think of it as an algebra of sorts, but for types.

Let's start off with a simple example:

```
define print_elements[A](l: List[A])
{
    for i in 0...l.size() - 1:
        print(l[i])
}
```

This defines a function which can show the elements of a list. To use a generic type, it must first be listed using square braces, right after the function name. Lily also requires that generic types are from 'A' to 'Z'. These serve as placeholders for types.

In the actual function, `A` will be replaced by a type. For this generic function to work right, time `A` is required, the same thing must be given. Here's some examples of calling the above function:

```
# Expected: List[A]
# Got: List[integer]
# Valid: A is always Integer
print_elements([1, 2, 3])

# Invalid: Expected List[A], but got Integer
# print_elements(10)
```

A more interesting example can be found in the List::push function. This is defined as `Function(List[A], A)`. That type is what is used to prevent elements of the wrong type from being added to a list. Here are some examples of it:

```
var int_list = [1, 2, 3]

# Expected List[A], and A.
# Received List[Integer], and Integer.
# Valid: A is always Integer.
int_list.push(4)

var tuple_list = [<[1, [2], "3"]>]

# Still valid, like above.
tuple_list.push(<[7, [8, 9], "10"]>)

# Since there is nothing to infer from, this has the type `List[Dynamic]`
var dynamic_list = []

dynamic_list.push(Dynamic(10))
dynamic_list.push(Dynamic("20"))
```

Generics can also be used as a means to transform values from one type to another

```
define f[A, B](input: A, f: Function(A => B)) : B
{
    return f(input)
}

# This is valid:
# The first parameter wants A, and gets integer.
# The second parameter wants something from A to B.
# A is still integer, and B is discovered to be string.
# This is a type-safe way to transform a value.
f(10, Integer::to_s)
```

Generics do have some caveats, however. You can't call any methods on a generic type, compare them, or initialize them with a concrete type. They also cannot have default values. However, even with those restrictions, it is easy to create functions that serve many uses while retaining full type safety.

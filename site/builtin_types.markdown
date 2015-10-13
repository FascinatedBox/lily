Builtin Types
=============

Lily comes with a handful of types baked into it that are available from any scope. For now at least, these classes **cannot** be inherited from (aside from Exception).

# boolean

A value that is either `true` or `false`.

# integer

This type is a signed 64-bit int. It has a range of +9,223,372,036,854,775,807 to to -9,223,372,036,854,775,808. Currently, operations in Lily do not check for overflow, and thus behavior during overflow is undefined. Here are some examples of integer literals:

```
123
-456
# Octal
0t7
# Hex
0xdeadbeef
# Binary
0b101010
```

# double

This type is a C double. It's minimum and maximum values are...really big. Negative and positive values are allowed, as well as decimal values. Here are some `double` literals.

```
1.5
1e-1
-1.0
```

Note: Lily will not allow integer literals to be assigned to values which have specified a want for a double.

# string

Lily's strings are utf-8 clean, and always 0-terminated. The methods of the string class are all utf-8 aware (so, for example, split is able to split on utf-8 codepoints, instead of by bytes). You can subscript from a string, and the index will be based off of codepoints, instead of bytes. The string class is also immutable: All string methods will create a new string instead of modifying the existing underlying string.

String literals begin and end with the double quote (`"`). Lily supports the following escape sequences within a string.

```
\n:   Newline
\r:   Carriage Return
\t:   Tab
\':   ' (single quote)
\":   " (double quote)
\\:   \
\nnn: nnn represents three decimal digits.
      Values allowed are \001...\127
```

# bytestring

Sometimes you need to interface with something that may give back strings with embedded \0's, or may kick back invalid utf-8. For those situations, Lily provides a bytestrings. This class is length counted, therein allowing embedded zeroes. This class provides a method to convert to a string, allowing string to be a 'pure', valid string, and for bytestring to handle the nastier cases.

Bytestring literals are like string literals, except they start with 'B' exactly before the opening double quote (`"`). Bytestring literals support all escape codes that string literals do, with the addition that bytestring literals allow `\nnn` escape sequences from `\001` to `\255`

# functions

Lily's functions are considered first-class. This means they can be assigned to vars, passed as parameters, inserted into lists, and more. There are no function literals, but there are lambdas, which are discussed later. The `define` keyword is responsible for creating new functions.

# any

This type is, at first glance, rather odd. It's not possible to create literals of type any. To have a variable of type any, the type must explicitly be annotated as being type any.

A variable of type any is best viewed as a box that can hold an unknown, unspecified type. Because it is not possible to know what type any contains immediately, it does not support any binary operations, any unary operations, or any methods. Instead, one must typecast the value away.

```
var v: any = 10

# A typecast is denoted by <value>.@(<newtype>)
var i = v.@(integer)
```

If a typecast fails, it will raise an exception called `TypecastError`. That error, and others, will be covered in more detail in a future section.

Sometimes, type any is required, but something else is given. In such cases, the value is placed into a hidden value of type any behind the scenes:
```
define f(input: any)
{
    
}

f(10)
f("20")

define g(input: integer) : any
{
    if input == 1:
        return 10
    else:
        return [1, 0]
}
```

The primary purpose of this class is to serve as a last resort. In certain cases, type inference doesn't have a common type and will use type any instead. In such cases, the action is referred to as **defaulting to any**. Sometimes this is useful, because the type isn't -super- important. In other cases, it's annoying. This type may be removed in the future.

# list

The list class is Lily's most basic container type. Lily's lists are qualified by a single type, which is the type expected for elements. For example, the type `list[integer]` denotes a list that is supposed to contain just integers. `list[function( => integer)]` would be a list that is only supposed to hold functions which take no parameters, but return an integer. Lily's lists are flexible and can be designated to hold any type.

```
var a = [1, 2, 3]

var b = ["1", "2", "3", "4"]

var c = [b, b]

# Subscript indexes are 0-based.
a[0] = 4

# Subscript get/set is an error if out of bounds.
# a[4] = 4

# Negative subscripts are allowed.
var d = a[-1]
```

Lily attempts to do some type inference, when it can:

```
define f : list[integer]
{
    # Inferred as an empty list[integer]
    return []
}

# Inferred as an empty list[list[integer]]
var v: list[list[integer]] = []
```

However, there are cases where `[]` is used and there is no expected type to infer, or the elements between `[]` are not of a consistent type. In such cases, the resulting list will have the type `list[any]`

# hash

This is a basic hash collection, representing a mapping from a key to a value. The annotation for this type has the key first, and the value second. Therefore, a hash that maps from string keys to integer values would be written as `hash[string, integer]`. Hashes are created similar to how lists are created:

```
var v = ["a" => 1, "b" => 2, "c" => 3]

# Show the value represented by "a".
show(v["a"])

# New values can be added through assignment.
v["d"] = 4

# With repeats, the right-most value has precedence.
v = ["a" => 1, "a" => 2] # Value: "a" => 2
```

Type inference works here too:

```
define f : hash[integer, list[string]]
{
    # Inferred as an empty hash[integer, list[string]]
    return []
}

var h: hash[integer, any] = [1, "a"]

h[1] = [20]
h[1] = 30
```

Currently, the only classes that can be keys within a hash are `integer`, `double`, and `string`. It is currently not possible to define a hashing function for the hash class to use. This is likely to change in the future.

Since `any` cannot be hashed, the interpreter insists upon all hash keys having exactly the same type (and being one of the above-mentioned types). However, it is possible for the values of a hash to default to `any`.

# tuple

Occasionally, it is useful to have a grouping of various different types. This class takes an arbitrary number of types within it, making it useful in situations where there is a need for a 'record' type that holds a bag of types. As with the above mentioned types, the subtypes of tuple are denoted by the square braces that come after the type.

Tuples are similar to lists, but have some interesting differences

```
# <[ ... ]> is the syntax for creating tuples.
var t = <[1, "2", [3]]>

# Invalid: Empty tuples are not allowed.
# var empty = <[]>

# Tuples are also 0-indexed.
var my_list = t[2]

# Tuple subscripts MUST be literals. This is why:
define subscript(my_tup: tuple[integer, string, list[integer]], x: integer)
{
    # Invalid: Lily has no way of knowing what the resulting type will be.
    # var invalid = my_tup[x]
}
```

# file

It's a file. Files can be opened with `file::open`, and some basic methods for IO are available on files.

# Exception

This class defines the most basic kind of raiseable error. This class is covered in more detail in the section about user-defined errors.

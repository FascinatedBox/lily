Builtin Types
=============

Lily comes with a handful of types baked into it that are available from any scope. For now at least, these classes **cannot** be inherited from (aside from Exception).

# Boolean

A value that is either `true` or `false`.

# Integer

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

# Double

This type is a C double. It's minimum and maximum values are...really big. Negative and positive values are allowed, as well as decimal values. Here are some `double` literals.

```
1.5
1e-1
-1.0
```

Note: Lily will not allow integer literals to be assigned to values which have specified a want for a double.

# String

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

# ByteString

Sometimes you need to interface with something that may give back strings with embedded \0's, or may kick back invalid utf-8. For those situations, Lily provides a bytestrings. This class is length counted, therein allowing embedded zeroes. This class provides a method to convert to a string, allowing string to be a 'pure', valid string, and for bytestring to handle the nastier cases.

Bytestring literals are like string literals, except they start with 'B' exactly before the opening double quote (`"`). Bytestring literals support all escape codes that string literals do, with the addition that bytestring literals allow `\nnn` escape sequences from `\001` to `\255`

# Functions

Lily's functions are considered first-class. This means they can be assigned to vars, passed as parameters, inserted into lists, and more. There are no function literals, but there are lambdas, which are discussed later. The `define` keyword is responsible for creating new functions.

# Dynamic

This class exists to hold values of other classes. `Dynamic` is useful because instances of `Dynamic` are not constrained to any single type. This allows a `list` to hold a mixture of values (with the actual values hidden behind `Dynamic` instances).

Fetching a value from Dynamic is done through typecasting. Since Lily strives to be a safe language, typecasts will yield either an `Option[<type>]` which holds a `Some(<type>)` on success and `None` on failure.

There are occasionally cases where Lily is not able to use inference to determine all the subtypes that a value should have. In such cases, `Dynamic` is used as a filler type, since it permits a wide range of values.

# List

The list class is Lily's most basic container type. Lily's lists are qualified by a single type, which is the type expected for elements. For example, the type `List[Integer]` denotes a list that is supposed to contain just integers. `List[Function( => Integer)]` would be a list that is only supposed to hold functions which take no parameters, but return an Integer. Lily's lists are flexible and can be designated to hold any type.

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

# With nothing to infer from, this has the type `List[Dynamic]`
var e = []
```

Lily attempts to do some type inference, when it can:

```
define f : List[Integer]
{
    # Inferred as an empty List[Integer]
    return []
}

# Inferred as an empty List[List[Integer]]
var v: List[List[Integer]] = []
```

# Hash

This is a basic hash collection, representing a mapping from a key to a value. The annotation for this type has the key first, and the value second. Therefore, a hash that maps from string keys to integer values would be written as `Hash[String, Integer]`. Hashes are created similar to how lists are created:

```
var v = ["a" => 1, "b" => 2, "c" => 3]

# Print the value represented by "a".
print(v["a"])

# New values can be added through assignment.
v["d"] = 4

# With repeats, the right-most value has precedence.
v = ["a" => 1, "a" => 2] # Value: "a" => 2
```

For now, only `Integer`, `Double`, and `String` are considered valid hash keys. In the future, it may be possible to have user-defined classes that are hashable.

It is also worth noting that Lily will insist that, when creating a hash, that there is some common bottom type to all elements in the hash.

# Tuple

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
define subscript(my_tup: Tuple[Integer, String, List[Integer]], x: Integer)
{
    # Invalid: Lily has no way of knowing what the resulting type will be.
    # var invalid = my_tup[x]
}
```

# File

It's a file. Files can be opened with `File::open`, and some basic methods for IO are available on files.

# Exception

This class defines the most basic kind of raiseable error. This class is covered in more detail in the section about user-defined errors.

# Option

This class defines an enum with two members: `Some(A)` or `None`. Any `Option` can be set to `None`, but only `Option[A]` can be set to `Some(A)`. The `Option` type is therefore useful when, say, a function may or may not have a valid result to return, but the reason isn't important.

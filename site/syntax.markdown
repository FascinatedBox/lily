Syntax
=====

Lily's syntax is likely to seem familiar to those who come from Python, Ruby, or other interpreted languages. First, and most importantly, there are no semicolons. Instead, Lily infers where a statement should end based on matching parentheses and determining where values apply:

```
print(
    "Hello")

print("World")
```

Since 'print' is a value, the second 'print' command is inferred to be the beginning of a different expression, instead of applying to the previous expression.

# Comments

There are two kinds of comments in Lily:

```
# This comment covers a single line.

#[ This
   covers
   multiple
   lines ]#
```

Multi-line comments do **not** nest.

# Identifiers

Identifiers must start with _ or a letter. But after that, numbers are allowed. Spaces are not okay though. Capital letters and underscores have no special meaning. As a fun note, utf-8 can be used in identifiers.

```
_i_like_pie
camelCase
abc123
I□UNICODE
```

# Special words

Lily reserves the following words:

```
__file__ __function__ __line__ break case class continue define do elif
else enum except false for if import match private protected raise return
self true try var while
```

The first three keywords are extra interesting:

`__line__` will always evaluate to the line of the current file.

`__function__` will always evaluate to the name of the name of the current function.

`__file__` will always evaluate to the name of the current file.

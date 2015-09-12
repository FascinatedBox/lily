Control Flow
============

It's hard to discuss control flow without discussing truth. 

Lily allows `boolean`, `integer`, `double`, `string`, `bytestring`, and `list` to be checked for truthiness. `integer` and `double` are considered truthy if they aren't 0 (or 0.0). `string` and `bytestring` are considered truthy if their length is not 0. Finally, `list` is considered truthy if it isn't empty. Attempting to test any other type for truthiness is a syntax error.

# if / elif / else

The simplest kind of condition.

```
var age = 18
if age >= 18:
    print("You can drive!\n")
else:
    print("You're not old enough yet. Sorry.\n")
```

Similar to Python, Lily requires `:` at the end of conditions. However, whitespace is not significant for Lily. Instead, the above condition is considered single-line: Each 'arm' of the condition can only run a single expression.

# Multi or single?

All blocks in Lily follow the same format: After the condition test, there is a colon. If, after the colon, there is a brace, then every 'arm' of the block allows for multiple expressions.

```
var v = false

if v == false: {
    v = true
    print("The value is now set to true.\n")
else:
    v = false
    print("The value has been changed to false.\n")
}
```

Lily's unique way of bracing means there's less chance of forgetting a curly brace, or having to add in curly braces because you have a single-line condition that needs to change. One brace to rule them all.

On a side note, it is a syntax error to put a multi-line block inside of a single-line block.

# while

```
var i = 10
var a = []

while i > 0:
    a.append(i)
```

Both `break` and `continue` 

# do while

```
do:
    print("Test.\n")
while 0:
```

Because Lily requires that all variables have a value, it is not possible to use variables defined within the scope of a `do` block to be used in the `while` condition. This is because Lily can't be completely sure that the variables within the `do` block have been properly initialized.

# for

Lily's for loop is rather simple compared to other interpreted languages. It currently only supports an integer range and integer step. A simple version looks like this:

```
for i in 0...10:
    printfmt("i is %d.\n", i)
```

By default, Lily will choose a step that goes in the appropriate direction: Either -1 each time, or +1 each time. You can also use an existing var for the loop increment as well:

```
var i = 0
var a = []
for i in 0...10:
    a.append(i)

printfmt("i is now %d.\n", i)
```

Alternatively, a step can be provided:

```
for i in 0...10 by 2:
    ...
```

Lily also supports `try`+`except` as well as `match` upon abstract data types. However, both of those features will be discussed in later sections.

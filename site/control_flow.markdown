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
    a.push(i)
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
    printfmt("i is %d.\n", i) # prints 0 to 10, inclusive.
```

A step can be provided, but is not required. If no step is provided, then Lily uses the value `1` (such as in the above example.

```
for i in -8...-2 by -2:
    print(i) # prints -8, -6, -4, -2
```

The increment variable can also be an existing value. In such a case, the existing value is overwritten with each pass. The increment variable's value will be the last value within the range:

```
var i = 10

for i in 1...3:
    ...

print(i) # prints 3
```

Some other bits to note:

```
for j in 1...1:
    ... # This will run, but only once.

for j in 10...0:
    ... # This does nothing.

# { ... } means the `for` allows multiple expressions.

for i in 0...5: {
    expr1
    expr2
    expr...
}
```

Lily also supports `try`+`except` as well as `match` upon abstract data types. However, both of those features will be discussed in later sections.

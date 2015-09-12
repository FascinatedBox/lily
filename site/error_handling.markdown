Error Handling
==============

Sometimes there are errors that occur from certain operations. A list has a subscript that goes too far, a division by zero, a bad format string, and more. When this occurs, Lily will raise an exception that can be caught.

To begin with, all exceptions that are defined within Lily have Exception as their base class. This class is defined internally as follows:

```
class Exception(msg: string) {
    var @message = message
    var @traceback: list[tuple[string, string, integer]] = []
}
```

Since it is defined like a normal class, instead of having a representation baked into the interpreter, it is possible to subclass it to provide more specific exceptions. That is, in fact, how Lily provides the exception classes that are built into it. Those builtin classes that derive from Exception are as follows:

* DivisionByZeroError: Raised when there is a division or modulo by 0 or 0.0

* IndexError: Raised when an list subscript is out of bounds.

* BadTypecastError: Raised when a value is unable to cast to a certain type.

* ValueError: Raised when a value is out of the expected range (ex: list::repeat with a negative index

* RuntimeError: Raised when there is an infinite loop in == or != (such as when there are recursive lists being compared.)

* KeyError: Raised when attempting to find a key in a hash that does not exist.

* FormatError: Raised when string::format or printfmt receive the wrong number of args for their format specifiers (ex: `string::format("%s")`)

* IOError: Raised when there is an issue calling a method of the file class (ex: Reading from a closed file, writing to a read-mode file, etc).

* SyntaxError: This is an error raised internally by the interpreter when parsing syntax errors. It's not available publically.

Instances of each of these errors (except SyntaxError) can be constructed using only a string as a message (ex: `ValueError::new("That value is incorrect.")`)

# Introducing try

Lily provides a common try+catch-style block as a means of capturing errors. Here's a simple example:

```
try:
    1 / 0
except DivisionByZeroError:
    print("Oh no, a division by zero.\n")
```

try blocks, like all blocks so far, comes in both a multi-line and a single-line version. In the above example, the error is caught and ignored. To catch the error and do something with it, an `as <name>` clause is required

```
try:
    1 / 0
except DivisionByZeroError as e:
    print(e.message)
```

While stack trace is provided, it is currently not useful as-is. This will be fixed in a future release.

Perhaps the most important thing about exceptions is how they are caught. If a clause is found that is a base class of the exception being thrown, then that clause will be picked. This allows `except Exception` to catch all possible errors.

```
try:
    1 / 0
except Exception as e:
    print("Caught it here.")
except DivisonByZeroError:
    print("Won't reach here.")
```

Not only will the clause for DivisionByZeroError not be reached (because Exception is a base class of the exception thrown). The following is trapped as a syntax error by Lily, because of that.

Errors can also be manually raised, using the `raise` keyword.

```
try:
    raise ValueError::new("This value is wrong.")
except Exception:
    ...
```

An important thing to remember when handling exceptions is that each block is considered to be in a different scope. Because there is no guarantee that variables within the try clause cannot be used in an except clause

```
try:
    var v = 1 / 0
except DivisionByZeroError:
    # This is a syntax error.
    printfmt("v is %d.\n", v)
```

# Custom Errors

It's possible to create custom exceptions as follows:

```
class MyError(message: string, code: integer) > Exception(message)
{
    var @code = code
}

try:
    raise MyError::new("The code is too large", 900)
except MyError as e:
    printfmt("Error message: %s, code: %d.\n", e.message, e.code)
```

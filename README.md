The Lily Programming Language
=====

[![Freenode](https://img.shields.io/badge/chat-on%20freenode-brightgreen.svg)](https://kiwiirc.com/client/irc.freenode.net/##lily)

[![Gitter](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/jesserayadkins/lily?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)
[![License (3-Clause BSD)](https://img.shields.io/badge/license-BSD%203--Clause-blue.svg?style=flat-square)](http://opensource.org/licenses/BSD-3-Clause)

[![Linux Build](https://travis-ci.org/jesserayadkins/lily.svg?branch=master)](https://travis-ci.org/jesserayadkins/lily)
[![Windows Build](https://ci.appveyor.com/api/projects/status/github/jesserayadkins/lily?svg=true)](https://ci.appveyor.com/project/JesseRayAdkins/lily)

Lily is a programming language that can be used for general-purpose programming (the default), or for server-side scripting (with code between `<?lily ... ?>` tags). Lily's design is inspired by a mixture of languages: C, F#, Haskell, Python, Ruby, and Rust. Lily's key features are:


* __Static typing, interpreted speed__: Lily is an interpreted language, which means that there's no compiler that you'll be waiting on. A fast turn-around time encourages exploration of ideas, instead of tricks to get build times down. The `pre-commit-hook.py` script churns through over 2000+ lines spread over 200+ files, typically under 3 seconds.

* __Safety first__: `Option` and `Either` are built-in and available everywhere.
Lily's design is inspired by a mixture of languages: C, Python, Ruby, F#, Rust, and Haskell.

* __The garbage collector__: Lily's garbage collector is able to take care of complicated resources, like those that the postgres module adds. Native Lily classes are taken care of automatically (no finalizers to worry about writing).

* __Pay only for what you use__: Let's say you write a whole program, and never use the `Either` enum. You also make it without using, say, `Double.to_s`. Since Lily is statically-typed throughout, it's only going to dynamically allocate memory for built-in classes and methods if it truly needs to. By doing so, there's no time wasted allocating memory for things that are never used.


In style, Lily is middle ground between functional concepts and object-oriented ideas. Lily's syntax is designed around both consistency and safety.

Here are some samples of different parts of Lily:

### Variable declaration

```
# This is a comment!
var age = 5
var a_string = "Hello"
var numbers = [1, 2, 3]
var suffixes = ["B" => 0, "KB" => 1000, "MB" => 1000000]

# Bytestrings allow escape codes above \127, and \000.
var bytes = B"\255\253\001"

# <[ ... ]> denotes a tuple.
var record = <["abc", 123, 45.67]>
```

### Functions

```
define say_hello
{
	print("Hello")
}

define return_ten: Integer
{
	return 10
}

define add(left: Integer, right: Integer): Integer
{
	return left + right
}

define subtract(left: Integer, right: Integer): Integer
{
	return left - right
}

# Functions are first-class values too.
var math_ops = ["+" => add, "-" => subtract]

math_ops["+"](10, 20) # 30

define total(numbers: Integer...): Integer
{
	var result = 0
	numbers.each{|n| result += n }
	return result
}

total()        # 0 
total(1, 2, 3) # 6
```

### Classes

```
# Default values must be either a constant or a literal value.
class Account(initial: *Integer = 0)
{
    # Similar to Scala, the body of a class is the constructor.
    # @<name> denotes class members.
    var @balance = initial
    define deposit(amount: Integer) {
        @balance += amount
    }
    define withdraw(amount: Integer) {
        @balance -= amount
    }
}

var my_account = Account(100)
my_account.withdraw(10)
print($"Account balance is now ^(my_account.balance).")
# my_account = Account() # uses default value of 0.

class Point(x: Integer, y: Integer)
{
    var @x = x
    var @y = y
    define move2D(moveX: Integer, moveY: Integer)
    {
        @x += moveX
        @y += moveY
    }
}

class Point3D(x: Integer, y: Integer, z: Integer) < Point(x, y)
{
    var @z = z
    define move3D(moveX: Integer, moveY: Integer, moveZ: Integer)
    {
        self.move2D(moveX, moveY)
        # Alternatively: Point.move2D(self, moveX, moveY)
        @z += moveZ
    }
}

class CustomError(code: Integer, message: String) < Exception(message)
{
    var @code = code
}
```

### Blocks

```
define bottles(count: *Integer = 10)
{
    # The { ... } allows for multiple expressions.
    while count > 0: {
        print(
$"""
^(count) bottles of beer on the wall,
^(count) bottles of beer,
take one down, pass it around,
^(count - 1) bottles of beer on the wall!
""")
        count -= 1
    }
}

bottles(5)
# bottles()   # Uses the default value of 10

define run_all_tests: Tuple[Integer, Integer] {
    var failed = 0
    var passed = 0

    define check(input: Boolean, message: String)
    {
        # The { here means that all branches allow multiple expressions.
        # A single } then closes the if.
        if input == true: {
            passed += 1
        else:
            print($"A test has failed (^(message)).")
            failed += 1
        }
    }
    check(1 + 1 == 2, "Make sure basic math still works.")

    return <[passed, failed]>
}

define letter_for_score(score: Integer): String
{
    # Omitting the { means all branches allow only 1 expression.
    if score > 90:
        return "A"
    elif score > 80:
        return "B"
    elif score > 70:
        # print("Average") # Not allowed
        return "C"
    elif score > 60:
        return "D"
    else:
        return "F"
}

define show_for_loop
{
    for i in 0...5:
        print($"i is ^(i).")

    # print(i) # Not allowed: i not in this scope.
    var i = 0
    for i in 0...5:
        print($"i is ^(i).")

    print(i) # 5
}

try: {
    var v = 1
    v = v / 0
except DivisionByZeroError:
    # print(v) # Not allowed: 'v' may or may not be valid.
    print("Can't divide by zero!")
}
```

### Generics

```
define transform[A, B](value: A, func: Function(A => B)): B
{
    return func(value)
}

transform(10, Integer.to_s) # "10"

# This time, use a lambda. Lily will infer the type for 'a'.
transform("  abc  ", {|a| a.trim().upper() })

define inplace_sort[A](input: List[A], cmp: Function(A, A => Boolean)): List[A]
{
    for i in 0...input.size() - 1: {
        var j = i
        while j > 0 && cmp(input[j - 1], input[j]): {
            var temp = input[j]
            input[j] = input[j - 1]
            input[j - 1] = temp
            j -= 1
        }
    }
    return input
}

inplace_sort([1, 3, 5, 2, 4], {|a, b| a > b})
# [1, 2, 3, 4, 5]

inplace_sort([1, 3, 5, 2, 4], {|a, b| a < b})
# [5, 4, 3, 2, 1]

inplace_sort([[1, 2], [1]], {|a, b| a.size() > b.size() })
# [[1], [1, 2]]
```

### Enums

```
enum TerminalColor
{
    Black
    Blue
    Cyan
    Green
    Magenta
    Red
    White
    Yellow
}

define terminal_example
{
    var foreground = Yellow

    #[ Enums can have a variant as a default argument, so long as the variant
       does not take arguments. ]#
    define change_fg(new_fg: *TerminalColor = Black)
    {
        foreground = new_fg
    }

    define is_fg_yellow: Boolean
    {
        # 'match' is required to be exhaustive.
        match foreground: {
            case Black:   return false
            case Blue:    return false
            case Cyan:    return false
            case Green:   return false
            case Magenta: return false
            case Red:     return false
            case White:   return false
            case Yellow:  return true
        }
        # Alternatively: return foreground == Yellow
    }
}

#[
Lily has a built-in enum called Option. It looks like this:

enum Option[A] {
    Some(A)
    None

    define is_some: Boolean {
        match self: {
            case Some(s): return true
            case None:    return false
        }
    }

    ...(and a few other methods)
}
]#

define log_error(message: String, where: *Option[File] = None)
{
    var to_file = where.unwrap_or(stdin)
    to_file.write(message)
}
```

### import

A module can be imported through, for example, `import server` or `import postgres as db`. The contents of the module are then accessible, but only through the name given (say, `server.httpmethod` or `db.Conn`. As of right now, all module imports are namespaced in some way (there is no `from x import *` equivalent right now).

The code at the toplevel of a module is run once, and only for the first time the module is imported.

Finally, files that are imported are only allowed to have code inside of them (no tags). This results in a strict separation between files that are code, and those that are not.

Modules may be provided (ex: the apache bridge provides the server package), but they must still be explicitly loaded. Such behavior is intentional, as it prevents code from accidentally relying on some module being provided. It allows, for example, a `server.lly` file to be established as a fallback to allow a script to run locally.

# Getting Started

You'll need a compiler that supports C11 (gcc and clang have been tested), and cmake. To build Lily, all you have to do is:

```
cmake .
make
make install
```

By default, the apache and postgres modules won't build, just in case you don't have the headers for them. To enable Apache, add `-DWITH_APACHE=on`. For postgres, add `-DWITH_POSTGRES=on`.

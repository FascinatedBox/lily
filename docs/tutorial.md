Lily Tutorial
=============

This tutorial provides a brief overview of the following:

* [Install](#install)
* [Modes](#modes)
* [Comments](#comments)
* [Literals](#literals)
* [Variables](#variables)
* [Blocks](#blocks)
* [Functions](#functions)
* [Classes](#classes)
* [Generics](#generics)
* [Lambdas](#lambdas)
* [Enums](#enums)
* [Import](#import)

### Install

To build Lily, you'll need `cmake` at version 3.0.0+ and a modern C compiler.
Lily don't have any dependencies, so all you need to do is:

```
cmake .
make
make install
```

You'll get a `lily` executable.
Create a file named `hello.lily` with the following:

```
print("Hello World!")
```

Use `lily hello.lily` to execute the file and see the famous phrase.
Create another file, called `template.lily`

```
<?lily print("Hello World!") ?>
```

Use `lily -t template.lily` to execute this file.

### Modes

The language supports being run in two different modes:

* In **standalone** mode, all content is code.
* In **template** mode, code is between `<?lily ... ?>` tags.

Regardless of how the first file is loaded, subsequent imports always operate in
**standalone** mode. This allows code files to be dropped alongside your
template files. The interpreter requires **template** mode files to have a
`<?lily ... ?>` at the very start of the first file (even if it's empty). That
restriction prevents code-only files from accidentally being rendered.

### Comments

Comments come in two flavors:

```
# Single-line comment

#[ Multi
   line
   comment ]#
```

### Literals

A handful of literals are included too:

```
true         # Boolean
10t          # Byte
0xFFt        # Byte (hex)
0b10         # Integer (binary)
123          # Integer
0c80         # Integer (octal)
0xff         # Integer
1.5          # Double
1e1          # Double (exponent)
"hello"      # String (internally \0 terminated, utf-8 clean)
B"\0Hello\0" # ByteString (a bunch of bytes).
[1, 2, 3]    # List[Integer]
[1 => "a"]   # Hash[Integer, String]
<[1, 2.0]>   # Tuple[Integer, Double]
```

Lily's primitive built-in types are simple and close to the machine:

* `Double` is a C `double`.

* `Integer` is a C `int64_t`.

* `String` and `ByteString` are boxed `char *` values with a length included.

* `Byte` and `Boolean` are ints as well, but with the interpreter providing
  a guarantee that they won't exceed their implied limits. 

Lily does not have an unsigned integral type built into it, nor does it have an
arbitrary precision (bignum) type. At this time, there are no plans to include
either in the built-in library.

### Variables

Variables must be declared before use, and cannot be NULL/nil.

```
var decl = [1, 2, 3]     # List[Integer]
decl = []                # OK
# var bad_decl           # Syntax error: No initial value for 'w'

var decla = 1, declb = 2 # OK

class Point
{
    public var @x = 10          # Class properties use @<name>
    public var y = 10           # Invalid
}
```

The body of a class is also its constructor.
Within a constructor, `@<name>` is used to make class property access explicit
and intentional.

### Blocks

Lily uses curly braces to denote the start/end of a multi-line blocks.
For most condition blocks, you can omit the braces if each branch covers only a
single expression. Unlike most curly brace languages, only one set of braces is
needed for the entire block. This results in a feel similar to Python, though
Lily does **not** treat whitespace as significant.
Here are some blocks:

```
var over_18 = true
var age = 16

if age > 18: {
    print("Over 18")
else:
    print("Not over 18")
    over_18 = false
}

var loop_i = 10

while loop_i > 0:
    loop_i -= 1

do:
    loop_i += 1
while loop_i == 1

for loop_i in 0...5:
    print(loop_i)

print($"loop_i after for is ^(loop_i)")

for i in 0...5 by 2:
    print(i)

scoped enum Color { Blue, Red, Green }

define is_red(c: Color): Boolean
{
    match c: {
        case Color.Red:
            return true
        case Color.Blue:
        case Color.Green:
            return false
    }
}

try:
    1 / 0
except Exception:
    print("Caught the division.")
except DivisionByZeroError:
    print("Shouldn't reach here.")
```

### Functions

The `define` keyword creates new function definitions.
Here are some basic functions and their corresponding types.

```
# Function(Integer, Integer => Integer)
define add(a: Integer, b: Integer): Integer { return a + b }
define multiply(a: Integer, b: Integer): Integer { return a * b }

# Function( => Integer)
define return_ten: Integer { return 10 }

# Function()
define no_op {}
```

Functions are first-class values, and can be used like so:

```
# Functions are first-class:
var math_ops = ["+" => add, "*" => multiply]
math_ops["+"](10, 20) # 30

# Function( => Function())
define return_no_op: Function() { return no_op }

return_no_op()()
```

Adding `...` to the end of a type denotes that the function can receive a
variable number of arguments of that type. The function receives the arguments
as a `List` of the type provided. If no arguments were passed, the `List` will
be empty.

```
# Function(Integer...) => Integer
define sum(numbers: Integer...): Integer
{
    var total = 0
    numbers.each(|e| total += e )
    return total
}

# sum() # 0
# sum(1, 2, 3) # 6
```

Adding `*` before a type, then `= <value>` after it denotes that the parameter
is optional. Optional arguments may be a simple value, or an expression.
Required arguments must not come after an optional argument.

The expressions of optional arguments, if run, are always run from left to
right. As a result, it's permissible for a parameter to depend on another to the
left of it.

```
# Function(*Integer => Integer)
define optarg(a: *Integer = 10): Integer { return a + 10 }

optarg(100) # 110
optarg() # 20

# Function(*Integer, *Integer, *Integer => Integer)
define my_slice(source: List[Integer],
                start: *Integer = 0,
                end: *Integer = source.size()): List[Integer]
{
    return source.slice(start, end)
}

my_slice([1, 2, 3], 1)    # [2, 3]
my_slice([4, 5, 6], 0, 1) # [4]
```

The calling function runs the optional argument expressions each time they are
needed. As a result, each invocation will receive fresh versions of a default
argument that do **not** carry over into the next invocation.

```
# Function(*List[Integer] => List[Integer])
define optarg_list(x: *List[Integer] = []): List[Integer]
{
    x.push(x.size())
    return x
}

optarg_list()             # [0]
optarg_list([1, 2])       # [1, 2, 2]
optarg_list()             # [0]
optarg_list([10, 20, 30]) # [10, 20, 30, 3]
```

Mixing variable and optional arguments is permissible. By default, the vararg
parameter receives an empty `List` if no values are passed. Mixing these two
features allows a different default value:

```
# Function(Integer, *Integer, *Integer... => Integer)
define optarg_sum(a: Integer,
                  b: *Integer = 10,
                  args: *Integer... = [20, 30]): Integer
{
    var total = a + b

    for i in 0...args.size() - 1:
        total += args[i]

    print(total)
    return total
}

optarg_sum(5)              # 65
optarg_sum(5, 20)          # 75
optarg_sum(10, 20, 30, 40) # 100
```

Placing `:<name>` before the name of a parameter will allow the function to be
called using keyword arguments. Keyword arguments allow calling a function with
arguments in a different order than the function's parameters. The function can
then be called either with positional arguments or keyword arguments.

```
# Function(Integer, Integer, Integer => Integer)
define simple_keyarg(:first x: Integer,
                     :second y: Integer,
                     :third z: Integer): List[Integer]
{
    return [a, b, c]
}

simple_keyarg(1, 2, 3)                         # [1, 2, 3]

simple_keyarg(1, :second 2, :third 3)          # [1, 2, 3]

simple_keyarg(:third 30, :first 10, :second 5) # [10, 5, 30]
```

It isn't necessary to name all arguments:

```
# Function(Integer, Integer)
define tail_keyarg(x: Integer, :y y: Integer) {}

tail_keyarg(10, 20)

tail_keyarg(10, :y 20)
```

Calling a function with keyword arguments has some restrictions:

```
# simple_keyarg(:first 1, 2, 3)
# Syntax error: Positional argument after keyword argument

# simple_keyarg(1, :first 1, 2, 3)
# Syntax error: Duplicate value provided to the first argument.

# simple_keyarg(1, 2, 3, :asdf 1)
# Syntax error: 'asdf' isn't a valid keyword.
```

Keyword arguments are evaluated and contribute to type inference in the order
that they're provided:

```
var keyorder_list: List[Integer] = []

define keyorder_bump(value: Integer): Integer
{
    keyorder_list.push(value)
    return value
}

define keyorder_check(:first  x: Integer,
                      :second y: Integer,
                      :third  z: Integer): List[Integer]
{
    return keyorder_list
}

keyorder_check(:second keyorder_bump(2),
               :first  keyorder_bump(1),
               :third  keyorder_bump(3))
               # [2, 1, 3]
```

A function call with keyword arguments is verified at parse-time. The vm does
not understand keyword arguments, and the type system does not carry keyword
information either.

Despite the above, keyword arguments can be mixed together with optional
arguments and variable arguments:

```
define optkey(:x x: *Integer = 10,
              :y y: *Integer = 20): Integer
{
    return x + y
}

optkey()        # 30
optkey(50, 60)  # 110
optkey(:y 170)  # 180
optkey(4, :y 7) # 11

define varkey(:format fmt: String,
              :arg args: *String...=["a", "b", "c"]): List[String]
{
    args.unshift(fmt)
    return args
}

varkey("fmt")                # ["fmt", "a", "b", "c"]
varkey("fmt", "1", :arg "2") # ["fmt", "1", "2"]
```

### Classes

Classes in Lily are intentionally limited to single inheritance. Class
properties must have their scope specified as `private`, `protected`, or
`public`. Since class members are accessed uniformly through `.` outside of a
class, the members must all be unique.

Similar to Ruby, class vars are required to have `@` as a prefix.

```
class Point(x: Integer, y: Integer)
{
    public var @x = x
    public var @y = y

    public define add(other: Point): self {
        @x += other.x
        @y += other.y
    }
}

class SimplerPoint(var @x: Integer, var @y: Integer) {}

class Point3D(x: Integer, y: Integer, z: Integer) < Point(x, y)
{
    public var @z = z
}
```

By default, all class methods take an implicit `self` as their first argument.
However, the `static` qualifier allows creation of methods that don't take that
implicit argument. Finally, the `static` qualifier, if it exists, must always
follow the scope qualifier (ex: `static public` is invalid).

```
class Utils {
    public static define square(x: Integer) { return x * x }
}

var v = Utils.square(10) # 100
```

### Generics

In Lily, classes, enums, and functions can make use of generics. Currently,
generics are quite limited: They must be a single capital letter, must be
declared before use, and there are no traits or qualifiers. All of those issues
will be addressed in a future version of the language. Even with those
restrictions, generics are quite useful.

```
define transform[A, B](input: A, fn: Function(A => B)): B
{
    return input |> fn
}

transform("10", String.parse_i) # Some(10)

define ascending(a: Integer, b: Integer): Boolean { return a > b }
define descending(a: Integer, b: Integer): Boolean { return a < b }

define sort_inplace[A](lst: List[A], cmp: Function(A, A => Boolean)): List[A]
{
    for i in 0...input.size() - 1: {
        var j = i
        while j > 0 &amp;&amp; cmp(input[j - 1], input[j]): {
            var temp = input[j]
            input[j] = input[j - 1]
            input[j - 1] = temp
            j -= 1
        }
    }
    return input
}

sort_inplace([1, 3, 5, 2, 4], ascending)  # [1, 2, 3, 4, 5]
sort_inplace([1, 3, 2],       descending) # [3, 2, 1]
```

### Lambdas

Sometimes you need a one-line or small function but there's no need to formally
declare it for later. Take the above example with the definitions of ascending
and descending sort functions. Lambdas are written as `(|<args>| ... )`.

```
sort_inplace([1, 3, 5, 2, 4], (|a, b| a > b))  # [1, 2, 3, 4, 5]
sort_inplace([1, 3, 2],       (|a, b| a < b))  # [3, 2, 1]

transform("A", (|a| a.parse_i().unwrap_or(10)) # 10

var lambda_list = [1, 2, 3]

lambda_list.map((|e| e + 1)) # [2, 3, 4]

# Since the only argument is a lambda, one set of parentheses can be dropped:

lambda_list.map(|e| e + 1) # [2, 3, 4]
```

### Enums

Enums are a datatype composed of a fixed set of variant classes. Variant classes
can be empty (taking no values), or take as many values as they wish.

```
enum Rgb {
    Red,
    Green,
    Blue

#[
    define bad_is_red: Boolean {
        match self: {
            case Rgb.Red: return true
            # Syntax error: Match is not exhaustive
        }
    }
]#
    define is_blue: Boolean {
        match self: {
            # case Blue:  return false
            # Syntax error: Use Rgb.Blue since Rgb is scoped.

            case Rgb.Red: return true
            else:         return false
        }
    }
}
```

Lily has two predefined enums, `Option` and `Result`, that better show what
enums are capable of.

```
enum Option[A] {
    Some(A),
    None
}

enum Result[A, B] {
    Failure(A),
    Success(B)
}
```

Functions such as String.parse_i return `None` instead of raising an exception.
Database queries for example, can make use of `Result` by returning either
`Success` or a `Failure` with an error to describe the problem.

Sometimes `match` encounters variants that takes values, and needs to decompose
the results. The decomposition must target names that aren't currently being
used.

```
define print_if_some[A](opt: Option[A])
{
    # Simpler version: opt.and_then(print)
    match opt: {
        case Some(s):
            print(s)
        else:
    }
}
```

You can use `_` in place of a name if you're not interested in the result:

```
var empty_match_example = None

match empty_match_example: {
    case Some(_):
    else:
}
```

If the `scoped` qualifier is used before an enum, then each variant usage must
be qualified by the name of the enum:

```
scoped enum ScopedColor { Red, Blue, Green }

var v = ScopedColor.Red

define f(a: *ScopedColor=ScopedColor.Red) { ... }

match v: {
    case ScopedColor.Red:
        ...
    else:
        ...
}
```

### Import

Lily's import system borrows a lot of ideas from Python.

```
# fib.lily

define fib(n: Integer): Integer
{
    if n < 2:
        return n
    else:
        return fib(n - 1) + fib(n - 2)
}

# somedir/point.lily

class Point(var @x: Integer, var @y: Integer)
{
    public define stringified: String {
        return "Point({0}, {1})".format(@x, @y)
    }
}

# first.lily

import fib
import sys as q, time
import somedir/point

fib.fib(5)
print(q.argv)
print(point.Point(10, 20).stringified())
print(time.Time.now())
```

Namespaces are often useful for making the source of a symbol clear and also to
prevent conflicts. But there are times when namespaces can feel like they're in
the way. In the above code, the user is forced to repeat `fib.fib` and
`time.Time`. To avoid that, Lily also provides direct imports of symbols from a
module like so:

```
# fib.lily

define fib(n: Integer): Integer { ... }

# example.lily

class Example { ... }

define example_fn { ... }

# start.lily

import (fib) fib
import (Example, example_fn) example
import (Time) time

print(fib(5))
print(Example())
print(Time.now())
# print(time.Time.now()) # Invalid: 'time' not loaded.
```

The above provides access to the symbols inside of the modules. However, the
modules themselves are not made available. This allows the function fib to take
the place of module fib despite Lily normally preventing duplicate names.

It should be noted that there is no intention of supporting wildcards with
imports (ex: no `import (*) from x`). In doing so, one does not have to refer to
another file to discover the symbols that have been added.

Regardless of the import method used, a module's toplevel code will only execute
the first time it is loaded.

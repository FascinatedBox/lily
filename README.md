The Lily Programming Language
=====

[![Gitter](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/jesserayadkins/lily?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

Lily is an interpreted, garbage-collected language that's a bit different than the rest:

**Static typing that doesn't get in the way:**
Every variable's type is fixed to what it's first assigned to, and all variables must have a starting value.
```
var some_int = 10        # Type: integer
var my_str = "asdf"      # Type: string
var a_list = [1, 2, 3]   # Type: list[integer]

a_list.append(some_int)  # Valid

a_list.append(my_str)    # Invalid
```

**Encourages composability and chaining with a fluid syntax:**
```
# You can call a method directly on a value...
[1].append(2)

# which is just syntatic sugar for this:
list::append([1], 4)

# Function piping is another option:
"    abc    " |> string::trim

define make_list(x: integer, y: integer => list[string])
{
    return [x, y]
}

make_list(10, 20).append(30)
```

**Includes parametric polymorphism (Rank-1)**
```
#[ The square braces denote a generic types, from A to Z.
   When apply is invoked, A must always be the same thing. ]#
define apply[A](input: A, f: function(A => A) => A) {
    return f(input)
}
define add_ten(x: integer) { return x + 10 }

# This is valid, because whenever A is needed, integer is given.
apply(10, add_ten)

# This is not: A is first list[integer], then integer.
apply([1], add_ten)

# You an expand upon this as such:

# This function will transform the input from type A, to type B.
define transform[A, B](input: A, f: function(A => B) => B)
{
    return f(input)
}

# integer::to_s has the type 'function(integer => string)'
# A is solved as integer, B as string.
# This is valid.
transform(10, 10.to_s)

# Lily supports lambdas, and lambdas participate in type inference.
# Here, the lambda has one var of type A. Type A = string in this case.
# 'trim' is a valid string method, and is allowed.
# The lambda return has the type list[string]. This becomes type B.
transform("10", {|a| [a.trim()]}
```

**Includes familiar OO features**
```
# The body of this class declaration becomes a function called 'new'.
# This is used as the class constructor.
# Class variables, like normal variables, must always have a starting value.
class Point(x: integer, y: integer)
{
    # Class vars are denoted by an @ prefix.
    public var @x = x
    protected var @y = y

    # 'self' is implicitly added to class methods.
    define copy( => Point) {
        return Point::new(@x, @y)
    }
}

var p = Point::new(1, 2)
p.x = 3 # Valid
p.y = 4 # Invalid: @y is protected within Point.
```

**Namespaced importing**

```
# (file: fib.lly)

define fib(n: integer => integer)
{
    if n < 2:
        return n
    else:
        return fib(n - 2) + fib(n - 1)
}

print("Hello")

# (file: start.lly)
import fib

fib::fib(10)

# The 'print' statement will only be run on the first import.
# Furthermore, access to everything within 'fib.lly' is gated off through 'fib'.
```

Getting Started
=====

For this, you'll need a **C11** compiler (gcc or clang will do) and CMake. That's it. Grab a copy of the source, enter the root directory, and do this:
```
cmake .
make
make install
```

This will build Lily without support for Apache or Postgres. To enable those, see the toplevel CMakeLists.txt for instructions.

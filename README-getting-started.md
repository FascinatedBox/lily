## Getting started ##
Getting Started

To build Lily, you will need
* A version of gcc with C11 support.
* autoconf
* automake

To build Lily (without the apache mod), follow these steps:

```
./autogen.sh
./configure
cd src/
make
make install
```

To build Lily with the apache mod, change the second line above to:

```
./configure --with-apache-mod
```

### The basics ###
Lily is a statically-typed language, and every variable is required to have a type of some sort. Additionally, Lily requires all variables to be assigned a value when they are declared to prevent issues with values being undefined.

```
# This is a comment. The rest of the line is ignored.

# The integer type is backed by a 64-bit signed int.
integer a = 10


# Double wraps around a C double.
double b = 10.0



# Strings support the common single-character escape codes and are also immutable.

# This is a single-line string.
string single_line = "abc\tdef"

# This is a multi-line string.
string multi_line = """This
is
neat"""

### This is a comment block. It ends with this: ###
string multi_line_two = ### Isn't consistency ### "really nice?"


# The any type can hold any kind of value.
any hold_anything = 10
hold_anything = 20.5
hold_anything = "55"

# A typecast can be used to obtain the type of an 'any'.
string contents_of_any = hold_anything.@(string)


# A list is a container of a particular kind of type.
list[integer] integer_list = [1, 2, 3]

# Lists can contain any kind of valid type:
list[list[integer]] string_matrix = [["1", "2", "3"], ["4", "5", "6"], ["7", "8", "9"]]

# Lists can also be empty
list[integer] empty_list = []

# ...or contain empty lists.
list[list[string]] empty_matrix = [[]]

# If a list has varying types, then the elements are converted to any.
list[any] any_list = [1, 2.2, "3"]

# They're also converted to any if an any is wanted.
list[any] another_any_list = [1, 2, 3]



# A hash is a container that takes two types: A key (the first), and a value (the second).
# The key must be an integer, double, or string.
hash[integer, string] integer_to_str = [1 => "1", 2 => "2"]

# Values are converted to any if an any is wanted...
hash[integer, any] varying_values = [1 => [1], 2 => [1 => 1]]

# ...or if an any is wanted.
hash[integer, any] any_autocast = [1 => 1, 2 => 2]

# For duplicate values, the right-most value 'wins'.
hash[integer, integer] right_wins = [1 => 1, 1 => 2, 2 => 3, 2 => 4]
# Result: 1 => 2, 2 => 4

# Empty hashes are fine too.
hash[integer, integer] empty_hash = []



# A tuple can contain any number of varying types.
# <[ starts a tuple, and ]> closes it.
tuple[string, integer] trace_record = <["test.ly", 14]>
# Tuple subscripts must be literals so the interpreter can infer the type.
string s = trace_record[0] # Valid
integer index = 0
s = trace_record[index] # Invalid: index is not a literal.


# A function is a callable body of code. There are two kinds of functions:
# A foreign function is implemented behind the scenes in some other language like C.
# A native function is one defined in Lily code.
# The interpreter takes care of this distinction for you.
# The function keyword is also used to declare new functions.

# This function does not take arguments, and does not return a value.
function first_function() {
    # Code goes here.
}

# This function takes one parameter, and returns nothing.
function second_function(integer a) {
    print("Hi there!\n")
}

# No inputs, but returns an integer.
function third_function( => integer) {
    return 10
}

integer get_result = third_function()

# The ... means that this function takes a variable number of arguments.
# Inside the function, the extra arguments are available in 'ints'.
function varargs(list[integer] ints...) {

}

# ints = [1]
varargs(1)

# ints = [1, 2, 3]
varargs(1, 2, 3)

# A function can also take or return functions.
function return_third( => function( => integer)) {
     return third_function
}

# The result of a function can be used in place:
integer cool = return_third()()

# Finally, functions can nest and scope out.
# Unfortunately, Lily does not support making functions closures (yet!)
function outer_func( => function(integer => integer) ) {
    function inner_func(integer a => integer) {
         return a * a
    }

    return inner_func
}
outer_func()(10) # Result: 20
```

### Show ###

One of Lily's unique features is a function called 'show'. This function takes one input of any type and shows a detailed version of the contents. It's able to traverse any list, any hash, shows what's inside of an 'any', and much more.

Try these examples:

```
show(10)
show("11")
show([[1, 2, 3], [4, 5, 6], [7, 8, 9]])
```

### Blocks ###

No language is complete without conditional evaluation. Within Lily, there are two kinds of blocks:
* A single-line block contains either a single expression, or a single-line block within itself.
* A multi-line block contains multiple expressions within itself.
* Both kinds of blocks can contain single-line blocks, but only multi-line blocks can hold multi-line blocks.
* For conditions, integers that are not 0, doubles that are not 0.0, non-empty strings, and non-empty lists are considered 'truthy'.

```
function take_integer(integer input) {
    # Each condition expects an expression or either elif/else afterward...
    if input < 10:
        print("input is less than 10.")
    elif a > 10:
        print("input is more than 10.")
    else:
        print("input is 10, exactly.")
}

function multi_line_example(integer input => integer) {
    integer result = input

    # The { after the if means that this is a multi-line block.
    # Each case is multiple lines, and the entire thing ends with the single }.
    # Fewer {} means fewer problems with {}.
    if input < 10: {
        result -= 1
        print("The result has been decreased!")
    elif input > 10:
        result += 1
        print("The result has been increased!")
    else:
        print("Nothing happened to the result!")
    }

    return input
}

# Multi-line while block
integer i = 10
while i != 0: {
    i -= 1
}

# This is an empty for loop. Only integer ranges are supported right now.
# Since i has been declared already, it's used as the range.
for i in 1...10: { }

# A step is also possible.
for i in 1...5 by 2: { }

# If the variable named doesn't exist, it's created.
for j in 1...10: {
    # j exists as an integer here...
}

i = j # Invalid: j has fallen out of scope.
i = 10

# do...while is also supported:
do: {
    i -= 1
} while i > 0:
```

### Classes ###

Lily allows the user to define custom classes. Inheritance for user-declared classes is not currently supported, but a single-inheritance model will be available in future versions of the language. There are some restrictions on classes:
* Class names must not be a single letter (why will be explained a bit later).
* Class properties cannot have the same name as a class method.
* Classes may only be declared within other classes, and the inner class will 'scope out'.

```
# This creates a function called Point::new(integer, integer => Point)
# x and y are not automatically properties.
# All class properties and class functions are automatically considered 'public'.
# For now, there are no private/protected type properties.
class Point(integer x, integer y) {
    # Everything in here is part of the class constructor.
    # This is also where class properties are declared.

    integer @x = x # The @ sigil denotes a class property.
    integer @y = y

    # Self is implicitly a part of this method.
    function addPoint(Point other) {
        # There is an implicit self which these @-marked properties target.
        # Since 'other' is not the self, the properties are accessed using a dot.
        @x = other.x
        @y = other.y
    }
}

# It is now possible to create instances of a Point.
Point p = Point::new(10, 20)

# Class methods are accessed using dot, as well as properties, for simplicity.
# This is also why properties can't have the name name as a class method.
p.addPoint(p) # @x = 20, @y = 40

# This will show that @x = 20 and @y = 40, instead of the address of the instance.
# Note that this is done without defining anything within the class!
show(p)

```

### Generics ###

Sometimes it would be nice to have a class that takes a generic kind of type and returns that type. Or a function that can loop over a list of any kind. This is where generics come in. First, an example:

```
function make_a_list[A](A input => list[A]) {
    return [input]
}
```
A few things are going on here:
* The ```[A]``` after the name of the function means that it takes a generic type called 'A'. In Lily, for simplicity, generics must start from A and go to Z (so no custom names of generic types). The language maker thought that this would lead to simpler generics both for users and internally.
* Within a generic function, the first thing that matches a generic type is what Lily expects all future instances of that type to be.
* Parametric polymorphism is awesome. Here are some more examples.

```
function two_values[A](A first, A second) {
}

two_values(1, 2) # Correct: A is integer in both cases.
two_values(1, "two") # Invalid: A first 'seen' as integer, then as a string.

# However...
any some_any = 10
two_values(some_any, [10])
# This is valid, because A is seen as any. [10] (a list of integer) is converted to any.


function add_them(integer a, integer b) { return a + b }

# How about something more complicated?
function foldLeft[A](list[A] values, A value, function f(A => A)) {
}

foldLeft([10], 0, add_them)
# This is valid, because in each case that 'A' is wanted, an integer is seen.

foldLeft(["a", "b", "c"], "", string::concat)
# This is also valid:
# The first argument is list[string], so A = string
# "" = string
# string::concat is function(string, string => string)

# Generics can also be paired up with classes:
class GenericPoint[A](A x, A y) {
    A @x = x
    A @y = y
}

# GenericPoint::new is a function this type: function [A](A, A => GenericPoint[A])
# Also, GenericPoint needs to know what type it contains.
GenericPoint[integer] p = GenericPoint::new(1, 2)

# A = any, so parameters are automatically converted to type any here.
GenericPoint[any] any_vals = GenericPoint::new(3, "4")
```

### Exceptions ###

Sometimes the interpreter or the programmer runs into a problem of some nature that needs to be reported upward. For this, the interpreter has defined a series of exceptions.

There are a handful of exceptions that are only raised at parse-time, and as such are not able to be raised or used in code. These are: ```SyntaxError```, ```ImportError```, and ```EncodingError```.

Here are the currently available exceptions:
* NoMemoryError: The interpreter ran out of memory during an operation.
* DivisionByZeroError: Attempt to divide or modulo by zero.
* BadTypecastError: Attempt to typecast an 'any' to a type that it does not contain.
* IndexError: Attempt to access an index on a list (set / get) that does not exist.
* NoReturnError: A function that specified it returned a value did not.
* ValueError: The 'by' of a for loop evaluated to zero.
* RecursionError: Attempt to recurse too deeply. This is to prevent infinite recursion.
* KeyError: Attempt to get a value from a hash that does not exist. (Attempts to set work, however).
* FormatError: string::format was given too few or too many arguments.

Additionally, all exceptions inherit from Exception, which inherits from ExceptionBase.

```
try: {
    integer a = 1 / 0
except Exception:
    # It's caught here, because DivisionByZeroError inherits Exception.
    print("Can't divide by zero!")
except DivisionByZeroError:
    # This will not be reached.
    # Additionally, this is invalid because a has fallen out of scope.
    a = 0
}

# Nested try+catch works too:
try: {
    try: {
        ValueError v = ValueError::new("Oh noes!")
        raise(v)
    except IndexError:
        # Won't happen.
    }
except ValueError as e:
    printfmt("The message is: %s!", e.message)
}

```

### Enum classes and variants ###

Lily does not allow variables without an assignment, so all values need to have some sort of value. However, there are situations where a value isn't yet known, or a value is complex to compute. For those situations, Lily provides an enum class type. An enum class is a container that can hold one varying kind of value. Here's an example in action:

```
enum class Option[A] {
    Some(A),
    None
}
```

Option is a class that takes one type, and is either a Some of that kind of type, or a non-existent None value. Continuing on...

```
Option[integer] maybe_integer = Some(10)
maybe_integer = None

Option[list[integer]] maybe_list = Some([1])
maybe_list = None

function f[A](list[Option[A]] values...) {
}

# If the interpreter has a list of some variant types, it attempts to find a common type.
f(Some(10)) # Valid: Option[integer] is the type of the list.
f(None) # Valid: Option[any] is the type, since nothing in particular is wanted.
f(Some(10), Some("10")) # Invalid: A is integer, then string.
f(Some(10.@(any)), Some("10")) # Valid: A = any, string => any conversion.
f(Some(10), Some(20), None) # Valid: Option[integer] again.

# If it can't, then everything is put into an 'any'.
# The first is an any containing Option[integer]
# The second is an any containing Option[string]
# The last is an any containing Option[any]
list[any] variant_list = [Some(10), Some("10"), None]
```

It's also possible for the variant members to be scoped, so that ```Option::Some``` and ```Option::None``` would be used in place of a plain ```Some``` or ```None```. To do this...

```
enum class Option[A] {
    ::Some(A),
    ::None
}
```

If the ```::``` marker is seen before the first variant type, then every variant type should have that as a prefix. This was chosen because it seemed like a good way to determine that something is scoped as code is read downward. Additionally, it was also inspired by Rust making scoped variants.

A 'match' block is used to switch on the values that might be within an option. A match block is unique:
* The input to a match block must be an enum class value. This will likely be changed in the future.
* A match block must always be a multi-line block.
* A match block must be exhaustive (all cases must be checked).
* match 'cases' are the same regardless of if the variants are scoped or not.

```
enum class Option[A] { Some(A), None }

Option[string] opt = Some("test")
match opt: {
    # Decomposition is required.
    case Some(s):
        printfmt("opt contains '%s'.", s)
    # Empty cases are allowed.
    case None:
}

```

Additionally, the contents of a enum class can be dissected with 'show'.

### Lambdas ###

Lambdas are useful when a function wants another function as an argument. Sometimes the function that's needed only needs to do a simple job, so creating a named function does not make sense.

A lambda begins with ```{|``` and ends with ```}```. Arguments for a lambda must not have types,
and are ended with a ```|```. Here are some examples:

```
function merge_values(
    integer a,
    integer b,
    function f(integer, integer => integer) => integer) {

    return f(a, b)
}

add_values(10, 11, {|first, second| first + second})
# In this situation, the lambda infers that it has two integers.
# The lambda returns a single expression: Whatever is between | and }.

# 'var' is a keyword that allows creating a variable, with an inferred type.
# Since the lambda cannot infer argument types, it takes no arguments.
# v's type is 'function ( => integer)'
var v = {|| 10}

var fail = { || 10}
# This fails because the opening token MUST be {|, not { ... |

# Lambdas also work with generics:
function lambda_gen[A, B](A value, function f(A => B) => B) {
    return f(value)
}

lambda_gen(10, {|integer_val| "10"}
# Valid: A = integer, so integer_val = integer.
# Result: B, which is inferred from the lambda's result
# B = string

lambda_gen([10], {|list_val| list_val.size()}
# Valid! A = list[integer]
# B = integer (because list::size returns an integer).

lambda_gen(
{|| {|| 10}},
{|lambda_val| lambda_val() }
# This too is correct!
# A = function( function( => integer) )
# The lambda value is called, and the integer result returned.
# B = integer
```

### Contributing ###

The language isn't complete yet. It's missing a lot of important things: Importing of files, class inheritance, traits, namespaces, a mysql wrapper, and a truly useful standard library.

Interested in helping out? Here's how:
* Write better documentation than the above.
* Write more tests.
* Tell other people about the language.
* Offer to take on one of the bugs in the current list of issues (or add some more ones).
* Other?

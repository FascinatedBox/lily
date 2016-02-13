Enum Classes
============

An enum class is a special type that allows only specified variant types inside of it. 

```
enum TerminalColor {
    Black
    Blue
    Cyan
    Green
    Magenta
    Red
    White
    Yellow
}
```

This creates a new enum called TerminalColor. The only values that can be assigned to TerminalColor are the members listed within the curly braces. Lily calls these members variants. Here's a couple examples of this in action:

```
var current_color = Black

define change_color(new_color: TerminalColor)
{
    current_color = new_color
}

change_color(Blue)

# Variants can be compared...
if current_color == Blue:
    print("It's blue now.\n")

# Invalid: Variants are not implicitly truthy or falsey
# if Blue: ...

# Type: List[TerminalColor]
var my_colors = [Blue, Red, White]

# Invalid: current_color has type TerminalColor
# current_color = 10
```

You can create variables that are of TerminalColor's type, but the individual colors themselves are not valid types. Instead, they should be looked at more as values. Enums also introduce a new kind of block, called `match`. It's used like this:

```
define name_for_color(t: TerminalColor) : string
{
    match t: {
        case Black:
            return "Black"
        case Blue:
            return "Blue"
        case Cyan:
            return "Cyan"
        case Magenta:
            return "Magenta"
        case Red:
            return "Red"
        case White:
            return "White"
        case Yellow:
            return "Yellow"
    }
}

printfmt("The name for this color is '%s'.\n", name_for_color(Blue))
```

`match` is an unusual block, in that it requires braces. As such, every `match` block is always a multi-line one. This will be fixed in a future release. Try to run the above, and you'll get a message like this:

```
SyntaxError: Match pattern not exhaustive. The following case(s) are missing:
* Green
```

This is arguably the best part of `match`: It always makes sure that all cases are covered. Here it is, again, with Green included.

```
define name_for_color(t: TerminalColor) : string
{
    match t: {
        case Black:
            return "Black"
        case Blue:
            return "Blue"
        case Cyan:
            return "Cyan"
        case Green:
            return "Green"
        case Magenta:
            return "Magenta"
        case Red:
            return "Red"
        case White:
            return "White"
        case Yellow:
            return "Yellow"
    }
}

printfmt("The name for this color is '%s'.\n", name_for_color(Blue))

```

But...it makes more sense for this to be a method that I can call on a TerminalColor, rather than an outside function. It's not possible to extend TerminalColor after it's already been declared. TerminalColor can instead be declared as such

```
enum TerminalColor {
    Black
    Blue
    Cyan
    Green
    Magenta
    Red
    White
    Yellow

    define name_for_color : string {
        match self: {
            ...
        }
    }
}

var v = Black
printfmt("The name for the color is '%s'.\n", v)
```

Do note, however, that an enum cannot have variables or expressions within the curly braces. Lily also requires that there are no extra enums declared after functions are declared.

# Option

Let's go to something that's more useful. You may recall that Lily is a language that requires that all vars have a starting value. However, this is not always practical. Perhaps you've got a class which will open up a connection only when it's explicitly called for. Lily defines a built-in enum called `Option`. It looks like this:

```
enum Option[A] {
    Some(A)
    None
}
```

Values of type Option can only be assigned to a Some that has the same subtype, or None. Since the None is not qualified by a subtype, any Option can be assigned to a None.

```
# Type: Option[Integer]
var my_opt = Some(1)

my_opt = None

# Type: List[Option[Integer]]
var nested_option = [Some(1), None, Some(2)]

nested_option[0] = None

# Type: List[Option[Dynamic]]
var opt_list = [None, None, None]

# Invalid: Option cannot be assigned to an Integer
# my_opt = 10
```

`match` works here too. However, there's a slight difference:

```
define get_or_else[A](opt: Option[A], or_else: A) : A
{
    match opt: {
        case Some(s):
            return s
        case None:
            return or_else
    }
}

get_or_else(Some(10), 10)
get_or_else(None, 1)
get_or_else(None, [1, 2, 3])
```

Since Some has a value inside of that, `match` requires that a parameter be named for that value. Lily knows what the types are for enums and their variants, and infers 's' to have the type 'A' (exactly what is to be returned). Lily requires that if a variant has values inside, that a variable is listed for each entry.

```
var v = Some(10)

match v: {
    # Invalid: Some takes 1 value
    case Some: 
        ...
    # Invalid: None does not take values
    case None(n)
}
```

By combining generics and enums, you can do some neat things:

```
enum Option[A] {
    Some(A)
    None

    define map[A, B](f: Function(A => B)) : Option[B]
    {
        match self: {
            case Some(s):
                return Some(f(s))
            case None:
                return None
        }
    }
}

var v = Some(10)

printfmt("Changed the value to '%s'.\n", v.map(Integer::to_s))
```

Extending Lily
==============

This guide covers how to extend Lily within C using `dyna_tools.py`. It covers
how to build classes, methods, toplevel functions, vars, and more. One caveat,
however, is that this guide focuses more on how to get started, and less on the
backend API.

# How loading works

Let's take a minute to talk about how modules are loaded. When your module is
loaded, it does **not** have a setup or teardown function. Instead, your module
provides a dynaload table that describes the contents of the module. Presenting
Lily with a dynaload table allows it to pick and choose what it wants to load,
instead of eagerly loading contents that may not be useful.

Each time you add or remove Lily items from your module, you'll need to re-run
`dyna_tools.py refresh <filename>`. This will generate `dyna_<filename>.h` and
possibly `extras_<filename>.h` in the same directory that `<filename>` is in if
they need to be built. If they are generated, but not within your `.c` file,
then they're automatically added to the end of your `.c` file.

# Dynaload and vm interaction

Lily is designed to be embedded, so writing to global variables is forbidden. At
the same time, Lily also holds a class id as part of every value to denote what
class it belongs to. But if Lily can load classes at any time, how do foreign
functions know what class id to give back to Lily?

The dynaload table that is generated also includes a listing of the classes that
the module loads. Right now, for example, postgres has 2: `Conn` and `Result`.
The interpreter sets aside a per-module array with 2 slots, and guarantees that
the array is setup in the same order that the module asked for. `[0]` will have
`Conn` when `Conn` is loaded, and `[1]` will have `Result` when that is loaded.

`dyna_tools.py` includes "extras", so that you won't need to worry about these
underlying mechanics directly.

# Getting started

`dyna_tools.py` works by reading special comments. Each comment starts with a
description line (ex: `package mymath`), a blank line, and then documentation.

```
/**
package mymath

This adds example math routines to Lily.

* add
* subtract
*/
```

It's important that `/**` and the closing `*/` be on their own line. For the
time being, the documentation is parsed as markdown. You can get documentation
for your module through `dyna_tools.py markdown <filename>`.

Your package should also follow Lily naming conventions:

* Classes, enums, and variants are CamelCased.

* Functions, vars, and packages are snake_cased.

# Caveats (to be addressed in the future).

* The input of `dyna_tools.py` is not verified, which will be addressed in a
  future release of the language.

* Custom exception classes cannot be raised.

* Scoped enums aren't possible yet.

* Creating a var requires a loader (that's more an issue of waste).

* 2+ modules in one file is untested, and may not work.

* In Lily, methods implicitly have 'self', and static methods aren't possible.
  Here, `self` must be explicitly required, which allows for static methods.

# Directives

## package

Usage:

```
package <name>

<doc>
```
    
The package directive specifies the name that a Lily script must use to load the
package. Either this, or the `embedded` directive must come before all others.

## embedded

Usage:

```
embedded <name>

<doc>
```

The difference between `embedded` and `package` is the resulting output. The
`package` directive is for shared libraries, whereas `embedded` is for modules
added by a program that also embeds the interpreter.

This directive creates a loader function, which is necessary because Lily won't
be able to use `dlopen/GetProcAddress` to locate module data. This will also
add a `register_<name>` macro to the `dyna_<filename>.h` file. To register your
module to Lily, use it like so:

```
lily_state *state = lily_new_state(lily_new_default_options());
register_<name>(state);
```

No other action is required.

## class

Usage:
```
class <name>

<doc>
```

This directive is used to add a new foreign class to Lily. Foreign classes are
not required to have a constructor, so you may omit it if you wish.

Requirements:
```
typedef struct {
    LILY_FOREIGN_HEADER
    (your contents)
} lily_$PackageName_$ClassName

static void destroy_$ClassName(lily_$PackageName_$ClassName *data)
{
    /* Do any cleanup needed. */

    lily_free(data);
}
```

Bear in mind that the destroy function and the typedef are case-sensitive. For
each class, you will get the following macros in your `extras_` file:

* **ARG_$ClassName**(state, index): Calls `lily_arg_generic(s, index)` and casts
  it to the typedef mentioned above.

* **ID_$ClassName**(state): Fetches the class id for the given class from the
  state's cid table. Only valid during vm execution.

* **INIT_$ClassName**(state, target): Calls `lily_malloc` to initialize `target`
  and prepares the resulting class value.

* **DYNA_$ClassName**(ids): Used when dynaloading vars. This fetches the class
  id from the ids block passed to the var loader. You won't have this generated
  if you don't have a loader (and you also won't need it).

## var

Usage:
```
var <name>: <type>

<doc>
```

This directive adds a new var at the toplevel of your module. Unlike with Lily
declarations, the type after the colon is required. The type must be a valid
Lily type for a var to have.

On the C side, your prototype should look like:

```
static lily_value *load_var_<name>(lily_options *options, uint16_t *ids)
```

If you need your own class or variant ids for your var, then you can pass call
the relevant `DYNA_` macro using `ids`.

## define

Usage:
```
define <name>(<prototype>)<return?>

<doc>
```

This directive adds a toplevel foreign function to the module. The prototype and
the return follow the same rules as a define within Lily, so you'll need to give
names to arguments.

You can specify optional arguments, but there's a catch. The optional value you
specify will be added to the documentation. However, it's your responsibility to
make sure the optional argument is initialized properly at runtime.

Here's an example of what you'll need to do:
```
/**
define add(a: Integer, b: *Integer=10): Integer

Add two numbers together, or use '10' if the second number isn't given.
*/
void lily_$PackageName__add(lily_state *s)
{
    int left = lily_arg_integer(s, 0);
    int right;

    if (lily_arg_count(s) == 2)
        right = lily_arg_integer(s, 1);
    else
        right = 10;

    lily_return_integer(s, left + right);
}
```

The two underscores are needed because the function isn't inside of a class.

If your function receives a variable number of arguments, the arguments are
placed into a list. In the event that no variable arguments were seen, the list
is empty.

It's currently not possible to have both optional and variable arguments for the
same function.

## method

Usage:
```
method <$ClassName>.<name>(<prototype>):<return>

<doc>
```

This directive creates a new method. The `$ClassName` must match with the
class name of the last `native`, `enum`, or `class` directive. Additionally,
`method` directives are assumed to belong to the last `native`, `enum`, or
`class` directive specified. Be careful of order.

Unlike methods that you declare in Lily, the prototype of the `method`
directive does not have an implicit `self`. For the time being, it is necessary
to add `self`, along with the type of self as the first argument, except for
methods that are intended to be static.

Expect that limitation to be removed in the near future, when native static
methods are possible.

## constructor

Usage:
```
constructor <$ClassName>(<prototype>): <return>

<doc>
```

This directive is used to create a constructor for the given class. The
constructor directive should specify that it returns an instance of the class.
A class is limited to at most one constructor, and it is uncertain if that limit
will be adjusted in the future.

Providing a constructor allows class instances to be created using
`Classname(<args>)`. The `native` directive requires this, though it's optional
for a `class` directive.

On the C side, a constructor should be written like:
```
void lily_$PackageName_$ClassName_new(lily_state *s)
{
    ...
}
```

For a `class` directive, you should create a new instance of your class,
initialize it, and return it.

The `native` directive carries with it a few assumptions. While inheritance is
possible, it's assumed that the class will be inheriting from another local
dynaloaded class. Any initialization from lower classes will need to be handled
by your C constructor function. Additionally, there's also the question of if
your constructor is being called directly, or if it's part of inheritance.

`lily_ctor_setup` handles most of the last problem. It will give you either the
working instance, or create a new one, with the result being transparent to you.

Here's one example:
```
/**
constructor ValueError(m: String): ValueError

Constructs a new `ValueError` instance using `m` for the message.
*/
void lily_$PackageName_ValueError_new(lily_state *s)
{
    lily_instance_val *result;
    lily_ctor_setup(s, &result, id, 2);

    lily_instance_set_value(result, 0, lily_arg_value(s, 0));
    lily_instance_set_list(result, 1, lily_new_list(0));
}
```

This example knows that `@message` is first and `@traceback` is second, since
that's the order they're listed in under `Exception`. `lily_ctor_setup` also
handles returning the class, if necessary.

## native

Usage:
```
class <name> [generics]? [> class]?
    property1?
    property2?
    ...?

<doc>
```

This directive adds a native class to the module. If you'd like your class to be
a container, then you can add generics. A base class can also be selected.
However, both features are optional.

Here are some examples:

```
/**
native Tainted[A]
    private var @value: A

...
*/

/**
native ValueError < Exception

...
*/

/**
native Exception

*/
```

When it comes to property declaration, the syntax is similar to what you write
in Lily code:

```
    private var @<name>: <type>
    protected var @<name>: <type>
    var @<name>: <type>
```

There is no `public` modifier, because public is the default.

On the C side, the `extras_` file will contain `DYNA_$ClassName` and
`ID_$ClassName`. There is no `INIT_$ClassName`, because the native class will
instead be created through the constructor.

This directive requires a `constructor` directive further on.

## enum

Usage:
```
enum <name> [generics]?
    variant1
    variant2
    ...

<doc>
```

The `enum` directives adds a new `enum` to the module. If you'd like the enum to
have generics, you can specify such, but that isn't required.

Similar to class properties, each variant must be indented, and there cannot be
any empty lines between the directive and the enums. Variants are written just
as you would write them in Lily.

Here's an example of `Option` from the core:
```
/**
enum Option[A]
    Some(A)
    None

...
*/
```

One caveat is that the variants created by this directive are available globally
to the module. Currently, there is not a way to have variants that are scoped
within their enum, even if one were to use `.<variant name>`.

On the C side, the `extras_` file will contain `ID_` and `DYNA_ID_` definitions
for the variants, but not the enum.

## Closing thoughts

Though it may seem intimidating at first, dynaload offers quite a bit to the
language. It offers a huge memory savings by loading only the items that are
absolutely necessary, with the loading mechanism largely handled by tooling and
the language.

`dyna_tools.py` is enough to get you up and working, but it's not polished quite
yet. You may run into a rough edge here or there. If you find the tool lacking,
please feel free to file an issue and/or send a PR.

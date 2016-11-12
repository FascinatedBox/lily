Lily Overview
=============

Are you looking to embed or extend Lily in C? First, you may want to read this.
This manual is designed to give some insight into Lily's design, and the
decisions made that will affect you as a Lily embedder/extender.

## Resource Decisions

A few of Lily's design decisions have been done with resource management and
language simplicity in mind:

* Destruction of a value is isolated. Destruction should not execute Lily code.
  Foreign classes added to Lily are allowed to have a pure-C teardown function,
  but no more.

* `try` has no `finally`, and native classes do not have a native destructor
  function. This means that the interpreter doesn't have to worry about
  executing code when leaving a certain context.

* There is no underlying `Object`-like class. The closest is the `Dynamic` which
  acts as a container for values that aren't a container (monomorphic) values.
  This means that each built-in class has a distinct lineage, and that they
  cannot be cast between each other. This lack of an `Object` class is why
  Lily's documentation places an emphasis on 

* Built-in classes (aside from Exception) and foreign classes cannot be
  inherited from. This means that values such as `List[Integer]` need to be
  refcounted, but do not need garbage collection as well.

## Kinds of values

The built-in classes can be divided into different logical groups:

* Primitive classes, such as `Integer`, don't need resource management of any
  kind.

* A `String` needs reference counting, but can't become cyclical, so it doesn't
  need garbage collection tracking.

* Lists can't become cyclical, but they may hold a value that can be.

* Finally, complex values like instances can themselves be cyclical, as well as
  hold cyclical values.

For instances, Lily uses the type system to determine if they need to be marked
by the gc or not. A class that isn't a container, and which doesn't include
values that may need marked won't itself be marked. 

## Layout of a value

Lily, the language, is a statically-typed language with the reference being an
interpreter. Internally, Lily is a pretty typical interpreter: It's fed some
source code, it parses it to build bytecode, and that bytecode is handed over to
a vm.

Local values for each function are stored in the `lily_value` struct. Containers
such as `List` and `Tuple` work by holding `lily_value` structs further inside
of themselves. The important parts are:

```
typedef struct lily_value_ {
    union {
        uint32_t flags;
        uint16_t class_id;
    };
    lily_raw_value value;
} lily_value;
```

A `List` value, for example, looks like this.

```
typedef struct lily_list_val_ {
    uint32_t refcount;
    uint32_t extra_space;
    uint32_t num_values;
    uint32_t pad;
    struct lily_value_ **elems;
} lily_list_val;
```

## How it works

The extension and embedding API is designed to make the details of different
values opaque. Different functions are provided to assign values to a `List` and
so on. This allows the interpreter to change the layout of different internal
structures without breaking API.

Reference counting works through a combination of the `lily_value`, and the
struct inside. The `flags` of `lily_value` indicate if the raw value is
derefable, if it's speculative (may have garbage collected contents), or if it
is marked (explicitly marked). When a value is deref'd to 0, it is destroyed.

Garbage collection works by creating a 'tag' that watches over a particular raw
value. When a threshold is reached, the garbage collector fires. It travels
through the locals of each function currently entered, and marks what it found.
What isn't found is destroyed, in the same manner as through reference counting.
This is important, because it means values are always destroyed in the same
manner, no matter what initiated the destroy.

Values inside of the interpreter are stored within a simple array that is pushed
onto as functions enter, and popped from on their exit. Currently, when a
function is entered, the number of values (termed its registers) are cleared
out. This clearing out also derefs values that were left behind when previous
functions exited.

With resource management, there's no "what if this isn't cleaned up properly".
Instead, the question is "when will this be cleared up?".

## What to remember

If you're extending Lily or embedding it, here are some tips to keep in mind:

* If you can, you should use API functions that can move whole values from A to
  B. This allows the flags (which denote gc information) to carry over properly.
  You should only work with raw values if they're raw values that you've just
  made.

* Some structures, such as `List` and `Tuple`, currently have the same internal
  layout. It's unwise to rely on this, as that layout may change in the future.

* If you're embedding the interpreter, and you introduce a package, that package
  must still be `import`ed by the script. This prevents scripts from implicitly
  relying on registered packages. A good example is `lily-apache` which provides
  a `server` package. It's possible to run the same script offline by creating
  a `server.lly` file exporting the same api.

## Dynaload

Lily the interpreter wasn't designed solely for templating. It's also been
designed to be embedded. The center of the interpreter is the `lily_state`
struct. This allows multiple copies of the interpreter to exist at once (the
repl, for example, is implemented by creating an interpreter within an
interpreter).

Every interpreter has overhead. Most interpreters don't have the amount of type
information that Lily does, so that's an additional kind of overhead. There's
also a need to balance having many built-in methods and the memory overhead of
including those methods. Classes and enums, both have bookkeeping overhead.

Lily's built-in library contains over 70 methods at the time of this writing,
and will almost certainly grow. It's unlikely that a script will use each and
every method listed. What dynaload does is to put away loading until necessary.

## How Dynaload works

Dynaload occurs entirely at parse-time. Every C module written contains what's
termed a `dynaload table`. The table (currently, an array of strings) contains
the information necessary to load a class, or a method, enums, and so on. The
`dyna_tools.py` script parses special comments to build it for you. This adds an
extra step (you must `dyna_tools.py refresh <source file>` for each addition or
removal). But the payoff is well worth it.

When Lily loads, for example, the built-in library, exactly 0 methods are
loaded. Some of the classes are not even loaded! Suppose, for example, our first
line of code in a fresh interpreter is:

`print("Hello World")`

The interpreter attempts to figure out what `print` is. Keyword? No. Class? No.
Function? No. It doesn't exist yet. So it goes to the built-in library, and
checks if something named `print` exists as a class, function, or variable. It
doesn't, so the definition is loaded there. The next time, `print` will be
found (so dynaloading is a one-time cost).

Right now, Lily has over 70 methods in its built-in library. Dynaload allows
Lily to have those methods, without every script always paying to initialize
every method.

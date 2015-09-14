Builtin functions
=================

These functions are built into Lily, and are always available from any scope without doing any importing.

# Functions

`print[A](input: A)`

Convert the value given into a string. Currently, this is done through an internal stringification process.

If Lily is running from Apache, then this will add content to the current page.

In any other case, this will write to stdout.


`printfmt(string: format, list[any] args...)`

This is like print, except that it allows `%` modifiers to indicate replacements. 

The following modifiers are supported:

* `%d`: Convert an integer value.

* `%f`: Convert a double value.

* `%s`: Convert anything to a string. This is currently a work-in-progress, as Lily has a crude mechanism for converting anything into a string. However, because Lily lacks virtual calls, it is unwise to use this for anything but built-in types such as lists and hashes.

If there are more format specifiers than arguments, then `ValueError` is raised.

This function is deprecated. It will be superceded by string interpolation.


`calltrace( => list[tuple[string, string, integer]])`

This returns a description of the current call stack, but as a tuple so that it's annoying and complicated to deal with.

There are plans to change the output to `list[string]` instead.


`show[A](value: A)`

This function prints a value to the current stdout (or the server if running from Apache). Instead of using the internal stringification routine that everything else does, it descends into values.

For lists, hashes, and tuples, it will print every value within.

For type any and enums, it will print the contents.

For native functions, it will print a "readable" output of the function's bytecode. This is handy for debugging if something is running incorrectly because of an emitting error, or a vm error.

For classes, it will print the contents of the class, as well as if the contents are private or protected. For classes that inherit from another, it will tell where the property came from.

This function is aware of circular references, and will write '(circular)' instead of printing a value in that case.

# Variables

`stdout: file`

This is a wrapper around C's stdout.

`stderr: file`

This is a wrapper around C's stderr.

`stdin: file`

This is a wrapper around C's stdin.

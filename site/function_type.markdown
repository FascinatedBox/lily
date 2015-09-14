function
========

The function type is the only type that is able to be called with arguments. In Lily, functions are considered 'first-class': They can be passed around as arguments, returned from a function, be placed into a list, and more.

Annotating a function is different than for other types. All other types place their subtypes between `[...]`. However, a function's subtypes are listed between `(...)`. There is no limitation on how many arguments that a function can support. However, a function is only allowed to return one value.

The annotation of a function looks nearly identical to how it is declared when the function made by `define`. In most cases, the annotation can be determined by removing the names of the function.

# Operations

Binary: `!=` `==`

Function comparison is done by internally seeing if two functions point to the same thing.

# Methods

This class does not have any methods.

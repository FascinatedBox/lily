tuple
=====

The tuple type is useful if you want a group of types which may or may not be related, but without actually creating a class. This type takes 1+ types within it, and is annotated as `tuple[type1, type2, type3, ...]`. Lily places the restriction that tuples are not allowed to have zero elements in them (as that seems silly).

Tuples are created using a special syntax to distinguish them from lists and hashes. To create a `tuple[integer, string]`, do `<[10, "abc"]>`.

# Operations

Binary: `!=` `==`

Comparison of tuples is done by deep equality. 

# Methods

This class has no methods.

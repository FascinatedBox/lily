Double
======

Lily's Double type is backed by a C double, and thus the same restrictions to a C double apply.

# Operations

Binary Operations: `!=` `==` `<` `<=` `>` `>=` `+` `-` `*` `/`

For basic arithmetic operations, if an integer and a double are involved, the result will be a double.

# Methods

`Double::to_i(self: Double) : Integer`

Convert a double to an integer. Internally, this is done through a C typecast to int64_t.

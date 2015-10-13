integer
=======

The integer type represents a 64-bit signed integer.

# Operations

Binary: `!=` `==` `<` `<=` `>` `>=` `+` `-` `*` `/` `%` `<<` `>>`

Unary: `+` `-` `!`

For basic arithmetic operations, if an integer and a double are involved, the result will be a double.

Shifts must be by an integer value.

# Methods

`integer::to_b(self: integer) : boolean`
Converts an integer value to a boolean. The resulting boolean is false if 0, and true in every other case (including negatives).

`integer::to_d(self: integer) : double`
Converts an integer value to a double. Internally, a C typecast is performed on the underlying int64_t value to coerce it into a double.

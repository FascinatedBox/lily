Integer
=======

The Integer type represents a 64-bit signed int.

# Operations

Binary: `!=` `==` `<` `<=` `>` `>=` `+` `-` `*` `/` `%` `<<` `>>`

Unary: `+` `-` `!`

For basic arithmetic operations, if an Integer and a Double are involved, the result will be a Double.

Shifts must be by an integer value.

# Methods

`Integer::to_b(self: Integer) : Boolean`
Converts an integer value to a boolean. The resulting boolean is false if 0, and true in every other case (including negatives).

`Integer::to_d(self: Integer) : Double`
Converts an integer value to a double. Internally, a C typecast is performed on the underlying int64_t value to coerce it into a double.

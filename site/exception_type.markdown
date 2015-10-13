exception
=========

The exception type represents the most basic kind of type that can be raised. Unlike other builtin classes, it can be inherited from.

# Operations

Binary: `!=` `==`

Comparison between Exception classes is done using deep equality. This is wrong, and will change.

# Members

`@traceback: list[string]`

This represents a single frame of traceback. If this frame comes from something in native Lily code, then it takes the form of `from <filename>:<linenum>: in <function>`. If it comes from foreign code (such as `list::apply`), then it takes the form of `from [C]: in <function>`.


`@message: string`

The exception error message.

# Methods

`exception::new(string: message)`

Construct a new exception having the given message.

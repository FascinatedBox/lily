exception
=========

The exception type represents the most basic kind of type that can be raised. Unlike other builtin classes, it can be inherited from.

# Operations

Binary: `!=` `==`

Comparison between Exception classes is done using deep equality. This is wrong, and will change.

# Members

`@traceback: list[tuple[string, string, integer]]`

This represents one frame of traceback. This will change later, so that traceback is instead represented by a list of strings. While it is possible to assign to this, be aware that it will be overwritten when an exception is caught.


`@message: string`

The exception error message.

# Methods

`exception::new(string: message)`

Construct a new exception having the given message.

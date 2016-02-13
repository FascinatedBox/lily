Exception
=========

The Exception type represents the most basic kind of type that can be raised. Unlike most other builtin classes, it can be inherited from.

# Operations

Binary: `!=` `==`

Comparison between Exception instances is done using shallow equality.

# Members

`@traceback: List[String]`

This represents a single frame of traceback. If this frame comes from something in native Lily code, then it takes the form of `from <filename>:<linenum>: in <function>`. If it comes from foreign code (such as `List::map`), then it takes the form of `from [C]: in <function>`.


`@message: String`

The exception error message.

# Methods

`Exception::new(message: String)`

Construct a new exception having the given message.

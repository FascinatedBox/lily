Tainted
=======

The Tainted type acts as a wrapper over a value that is considered unsafe for direct use. The Tainted type holds a private reference to whatever is passed into it, thus forbidding direct use of something that is unsafe. To access the value inside, callers must call `Tainted::sanitize` to receive a safe value for their use.

Unlike most builtin types, Tainted can be inherited from.

# Operations

Binary: `!=` `==`

# Members

`private @value: A`

The value that is being held. Note that this is only a reference to the value, not a deep copy of the value.


# Methods

`Tainted::new[A](value: A)`

Construct a new Tainted value that will wrap over the data provided.

`Tainted::sanitize[A, B](f: function(A => B)):B`

This calls the function `f` with `@value`, and returns the result.

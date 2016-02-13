Option
======

The Option type is an enum that is defined internally like so:

```
enum Option[A]
    Some(A)
    None
}
```

Since None is not parameterized by `A`, it is permissible to assign an Option of any subtype to None.

# Operations

Binary: `!=` `==`

# Members


# Methods

`Option::and[A, B](self: Option[A], other: Option[B]):Option[B]`

If `self` is `None`, then this returns `None`.

Otherwise, this returns `other`.


`Option::and_then[A, B](self: Option[A], fn: Function(A => Option[B])):Option[B]`

If `self` is `None`, then this returns `None`.

Otherwise, the wrapped value is passed to `fn`, and the result is what `fn` returns.


`Option::is_none[A](self: Option[A]):Boolean`

Returns `true` if `self` is `None`, `false` otherwise.


`Option::is_some[A](self: Option[A]):Boolean`

Returns `false` if `self` is `None`, `false` otherwise.


`Option::map[A, B](self: Option[A], fn: Function(A => B)):Option[B]`

If `self` is `None`, then this returns `None`.

Otherwise, transforms the wrapped value using `fn`.


`Option::or[A](self: Option[A], fallback: Option[A]):Option[A]`

If `self` is `None`, then this returns `fallback`.

Otherwise, this returns `self`.


`Option::or_else[A](self: Option[A], fn: Function( => Option[A])):Option[A]`

If `self` is `None`, then this returns whatever `fn()` returns.

Otherwise, this returns `self`.


`Option::unwrap[A](self: Option[A]):A`

If `self` is `None`, then this raises `ValueError`.

Otherwise, this returns the value wrapped by `self`.


`Option::unwrap_or[A](self: Option[A], fallback: A):A`

If `self` is `None`, then this returns `fallback`.

Otherwise, this returns the value wrapped by `self`.


`Option::unwrap_or_else[A](self: Option[A], fn: Function( => A)):A`

If `self` is `None`, then this returns whatever `fn()` returns.

Otherwise, this returns the value wrapped by `self`.

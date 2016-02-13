Dynamic
=======

Instances of the `Dynamic` class are containers that are not constrained to any single type. This can be used to delay type-checking until run-time, as well as allowing `list` to hold a multitude of values.

Creating a new `Dynamic` instance can be done like so:

```
var a = Dynamic(10)
    a = Dynamic("asdf")
    a = Dynamic(Dynamic(10))
```

Getting the value from a `Dynamic` requires a typecast. Since the typecast might fail, the result is set to `Option[<cast type>]`. Since the result is an `Option`, you can treat it like any other `Option`.

```
class Person(name: string) { var @name = name }

var values = [Dynamic(Dynamic(10)), Dynamic("asdf"), Dynamic(Person("Bob"))]

values[1].@(string) # Some("asdf")

match values[2].@(Person): {
    case Some(p):
        print(p.name)
    case None:
        print("Oh no!")
}

var my_int =
        values[0]
        .@(Dynamic)
        .unwrap()
        .@(Integer)
        .unwrap() # 10

var bad_cast = values[2].@(integer) # None
```

One limitation with `Dynamic` is that typecasts are now allowed to use subtypes like `List[integer]` or `Function()`. This limitation exists because Lily retains class information at runtime, but discards all subtyping information. Because of that, there is no way to determine if, say, an empty list holds the desired type.

# Operations

Binary: `!=` `==`

Equality for `Dynamic` is done recursively and uses a strategy appropriate for whatever class the `Dynamic` instance holds. Attempting to compare an infinite `Dynamic` to another will result in `RuntimeError` being raised.

# Methods

`Dynamic::new[A](value: A)`

Create a new `Dynamic` instance that wraps around the value is provided. `Dynamic(<value>)` is just syntatic sugar for `Dynamic(<value>)`

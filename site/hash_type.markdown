hash
====

The hash type represents a mapping from a key to a value. When annotating a hash type, the key comes first, and the value afterward. A hash mapping from integer to string could thus be annotated as `hash[integer, string]`. Within the method listing, type A refers to the key, and type B refers to the value.

The following types are valid hash keys: `integer`, `double`, and `string`.

Values can be added to hashes through subscript assignment. However, attempting to retrieve a key that does not exist will trigger `KeyError`.

Hashes are currently considered incomplete.

# Operations

Binary: `!=` `==`

Hashes, like lists, do deep comparisons for equality. As such, `[1 => "a"] == [1 => "a"]` is `true`.

# Methods

`hash::clear(self: hash[A, B])`

Removes all key-value pairs from the hash.


`hash::delete(self: hash[A, B], key: A)`

This attempts to erase `key` from the hash. If `key` does not exist within the hash, then nothing happens.


`hash::each_pair(self: hash[A, B], f: function(A, B))`

This calls `f` on each key-value pair that exists within the hash.


`hash::has_key(self: hash[A, B], key: A):boolean`

Checks if `key` exists within the hash.


`hash::keys(self: hash[A, B]) : list[A]`

Returns a list of all keys that are present within the hash.


`hash::get(self: hash[A, B], key: A, or_else: B) : B`

Attempt to get `key` from within the hash. If successful, return the matching value. If `key` does not exist within the hash, then return `or_else` instead. This can be considered a safe alternative to a subscript, as it does not raise any errors.


`hash::map_values(self: hash[A, B], f: function(B => C)):hash[A, C]`

This iterates over the hash given, calling `f` with each value held.

A new hash is built that maps the old keys to the new values. The old hash is not modified.

```
var h = ["a" => -1, "b" => 5, "c" => 11]

h.map_values{|v| (v * v).to_s() }
# ["a" => "-2", "b" => "10", "c" => "22"]

h.map_values{|v| v + 1}
# ["a" => 0, "b" => 6, "c" => 12]
```


`hash::size(self: hash[A, B]):integer`

Returns the number of key-value pairs within the hash.

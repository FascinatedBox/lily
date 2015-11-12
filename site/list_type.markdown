list
====

The list type represents a type-safe container. The list type takes one subtype, which is the type of the elements that are allowed within it. Within the methods listed, this element type is referred to as the generic type `A`.

# Operations

Binary: `!=` `==`

Lists compare using deep equality. Therefore `[1] == [1]` is `true`.

# Methods

`list::append(self: list[A], value: A)`

Add an element to the list. If the list has the type `list[any]`, then any type can be appended to the list.


`list::each(self: list[A], f: function(A)): list[A]`

This calls `f` once, using each element in the list. The result of this function is the list that was passed into it.

Examples:

```
[1, 2, 3].each{|a| printfmt("%d -- ", a) }
```

Results in:

```
1 -- 2 -- 3 --
```


`list::each_index(self: list[A], f: function(integer)): list[A]`

This is similar to `list::each`, except that the function receives the index (from 0) of elements within the list.

The result of this function is the list that was passed into it.


`list::fill(n: integer, value: A => list[A])`

This creates a new list, based on `value` being repeated `n` times.

If `n` is less than zero, then `ValueError` is raised.


`list::pop(self: list[A]):A`

Takes the top-most element of the list out, and returns it.

If the list is empty, then IndexError is raised.


`list::reject(self: list[A], f: function(A => boolean)) : list[A]`

Returns a list of all elements in `self` for which the predicate `f` returns `false`.

Examples:

```
# Value: [1, 3, 5]
[1, 2, 3, 4, 5, 6].select{|a| a % 2 == 0}

# Value: ["ba", "bb"]
["aa", "ab", "ba", "bb"].select{|a| a.startswith("a")}
```


`list::select(self: list[A], f: function(A => boolean)) : list[A]`

Returns a list of all elements in `self` for which the predicate `f` returns `true`.

Examples:

```
# Value: [2, 4, 6]
[1, 2, 3, 4, 5, 6].select{|a| a % 2 == 0}

# Value: ["aa", "ab"]
["aa", "ab", "ba", "bb"].select{|a| a.startswith("a")}
```


`list::size(self: list[A]) : integer`

Returns the number of elements that are contained within `self`. This can be used in conjunction with a for loop as follows:

```
for i in 0...x.size() - 1:
    ...
```

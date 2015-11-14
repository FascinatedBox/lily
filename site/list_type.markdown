list
====

The list type represents a type-safe container. The list type takes one subtype, which is the type of the elements that are allowed within it. Within the methods listed, this element type is referred to as the generic type `A`.

# Operations

Binary: `!=` `==`

Lists compare using deep equality. Therefore `[1] == [1]` is `true`.

# Methods

`list::clear(self: list[A])`

Removes all elements from the list.


`list::count(self: list[A], f: function(A => boolean)): integer`

This calls `f` on each element of the list. The result is a count of how many times the predicate function `f` returned true.

Examples:

```
[1, 2, 3, 4].count{|a| a % 2 == 0} # 2

["abc", "def", "ghi"].count{|v| v.startswith("z") } # 0
```


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


`list::fold(self: list[A], value: A, f: function(A, A => A)):A`

This function can be used to combine elements of a list together. It works by repeatedly calling `f`, which takes two elements and defines how to combine them.

On the first pass, `f` is called with `value` and element 0 of the list.

Subsequent passes use the result of the last call, and the next element of the list.

```
define add(a: integer, b: integer): integer { return a + b }

var v = [1, 2, 3]
v.fold(0, add) # 6
v.fold(20, add) # 26

var strings = ["a", "b", "c"]
strings.fold("", {|a, b| a.concat(b)}) # "abc"

var empty: list[integer] = []
empty.fold(100, {|a, b| 0}) # 100
```


`list::insert(self: list[A], pos: integer, value: A)`

Insert a single value into a list, before position `pos`. Negative values are accepted as well.

If `pos` is out of range, then `IndexError` is raised.

```
var v = [0, 1, 3]

v.insert(-1, 2)

v == [0, 1, 2, 3] # true

v.insert(0, -1)

v == [-1, 0, 1, 2, 3] # true
```


`list::pop(self: list[A]):A`

Takes the top-most element of the list out, and returns it.

If the list is empty, then IndexError is raised.


`list::push(self: list[A], value: A)`

Add an element to the list. If the list has the type `list[any]`, then any type can be pushed to the list.


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


`list::shift(self: list[A]): A`

This takes the first element from the list and returns it.

If the list is empty, then IndexError is raised.

Examples:

```
var values = [10, 20, 30]

values.shift() # 10
values.size() # 2

values == [20, 30] # true
```


`list::size(self: list[A]) : integer`

Returns the number of elements that are contained within `self`. This can be used in conjunction with a for loop as follows:

```
for i in 0...x.size() - 1:
    ...
```


`list::unshift(self: list[A], A)`

This adds an element at the front of a list.

Example:

```
var v = [1, 2, 3]

v.unshift(0)
v.unshift(-1)
v == [-1, 0, 1, 2, 3] # true
```

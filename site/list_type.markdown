List
====

The List type represents a type-safe container. The list type takes one subtype, which is the type of the elements that are allowed within it. Within the methods listed, this element type is referred to as the generic type `A`.

# Operations

Binary: `!=` `==`

Lists compare using deep equality. Therefore `[1] == [1]` is `true`.

# Methods

`List::clear(self: List[A])`

Removes all elements from the list.


`List::count(self: List[A], f: Function(A => Boolean)): Integer`

This calls `f` on each element of the list. The result is a count of how many times the predicate function `f` returned true.

Examples:

```
[1, 2, 3, 4].count{|a| a % 2 == 0} # 2

["abc", "def", "ghi"].count{|v| v.startswith("z") } # 0
```


`List::each(self: List[A], f: Function(A)): List[A]`

This calls `f` once, using each element in the list. The result of this function is the list that was passed into it.

Examples:

```
[1, 2, 3].each{|a| printfmt("%d -- ", a) }
```

Results in:

```
1 -- 2 -- 3 --
```


`List::each_index(self: List[A], f: Function(Integer)): List[A]`

This is similar to `List::each`, except that the function receives the index (from 0) of elements within the list.

The result of this function is the list that was passed into it.


`List::delete_at(self: List[A], pos: Integer)`

This destroys the item in the list at `pos`. If `pos` is negative, then it is wrapped around similarly to `List::insert`.

Attempting to delete an index from an empty list, or from a non-existent position raises `IndexError`

```
var v = [1, 2, 3]

v.delete_at(-1)

v == [1, 2]

v.delete_at(0)

v == [2]
```


`List::fill(n: Integer, value: A => List[A])`

This creates a new list, based on `value` being repeated `n` times.

If `n` is less than zero, then `ValueError` is raised.


`List::fold(self: List[A], value: A, f: Function(A, A => A)):A`

This function can be used to combine elements of a list together. It works by repeatedly calling `f`, which takes two elements and defines how to combine them.

On the first pass, `f` is called with `value` and element 0 of the list.

Subsequent passes use the result of the last call, and the next element of the list.

```
define add(a: Integer, b: Integer): Integer { return a + b }

var v = [1, 2, 3]
v.fold(0, add) # 6
v.fold(20, add) # 26

var strings = ["a", "b", "c"]
strings.fold("", {|a, b| a.concat(b)}) # "abc"

var empty: List[Integer] = []
empty.fold(100, {|a, b| 0}) # 100
```


`List::insert(self: List[A], pos: Integer, value: A)`

Insert a single value into a list, before position `pos`. Negative values are accepted as well.

If `pos` is out of range, then `IndexError` is raised.

```
var v = [0, 1, 3]

v.insert(-1, 2)

v == [0, 1, 2, 3] # true

v.insert(0, -1)

v == [-1, 0, 1, 2, 3] # true
```


`List::pop(self: List[A]):A`

Takes the top-most element of the list out, and returns it.

If the list is empty, then IndexError is raised.


`List::push(self: List[A], value: A)`

Add an element to the end of the list.


`List::reject(self: List[A], f: Function(A => boolean)) : List[A]`

Returns a list of all elements in `self` for which the predicate `f` returns `false`.

Examples:

```
# Value: [1, 3, 5]
[1, 2, 3, 4, 5, 6].select{|a| a % 2 == 0}

# Value: ["ba", "bb"]
["aa", "ab", "ba", "bb"].select{|a| a.startswith("a")}
```


`List::select(self: List[A], f: Function(A => boolean)) : List[A]`

Returns a list of all elements in `self` for which the predicate `f` returns `true`.

Examples:

```
# Value: [2, 4, 6]
[1, 2, 3, 4, 5, 6].select{|a| a % 2 == 0}

# Value: ["aa", "ab"]
["aa", "ab", "ba", "bb"].select{|a| a.startswith("a")}
```


`List::shift(self: List[A]): A`

This takes the first element from the list and returns it.

If the list is empty, then IndexError is raised.

Examples:

```
var values = [10, 20, 30]

values.shift() # 10
values.size() # 2

values == [20, 30] # true
```


`List::size(self: List[A]) : Integer`

Returns the number of elements that are contained within `self`. This can be used in conjunction with a for loop as follows:

```
for i in 0...x.size() - 1:
    ...
```


`List::unshift(self: List[A], A)`

This adds an element at the front of a list.

Example:

```
var v = [1, 2, 3]

v.unshift(0)
v.unshift(-1)
v == [-1, 0, 1, 2, 3] # true
```

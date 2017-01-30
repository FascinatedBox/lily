Each of these benchmarks attempts to replicate the same algorithm in a different
language. Lily may be a statically-typed language, but the reference
implementation is an interpreter. I've therefore decided to pit Lily up against
similar languages: Python, Ruby, and Lua.

The benchmarks here are copied from the
[Wren programming language](https://github.com/munificent/wren).

### binary_trees

This benchmark stresses object creation and garbage collection. It builds a few
big, deeply nested binaries and then traverses them.

### fib

This benchmark runs a naive Fibonacci a few times. This stresses heavy function
entry/exit, and arithmetic. It's not very representative of real-world code
though.

### for

This was originally made to test the speed of for loops. It serves as a useful
baseline of hash performance, since it builds a large integer-based hash and
does a single accumulating loop through it. In Lily's case, this also stresses
a foreign function calling back into a native one.

### map_numeric

This does the same work as 'for', but finishes by deleting the elements one at
a time.

### map_string

This builds a large hash string to int hash, then does the same as map_numeric
(iterate and manually delete elements). Together they're useful for isolating
problems in the performance of hashes.

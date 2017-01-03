API: Alloc
==========

## Introduction

File: `lily_api_alloc.h`

This manual covers the allocation API. Lily's allocation API currently works
through wrappers to C's malloc, realloc, and free. It's unlikely that you'll
need to interact with these functions directly, but they're documented in the
event that you do.

The most likely case for needing to include this file is when you're introducing
a foreign struct to the interpreter. In such a case, the initialization macro
that `dyna_tools.py` generates will use `lily_malloc` for allocating memory for
the structure. Your `destroy_` function for the struct should use `lily_free` to
delete the memory accordingly.

A key difference to these functions and their usual C counterparts is that they
abort when there is an allocation failure.

## API

### `void *lily_malloc(size_t size)`

Attempt to allocate `size` bytes of memory and return it, calling C's `abort` if
unable to do so.

### `void *lily_realloc(void *ptr, size_t new_size)`

Attempt to resize `ptr` to be `new_size` bytes wide. If `new_size` is 0, then
it is the same as calling `lily_free`. If unable to allocate the requested
amount of memory, this will call C's `abort`.

### `void lily_free(void *ptr)`

Equivalent to `free(ptr)`, but provided for consistency.

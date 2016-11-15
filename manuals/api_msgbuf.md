API: Msgbuf
===========

## Introduction

File: `lily_api_msgbuf.h`

This manual covers the API surrounding msgbuf (short for message buffer). The
goal of msgbuf is to provide a buffer that callers can insert into without
worrying about allocation checks. The design of msgbuf is simple: It's a buffer
with a current position and a size. Functions that add to it always size-check
first. There's currently no ability to save and restore partial output. Instead,
functions are expected to use the msgbuf all at once, then clear it for the next
pass.

The design of msgbuf, both internally and through api, is that callers should be
responsible for flushing the msgbuf before use, instead of after. This makes
flushing problems evident at their source, instead of appearing mysteriously
later on.

If you think you may be adding a large amount of data to a msgbuf, consider
creating a purpose-built msgbuf instead of using the vm's one. This will prevent
a large buffer from hanging around.

## API

### Fetch

#### `lily_msgbuf *lily_get_msgbuf(lily_state *s)`

Fetch the working msgbuf **and** flush it. Most callers will want this, which
is why it does not have a suffix.

#### `lily_msgbuf *lily_get_msgbuf_noflush(lily_state *s)`

In rare cases, the caller needs to grab the working msgbuf, but without clearing
out the former contents. This will do that.

#### `const char *lily_mb_get(lily_msgbuf *msgbuf)`

Fetch the contents of a msgbuf. The contents should be `\0` terminated, and
valid utf-8 as well.

## Setup/Teardown

#### `lily_msgbuf *lily_new_msgbuf(int initial)`

Create a new msgbuf, with `initial` size reserved.

#### `void lily_free_msgbuf(lily_msgbuf *msgbuf)`

Free a msgbuf. You should only call this on a msgbuf you've made.

#### `void lily_mb_flush(lily_msgbuf *msgbuf)`

Set the msgbuf's position to 0 and `\0` terminate it, effectively removing all
prior contents.

## Insertion

#### `void lily_mb_add(lily_msgbuf *msgbuf, const char *str)`

Add `str` to the msgbuf. `str` is assumed to be a `\0` terminated string.

#### `void lily_mb_add_char(lily_msgbuf *msgbuf, char ch)`

Add a single character (`ch`) to the msgbuf. 

#### `void lily_mb_add_slice(lily_msgbuf *msgbuf, const char *str, int start, int stop)`

Adds the contents of `str` from `start` to `stop`.

#### `void lily_mb_add_value(lily_msgbuf *msgbuf, lily_state *s, lily_value *value)`

This adds `value` to the msgbuf. The state `s` is used to provide class
information, since `lily_value` stores class information using class identities
instead of full class pointers.

If `value` is a `String`, then the contents of it are directly inserted into the
msgbuf (without quoting).

The content added to the msgbuf is the same as what the result would be from
doing an interpolation of your value. That is because this is the function used
for handling string interpolation.

#### `void lily_mb_add_fmt(lily_msgbuf *msgbuf, const char *fmt, ...)`

This calls `lily_mb_add_fmt_va` with the arguments provided.

#### `void lily_mb_add_fmt_va(lily_msgbuf *msgbuf, const char *fmt, va_list va)`

This inserts elements from `va` into the msgbuf, according to `fmt`. As with
`sprintf`, this function should be handled with care.

The following values are accepted:

`%d`: Add an `int` value to the msgbuf.

`%s`: Add a `char *` value to the msgbuf.

`%c`: Add a `char` value to the msgbuf.

`%p`: Add a `void *` value to the msgbuf.

`%%`: Add `%` to the msgbuf.

Custom format specifiers include:

`^T`: Add a `lily_type *` value to the msgbuf. Function types are printed
without the names of their arguments, or their underlying default values. This
format specifier is used internally to represent type information.

Currently, the function does not accept any format modifiers.

#### `const char *lily_mb_sprintf(lily_msgbuf *msgbuf, const char *fmt, ...)`

This is a helper function that is equivalent to the following:

```
lily_mb_flush(msgbuf);
lily_mb_fmt(msgbuf, fmt, ...);
return lily_mb_get(msgbuf);
```

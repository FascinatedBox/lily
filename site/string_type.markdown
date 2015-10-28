string
======

The string type is backed by a C string, and guaranteed to be both utf-8 clean and with no embedded zeroes.

It is possible to subscript from a string. Subscripts are done by character, not by byte. As a result, the string `"ĀēĪōŪ"` has five elements. Subscripts also start from a zero index. It is also to negatively subscript a list, wherein -1 indicates the last element.

Strings are immutable: Standard library functions will always create new copies of a string, instead of modifying an existing one.

# Operations

Binary: `!=` `==` `<` `>` `<=` `>=`

Comparisons and equality for strings are done by comparing the bytes of each string. Comparisons are done using case-sensitive matching. Internally, it just uses C's strcmp.

# Methods

`string::concat (self: string, other: string) : string`

Return a new string that is a combination of `self` and `other`.


`string::endswith (self: string, other: string) : boolean`

Returns `true` if `self` ends with `other`, `false` otherwise. If `other` is empty, then the result is always `true`.


`string::find (self: string, needle: string) : integer`

Attempt to locate `needle` within `self`. If `needle` is found, then the result is the index where `needle` starts. If it is not found, the result is -1.


`string::format (self: string, any...) : string`

This uses `self` as a formatting string, with the values coming from whatever extra arguments are passed. The following format specifiers are accepted:

* `%d`: Convert an integer value.

* `%f`: Convert a double value.

* `%s`: Convert anything to a string. This is currently a work-in-progress, as Lily has a crude mechanism for converting anything into a string. However, because Lily lacks virtual calls, it is unwise to use this for anything but built-in types such as lists and hashes.

If there are more format specifiers than arguments, then `ValueError` is raised.

It is currently *not* possible to specify a size along with a format specifier (ex: `%3d`).


`string::htmlencode (self: string) : string`

Encode a string so that it is suitable for being written to something expecting well-formatted html. `&` becomes `&amp;`, `<` becomes `&lt;`, `>` becomes `&gt;`.


`string::isalnum (self: string) : boolean`

Return `true` if every character in a string is within a-z or within 0-9, `false` otherwise.


`string::isalpha (self: string) : boolean`

Return `true` if every character in a string is within a-z, `false` otherwise.


`string::isdigit (self: string) : boolean`

Return `true` if every character in a string is within 0-9, `false` otherwise.


`string::isspace (self: string) : boolean`

Return `true` if every character in a string is one of ` \t\r\n`, `false` otherwise.


`string::lower (self: string) : string`

This returns a string where all characters in the A-Z range are converted into their equivalents within a-z. Unicode characters are untouched.


`string::lstrip (self: string, to_strip: string) : string`

This returns a string where `to_strip` has been removed from the start of `self`. If `to_strip` is an empty string, then the input string is returned as-is.


`string::rstrip (self: string, to_strip: string) : string`

This returns a string where `to_strip` has been removed from the end of `self`. If `to_strip` is an empty string, then the input string is returned as-is.


`string::split (self: string, *by: string=" ") : list[string]`

This splits `self` by each character (not byte) that is within `by`. By default, it will split just by spaces.

ValueError is raised if `by` is an empty string.


`string::startswith (self: string, other: string) : boolean`

Returns `true` if `self` begins with `other`, `false` otherwise. If `other` is empty, then the result is always `true`.


`string::strip (self: string, to_strip: string) : string`

This acts as a combination of lstrip and rstrip, returning a string which has `to_strip` removed from both the start and end of `self`. If `to_strip` is an empty string, then the input string is returned as-is.


`string::to_i (self: string) : integer)`

This attempts to convert a string into an integer. The function assumes that `self` describes a decimal value (and not, say, octal or hex). If unable to convert the string to an integer, `ValueError` is raised.

Examples:

```
# Value: -5
"-5".to_i()

# Value: 1
"00000001".to_i()

# Invalid: Hex is not supported
# "0x10".to_i()

# Invalid: The whole thing must be an integer
# "10cats".to_i()
```


`string::trim (self: string) : string`

This function returns with consecutive whitespace (`" \t\r\n"`) removed from both the start and end of the string.


`string::upper (self: string) : string`

This returns a string where all characters in the a-z range are converted into their equivalents within A-Z. Unicode characters are untouched.

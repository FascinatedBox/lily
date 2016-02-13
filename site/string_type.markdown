String
======

The String type is backed by a C string, and guaranteed to be both utf-8 clean and with no embedded zeroes.

It is possible to subscript from a string. Subscripts are done by character, not by byte. As a result, the string `"ĀēĪōŪ"` has five elements. Subscripts also start from a zero index. It is also to negatively subscript a list, wherein -1 indicates the last element.

Strings are immutable: Standard library functions will always create new copies of a string, instead of modifying an existing one.

# Operations

Binary: `!=` `==` `<` `>` `<=` `>=`

Comparisons and equality for strings are done by comparing the bytes of each string. Comparisons are done using case-sensitive matching. Internally, it just uses C's strcmp.

# Methods

`String::concat (self: String, other: String) : String`

Return a new string that is a combination of `self` and `other`.


`String::endswith (self: String, other: String) : Boolean`

Returns `true` if `self` ends with `other`, `false` otherwise. If `other` is empty, then the result is always `true`.


`String::find (self: String, needle: String) : Option[Integer]`

Attempt to locate `needle` within `self`.

If `needle` is found, then the result is a `Some` of the index where `needle` starts.

Otherwise, the result is `None`.


`String::format (self: String, Dynamic...) : String`

This uses `self` as a formatting string, with the values coming from whatever extra arguments are passed. The following format specifiers are accepted:

* `%d`: Convert an integer value.

* `%f`: Convert a double value.

* `%s`: Convert anything to a string. This is currently a work-in-progress, as Lily has a crude mechanism for converting anything into a string. However, because Lily lacks virtual calls, it is unwise to use this for anything but built-in types such as lists and hashes.

If there are more format specifiers than arguments, then `ValueError` is raised.

It is currently *not* possible to specify a size along with a format specifier (ex: `%3d`).


`String::htmlencode (self: String) : String`

Encode a string so that it is suitable for being written to something expecting well-formatted html. `&` becomes `&amp;`, `<` becomes `&lt;`, `>` becomes `&gt;`.


`String::isalnum (self: String) : Boolean`

Return `true` if every character in a string is within a-z or within 0-9, `false` otherwise.


`String::isalpha (self: String) : Boolean`

Return `true` if every character in a string is within a-z, `false` otherwise.


`String::isdigit (self: String) : Boolean`

Return `true` if every character in a string is within 0-9, `false` otherwise.


`String::isspace (self: String) : Boolean`

Return `true` if every character in a string is one of ` \t\r\n`, `false` otherwise.


`String::lower (self: String) : String`

This returns a string where all characters in the A-Z range are converted into their equivalents within a-z. Unicode characters are untouched.


`String::lstrip (self: String, to_strip: String) : String`

This returns a string where `to_strip` has been removed from the start of `self`. If `to_strip` is an empty string, then the input string is returned as-is.


`String::parse_i (self: String) : Option[Integer]`

This attempts to convert a string into an integer. `self` is assumed to be a plain base-10 decimal value.

If successful, a `Some` of the numeric value is returned.

Otherwise, `None` is returned.

Examples:

```
"-5".parse_i() # Some(-5)

"00000001".to_i() # Some(1)

# Sorry, but no hex.
"0x10".to_i() # None

# Invalid: The whole thing must be an integer
"10cats".to_i() # None
```


`String::rstrip (self: String, to_strip: String) : String`

This returns a string where `to_strip` has been removed from the end of `self`. If `to_strip` is an empty string, then the input string is returned as-is.


`String::split (self: String, *by: String=" ") : List[String]`

This splits `self` by each character (not byte) that is within `by`. By default, it will split just by spaces.

ValueError is raised if `by` is an empty string.


`String::startswith (self: String, other: String) : Boolean`

Returns `true` if `self` begins with `other`, `false` otherwise. If `other` is empty, then the result is always `true`.


`String::strip (self: String, to_strip: String) : String`

This acts as a combination of lstrip and rstrip, returning a string which has `to_strip` removed from both the start and end of `self`. If `to_strip` is an empty string, then the input string is returned as-is.


`String::trim (self: String) : String`

This function returns with consecutive whitespace (`" \t\r\n"`) removed from both the start and end of the string.


`String::upper (self: String) : String`

This returns a string where all characters in the a-z range are converted into their equivalents within A-Z. Unicode characters are untouched.

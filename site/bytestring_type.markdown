bytestring
==========

The bytestring type represents a series of bytes that may have invalid utf-8 in them, and may have embedded zeroes. Literals of this type can be created through using a B prefix to a string (ex: B"\000test\000")

# Operations

Binary Operations: `!=` `==`

Bytestring equality is by deep equality, and all bytes of the bytestring are compared.

# Methods

`bytestring::encode(bytestring self, encode: *string="error") : string`

Convert a bytestring into a string. Currently, the only method of encoding is "error", but more options will be added later. `encode` is also case-sensitive.

Encoding options:

* error: If the bytestring contains either embedded zeroes or an invalid utf-8 sequence, then `ValueError` is raised.

If `encode` is not one of the supported options, then `ValueError` is raised.

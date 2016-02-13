file
====

The file class is a wrapper around C's FILE. It currently provides a bare minimum necessary to read and write from files. This API should not be considered final, as a facelift is in order for it.

Attempting to use read operations on write files (or vice versa) will result in IOError being raised. Similarly, operating on a closed file will also raise IOError.

# Methods

`File::close(self: File)`

Close the file given. Attempting to close a file that is already closed will not do anything. For short scripts, this is not entirely necessary, as Lily's refcounting and garbage collection will eventually collect and then close files that are left open.


`File::readline(self: File) : bytestring`

This reads a single line from the given file. This returns the line as a bytestring, instead of a string in case the file contains either invalid utf-8, or embedded zeroes inside of it.


`File::write[A](self: File, value: A)`

This uses Lily's internal stringification to convert the value into a string if it is not already one. The contents of that string are then written to the file given.


`File::print[A](self: File, value: A)`

This is like `File::write`, except it writes a newline (`\n`) after the text.


`File::open(String: filename, String: mode)`

Attempt to open a file named `filename` with the mode provided.

Mode can be:

* `r` : Open file for reading. It must exist.

* `w` : Open file for writing. If another file with the same name exists, the old one is erased.

* `a` : Writing operations will push data to the file. The file is created if it does not exist.

* `r+` : Opens the file for both reading and writing. It must exist.

* `w+` : Creates an empty file for both reading and writing.

* `a+` : Opens a file for reading and pushing.

If the file cannot be open, or the mode does not start with one of `arw`, then IOError is raised.

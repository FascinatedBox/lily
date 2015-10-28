server
======

This package provides a means of accessing data from the Apache web server in a way that doesn't rely on the internals of it.

This module will be getting a facelift by the end of this release.

# Getting Apache to serve Lily files

This is not a default module. To build this module, you will first need to acquire Apache's development headers. Once that is done, execute `cmake -DWITH_APACHE=ON`. You can then execute the typical make and make install. The make install step will install the 'mod_lily.so' library in Apache's module directory.

You'll now need to edit Apache's configuration file so that it knows how to load the mod_lily library, and to tell it what directories are to be handled using this module. You will likely need root privilege on your system (assuming it's a Linux-style system).

On my system, the configuration file is located here: `/etc/httpd/conf/httpd.conf`

For the first task, find an area that mentions 'Dynamic Shared Object (DSO) Support'. You'll need to write in a command to load the Lily library, which was just added to Apache's modules.

`LoadModule lily_module modules/mod_lily.so`

You'll now need to specify a directory that will be serving Lily files. I managed to get the whole of the cgi-bin directory to serve Lily files using the following configuration, nowhere in particular within Apache's same configuration file.

```
<Directory "/var/www/cgi-bin/">
    SetHandler lily
    Options +ExecCGI
</Directory>
```

You'll now need to either reboot Apache. The server should come back up, and report no errors.

In the event of an error (particularly "undefined symbol suchandsuch"), that is a bug in Lily, and I would appeciate a bug report.

However, if that is not the case, you should be able to serve Lily files now

# Actually serving Lily files

When Lily runs from Apache, it runs in tagged mode. This means that code will be between `<?lily ... ?>` tags, and everything else is sent as-is to the server. Lily also provides direct, easy access to server GET, POST, and ENV variables. Lily aims to run with as little configuration as possible. Lily's design is greatly inspired by PHP, but it shares a number of key differences.

* Lily's mechanism to include other files is the `import` keyword, and has semantics inspired by Python. When a file is loaded, the name of that file becomes a namespace through which the variables, classes, and methods are accessed.

* When Lily is run from Apache, the library that binds them together registers the `server` package within Lily. This allows Lily to access the API listed at the bottom of this file, but only after explicitly saying `import server`. Requiring this import (instead of assuming it), is intentional. Doing so means it is possible to run a file meant for the server from the command line, but with a `server.lly` file that stands in place of the server. That, in turn, can be used for testing.

* The imports that a file does are always in tagged mode. This means that you can have files that are only meant to be code and do not have `<?lily ... ?>` tags in them. This has the added benefit that you can 'drag and drop' your code-only files right alongside your other server files if you so wish. Lily will only serve files that have `<?lily` at the absolute first part of them (as a means of proving that the file is to be served. The initial tag can be empty though).

* Server variables (get, post, env) are provided through `server::get`, `server::post`, and `server::env` respectively, as hashes that use a string to map to a Tained string (`hash[string, Tainted[string]]`). The Tainted datatype does not allow direct access to contents, but instead requires that a sanitizing function be sent to it before accessing the contents. This prevents code such as `server::write(server::get["username"])`.

* Lily internally assumes that all strings are valid utf-8 and zero terminated. If any key+value pair has invalid utf-8 within it, then the pair will be excluded from the server variables.

# Classes

This module does not have any classes.

# Methods

`server::escape(string):string`

This encodes a string so that it is suitable for being directly printed to the server. `&` becomes `&amp;`, `<` becomes `&lt;`, `>` becomes `&gt;`.


`server::write(string)`

This function will write a string directly to the server without escaping it at all. However, there is a caveat: This function will only work on string literals. The reason for this, is that string literals are considered to be safe (You wouldn't XSS yourself, right?). If a string is passed to this function that is not a literal, then `ValueError` is raised.


`server::write_raw(string)`

This function takes any kind of a string (not just literals) and writes it directly to the server. It is assumed that the user has properly escaped the string using `server::escape`.


# Variables

`server::post: hash[string, Tainted[string]]`

This contains all variables that were sent to the server using HTTP post. If no variables were sent, then this is empty.


`server::env: hash[string, Tainted[string]]`

This contains the server's environment variables.


`server::get: hash[string, Tainted[string]]`

This contains all variables that were sent to the server using HTTP get.


`server::httpmethod: string`

The http method that was used, in all caps. Common values are POST, GET, PUT, and DELETE

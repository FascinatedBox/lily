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

If you've used PHP, it's about the same. When run from Apache, the interpreter is put into tagged mode. This means that it expects code to be between `<?lily ... ?>` tags, and everything else will be sent to the client as pure html. There are some differences between Lily and PHP though:

* Files that Lily imports are imported in code-only mode. This means it is impossible to import a file that will accidentally send headers. This also forces a separation between files that have html and are to be served, and files that are not meant to be served.

* Files that Lily is to serve must have `<?lily` as the absolute first part of them, even if it is empty. If this is not present at the absolute beginning of the file, Lily will refuse to serve it. This is to prevent the first tag coming in after headers are sent, and also to prevent serving files that are just code.

* Lily exposes server variables as being within a server namespace, rather than as superglobals.

* Lily employs a technique called dynaloading. If there is no reference to, say, the `server::post` variable anywhere in the code, then the server's post information will not be loaded into Lily at all.

# Classes

This module does not have any classes.

# Methods

This module does not have any methods.

# Variables

`server::post: hash[string, string]`

This contains all variables that were sent to the server using HTTP post. If no variables were sent, then this is empty.


`server::env: hash[string, string]`

This contains the server's environment variables


`server::get: hash[string, string]`

This contains all variables that were sent to the server using HTTP get.


`server::httpmethod: string`

The http method that was used, in all caps. Common values are POST, GET, PUT, and DELETE

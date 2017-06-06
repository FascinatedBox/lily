/**
library core

Psuedo root of all pre-registered modules.

This is a fake module created to link together all of the modules that are
automatically loaded in the interpreter.

Except for the `builtin` module, all modules listed here can be imported from
any file inside of the interpreter. This is unlike other modules, which are
loaded using relative paths. These modules can be imported like `import sys`,
then used like `print(sys.argv)`.

The `builtin` module is unique because the classes, enums, and functions are the
foundation of the interpreter. Instead of requiring it to be imported, the
contents of `builtin` are instead available without a namespace.
*/

/**
PackageFiles
    lily_pkg_builtin.c
    lily_pkg_random.c
    lily_pkg_sys.c
    lily_pkg_time.c

*/

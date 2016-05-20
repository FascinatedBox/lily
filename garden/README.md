Garden
======

This is my attempt at creating a simple "package manager" for Lily. At the
moment, the script is as basic as it can possibly be, but that means that there
is a lot of room for improvement.

Currently, the only command for it is "install". Example usage:

```
garden install jesserayadkins/lily
```

What garden does is to create a `packages` directory (if there isn't one), and
to checkout `jesserayadkins/lily` on github. It will attempt to cmake, then make
it. From there, supposing you have a script in the origin directory, that
starting script can do, say, `use lily` and have access to whatever is exported.

This tool is provided as a minimum effort proof-of concept, and should not be
used by anyone seriously.

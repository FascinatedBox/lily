Garden
======

This is my attempt at creating a simple "package manager" for Lily. At the
moment, the script is as basic as it can possibly be, but that means that there
is a lot of room for improvement.

There is currently only one command. Example usage:

```
// The default file will be parsed ('.garden')
./garden.py install

-- or --

// Download directly from a provider (github only at the moment)
./garden.py install github FascinatedBox/postgres = 0.0.1
```

## Install

What garden does is create a `packages` directory (if there isn't one), and
to checkout `FascinatedBox/lily` on github. It will attempt to cmake, then make
it. From there, supposing you have a script in the origin directory, that
starting script can do, say, `import postgres` and have access to whatever is
exported.


This tool is provided as a minimum effort proof-of concept, and should not be
used by anyone seriously.

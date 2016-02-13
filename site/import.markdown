import
======

It's hard to write a useful program without being able to include other files. Lily allows the inclusion of other files using `import`.

```
# fib.lly
define fib(n: Integer) : Integer
{
    if n < 2:
        return n
    else:
        return fib(n + 1) + fib(n - 1)
}

var example = 1

# other.lly
import fib

fib::fib(10)

print(fib::example)
```

When a file is imported, the name that was given to import it acts as a namespace of sorts. Since the file was named 'fib', then the functions, classes, and variables inside of it must be accessed using that same namespace.

If a module defines classes, then the classes of that module can be used in declarations:

```
# json.lly
class Error(message: string) > Exception(message)
{
    ...
}

# main.lly
import json

define checkJsonError(e: json::Error) { ... }

try:
    ...
except json::Error:
    ...
```

It's also possible to import modules using other names:

```
import fib as f

f::fib(10)
```

# Modes

One may recall that Lily has two modes of being run. The normal mode is where everything is code. In the other mode, code is between `<?lily ... ?>` tags, and everything else is written as straight html (to apache for mod_lily, and to stdout if not).

When a module is imported, it is always imported in non-tag mode. This is intentional, and has many benefits:

* It forces a separation of concerns. One set of files contains logic, and, for tagged mode, another set contains the glue and the layout.

* You can import the same set of files, regardless of the mode you are running. Got a neat library of modules? Drag and drop them into the server.

* If Lily is run in tagged mode, it will refuse to do anything if a `<?lily` starting tag is not at the very front of the first file. This prevents people from getting dumps of your code-only files.

* Since files are not imported in tagged mode, there is no chance that they will accidentally print something to the server. Ever get a 'headers already sent' in PHP because an included file accidentally had a newline? In Lily, that's simply not possible.

# The diamond problem

It's possible that one file will import two other files, and each of those files will import a single file between them. This common is referred to as the diamond problem, and it's a common issue with multiple inheritance. However, the issue also applies here.

To explain how Lily deals with this, consider the following setup:

```
# four.lly
value = 10

# three.lly
import four
four::value += 1

# two.lly
import four
four::value += 1

# one.lly
import two, three
```

The actions are as follows:

* one.lly is run. This calls for an import to two.lly

* two.lly is imported, and calls to import four.lly

* four.lly is imported, and defines value as 10

* two.lly increases four::value by 1

* three.lly is imported. The call to import four.lly is ignored, because something else already imported it. four::value is still increased by 1.

The result is that four::value is set to 2. Lily does this by running the toplevel part of each import once, and only once.

Finally, this example shows how multiple files can be imported at the same time.

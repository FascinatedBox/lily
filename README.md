Linux: [![Linux Build](https://travis-ci.org/FascinatedBox/lily.svg?branch=master)](https://travis-ci.org/FascinatedBox/lily)

Windows: [![Windows Build](https://ci.appveyor.com/api/projects/status/github/FascinatedBox/lily?svg=true)](https://ci.appveyor.com/project/FascinatedBox/lily)

### Sample

```
print("Hello from Lily!")

class Cat(name: String)
{
    # The body of a class is the constructor.
    # Properties start with @<name>
    var @name = name
    print($"My name is ^(@name).")
}

# Types can almost always be inferred. This is List[Cat].
var cats = [Cat("Muffin"), Cat("Snowball")]

# a |> b is the same as b(a)
cats.map(|c| c.name) |> print

# Option is either Some(A), or None.
var maybe_cat: Option[Cat] = None

match maybe_cat: {
    case Some(s): print("Cat.")
    case None:    print("None")
}
```

### Why Lily?

* Lily is statically-typed, but with a reference implementation as an interpreter. This gives the benefit of type safety, with a fast turn around time that comes with most interpreters.

* The interpreter can execute files in a template mode or a standalone mode. In template mode, code is between `<?lily ... ?>` tags, allowing you to use Lily where you may have once used PHP. The `apache` folder contains the code to get a basic `mod_lily` working within the Apache web server.

* Internally, Lily's state is managed inside of a C struct and the interpreter does not write global data. This allows embedding one or even multiple interpreters independent of each other. The interpreter can be extended in C to provide new classes, methods, functions, and variables. More information can be found within `src/lily_api_*.h` files, notably `src/lily_api_value.h`. Proper documentation will be coming soon.

* The interpreter's internals are tightly packed, so that it uses very little memory. The interpreter has also been designed to only load builtin methods, functions, variables that are needed. This system, which I have termed dynaload, extends to shared libraries that the interpreter loads as well as being woven in the core itself. dynaload, along with tight packing, allows the interpreter to have a very low overhead.

* Memory is managed by refcounting, with garbage collection for handling cycles. The interpreter uses the type information it has to tag objects that may become circular, so that simple objects do not impose a penalty. This design means there is no heap that must be compacted after garbage collection. More importantly, this design makes it easy to compose whole programs that will never be paused by a garbage collection by avoiding cyclical structures.

### Install

[Want to try it first? Here's a sandbox.](https://FascinatedBox.github.io/lily/sandbox.html)

You need `cmake` and a compiler that understands C11.

```
cmake .
make
make install
```

To build and use `mod_lily`:

```
cmake -DWITH_APACHE=on .
make
make install
# Restart apache

# Edit httpd.conf to add:
LoadModule lily_module <where cmake installed mod_lily.so>

# To any directory block, add:
SetHandler lily

# Test it (note: a <?lily tag must be at the very start.
<?lily
use server
server.write("Hello")
?>
```

### Resources

Lily is a very young language and the community is still growing.

- IRC: You can chat with other Lily users on [freenode #lily](https://webchat.freenode.net/?channels=%23lily).

- Tutorial: https://FascinatedBox.github.io/lily/tutorial.html

- Reference: https://FascinatedBox.github.io/lily/reference.html

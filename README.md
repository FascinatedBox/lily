## Lily is a programming language focusing safety and expressiveness.

Linux: [![Linux Build](https://travis-ci.org/jesserayadkins/lily.svg?branch=master)](https://travis-ci.org/jesserayadkins/lily)
Windows: [![Windows Build](https://ci.appveyor.com/api/projects/status/github/jesserayadkins/lily?svg=true)](https://ci.appveyor.com/project/JesseRayAdkins/lily)

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
var cats = [
    Cat("Muffin"),
    Cat.new("Fuzzy"),
    "Snowball" |> Cat]

# a |> b is the same as b(a)
cats.map{|c| c.name} |> print

# Option is either Some(A), or None.
var maybe_cat: Option[Cat] = None

match maybe_cat: {
    case Some(s): print("Cat.")
    case None:    print("None")
}
```

Lily brings ideas from a wide span of different programming languages. Influences include C, Python, Lua, Ruby, F#, Rust, and Haskell, in no particular order. The ideas come together to form a unique language, a salad like no other.

### How Lily stands out from other languages:

##### Statically-typed, but interpreted

I thought, briefly, about using llvm and making a compiled language. But I wanted something different. One of the ways that Lily stands out is how fast the parser is. Take `test/pass/check_list_methods.lly` for example. It's about 200 lines, and it runs in .007 seconds on my machine, including parsing and execution. A big focus during Lily's development has been about not just execution speed, but also parsing speed.

##### Refcounted first, garbage collected second

Lily's preferred way of managing values is to use reference counting. Reference counting has the benefit of giving consistent, and more easily measured performance. There's no fear of the garbage collector suddenly leaping into action right at the worst time, or having to tune the collector to minimize pauses.

Lily's garbage collection is both precise and simple. Any value that has a potential to become cyclical is given a tag. When enough tags accumulate, regardless of how many values are present, a mark + sweep action is performed.

Lily uses the type system to be smart and tag as little as it can. Types are grouped into three categories, to aid in that. The first category, simple, is never tagged. These are things like `Integer` and `String`. `Lists`, and `Hashes` can be speculative: They may or may not hold a tagged value, but are not tagged themselves. The third group, tagged, is reserved for values like `Dynamic` for which the contents can vary.

##### Embed, extend, and more

Similar to Lua, everything about the Lily interpreter is boxed into a state object. The interpreter never writes to global data, so it's safe to embed one or more Lily interpreters in your program.

Lily can also be extended. The postgres packages gives Lily access to a Postgres Conn object, as well as Postgres query result objects. These objects are provided to Lily in such a way that Lily can automatically close them up and free them for you, reducing the potential for leaks. More on that later.

Lily can also be used as a templating language, with code between `<?lily ... ?>`. The mod_lily package can be used by Apache as a way to get Lily to serve web pages. This functionality is backed into the core of the language, and not bolted on.

##### Dynaload

A common problem in many interpreted languages is how to make functions written in C (or something else) available to the language. When you write a program in Lily, regardless of how big it is, you probably won't use all of Lily's built-in functions. You probably won't use every method of every class, you may not use exceptions. This applies to packages like Postgres too: You probably won't use every function that's in there either.

A better strategy, one might argue, is to load things only when they are absolutely needed. For example, if you load Postgres, but never use `Conn`, then `Conn` shouldn't be loaded. But, for example, once `postgres.Conn.open` is seen, then both `postgres.Conn` (the class) and `postgres.Conn.open` (the method) should be loaded and made available.

The above strategy is what Lily terms dynaloading, and it's woven deep into the parser. But on the outside, the dynaload table that is provided is a mostly-human-readable list of strings. 

##### Low, low memory use

A large focus in Lily's development has been in reducing memory use. Dynaloading allows the interpreter to only spend memory allocating information for the classes, the methods, the enums, and so on that you're going to actually use. Zero waste. A lot of time has also been spent in packing structures smartly to reduce their memory cost.

So even though Lily has a lot of features: Enum classes, user-defined classes, exceptions, and generics, you'll find that Lily doesn't hog memory.

##### Easy install

Do you have a modern C compiler? How about CMake? Then you've got everything you need. Building Lily is as easy as:

```
cmake .
make
make install
```

There are additional options for building the interpreter, which are documented on Lily's website.

##### Community

Lily is a very young language and the community is still growing.

- IRC: You can chat with other Lily users on [freenode #lily](https://webchat.freenode.net/?channels=%23lily).

##### Learn more?

[Here's a more complete tutorial that goes through the various language features.](https://jesserayadkins.github.io/lily/tutorial.html)

Want to try Lily before you download it? [Here's a sandbox provided by compiling Lily with emscripten.](https://jesserayadkins.github.io/lily/sandbox.html)

Documentation for built-ins can be found [here.](https://jesserayadkins.github.io/lily/reference.html)

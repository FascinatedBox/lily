<div align="center">
  <picture>
    <img alt="Lily Logo" src="https://lily-lang.org/assets/lily-logo.svg">
  </picture>
</div>

[![Discord](https://img.shields.io/discord/1416899236196253850)](https://discord.com/invite/yxspNFzfDv)
[![Pipeline Status](https://gitlab.com/FascinatedBox/lily/badges/main/pipeline.svg)](https://gitlab.com/FascinatedBox/lily/-/commits/main)
[![Windows Build](https://ci.appveyor.com/api/projects/status/gitlab/FascinatedBox/lily?svg=true)](https://ci.appveyor.com/project/FascinatedBox/lily)
[![Coverage Report](https://gitlab.com/FascinatedBox/lily/badges/main/coverage.svg)](https://FascinatedBox.gitlab.io/lily/src/index.html)

## Lily

Lily is a programming language focused on expressiveness and type safety.

## Sample

```
scoped enum Color { Black, Blue, Cyan, Green, Magenta, Red, White, Yellow }

class Terminal(public var @foreground: Color, width_str: String)
{
    public var @width = width_str.parse_i().unwrap_or(80)

    public define set_fg(new_color: Color) {
        @foreground = new_color
    }
}

var terms = [Terminal(Color.White, "A"), Terminal(Color.Red, "40")]

terms.each(|e| e.width += 20 )
     |> print
```

## Features

#### Templating

By default, Lily runs in **standalone** mode where all content is code to
execute. But Lily can also be run in **template** mode. In **template** mode,
code is between `<?lily ... ?>` tags. When a file is imported, it's always
loaded in **standalone** mode, so that it doesn't accidentally send headers.
Files that are imported are also namespaced (no 'global namespace').

#### Embeddable

Lily may be a statically-typed language, but the reference implementation is an
interpreter. The interpreter as well as its API have been carefully designed
with sandboxing in mind. As a result, it's possible to have multiple
interpreters exist alongside each other.

#### Shorter edit cycle

Another benefit from having the reference implementation as an interpreter is a
shorter turn around time. The interpreter's parser is comparable in speed to
that of languages using an interpreter as their reference.

## Building

You need a C compiler and CMake (3.0.0 +). There are no external dependencies.

To build Lily, execute the following in a terminal:

```
cmake .

make
```

Note: Windows users may need to add `-G"Unix Makefiles"` to the end of the cmake
invocation.

The above will build the `lily` executable, as well as a liblily that you can
use with your program. It also builds `pre-commit-tests`.

## Running tests

The centerpiece of Lily's testing is `test_main.lily` in the `test` directory.
That file imports and invokes a large number of tests that cover a lot of Lily.

The `make` command also builds `covlib` and `pre-commit-tests`. No additional
commands are necessary. `covlib` is a library that tests some parts of Lily that
native code can't test. `pre-commit-tests` is a special runner that executes
`test_main.lily`.

To run Lily's tests, execute `pre-commit-tests` from the directory it's in after
building Lily.

## Resources

* [Documentation](http://lily-lang.org)

* [Builtin module reference](http://lily-lang.org/core/module.core.html)

* [Try it in your browser](http://lily-lang.org/intro-sandbox.html)

## Packaging

The [lily-garden](https://gitlab.com/FascinatedBox/lily-garden) repository
contains a package manager (Garden) that simplifies the install of Lily
packages.

## License

[MIT](https://gitlab.com/FascinatedBox/lily/blob/main/license.txt)

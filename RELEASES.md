Version 1.10 (2019-10-10)
=========================

What's new:

* Lily's library importing now works on Windows. AppVeyor (Windows CI) now runs
  the full Lily test suite on Windows using ninja (#411).

* Findlily.cmake has been created to allow cross-platform extension libraries.
  Libraries in garden have been updated to use it.

* A lily_optional_* family of functions has been added to make working with
  optional arguments easier (#366).

* The interal test suite has been completely rewritten, now using a style
  similar to testkit. The new test suite is far more organized, and in the
  process a few useless tests were removed. The rewrite has shown areas that
  badly need extra testing, as well as opening up interesting possibilities that
  will be explored by future releases (#444).

Changes:

* Modules registered to the interpreter are now only visible from the first
  package (#352).

* The scoop type can no longer be on the right side of an initialization
  expression (#445).

Version 1.9 (2019-7-10)
=======================

This is a longer release (4 months instead of the usual 3). This release
contains several fixes and changes to allow creating testkit, a Lily testing
library. Some changes were made in hopes of leveraging the interpreter to have
native documentation generation. Work on that remains incomplete since there
are mechanisms that the interpreter needs in order to properly implement it.

What's new:

* New api call: `lily_validate_content` (#425). This implements a syntax-only
  pass over content, primarily for a future interpreter-based docgen.

* Added `lily_value_group` and `lily_value_get_group`. The first is an enum, and
  the second is a function. These can be used to get information about values
  that are not inside the interpreter's stack (#434).

Changes:

* Remove 'copy_str_input' from config (#427). The string data sent to parse and
  render functions is no longer copied, but path data is.

* Containers pushed through the api are now always gc speculative (#433). This
  fixes potential leaking/crashing if gc tagged values are put inside of them.
  One example of that happening is a `List.map` call that maps values into new
  circular classes.

* `calltrace`'s output now matches exception output (#419).

Fixes:

* Class method dynaload search is now recursive (#428). This was encountered
  when trying to inherit from spawni's `Interpreter`.

* Static methods can now use class methods if they send a self (#430).

* Import functions now account for missing a 'use' function (#431). This fixes
  crashing when an import hook tries to use an import method without first
  setting a directory.

* Mismatched `Function` return types now properly narrow down to `Unit` (#429).

* `lily_import_string` no longer writes the data as the path (#435). This was
  causing very strange error messages.

* Docblocks now work on public methods (#432).

Version 1.8 (2019-3-10)
=======================

This is a short release (2 months instead of the usual 3). Since this is an even
release, the plan was to write a testing system while leaving the core alone.
However, the testing system needs several internal adjustments that don't fit
with an even release. As a result, this release is being pushed early. The next
release, 1.9, will absorb the extra month. This should not happen again.

What's new:

* Symtab symbol internals have been shuffled around a bit. These adjustments
  have no bearing on the workings of scripts. They were done to make a future
  inspection package easier by having elements such as the next pointer of a
  symbol in a consistent place.

* lily.h now properly marks `lily_import_use_package_dir` as being available. It
  was available, but due to a typo, `lily_import_use_local_dir` was mentioned
  twice.

* Store the main module's path as a string literal. This lets spawni not worry
  about the lifetime of the path that it's sending. (#426).

Fixes:

* `__function__` in `__main__` no longer crashes (#420).

* Fix error message display when shadowing a class constructor parameter (#421).

* Fix module lookup not using the proper path, causing modules to be created
  that did not need to be. Consider a situation where 'a' directly imports 'b'
  and 'c'. Module 'b' directly imports 'c'. All modules are in the same
  directory, so there should only be one copy of 'c'. Because the wrong path was
  used, 'a' and 'b' had different copies of 'c'. (#423, reported by @Zhorander).

* Super ctors can't use base members in init (#424).

Version 1.7 (2019-1-10)
=======================

What's new:

* Lily now has a built-in math library. Thanks @Zhorander.

* `Double` now allows for unary negation. (#401)

* `List` can now be subscripted by `Byte`. (#403)

* Containers (`List`, `Hash`, and `Tuple`) now support trailing commas. (#409)

* The import hook and content loading systems have been largely rewritten,
  breaking all existing embedders. The new content loading api is easier to
  test, and potentially extendable too. The new import hook system allows for
  writing an xUnit-styled testing system. (#416)

Changes:

* `File.each_line` now includes the newlines in lines it reads. This makes it
  consistent with `File.read_line`. (#382)

* Shorthand constructor vars `class Example(public var @x) { ... }` now require
  a scope. The last release made it optional to have a scope. This release makes
  it required. (#400)

* Optional arguments now use a new opcode `o_jump_if_set`. Previously, they used
  `o_jump_if_not_class` with `0`. This allows the internals to specialize both
  cases. (#395)

* The name of the function that backs an imported file is now `__module__`.
  Previously, it was `__import__`.

* If a foreign library causes an exception, the traceback uses the library name
  in brackets if it's registered, or the path to the library if it isn't.
  Previously, they all wrote `[C]`. (#399)

* Errors should no longer include `./` at the front of traceback. (fixes #355).

* `List.push` and `List.shift` now return `self` to allow chaining instead of
  `Unit`. (#405, #407)

* The name of a module (and the dynaload/info tables) are now derived from the
  name provided to `import`. Previously, it was derived from the path that was
  given. This allows one library to provide multiple sets of dynaload/info
  tables. (#415)

Fixes:

* The backslash-newline string escape code that strips leading whitespace from
  the next line was not working inside a lambda (#401).

* Class properties could not start with B (#402). Found by @Zhorander.

* Closing over `self` in an enum method no longer crashes (#392).

* `List` and `Hash` literals can now narrow their values. Previously, the
  following code would fail on the last line because the type would not narrow
  down: (#404)

```
class One {}
class Two < One {}
class Three < Two {}

var a = [One(), Two(), Three()]
var b = [Three(), Two(), One()]
```

Version 1.6 (2018-10-10)
========================

What's new:

* Lily's package manager Garden has been entirely rewritten. The new version
  is hosted in a different repository:
  https://gitlab.com/FascinatedBox/lily-garden. The new Garden is able to
  install requirements of a package, build foreign packages, and also has a
  whitelist that includes sha verification. The old Garden has been removed.

* Shorthand constructor vars now require a scope (#333). Previously, shorthand
  constructor vars were automatically public and did not allow a scope. The old
  style will be removed in the next release.

* Work on increasing coverage (#342) led to uncovering several bugs. The issue
  is not completely solved yet, as there are still spots of the interpreter
  left to cover. However, covering them would involve breaking API as well as
  changing internals. The issue will be closed during the next release.

* Values no longer store class id information in their 'flags' section. This is
  an internal change that should hopefully fix an endian-related bug detailed
  in (#327).

Fixes:

* Fix exception capture inside of a closure (#378).

* Fix crash when double-calling a single keyword argument (#379).

* `Function` return type should now always narrow to `Unit` (#380).

* Don't silently stop import on invalid character in the source (#381).

* Rewind should not save broken imports (#383).

* Fix `String.strip` unicode handling (#384).

* Fix crash on invalid use of import (ex: `import ?`) (#385).

Version 1.5 (2018-7-11)
=======================

What's new:

* New api function: `File.flush`.

Changes:

* The class `Dynamic` has been removed from the language.

* Blocks now require braces instead of them being optional.

* Dynaload now uses info and call tables, so libraries compiled against older
  versions of Lily will need to have `bindgen.lily` run on them again.

Version 1.4 (2018-4-10)
=======================

What's new:

* New api functions: `List.get`, `List.zip`, `List.fill`.

* (#273) Coroutines, one of the longest standing requests in the tracker, have
  now finally been implemented. Coroutines are implemented behind a `Coroutine`
  class. Internally they work by creating a vm and exposing it as a value. How
  coroutines work is similar to how they work in Lua.

Changes:

* `Hash.get` now returns an `Option` and no longer requires a default value.
  This function predates `Option` which is why it didn't do that to begin with.

* (#369) Lily's builtin Random library has been changed to a different source.
  The new source is released under The Unilicense, whereas the old source was
  under a license requiring attribution. For what is hopefully the time being,
  the Random library is now restricted to ranges 32 bits wide instead of the
  full 64 bit spectrum. It is expected that this will not be a huge problem.

Fixes:

* (#365) Fixed a rare potential crash when calling a foreign function taking
  optional arguments.

* The vm now does a gc sweep after dropping the register. This avoids a memory
  leak in the case of a global register holding a self-linked value.

Version 1.3 (2018-1-10)
=======================

What's new:

* Keyword arguments are a huge highlight of this release. Keyword arguments are
  written as `:<identifier>`. Keyword arguments are handled at emit-time,
  meaning that there is zero runtime cost to most keyword argument calls. The
  exception to that is in the interesting case where optional arguments and
  keyword arguments come together. Because keyword arguments are processed at
  emit-time, they are only usable with functions called by name or variants.
  Tooling has been updated to allow keyword arguments from foreign functions.

* The `forward` qualifier to allow forward definitions. While there are pending
  forward definitions, the programmer cannot import or declare vars to prevent
  variables from being referenced before their use. This qualifier works on both
  toplevel definitions and methods. Forward definitions only take types (names
  are not permitted), and thus are not eligible for keyword arguments.

* The `static` qualifier to allow for class methods that do not take an implicit
  self. These are nice for utility classes.

* `import` no longer accepts slashed paths like `import x/y/z`. Instead, paths
  provided must either be a single identifier `import x` or a string literal
  `import "x/y/z"`. String literals are checked, however, to make sure that only
  the forward slash is used (which becomes the appropriate path character),
  along with other safety checks. This was done to make parsing import simpler,
  as it now accepts only a single token.

* The `lily.h` api file of the language has been completly redocumented. It is
  now processed with NaturalDocs to provide documentation for Lily's api calls.

Version 1.2 (2017-10-10)
========================

What's new:

* Optional arguments can now be whole expressions. Previously, they could only
  be a single value.

* Get around to implementing bitwise not `~` and making `^=` work right.

* The backend scoop types are now `$1` and `$2` instead of `1` and `2`.

* Class methods no longer have `public` as a default scope. Instead, a user must
  explicitly specify `public` / `protected` / `private`.

* Expressions such as `var v = []` no longer use `Dynamic` to solve for unknown
  types. Code such as the above is now a syntax error.

* It is now possible to directly import symbols from another module through
  `import (x, y) z`.

* A large number of tests were added which resulted a few bugs showing up and
  being fixed.

Version 1.1 (2017-7-10)
=======================

What's new:

* The `match` keyword now works against classes. It decomposes the value to one
  new var instead of rewriting the origin var. It works against any class that
  does not use generics.

* `match` has also been extended to work on `Dynamic`. The old style of using
  a cast (`.@(type)`) now effectively does nothing. It may do something in the
  future or be removed.

* Addition of `++` which stringifies and joins any two values together.

* A configuration struct is now back. It includes an import hook that calls back
  into a user-defined function. That user-defined function can supply the paths
  that it wants to. This may be useful in the future for bootstrapping source
  files.

* `dyna_tools.py` no longer exists. Users should instead see the `lily-parsekit`
  repo. That repo contains `bindgen.lily` for generating C bindings and
  `docgen.lily` for generating docs from files with C bindings. This repo may be
  the foundation of Lily's tooling suite in the future.

* The testing suite is now run by an executable that is a thin layer over a
  primarily Lily-driven test suite. The testing suite is mostly the passing and
  failing tests from before. Failing tests are run through subinterpreters which
  I find rather nifty.

* Several adjustments have been made to closures to finally make them stable.

* The value api has been changed so that users no longer create values out of a
  register and push it on later. Instead, creating a value that holds other
  values puts it into a register, then returns the raw value. This means values
  are no longer created without a state object.

* `\/` escape character added. It's `\\` on Windows and `/` elsewhere.

* Lots of internal cleanups and a few performance improvements.

Version 1.0 (2017-2-1)
======================

What's new:

* Most of the work in this release was done in the api. A massive amount
  of changes were made, breaking everything written against a prior
  version of the language. The result is now a cleaner api, broken up
  into msgbuf, embed, and value operations.

* The site has been redone, and the `lily-lang.org` domain has been
  acquired. For now, it redirects to the gh-pages of
  `FascinatedBox/lily-site`, but will eventually stand on its own.

* The api is now stable enough that apache is outside of core, along
  with the `Tainted` class that it exported.

* Added a basic random and time library to the builtin library. Those
  will eventually be pushed out to the standard library.

* File suffix is now `.lily`.

Syntax:

* `Either` is now `Result`, with the members being `Success` and
  `Failure`. 

* `List.fill` renamed to `List.repeat`.

* `String.format` and `File.read` added to builtins. These were brought
  over from @stevedonovan's lily-extras.

* Lambdas use `(|<args>| ... )` instead of `{|<args>| ... }` (braces
  versus parentheses). This change makes it so braces are limited to
  blocks.

* Added the `scoped` keyword to denote that variants should always be
  qualified by their enum name (versus the old style of needing a dot
  before each variant name). Scoped variants now always take their enum
  name first in every case (suggested by @radiofreejohn).

Version 0.18 (2016-10-9)
========================

Neat features and the foundation for 1.0.

What's new:

* The backend now has a proper api. Callers now receive a lily_state,
  and use lily_arg_* functions with indexes to receive values. Calls
  back into the interpreter are done through lily_push_* functions.
  See lily_api_* files, notably lily_api_value.h.

* The api is now complete enough that postgres has been moved outside
  of Lily's core. The next release will include moving emscripten and
  apache outside of Lily's core.

* I rewrote a large part of how functions are transformed into
  closures. This has made closures much more stable.

* The interpreter can now rewind parse state when there is a parse
  or vm error. This means the interpreter's state is no longer
  undefined if there is an error. A simple repl has been created by
  creating rewinding, and creating an interpreter inside of an
  interpreter.

Syntax:

* `use` is gone now. `import` has been given extra paths to load from,
  so that it does what `use` did before.

* Functions that previously returned nothing now return a `Unit` type.
  This distinction allows code such as `[1, 2, 3].map(print)`, because
  there is no distinction between functions that return a value, and
  those that return `Unit`.

* `class.new` is now hidden. Use `class()` instead to construct values
  of classes with a constructor.

* `match` can now use `_` in cases to skip decomposition.

* `match` now allows using an `else` default clause.

* Class methods can now use `self` as a return type. This allows an
  extended class to use a base class method, but still retain the
  extended class type.

Credits:

* @a-p- and @stevedonovan, for reporting numerous bugs.

Version 0.17 (2016-7-10)
========================

This is huge not because of features, but because Lily turns 5 today.

What's new:

* Expanded platform support:

  Windows, through MSVC (credit: @TyRoXx)

  Windows, through mingw (credit: @stevedonovan)

  BSD (credit: @alpha123)

* Create a script, garden.py, for doing package installations.
  (credit: joey.clover, @azbshiri)

* Classes in Lily are now grouped into three categories:

  Simple, which can never be cyclical.

  Speculative, which may hold cyclical values (but cannot be cyclical
  themselves).

  Tagged, which should be tagged.

  `List`, `Hash`, and `Tuple` are now considered speculative, and no
  longer gc tagged. This makes it easier to write whole programs that
  do not create gc tagged objects.

* Lily has a new keyword, `use`, which is for loading packages. A
  package is a collection of different modules. `import` is now
  relegated to being relative to the root of the current package. This
  change makes it easier to install a package that might depend on a
  second package at a particular version.

  Do note that the packaging system is still experimental.

* Created two new types, `~1` and `~2` that are currently restricted
  to the backend. These types are unique in that they remember what
  they are matched against, but perform no type checking. This allows
  the following definitions:
  
  `Tuple.push[A](self: Tuple[~1], value: A): Tuple[~1, A]`

  and

  `Tuple.merge(self: Tuple[~1], other: Tuple[~2]): Tuple[~1, ~2]`
  
  These definitions work on all `Tuple` values, regardless of arity.

* Created `dyna_tools.py`, so that creating packages and documentation
  for those packages is easier.

* Created `List.join`.

Version 0.16 (2016-4-2)
=======================

New stuff:

* All built-in classes are now titlecased. Previously, only the ones
  that derived from Exception were titlecased (and Tainted). This was
  done to make the language be more consistent, as there is now an
  expectation that all class names will start with a capital letter.

* The '::' token is now the '.' token. This makes access consistent,
  and with no need to remember if something is an instance or a
  property.

* What was previously any is now Dynamic. Dynamic is created
  explicitly, and is allowed to nest. The name and functionality are
  inspired by Haskell.

* The interpreter no longer solves types. As such, the interpreter now
  uses erasure instead of reification. This was done for a few
  reasons:
  1. The vm had several problems trying to get this right. Often, it
  would fail when emitter and parser would succeed, but never the
  other way around.
  2. Runtime solving was occasionally useless. For example, a
  List.append-like function will work the same regardless of what
  types are at play inside of it.
  3. Suspicions that runtime solving could get in the way of future
  attempts at concurrency.

* The interpreter finally understands unification. When building a
  List or Hash, the elements are unified down to a common bottom type.
  Furthermore, trying to create a List or Hash that does not have a
  consistent type is now an error (previously, the elements were
  converted to type 'any').

* `Option` and `Either` enums are now baked into the core.

* Enums are now allowed to be optional arguments, provided that their
  default value is an empty variant. What makes this useful is that
  you can make a default of, say, `*Option[File] = None` as a means of
  ascribing a default value to effectively any kind of type. This
  assumes, of course, that you're okay with the function transparently
  picking what the default is.

* Interpolation is now supported by starting a string (multi or single
  line) with '$'. Interpolation allows single expressions within a
  string through `^( ... )`.

* String.to_i is now String.parse_i, and returns an `Option[Integer]`
  instead of raising on failure. Similarly, String.find now returns an
  `Option[Integer]` instead of raising on failure. ByteString.encode
  now pushes back an `Option[String]` instead of throwing.

* `print` now prints directly to stdout, instead of the printing impl.
  This is important, because it was previously possible to bypass
  Apache's writing functions through it.

* Lambdas can now specify types for their arguments.

* The site now has an area to try the code online, through a
  emscripten-compiled version of the interpreter.

* Classes can be initialized through `Class()` instead of
  `Class::new()`. This style is preferred now.

* New functions for postgres: Conn.open, Result.row_count,
  Result.each_row, Result.close

Removed:

* `show` is now gone.

* With the introduction of interpolation, `String.format`, `printfmt`,
  and `String.concat`.

* `Double` is no longer considered hashable.

* `postgres.Error` is gone, having been replaced by using an `Either`
  instead.

Bugs fixed:

* Call piping to a variant (`1 |> Some`) was crashing.

* Closing stdout and calling `print` worked. It doesn't now.

Version 0.15 (2015-12-11)
=========================

New stuff:

* Lily's license is 3-clause BSD. Do whatever you want with it, just
  give me credit, okay?

* Exception traceback is now typed `list[string]` instead of
  `list[tuple[string, string, integer]]`.

* Exception traceback is no longer built if there is no `as <name>`
  clause. This makes simple `except <error>` faster in cases where the
  presence of an error is important (and not the trace or message).

* A postgre module has been added. It supports running very basic
  queries.

* The interpreter is a lot smarter about returning a value now. If
  all branches of an if/else, try+except, or match block all either
  return a value OR raise an exception, the interpreter considers
  that entire block to return a value. This makes things like
  fibonacci less...weird.

* Strings are subscriptable and return strings. Offsets are by utf-8
  codepoints. Negative subscripts are allowed too.

* Class-level methods for enums (they get self too, also)

* `f{|| x}` == `f({|| x })`

* `private` and `protected`

* Create a Tainted class. This class represents evil data. Apache's
  post, get, and env are now `hash[string, Tainted[string]]`.

* The apache binding now rejects values from post, get, and env if
  they are not valid utf-8.

* `print` adds a newline. Use `file::write` to not show a newline.

Changes:

* The return of a function is denoted by `:type`. This makes functions
  that take other functions as an argument easier to read.

* `enum class` is now just `enum`

* Tag-mode files must start with `<?lily` instead of just having it
  somewhere. This is partly to prevent accidentally serving files that
  are just code.

* F#'s call-piping (`|>`) has been added.

* boolean class, `true`, `false`.

* Lambdas now support multiple statements.

* string::format / printfmt support %s on anything. Be warned: Both of
  those functions are likely to die in the next release, in favor of
  proper interpolation.

* `RecursionError` => `RuntimeError`

* I fixed the broken behavior of closures that was present in the last
  release.

* `list::append` => `list::push`

New functions:

* string::split, 

* list::clear, list::count, list::delete_at, list::each_index,
  list::fill, list::fold, list::insert, list::map, list::pop,
  list::reject, list::select, list::shift, list::unshift

* hash::each_pair, hash::clear, hash::delete, hash::has_key,
  hash::map_values, hash::merge, hash::reject, hash::select,
  hash::size

Removed:

* list::apply. I'm sorry that I ever wrote that. It's bad.

Bugs fixed:

* Foreign functions can call other foreign functions now. This makes
  `["a", "b", "c"].each(string::upper)` not crash.

* `{|| __file__ }` should not print `(lambda)`.

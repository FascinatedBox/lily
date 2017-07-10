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

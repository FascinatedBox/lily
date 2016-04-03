Version 0.16 (2015-4-2)
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

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

# Contributing

Pull requests are awesome, and highly welcome!

First, you'll need to fork and clone the repo:
```
git clone git@github.com:your_name_here/lily.git
```

You'll need `CMake` and a modern C compiler that supports C11. A recent version of clang or gcc will do. Setup is as easy as:

```
cmake ; make
```

Building the apache module can be done adding `-DWITH_APACHE=on` to `CMake`, postgres through `-DWITH_POSTGRES=on`.

Make your change, and add some tests too.

Running all of the tests is as easy as:

```
python pre-commit-hook.py
```

Push to your fork and [submit a pull request][pr].

[pr]: https://github.com/FascinatedBox/lily/compare/

Now you're waiting on me. I'll try to comment on your pull request in a timely manner, hopefully no more than a couple days. I might mention some changes, or have improvements.

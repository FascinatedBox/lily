# Contributing

Contributions to Lily's development are welcome. There are many ways to help with Lily's development. This guide explains how Lily is organized and how to make contributions to various parts of it.

## Documentation

The official website of the language is lily-lang.org. The website includes a tutorial, an online sandbox for trying Lily code, documentation for Lily's predefined packages, and documentation for Lily's api `lily.h`.

The tutorial documentation, sandbox examples, and website assets are located in the lily-docs repository: https://gitlab.com/FascinatedBox/lily-docs. Scripts that are used to build the website are also included in the repository. If you find an error in the tutorial or an example, you can file a merge request against that repository.

Documentation for Lily's predefined packages and api is in this repository. If you find an error in documentation for either, you can file a merge request against this repository. Once the merge is done, I will update the website to show the new change.

The api documentation is done entirely through [Natural Docs](https://naturaldocs.org). Documentation for Lily's predefined packages and library packages is done through the docgen script located in [FascinatedBox/lily-parsekit](https://gitlab.com/FascinatedBox/lily-parsekit). Problems with documentation generation or binding generation should be filed against that repository.

## Tools

There are several tools that help the development of Lily code.

Lily's package manager is a Python script called Garden, located at https://gitlab.com/FascinatedBox/lily-garden. Garden works off of a whitelist located at https://gitlab.com/FascinatedBox/lily-catalog.

The [FascinatedBox/lily-parsekit](https://gitlab.com/FascinatedBox/lily-parsekit) repository contains bindgen and docgen, both of which are written in Lily. If you want to create a foreign library in Lily, you'll need bindgen to create the bindings (the format is listed in the repository). Once you're done, you can use docgen to build documentation for your library. Your library's documentation will look like the documentation for predefined packages since they also use docgen.

The [FascinatedBox/lily-repl](https://gitlab.com/FascinatedBox/lily-repl) contains a basic repl for Lily for trying out code samples. The repl is a tiny Lily script that uses [FascinatedBox/lily-spawni](https://gitlab.com/FascinatedBox/lily-spawni) to generate a subinterpreter to which code is fed.

## Testing Layout

If you're filing a merge request that adds new syntax to the interpreter or changes existing syntax, you'll need to include tests.

The interpreter's testing suite is contained in the `test` directory. The testing suite is run by calling `pre-commit-tests`. The test suite's main file is `test/test_main.lily`. The `test/test.lily` defines the testing class and creates the initial test object. From there, various testing files are imported. Each testing file adds a series of test cases to be tried.

Testing files are named according to what they cover:

`feature_*.lily`: These files test a certain area of the interpreter. The tests in these files should be expected to pass.

`fail_*.lily`: These are tests that should fail for one reason or another. These are separated from passing tests because features often have numerous failing tests.

`verify_basics.lily`: This tests interpreter intrinsics like truthiness of certain constructs, digit/string collectin, comments, and so forth.

`verify_coverage.lily`: This file is a catchall for tests that don't quite fit anywhere else.

`verify_covlib.lily`: This contains functions that use `covlib` to exercise less-used parts of the interpreter.

`verify_rewind.lily`: This tests how the interpreter rewinds out of improper code. These tests ensure the repl (which relies on subinterpreters) functions correctly.

`verify_sandbox.lily`: This file contains the samples that the interpreter's sandbox uses. These ensure that the interpreter's sandbox won't break on update.

`verify_pkg_*.lily`: These files test a predefined package. Tests of predefined packages should include full coverage of any functions, vars, etc. defined by the package. They should also include all relevant passing and failing tests.

`verify_*.lily`: The remaining verify files are written to test a builtin class or enum of the name provided. These files should contain all relevant passing and failing tests.

## Writing Tests

Some guidelines to follow when writing new test files or adjusting currently-existing test files:

Tests against a particular method of a class or a function of a package should be grouped together. It also helps if the test groups are organized alphabetically.

Testing files are allowed to define methods that they need, but should not rely on global variables. They should not rely on toplevel classes or enums either. Tests that need declarations should run code in a subinterpreter since their contexts are cleaned up when the subinterpreter finishes.

New tests against a particular part of the interpreter should be grouped up with similar tests insofar as possible. If unsure, new tests can be put at the bottom of the relevant testing file.

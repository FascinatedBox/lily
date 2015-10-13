Introducing Lily
================

Lily is an interpreted language that features static typing that's there to help you, rather than to get in the way. It mixes ideas from a number of languages, notably Scala, Rust, Ruby, and Python. 

By default, Lily will treat the entire source file as being code, and run it. This is called standalone mode. The alternative is tag mode. In tag mode, the interpreter will process anything between `<?lily ... ?>` as code, and anything outside as raw HTML.

```
define is_integer(input: string) : boolean
{
    try: {
        input.to_i()
        return true
    except Exception:
        return false
    }
}

# Inferred type: list of strings (list[string])
var to_convert = ["123", "99", "10pigs", "asdf"]

# Drop the ones that can't be converted to integers.
to_convert.select(is_integer)

# Inferred type: list[integer]
var integer_list = to_convert.map{|s| s.to_i()}

print(integer_list)
```

Features:

* Types that doesn't get in the way. A variable's type is determined by the first thing it's assigned to. Beyond that, Lily uses type inference so that the types (and the safety they provide) is still there.

* Code from different files is imported into a namespace. No worries about global namespace pollution between different files.

* Exceptions are baked into the language, and into the core library. No error codes.

* Lily employs a technique called dynaload throughout the interpreter. It won't load information about functions, unless it can verify that a given function will be used. This allows a simple 'Hello World' script to run in under 9K of memory.

* Lily is very fast. The test suite has 350 tests, and a separate instance of the interpreter is booted up for each one. The test runner executes all of these tests in under three seconds.

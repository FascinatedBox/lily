# Lily builtin library

## Definitions and terms
For the apache module, anything sent to 'output' is given to the server as html.
For anything else, 'output' is stdout.

## Globals
Global functions are available in any scope at any time.

#### printfmt(string format, any args...)
This writes the string **format** to output. **args** is a series of values passed. The following format specifiers are allowed:
* %i : An integer
* %d : A double
* %s : A string
```
any a = 10
double b = 5.5
string c = "abc"

printfmt("a is %i, b is %d, c is %s.\n", a, b, c)
# a is 10, b is 5.5, c is abc.
```

#### print(string text)
* This writes **text** to output.

## Class string
Library functions are called using a value of that class, like so:
```
string abc = "abc"
string abcdef = abc.concat("def") # "abcdef"
```

#### string::concat(string _input_, string _to_append_ => string)
This creates a new string based upon two strings joined together.

#### string::endswith(string _input_, string _suffix_ => integer)
Checks if **input** ends with **suffix**. Returns 1 if yes, 0 if no.

#### string::find(string _input_, string _tofind_ => integer)
Attempt to locate the first occurance of **tofind** within **input**. On success, the start of the match is returned (a valid index). On failure, -1 is returned.

#### string::htmlencode(string _input_ => string)
Returns a newly-made string but with the html entities within **input** converted. Here are the conversions done:
* & becomes &amp;amp;
* < becomes &amp;lt;
* > becomes &amp;gt;

#### string::isalpha(string _input_ => integer)
Checks if **input** is composed of entirely alphabetic characters. Returns 1 if yes, 0 if no.

#### string::isalnum(string _input_ => integer)
Checks if **input** is composed of entirely alphanumeric characters. Returns 1 if yes, 0 if no.

#### string::isdigit(string _input_ => integer)
Checks if **input** is composed of entirely numeric characters. Returns 1 if yes, 0 if no.

#### string::isspace(string _input_ => integer)
Checks if **input** is composed of entirely space characters. Space characters are defined as " \t\n\v\f\r". Returns 1 or 0.

#### string::lower(string _input_ => string)
Creates a new string based off of **input** but with all uppercase characters turned into lowercase ones. Utf-8 characters are ignored.

#### string::lstrip(string _input_, string _tostrip_ => string)
Returns a newly-made string with **tostrip** removed from the *left* of **input**. **tostrip** can be a single character or a series of characters. Utf-8 characters also allowed.

#### string::rstrip(string _input_, string _tostrip_ => string)
Returns a newly-made string with **tostrip** removed from the *right* of **input**. **tostrip** can be a single character or a series of characters. Utf-8 characters also allowed.

#### string::startswith(string _input_, string _prefix_ => integer)
Checks if **input** starts with **prefix**. Returns 1 if yes, 0 if no.

#### string::strip(string _input_, string _tostrip_ => string)
Returns a newly-made string with **tostrip** removed from the *left* and *right* of **input**. **tostrip** can be a single character or a series of characters. Utf-8 characters also allowed.

#### string::trim(string _input_ => string)
Returns a newly-made string with the characters " \t\r\n" removed from *both* sides of the string. This is a convenience function that does the same as:
```
input.strip(" \t\r\n")
```

#### string::upper(string _input_ => string)
Creates a new string based off of **input** but with all lowercase characters turned into uppercase ones. Utf-8 characters are ignored.


## Class List
The first type of a list is referred to as **A**
list[A] as the first argument means that the function can take a list of any type.

#### list::apply(list[A] _input_, function callee(A => A))
This calls the function **callee** on each element of **input**. The result of calling **callee** is set onto the corresponding element of **input**.
```
function times_two(integer a => integer)
{
	return a * 2
}

list[integer] integer_list = [1, 2, 3]
integer_list.apply(times_two)
# integer_list is now [2, 4, 6]
```

#### list::append(list[A] _input_, A newelem)
Adds **newelem** to the end of **input**. A syntax error occurs if **newelem** has a different type than **input** contains.
Note: If **A** is an any, then values passed as **newelem** are automatically made into anys.
```
list[integer] integer_list = [1, 2, 3]
integer_list.append(10) # integer_list is now [1, 2, 3, 4]

list[list[integer]] deep_list = [[1, 2, 3], [4, 5, 6]]
deep_list.append([7, 8, 9]) # deep_list is now [[1, 2, 3], [4, 5, 6], [7, 8, 9]]

list[any] any_list = [1, 2.2, "3", [4]]
# any is a special case, because anything can be changed into an any.
any_list.append([5, 6, 7])
any_list.append(89)
any_list.append(integer_list)
```

#### list::size(list[A] _input_ => integer)
Returns a count of the elements within **input**.


## Class Hash
Hash has two different types inside of it: **A** is the key, **B** is the value.

#### hash::get(hash[A, B] _input_, A _to_find_, B _default_value_ => V)
This attempts to find **to_find** within **input**. If the key can be found, then the value associated with that key is returned. If it cannot be found, then **default_value** is returned.

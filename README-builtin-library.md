# Lily builtin library

## Definitions and terms
For the apache module, anything sent to 'output' is given to the server as html.
For anything else, 'output' is stdout.

## Globals
Global functions are available in any scope at any time.

#### printfmt(format: string, args: list[any]...)
This writes the string **format** to output. **args** is a series of values passed. The following format specifiers are allowed:
* %d : An integer
* %f : A double
* %s : A string
```
var a: any = 10
var b = 5.5
var c = "abc"

printfmt("a is %d, b is %f, c is %s.\n", a, b, c)
# a is 10, b is 5.5, c is abc.
```

#### print(text: string)
* This writes **text** to output.

## Class string
Library functions are called using a value of that class, like so:
```
var abc = "abc"
var abcdef = abc.concat("def") # "abcdef"
```

#### string::concat(_input_: string, _to_append_: string => string)
This creates a new string based upon two strings joined together.

#### string::endswith(_input_: string, _suffix_: string => integer)
Checks if **input** ends with **suffix**. Returns 1 if yes, 0 if no.

#### string::find(_input_: string, _tofind_: string => integer)
Attempt to locate the first occurance of **tofind** within **input**. On success, the start of the match is returned (a valid index). On failure, -1 is returned.

#### string::htmlencode(_input_: string => string)
Returns a newly-made string but with the html entities within **input** converted. Here are the conversions done:
* & becomes &amp;amp;
* < becomes &amp;lt;
* > becomes &amp;gt;

#### string::isalpha(_input_: string => integer)
Checks if **input** is composed of entirely alphabetic characters. Returns 1 if yes, 0 if no.

#### string::isalnum(_input_: string => integer)
Checks if **input** is composed of entirely alphanumeric characters. Returns 1 if yes, 0 if no.

#### string::isdigit(_input_: string => integer)
Checks if **input** is composed of entirely numeric characters. Returns 1 if yes, 0 if no.

#### string::isspace(_input_: string => integer)
Checks if **input** is composed of entirely space characters. Space characters are defined as " \t\n\v\f\r". Returns 1 or 0.

#### string::lower(_input_: string => string)
Creates a new string based off of **input** but with all uppercase characters turned into lowercase ones. Utf-8 characters are ignored.

#### string::lstrip(_input_: string, _tostrip_: string => string)
Returns a newly-made string with **tostrip** removed from the *left* of **input**. **tostrip** can be a single character or a series of characters. Utf-8 characters also allowed.

#### string::rstrip(_input_: string, _tostrip_: string => string)
Returns a newly-made string with **tostrip** removed from the *right* of **input**. **tostrip** can be a single character or a series of characters. Utf-8 characters also allowed.

#### string::startswith(_input_: string, _prefix_: string => integer)
Checks if **input** starts with **prefix**. Returns 1 if yes, 0 if no.

#### string::strip(_input_: string, _tostrip_: string => string)
Returns a newly-made string with **tostrip** removed from the *left* and *right* of **input**. **tostrip** can be a single character or a series of characters. Utf-8 characters also allowed.

#### string::trim(_input_: string => string)
Returns a newly-made string with the characters " \t\r\n" removed from *both* sides of the string. This is a convenience function that does the same as:
```
input.strip(" \t\r\n")
```

#### string::upper(_input_: string => string)
Creates a new string based off of **input** but with all lowercase characters turned into uppercase ones. Utf-8 characters are ignored.


## Class integer
#### integer::to_string(input: integer => string)
Returns a string representing the decimal value of the given integer.


## Class List
The first type of a list is referred to as **A**
list[A] as the first argument means that the function can take a list of any type.

#### list::apply(_input_: list[A], callee: function(A => A))
This calls the function **callee** on each element of **input**. The result of calling **callee** is set onto the corresponding element of **input**.
```
function times_two(a: integer => integer)
{
	return a * 2
}

var integer_list = [1, 2, 3]
integer_list.apply(times_two)
# integer_list is now [2, 4, 6]
```

#### list::append(_input_: list[A], newelem: A)
Adds **newelem** to the end of **input**. A syntax error occurs if **newelem** has a different type than **input** contains.
Note: If **A** is an any, then values passed as **newelem** are automatically made into anys.
```
var integer_list = [1, 2, 3]
integer_list.append(10) # integer_list is now [1, 2, 3, 4]

var deep_list = [[1, 2, 3], [4, 5, 6]]
deep_list.append([7, 8, 9]) # deep_list is now [[1, 2, 3], [4, 5, 6], [7, 8, 9]]

var any_list: list[any] = [1, 2.2, "3", [4]]
# any is a special case, because anything can be changed into an any.
any_list.append([5, 6, 7])
any_list.append(89)
any_list.append(integer_list)
```

#### list::size(_input_: list[A] => integer)
Returns a count of the elements within **input**.


## Class Hash
Hash has two different types inside of it: **A** is the key, **B** is the value.

#### hash::get(_input_: hash[A, B], _to_find_: A, _default_value_: B => B)
This attempts to find **to_find** within **input**. If the key can be found, then the value associated with that key is returned. If it cannot be found, then **default_value** is returned.

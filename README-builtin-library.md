# Lily builtin library

## Definitions and terms
For the apache module, anything sent to 'output' is given to the server as html.
For anything else, 'output' is stdout.

## Globals
Global functions are available in any scope at any time.

#### printfmt(string format, object args...):nil
This writes the string **format** to output. **args** is a series of values passed. The following format specifiers are allowed:
* %i : An integer
* %n : A number
* %s : A string
```
object a = 10
number b = 5.5
str c = "abc"

printfmt("a is %i, b is %n, c is %s.\n", a, b, c)
# a is 10, b is 5.5, c is abc.
```

#### print(string text):nil
* This writes **text** to output.

## Class str
Library functions are called using an object of that class, like so:
```
str abc = "abc"
str abcdef = abc.concat("def") # "abcdef"
```

#### str::concat(str _input_, str _to_append_):str
This creates a new string based upon two strings joined together.

#### str::endswith(str _input_, str _suffix_):integer
Checks if **input** ends with **suffix**. Returns 1 if yes, 0 if no.

#### str::find(str _input_, str _tofind_):integer
Attempt to locate the first occurance of **tofind** within **input**. On success, the start of the match is returned (a valid index). On failure, -1 is returned.

#### str::htmlencode(str _input_):str
Returns a newly-made string but with the html entities within **input** converted. Here are the conversions done:
* & becomes &amp;amp;
* < becomes &amp;lt;
* > becomes &amp;gt;

#### str::isalpha(str _input_):integer
Checks if **input** is composed of entirely alphabetic characters. Returns 1 if yes, 0 if no.

#### str::isalnum(str _input_):integer
Checks if **input** is composed of entirely alphanumeric characters. Returns 1 if yes, 0 if no.

#### str::isdigit(str _input_):integer
Checks if **input** is composed of entirely numeric characters. Returns 1 if yes, 0 if no.

#### str::isspace(str _input_):integer
Checks if **input** is composed of entirely space characters. Space characters are defined as " \t\n\v\f\r". Returns 1 or 0.

#### str::lower(str _input_):str
Creates a new string based off of **input** but with all uppercase characters turned into lowercase ones. Utf-8 characters are ignored.

#### str::lstrip(str _input_, str _tostrip_):str
Returns a newly-made string with **tostrip** removed from the *left* of **input**. **tostrip** can be a single character or a series of characters. Utf-8 characters also allowed.

#### str::rstrip(str _input_, str _tostrip_):str
Returns a newly-made string with **tostrip** removed from the *right* of **input**. **tostrip** can be a single character or a series of characters. Utf-8 characters also allowed.

#### str::startswith(str _input_, str _prefix_):integer
Checks if **input** starts with **prefix**. Returns 1 if yes, 0 if no.

#### str::strip(str _input_, str _tostrip_):str
Returns a newly-made string with **tostrip** removed from the *left* and *right* of **input**. **tostrip** can be a single character or a series of characters. Utf-8 characters also allowed.

#### str::trim(str _input_):str
Returns a newly-made string with the characters " \t\r\n" removed from *both* sides of the string. This is a convenience function that does the same as:
```
input.strip(" \t\r\n")
```

#### str::upper(str _input_):str
Creates a new string based off of **input** but with all lowercase characters turned into uppercase ones. Utf-8 characters are ignored.


## Class List
The first type of a list is referred to as **T**
list[T] as the first argument means that the function can take a list of any type.

#### list::apply(list[T] _input_, method callee(T):T):nil
This calls the method **callee** on each element of **input**. The result of calling **callee** is set onto the corresponding element of **input**.
```
method times_two(integer a):integer
{
	return a * 2
}

list[integer] integer_list = [1, 2, 3]
integer_list.apply(times_two)
# integer_list is now [2, 4, 6]
```

#### list::append(list[T] _input_, T newelem):nil
Adds **newelem** to the end of **input**. A syntax error occurs if **newelem** has a different type than **input** contains.
Note: If **T** is an object, then values passed as **newelem** are automatically made into objects.
```
list[integer] integer_list = [1, 2, 3]
integer_list.append(10) # integer_list is now [1, 2, 3, 4]

list[list[integer]] deep_list = [[1, 2, 3], [4, 5, 6]]
deep_list.append([7, 8, 9]) # deep_list is now [[1, 2, 3], [4, 5, 6], [7, 8, 9]]

list[object] object_list = [1, 2.2, "3", [4]]
# object is a special case, because anything can be changed into an object.
object_list.append([5, 6, 7])
object_list.append(89)
object_list.append(integer_list)
```

#### list::size(list[T] _input_):integer
Returns a count of the elements within **input**.

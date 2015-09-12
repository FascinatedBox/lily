postgre
=======

The postgre package allows access to the postgres database. This API provides a way of doing the most basic querying of the database, and little else. There are plans to expand this module.

This is not a default module. To build this module, you will first need to get development headers for the C library libpq. Once you've done that, then execute `cmake -DWITH_POSTGRE=ON`. You may then use make and make install to install the lily postgres library.

This library is very incomplete, and will receive a facelift in the near future.

# Variables

This package does not provide any top-level variables.

# Functions

This package does not provide any top-level functions.

# Classes

Note: The classes that this module exports are considered 'built-in', and thus unable to be inherited from. This may change in a future release of Lily.

## Error

Exceptions of this class are raised when there is an error of some sort calling a function defined within here. In the future, this error will be accompanied by others.

This class inherits directly from Lily's core Exception class.

## Conn

`Conn::new(*host: string="", *port: string="", *dbname: string="", *login: string="", *password: string="" => Conn)`

Attempt to initialize a new connection to the database. To use the defaults, either don't pass an argument, or pass an empty string to them. Host and port will default such that the connection will target localhost.

If unable to connect to the database, the Error class above is raised, with the message describing the issue.

This connection is a synchronous, blocking connection to postgre. It can be used to execute extremely limited queries and fetching from the database.


`Conn::query(self: Conn, format: string, args: list[string] => Result)`

This executes a query against the database, and stores the result of that query into a Result object. The format string is not in postgre's format, but instead operates on familiar ? replacement.

Example:

```
conn.query("select * from ? where name like ?", "employees", "Bob")
```

If the query request asks for more args than what are given, then FormatError is raised.

If the query is invalid, or returns a result that Lily does not understand, then the Error class mentioned above is raised.

## Request

`Result::fetchrow(self: Result => list[string])`

Fetch a new row from the given result. If the Result does not contain any rows, or all rows have currently been read, then the `Error` class is raised. Each column's data is then converted into a string.

Lily represents a null column value with the string `(null)`, as Lily does not allow a variable to not be initialized. It is thus not possible to determine if a text-based column actually contains `(null)`, or has a string with the value `(null)`.

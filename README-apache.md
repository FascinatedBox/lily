# The Apache Module
## Installing mod_lily
First off, mod_lily does not build by default. It must be configured on.
```
./configure --with-apache-mod
cd src/apache/
make
# You need to be root or use sudo from here on.
make install
make restart
```

From here, it should be possible to access Lily files from http://localhost/lily/

## Using mod_lily
* I recommend checking that mod_lily works by trying to access an html-only page before doing anything else. This way, you'll know if a problem is with the server or the interpreter.
* Anything NOT in <@lily ... @> is considered raw html and sent. Within those tags, only data explictly printed (via print or printfmt) is sent.
* The module creates a package called 'server', available globally. This can be used to get information about the server.


## The server package
It is recommended to use str::htmlencode to encode any html entities that may be present in user-sent data. The htmlencode function can be done on any str, like so:
```
# If "name" exists in POST vars, get it. If not, use ""
# Take that and htmlencode it to avoid vulnerabilities.
str name = server::post.get("name", "").htmlencode()
```

#### str server::httpmethod
This is a str containing the request method sent, in all caps. For post, it's POST, get is GET, etc.
Since **server** is a package, values within it are accessed like this:
```
if server::httpmethod == "POST": ...
```

#### hash[str, str] server::env
This contains a bunch of different environment variables. They differ depending on the request sent. To see what it contains, try doing this
```
show server::env
```

#### hash[str, str] server::get
This contains all of the server's GET variables. If the request is not a GET request, this will be empty.
While this is a hash, it is recommended to use the hash::get to retrieve the values, since values that don't exist will cause an error to be raised.
```
# So if you'd like the value of the parameter name, simply do...
server::get.get("test").htmlencode()
```

#### hash[str, str] server::post
This contains all of the server's POST variables. If the request is not a POST request, this will be empty.
This should be accessed using hash::get, just like server::get should, for the same reasons.

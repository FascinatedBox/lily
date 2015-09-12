Getting Started
===============

This page will guide you through the process of getting Lily to work, 
first in a vanilla configuration, and then with optional modules.

To begin with, you'll need `cmake`, and a compiler that supports C11. 
Newer versions of either gcc or clang should work just fine.

To get the most basic build of Lily working, fire up a shell and run:

```
git clone https://github.com/jesserayadkins/lily
cd lily
cmake .
make
make install
```

If all goes well, you'll have a lily executable. You can use this to 
execute files directly (in standalone mode) using `./lily <file>` or in 
tagged mode using `./lily -t <file>`. Alternatively, lily can also 
process strings passed through the command line via `./lily -s 
<string>`.

By default, both the postgre and apache modules are not built, as they 
may need things not on your system. For instructions on how to build 
each, refer to the documentation for the server and postgre modules.

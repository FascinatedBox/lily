This directory contains manuals that explain different parts of Lily. These are
primarily centered around embedding and extending the language in C.

## Basic manuals

**lily_overview.md** gives a brief overview of Lily's features, and how they
translate to its workings in C.

**extending_lily.md** explains how to use `dyna_tools.py` to build the dynaload
tables that Lily needs. It shows how to add custom classes, methods, vars, and
so on. It also goes into how dynaload works in a little more detail than the
overview.

## API manuals

**api_alloc.md** briefly explains the small allocation API that the interpreter
uses. You're unlikely to need this, unless you're extending the interpreter with
a foreign struct.

**api_msgbuf.md** explains `lily_msgbuf`, a buffer with insertion functions that
do size checks under the hood. Functions that manipulate text are likely to use
the interpreter's shared buffer (or want one of their own), and may need this.

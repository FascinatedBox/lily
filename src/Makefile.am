if APACHE_MOD
SUBDIRS = apache
else
SUBDIRS =
endif

# These files form the core of the interpreter.
lily_SRCS=csiphash.c \
	lily_parser.c \
	lily_lexer.c \
	lily_ast.c \
	lily_emitter.c \
	lily_symtab.c \
	lily_vm.c \
	lily_debug.c \
	lily_raiser.c \
	lily_msgbuf.c \
	lily_gc.c \
	lily_value.c \
	lily_bind.c \
	lily_cls_integer.c \
	lily_cls_double.c \
	lily_cls_list.c \
	lily_cls_string.c \
	lily_cls_hash.c \
	lily_cls_all_exceptions.c \
	lily_pkg_sys.c \
	lily_class_funcs.c

bin_PROGRAMS = lily lily_aft

# Lily's core is designed to be modular. It has various ways of running it.
# I call anything that invokes Lily a 'runner'. Different runners have
# different purposes and ways of running the interpreter.

### The runners ###

# lily is the main runner for the language. It's able to handle strings passed
# from the command-line, as well as files.
lily_SOURCES = lily_main.c \
	$(lily_SRCS)

# lily_aft (allocation fail tester) was designed to run the interpreter but
# only allow a certain number of allocations. This is used with blastmaster.py
# to ensure that a failure to allocate will not cause the server to crash
# or leak memory.
# This has uncovered a lot of bugs.
# lily_aft expects a file from the command-line, like lily_fs does.
lily_aft_CFLAGS = -DAFT_ALLOC
lily_aft_SOURCES = aft_main.c \
	$(lily_SRCS)

blastmaster: lily lily_aft
	python blastmaster.py

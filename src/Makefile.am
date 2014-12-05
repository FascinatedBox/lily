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

# Here are some targets to make things easier.

# This runs the basic sanity test which is at the given location. This runs
# quite a few tests to make sure the interpreter is still sane (hence the
# name). 
sanity: lily
	./lily test/pass/sanity.ly

# Run the sanity test again, this time through valgrind. This can be thought
# of as a precursor to doing blastmaster.py: If this fails, then blastmaster's
# tests are sure to do so.
vg-sanity: lily
	valgrind ./lily test/pass/sanity.ly

blastmaster: lily lily_aft
	python blastmaster.py

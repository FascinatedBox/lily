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
	lily_cls_bytestring.c \
	lily_cls_list.c \
	lily_cls_string.c \
	lily_cls_hash.c \
	lily_cls_file.c \
	lily_pkg_sys.c \
	lily_class_funcs.c \
	lily_membuf.c \
	lily_type_stack.c

noinst_LTLIBRARIES = liblily.la
liblily_la_SOURCES = $(lily_SRCS)
liblily_la_CFLAGS = $(AM_CFLAGS) -Wpadded

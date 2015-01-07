AM_CFLAGS = -I/usr/include/httpd/ -I/usr/include/apr-1/ -I../
APACHECTL = apachectl

lily_SRCS=../csiphash.c \
	../lily_parser.c \
	../lily_lexer.c \
	../lily_ast.c \
	../lily_emitter.c \
	../lily_symtab.c \
	../lily_vm.c \
	../lily_debug.c \
	../lily_raiser.c \
	../lily_msgbuf.c \
	../lily_gc.c \
	../lily_value.c \
	../lily_bind.c \
	../lily_cls_integer.c \
	../lily_cls_double.c \
	../lily_cls_list.c \
	../lily_cls_string.c \
	../lily_cls_hash.c \
	../lily_cls_all_exceptions.c \
	../lily_pkg_sys.c \
	../lily_class_funcs.c \
	../lily_membuf.c

lib_LTLIBRARIES = mod_lily.la
mod_lily_la_SOURCES = mod_lily.c \
	$(lily_SRCS)

mod_lily_la_LDFLAGS = -version-info 0:11:0 -module

# These were copied from the makefile that apxs -g -n "lily" made.
restart:
	$(APACHECTL) restart
start:
	$(APACHECTL) start
stop:
	$(APACHECTL) stop

# Build it all again, install it, and reboot the server.
reload: all install-exec-local restart

# Install the module to the server.
install-exec-local:
	apxs -i -a -n "lily" mod_lily.la

# Build it all, install, reboot the server, then check the basics.
# This will be the sanity test until the server is more functional.
test: reload
	lynx -mime_header http://localhost/lily

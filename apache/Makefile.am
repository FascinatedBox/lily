AM_CFLAGS = -I/usr/include/httpd/ -I/usr/include/apr-1/ -I../src/
APACHECTL = apachectl

lib_LTLIBRARIES = mod_lily.la
mod_lily_la_SOURCES = mod_lily.c
mod_lily_la_LIBADD = ../src/liblily.la
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

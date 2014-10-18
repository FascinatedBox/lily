AC_INIT([lily], [0.11], [jesserayadkins@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_PROG_CC
AM_PROG_CC_C_O
AC_CONFIG_HEADERS([config.h])
AC_ENABLE_SHARED
AC_DISABLE_STATIC
AM_PROG_AR
LT_INIT
AC_PROG_LIBTOOL(libtool)

CFLAGS="-Wall -O2 -g3"

AC_ARG_WITH([apache_mod],
    AS_HELP_STRING([--with-apache-mod], [Build mod_lily for apache.]))

AC_CONFIG_FILES([
 Makefile
 src/Makefile
])

AS_IF([test "x$with_apache_mod" = "xyes"],
      AC_CONFIG_FILES([src/apache/Makefile]))
AM_CONDITIONAL([APACHE_MOD], [test "x$with_apache_mod" = "xyes"])

AC_OUTPUT

# Google BSD license https://developers.google.com/google-bsd-license
# Copyright 2012 Google Inc. wrightt@google.com
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([ios_webkit_debug_proxy], [1.7.1], [https://github.com/google/ios-webkit-debug-proxy/issues])
AM_INIT_AUTOMAKE([1.10 no-define])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL
PKG_PROG_PKG_CONFIG

# Checks for libraries.
PKG_CHECK_MODULES(libimobiledevice, libimobiledevice-1.0 >= 1.2.0)
PKG_CHECK_MODULES(libplist, libplist >= 1.12)
AC_CHECK_LIB([plist], [plist_to_xml],
             [ ], [AC_MSG_FAILURE([*** Unable to link with libplist])],
             [$libplist_LIBS])
AC_CHECK_LIB([m], [log10])
AC_CHECK_LIB([imobiledevice], [idevice_new],
             [ ], [AC_MSG_FAILURE([*** Unable to link with libimobiledevice])],
             [$libimobiledevice_LIBS])
LT_INIT

# Defines versions of required modules
libimobiledevice_version=`$PKG_CONFIG --modversion libimobiledevice-1.0`
libplist_version=`$PKG_CONFIG --modversion libplist`

AC_DEFINE_UNQUOTED([LIBIMOBILEDEVICE_VERSION], ["$libimobiledevice_version"], [ ])
AC_DEFINE_UNQUOTED([LIBPLIST_VERSION], ["$libplist_version"], [ ])

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_RESOLV
AC_CHECK_HEADERS([arpa/inet.h inttypes.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h sys/socket.h sys/time.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT8_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memmove memset regcomp select socket stpcpy strcasecmp strncasecmp strchr strdup strndup strrchr strstr strtol])
AC_CHECK_DECL(idevice_connection_get_fd, [AC_DEFINE([HAVE_IDEVICE_CONNECTION_GET_FD], 1, [ ])], [], [#include <libimobiledevice/libimobiledevice.h>])

AC_CONFIG_FILES([Makefile src/Makefile include/Makefile examples/Makefile])

CFLAGS="${CFLAGS} -Wall -Werror"

AC_OUTPUT

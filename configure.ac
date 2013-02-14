# Google BSD license http://code.google.com/google_bsd_license.html
# Copyright 2012 Google Inc. wrightt@google.com
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([ios_webkit_debug_proxy], [1.0], [wrightt@google.com])
AM_INIT_AUTOMAKE([1.10 no-define])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_CC_C_O

# Checks for libraries.
AC_CHECK_LIB([imobiledevice], [idevice_connect])
AC_CHECK_LIB([plist], [plist_to_xml])
AC_CHECK_LIB([m], [log10])

# Checks for header files.
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

AC_CONFIG_FILES([Makefile src/Makefile include/Makefile examples/Makefile])
AC_OUTPUT

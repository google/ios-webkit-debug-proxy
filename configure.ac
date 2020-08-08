# Google BSD license https://developers.google.com/google-bsd-license
# Copyright 2012 Google Inc. wrightt@google.com
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([ios_webkit_debug_proxy], [1.8.8], [https://github.com/google/ios-webkit-debug-proxy/issues])
AM_INIT_AUTOMAKE([1.10 no-define])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL
PKG_PROG_PKG_CONFIG

# Checks for libraries.
PKG_CHECK_MODULES(libimobiledevice, libimobiledevice-1.0 >= 1.3.0)
PKG_CHECK_MODULES(libplist, libplist-2.0 >= 2.2.0)
PKG_CHECK_MODULES(openssl, openssl >= 0.9.8)
AC_CHECK_LIB([plist-2.0], [plist_to_xml],
             [ ], [AC_MSG_FAILURE([*** Unable to link with libplist])],
             [$libplist_LIBS])
AC_CHECK_LIB([m], [log10])
AC_CHECK_LIB([imobiledevice-1.0], [idevice_new],
             [ ], [AC_MSG_FAILURE([*** Unable to link with libimobiledevice])],
             [$libimobiledevice_LIBS])
LT_INIT

# Defines versions of required modules
libimobiledevice_version=`$PKG_CONFIG --modversion libimobiledevice-1.0`
libplist_version=`$PKG_CONFIG --modversion libplist-2.0`

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

# Check for operating system
AC_MSG_CHECKING([whether to enable WIN32 build settings])
case ${host_os} in
  *mingw*|*msys*|*cygwin*)
    win32=true
    AC_MSG_RESULT([yes])
    AC_DEFINE(WIN32_LEAN_AND_MEAN, 1, [Define to limit the scope of windows.h])
    AC_DEFINE(__USE_MINGW_ANSI_STDIO, 1, [Define to use C99 printf/snprintf in MinGW])
    ;;
  *)
    win32=false
    AC_MSG_RESULT([no])
    ;;
esac
AM_CONDITIONAL(WIN32, test "x$win32" = "xtrue")

# Check for pcre presence if regex.h is absent
AC_CHECK_HEADER(regex.h, [ac_have_regex_h="yes"], [ac_have_regex_h="no"])
if test "x$ac_have_regex_h" = "xno"; then
  PKG_CHECK_MODULES(libpcreposix, libpcreposix, [], [AC_MSG_ERROR([Neither regex.h nor pcre headers were found])])
else
  AC_DEFINE(HAVE_REGEX_H, 1, [regex.h is present])
fi

AC_CHECK_FUNCS([memmove memset regcomp select socket strcasecmp strncasecmp strchr strdup strndup strrchr strstr strtol strcasestr getline])

AC_CONFIG_FILES([Makefile src/Makefile include/Makefile examples/Makefile])

CFLAGS="${CFLAGS} -Wall -Werror"

AC_OUTPUT

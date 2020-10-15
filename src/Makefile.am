# Google BSD license https://developers.google.com/google-bsd-license
# Copyright 2012 Google Inc. wrightt@google.com

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/ios-webkit-debug-proxy

AM_CFLAGS = $(GLOBAL_CFLAGS) $(libimobiledevice_CFLAGS) $(libplist_CFLAGS) $(libpcreposix_CFLAGS) $(openssl_CFLAGS)
AM_LDFLAGS = $(libimobiledevice_LIBS) $(libplist_LIBS) $(libpcreposix_LIBS) $(openssl_LIBS)

lib_LTLIBRARIES = libios_webkit_debug_proxy.la
libios_webkit_debug_proxy_la_LIBADD =
libios_webkit_debug_proxy_la_LDFLAGS = $(AM_LDFLAGS)
libios_webkit_debug_proxy_la_SOURCES = ios_webkit_debug_proxy_main.c \
    base64.c base64.h \
    char_buffer.c char_buffer.h \
    device_listener.c device_listener.h \
    hash_table.c hash_table.h \
    ios_webkit_debug_proxy.c ios_webkit_debug_proxy.h \
    port_config.c port_config.h \
    rpc.c rpc.h \
    sha1.c sha1.h \
    socket_manager.c socket_manager.h \
    validate_utf8.h \
    webinspector.c webinspector.h \
    websocket.c websocket.h

bin_PROGRAMS = ios_webkit_debug_proxy
ios_webkit_debug_proxy_SOURCES = ios_webkit_debug_proxy_main.c \
    base64.c base64.h \
    char_buffer.c char_buffer.h \
    device_listener.c device_listener.h \
    hash_table.c hash_table.h \
    ios_webkit_debug_proxy.c ios_webkit_debug_proxy.h \
    port_config.c port_config.h \
    rpc.c rpc.h \
    sha1.c sha1.h \
    socket_manager.c socket_manager.h \
    validate_utf8.h \
    webinspector.c webinspector.h \
    websocket.c websocket.h
ios_webkit_debug_proxy_CFLAGS = $(AM_CFLAGS)
ios_webkit_debug_proxy_LDFLAGS = $(AM_LDFLAGS)

# Google BSD license http://code.google.com/google_bsd_license.html
# Copyright 2012 Google Inc. wrightt@google.com

AM_CPPFLAGS = -I$(top_srcdir)/include

AM_CFLAGS = $(GLOBAL_CFLAGS) $(libimobiledevice_CFLAGS) $(libplist_CFLAGS)
AM_LDFLAGS = $(libimobiledevice_LIBS) $(libplist_LIBS)

bin_PROGRAMS = ios_webkit_debug_proxy
ios_webkit_debug_proxy_SOURCES = ios_webkit_debug_proxy_main.c \
    base64.c base64.h \
    char_buffer.c char_buffer.h \
    device_listener.c device_listener.h \
    hash_table.c hash_table.h \
    ios_webkit_debug_proxy.c ios_webkit_debug_proxy.h \
    port_config.c port_config.h \
    sha1.c sha1.h \
    socket_manager.c socket_manager.h \
    validate_utf8.h \
    webinspector.c webinspector.h \
    websocket.c websocket.h

# Google BSD license http://code.google.com/google_bsd_license.html
# Copyright 2012 Google Inc. wrightt@google.com

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src

AM_CFLAGS = $(GLOBAL_CFLAGS) $(libimobiledevice_CFLAGS) $(libplist_CFLAGS)
AM_LDFLAGS = $(libimobiledevice_LIBS) $(libplist_LIBS)

noinst_PROGRAMS = ws_echo1 ws_echo2 wi_script dl_client

ws_echo1_SOURCES = ws_echo1.c \
    ws_echo_common.c ws_echo_common.h  \
    base64.h \
    char_buffer.h \
    sha1.h \
    websocket.h
ws_echo1_LDADD = \
    ../src/base64.o \
    ../src/char_buffer.o \
    ../src/sha1.o \
    ../src/websocket.o

ws_echo2_SOURCES = ws_echo2.c \
    ws_echo_common.c ws_echo_common.h \
    base64.h \
    char_buffer.h \
    sha1.h \
    socket_manager.h \
    hash_table.h \
    websocket.h
ws_echo2_LDADD = \
    ../src/base64.o \
    ../src/char_buffer.o \
    ../src/hash_table.o \
    ../src/sha1.o \
    ../src/socket_manager.o \
    ../src/websocket.o

wi_script_SOURCES = \
    wi_script.c \
    char_buffer.h \
    webinspector.h
wi_script_LDADD = \
    ../src/char_buffer.o \
    ../src/webinspector.o

dl_client_SOURCES =  \
    dl_client.c \
    char_buffer.h \
    device_listener.h \
    hash_table.h
dl_client_LDADD = \
    ../src/char_buffer.o \
    ../src/device_listener.o \
    ../src/hash_table.o

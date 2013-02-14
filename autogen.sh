#!/bin/sh
#
# Google BSD license http://code.google.com/google_bsd_license.html
# Copyright 2012 Google Inc. wrightt@google.com

run () {
  echo $*
  $* || exit 1
}

run aclocal
run autoheader
run automake --add-missing
run autoconf

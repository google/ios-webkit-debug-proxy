/* Copyright (C) 1991, 1992, 1995, 1996, 1997 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#ifndef __GETLINE_H
#define __GETLINE_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HAVE_GETLINE
static inline int getline(char **lineptr, size_t *n, FILE *stream)
{
  if (lineptr == NULL || n == NULL)
  {
    return -1;
  }
  if (*lineptr == NULL || *n == 0)
  {
    *n = 120;
    *lineptr = (char *) malloc (*n);
    if (*lineptr == NULL)
    {
      return -1;
    }
  }
  if (fgets (*lineptr, *n, stream))
    return *n;
  else
    return -1;
}
#endif

#endif

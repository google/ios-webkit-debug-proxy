/* getline.c -- Replacement for GNU C library function getline

Copyright (C) 1993 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.  */

/* Written by Jan Brittenson, bson@gnu.ai.mit.edu.  */

#ifndef __GETLINE_H
#define __GETLINE_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>

#ifndef HAVE_GETLINE
/* Always add at least this many bytes when extending the buffer.  */
#define GETLINE_MIN_CHUNK 256

static inline int getstr(char **lineptr, size_t *n, FILE *stream,
    char terminator, int offset) {
  int nchars_avail;  /* Allocated but unused chars in *LINEPTR.  */
  char *read_pos;    /* Where we're reading into *LINEPTR. */
  int ret;

  if (!lineptr || !n || !stream)
  {
    errno = EINVAL;
    return -1;
  }

  if (!*lineptr)
  {
    *n = GETLINE_MIN_CHUNK;
    *lineptr = malloc (*n);
    if (!*lineptr)
    {
      errno = ENOMEM;
      return -1;
    }
  }

  nchars_avail = *n - offset;
  read_pos = *lineptr + offset;

  for (;;)
  {
    int save_errno;
    register int c = getc (stream);

    save_errno = errno;

    /* We always want at least one char left in the buffer, since we
       always (unless we get an error while reading the first char)
       NUL-terminate the line buffer.  */

    assert((*lineptr + *n) == (read_pos + nchars_avail));
    if (nchars_avail < 2)
    {
      if (*n > GETLINE_MIN_CHUNK)
        *n *= 2;
      else
        *n += GETLINE_MIN_CHUNK;

      nchars_avail = *n + *lineptr - read_pos;
      *lineptr = realloc (*lineptr, *n);
      if (!*lineptr)
      {
        errno = ENOMEM;
        return -1;
      }
      read_pos = *n - nchars_avail + *lineptr;
      assert((*lineptr + *n) == (read_pos + nchars_avail));
    }

    if (ferror (stream))
    {
      /* Might like to return partial line, but there is no
         place for us to store errno.  And we don't want to just
         lose errno.  */
      errno = save_errno;
      return -1;
    }

    if (c == EOF)
    {
      /* Return partial line, if any.  */
      if (read_pos == *lineptr)
        return -1;
      else
        break;
    }

    *read_pos++ = c;
    nchars_avail--;

    if (c == terminator)
      /* Return the line.  */
      break;
  }

  /* Done - NUL terminate and return the number of chars read.  */
  *read_pos = '\0';

  ret = read_pos - (*lineptr + offset);
  return ret;
}

static inline int getline(char **lineptr, size_t *n, FILE *stream)
{
  return getstr(lineptr, n, stream, '\n', 0);
}
#endif

#endif

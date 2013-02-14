// Google BSD license http://code.google.com/google_bsd_license.html
// Copyright 2012 Google Inc. wrightt@google.com

#ifndef CHAR_BUFFER_H
#define	CHAR_BUFFER_H

#ifdef	__cplusplus
extern "C" {
#endif


#include <stdlib.h>


struct cb_struct {
  char *begin;
  char *head;
  char *tail;
  char *end;

  const char *in_head;
  const char *in_tail;
};
typedef struct cb_struct *cb_t;

cb_t cb_new();

void cb_free(cb_t buffer);

void cb_clear(cb_t buffer);

int cb_ensure_capacity(cb_t self, size_t needed);

// Instead of copying our input into our my->in, e.g.:
//    cb_ensure_capacity(my->in, length);
//    memcpy(my->in->tail, buf, length);
//    my->in->tail += length;
// we'll avoid the memcpy, if possible.  The shared pointer is
// "my->in_head".
int cb_begin_input(cb_t self, const char *buf, ssize_t length);

int cb_end_input(cb_t self);


// Print a buffer to a new string.
//
// @param to_buf
//    Output buffer, e.g. will be set to:
//    " 61 62 0A           ab.\n"+
//    " 63 0A              c.   +3\0"+
//    If NULL then nothing will be written but the return value will be the
//    required minimal length.
// @param buf
//    Input buffer, e.g. "ab\nc\ndef"
// @param length
//    Input length, e.g. 8
// @param max_width
//    Max line length, e.g. 25, or negative for no limit
// @param max_lines
//    Max number of lines, e.g. 2, or negative for no limit
// @result
//    Number of characters written, not including the trailing '\0', e.g. 51
size_t cb_sprint(char *to_buf, const char *buf, ssize_t length,
                 ssize_t max_width, ssize_t max_lines);

// Like sb_sprint, but dynamically allocates the *to_buf_ptr.
int cb_asprint(char **to_buf_ptr, const char *buf, ssize_t length,
               ssize_t max_width, ssize_t max_lines);



#ifdef	__cplusplus
}
#endif

#endif	/* CHAR_BUFFER_H */


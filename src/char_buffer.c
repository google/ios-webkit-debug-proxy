// Google BSD license http://code.google.com/google_bsd_license.html
// Copyright 2012 Google Inc. wrightt@google.com

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "char_buffer.h"

#define MIN_LENGTH 1024

cb_t cb_new() {
  cb_t self = (cb_t)malloc(sizeof(struct cb_struct));
  if (self) {
    memset(self, 0, sizeof(struct cb_struct));
  }
  return self;
}

void cb_free(cb_t self) {
  if (self) {
    if (self->begin) {
      free(self->begin);
    }
    free(self);
  }
}

void cb_clear(cb_t self) {
  self->head = self->begin;
  self->tail = self->begin;
}

int cb_ensure_capacity(cb_t self, size_t needed) {
  if (!self->begin) {
    size_t length = (needed > MIN_LENGTH ? needed : MIN_LENGTH);
    self->begin = (char *)malloc(length * sizeof(char));
    if (!self->begin) {
      perror("Unable to allocate buffer");
      return -1;
    }
    self->head = self->begin;
    self->tail = self->begin;
    self->end = self->begin + length;
    return 0;
  }
  size_t used = self->tail - self->head;
  if (!used) {
    self->head = self->begin;
    self->tail = self->begin;
  }
  size_t avail = self->end - self->tail;
  if (needed > avail) {
    size_t offset = self->head - self->begin;
    if (offset) {
      if (used) {
        memmove(self->begin, self->head, used);
      }
      self->head = self->begin;
      self->tail = self->begin + used;
      avail += offset;
    }
    if (needed > avail) {
      size_t length = self->end - self->begin;
      size_t new_length = used + needed;
      if (new_length < 1.5 * length) {
        new_length = 1.5 * length;
      }
      char *new_begin = (char*)realloc(self->begin,
          new_length * sizeof(char));
      if (!new_begin) {
        perror("Unable to resize buffer");
        return -1;
      }
      self->begin = new_begin;
      self->head = new_begin;
      self->tail = new_begin + used;
      self->end = new_begin + new_length;
    }
  }
  return 0;
}

int cb_begin_input(cb_t self, const char *buf, ssize_t length) {
  if (!buf || length < 0) {
    return -1;
  }
  // Instead of always doing a memcpy into our buffer, see if we can
  // use the buf as-is
  int can_share = (!self->begin || self->tail == self->head);
  if (can_share) {
    self->in_head = buf;
    self->in_tail = buf + length;
  } else {
    if (cb_ensure_capacity(self, length)) {
      return -1;
    }
    if (length > 0) {
      memcpy(self->tail, buf, length);
      self->tail += length;
    }
    self->in_head = self->head;
    self->in_tail = self->tail;
  }
  return 0;
}

int cb_end_input(cb_t self) {
  int did_share = (!self->begin || self->in_tail != self->tail);
  if (did_share) {
    size_t length = self->in_tail - self->in_head;
    if (length > 0) {
      // We used the input buf as-is, but some bytes remain, so save them
      if (cb_ensure_capacity(self, length)) {
        return -1;
      }
      memcpy(self->tail, self->in_head, length);
      self->tail += length;
    }
  } else {
    self->head += self->in_head - self->head;
  }
  self->in_head = NULL;
  self->in_tail = NULL;
  return 0;
}

// similar to socat output, e.g.:
// 47 45 54 20 2F 64 65 76 74 6F 6F 6C 73 2F 49 6D 61 67 65  GET /devtools/Image
// ...
size_t cb_sprint(char *to_buf, const char *buf, ssize_t length,
    ssize_t max_width, ssize_t max_lines) {
  if (length <= 0) {
    if (to_buf) {
      *to_buf = '\0';
    }
    return 0;
  }

  char *s = to_buf;
  size_t n = 0;

#define APPEND(v) if (s) { *s++ = (v); } n++

  size_t i = 0;
  size_t num_lines = 0;

  size_t chars_per_line;
  if (max_width >= 0) {
    chars_per_line = (max_width > 6 ? ((max_width - 2) >> 2) : 1);
  } else {
    size_t max_cpl = 1;
    size_t curr_cpl = 0;
    for (i = 0; i < length; i++) {
      unsigned char ch = buf[i++];
      curr_cpl++;
      if (ch == '\n') {
        if (curr_cpl > max_cpl) {
          max_cpl = curr_cpl;
        }
        if (max_lines >= 0 && ++num_lines > max_lines) {
          break;
        }
        curr_cpl = 0;
      }
    }
    chars_per_line = max_cpl;
  }

  i = 0;
  num_lines = 0;
  while (1) {
    size_t j;
    size_t rem = chars_per_line;
    for (j = i; j < length && rem; ) {
      unsigned char ch = buf[j++];
      static char* hexchars = "0123456789ABCDEF";
      APPEND(' ');
      APPEND(hexchars[(ch >> 4) & 0xF]);
      APPEND(hexchars[ch & 0xF]);
      rem--;
      if (ch == '\n') {
        break;
      }
    }
    size_t rem2 = rem;
    for (rem2 = rem; rem2 > 0; rem2--) {
      APPEND(' ');
      APPEND(' ');
      APPEND(' ');
    }
    APPEND(' ');
    APPEND(' ');
    while (i < j) {
      unsigned char ch = buf[i++];
      APPEND(ch < 32 || ch > 126 ? '.' : ch);
    }
    if (i >= length) {
      break;
    }
    if (max_lines >= 0 && ++num_lines > max_lines) {
      for (rem2 = rem; rem2 > 0; rem2--) {
        APPEND(' ');
      }
      APPEND(' ');
      APPEND('+');
      if (s) {
        size_t k = sprintf(s, "%zd", length - i);
        s += k;
        n += k;
      } else {
        n += (int)(log10(length - i) + 0.5) + 1;
      }
      break;
    }
    APPEND('\n');
  }
  if (s) {
    *s++ = '\0';
  }
  return n;
}

int cb_asprint(char **ret, const char *buf, ssize_t length,
    ssize_t max_width, ssize_t max_lines) {
  size_t n = cb_sprint(NULL, buf, length, max_width, max_lines);
  *ret = (char *)malloc((n+1) * sizeof(char));
  if (!*ret) {
    return -1;
  }
  return cb_sprint(*ret, buf, length, max_width, max_lines);
}


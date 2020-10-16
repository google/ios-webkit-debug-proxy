// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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
      static const char* hexchars = "0123456789ABCDEF";
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

int hex2int(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  else if (c >= 'a' && c <= 'f')
    return 10 + c - 'a';
  else if (c >= 'A' && c <= 'F')
    return 10 + c - 'A';
  else
    return -1;
}

int cb_sscan(char *to_buf, size_t *to_length, const char *buf) {
  if (!to_buf || !to_length || !buf) {
    return -1;
  }
  *to_length = 0;
  const char *f = buf;
  char *t = to_buf;
  while (*f) {
    for (; *f == ' '; f++) {
    }
    while (*f != ' ' && *f != '\n') {
      int h0 = (*f ? hex2int(*f++) : -1);
      int h1 = (*f ? hex2int(*f++) : -1);
      char ch = (*f ? *f++ : '\0');
      if (h0 < 0 || h1 < 0 || ch != ' ') {
        return -1;
      }
      *t++ = (h0 << 4) | h1;
      *to_length += 1;
    }
    if (*f == ' ') {
      while (*++f && *f != '\n') {
      }
    }
    if (*f && *f++ != '\n') {
      return -1;
    }
  }
  return 0;
}

int cb_asscan(char **ret, size_t *to_length, const char *buf) {
  if (!ret || !*ret || !to_length || !buf) {
    return -1;
  }
  *ret = (char *)calloc(strlen(buf) + 1, sizeof(char));
  int rval = cb_sscan(*ret, to_length, buf);
  if (*ret && to_length) {
    *ret = (char*)realloc(*ret, *to_length * sizeof(char));
  }
  return rval;
}

#ifndef __MACH__
char *strnstr(const char *s1, const char *s2, size_t n) {
  size_t len = strlen(s2);
  if (n >= len) {
    char c = *s2;
    const char *end = s1 + (n - len);
    const char *s;
    for (s = s1; *s && s <= end; s++) {
      if (*s == c && !strncmp(s, s2, len)) {
        return (char *)s;
      }
    }
  }
  return NULL;
}
#endif

// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_config.h"


struct pc_entry_struct;
typedef struct pc_entry_struct *pc_entry_t;

struct pc_entry_struct {
  const char *device_id;
  int min_port;
  int max_port;

  // we need a list of these, so put the link here
  pc_entry_t next;
};

struct pc_struct {
  regex_t *re;
  regmatch_t *groups;
  pc_entry_t head;
  pc_entry_t tail;
};

pc_t pc_new() {
  pc_t self = malloc(sizeof(struct pc_struct));
  if (self) {
    memset(self, 0, sizeof(struct pc_struct));
  }
  return self;
}

void pc_clear(pc_t self) {
  if (self) {
    pc_entry_t e = self->head;
    while (e) {
      pc_entry_t next = e->next;
      memset(e, 0, sizeof(struct pc_entry_struct));
      free(e);
      e = next;
    }
    self->head = NULL;
    self->tail = NULL;
  }
}

void pc_free(pc_t self) {
  if (self) {
    pc_clear(self);
    free(self->groups);
    if (self->re) {
      regfree(self->re);
    }
    memset(self, 0, sizeof(struct pc_struct));
    free(self);
  }
}

void pc_add(pc_t self, const char *device_id, int min_port, int max_port) {
  pc_entry_t e = malloc(sizeof(struct pc_entry_struct));
  e->device_id = device_id;
  e->min_port = min_port;
  e->max_port = max_port;
  e->next = NULL;
  if (self->tail) {
    self->tail->next = e;
  } else {
    self->head = e;
  }
  self->tail = e;
}

int pc_parse(pc_t self, const char *line, size_t len,
    char **to_device_id, int *to_min_port, int *to_max_port) {
  if (!self->re) {
    self->re = malloc(sizeof(regex_t));
    if (regcomp(self->re,
          "^[ \t]*"
          "(([a-f0-9]{40}|\\*|null)[ \t]*:?|:)"
          "[ \t]*(-?[0-9]+)"
          "([ \t]*-[ \t]*([0-9]+))?"
          "[ \t]*$", REG_EXTENDED | REG_ICASE)) {
      perror("Internal error: bad regex?");
      return -1;
    }
    size_t ngroups = self->re->re_nsub + 1;
    self->groups = calloc(ngroups, sizeof(regmatch_t));
  }
  size_t ngroups = self->re->re_nsub + 1;
  regmatch_t *groups = self->groups;
  char *line2 = calloc(len+1, sizeof(char));
  memcpy(line2, line, len);
  int is_not_match = regexec(self->re, line2, ngroups, groups, 0);
  free(line2);
  if (is_not_match) {
    return -1;
  }
  char *device_id;
  if (groups[2].rm_so >= 0) {
    size_t len = groups[2].rm_eo - groups[2].rm_so;
    if (strncasecmp("null", line + groups[2].rm_so, len)) {
      device_id = strndup(line + groups[2].rm_so, len);
    } else {
      device_id = NULL;
    }
  } else {
    device_id = strdup("*");
  }
  int min_port = strtol(line + groups[3].rm_so, NULL, 0);
  int max_port = min_port;
  if (groups[4].rm_so >= 0 && groups[5].rm_so >= 0) {
    max_port = strtol(line + groups[5].rm_so, NULL, 0);
  }
  *to_device_id = device_id;
  *to_min_port = min_port;
  *to_max_port = max_port;
  return 0;
}

const char *pc_add_line(pc_t self, const char *line, size_t len) {
  const char *curr = line;
  const char *stop = line + len;
  while (curr < stop) {
    while (curr < stop && (*curr == ' ' || *curr == '\t')) {
      curr++;
    }
    const char *end = curr;
    while (end < stop &&
        *end && *end != '\n' && *end != '#' && *end != ',') {
      end++;
    }
    if (curr < end) {
      char *device_id;
      int min_port;
      int max_port;
      if (pc_parse(self, curr, end - curr,
            &device_id, &min_port, &max_port)) {
        return curr;
      }
      pc_add(self, device_id, min_port, max_port);
    }
    if (*end != ',') break;
    curr = end+1;
  }
  return NULL;
}

int pc_add_file(pc_t self, const char *filename) {
  FILE *f = fopen(filename, "rt");
  if (!f) {
    fprintf(stderr, "Unknown file: %s\n", filename);
    return -1;
  }

  int ret = 0;
  int line_num;
  char *line = NULL;
  size_t line_capacity = 0;
  for (line_num = 0; ; line_num++) {
    ssize_t len = getline(&line, &line_capacity, f);
    if (len < 0) break;
    const char *error = pc_add_line(self, line, len);
    if (error) {
      ret = -1;
      fprintf(stderr, "Ignoring %s:%d: %.*s", filename, line_num,
          (int)(error-line), error);
    }
  }
  free(line);
  fclose(f);
  return ret;
}

const pc_entry_t pc_find(pc_t self, const char *device_id) {
  pc_entry_t e;
  for (e = self->head; e; e = e->next) {
    const char *s = e->device_id;
    if ((s && !strcmp(s, "*")) ||
        (device_id ? (s && !strcasecmp(s, device_id)) : !s)) {
      return e;
    }
  }
  return NULL;
}

int pc_select_port(pc_t self, const char *device_id,
    int *to_port, int *to_min_port, int *to_max_port) {
  const pc_entry_t config = pc_find(self, device_id);
  if (!config) {
    *to_min_port = -1;
    *to_max_port = -1;
    *to_port = -1;
    return -1;
  }
  *to_min_port = config->min_port;
  *to_max_port = config->max_port;
  if (*to_port >= 0 &&
      (*to_port < *to_min_port || *to_port > *to_max_port)) {
    *to_port = -1;
  }
  return 0;
}


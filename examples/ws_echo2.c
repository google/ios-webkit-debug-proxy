// Google BSD license http://code.google.com/google_bsd_license.html
// Copyright 2012 Google Inc. wrightt@google.com

//
// A select-based websocket "echo" server
//
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef WIN32
#include <windows.h>
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#include <errno.h>

#include <iwdp/socket_manager.h>
#include "ws_echo_common.h"

struct my_sm_struct {
  int port;
};
typedef struct my_sm_struct *my_sm_t;

sm_status on_accept(sm_t sm, int server_fd, void *server_value,
    int fd, void **to_value) {
  int port = ((my_sm_t)server_value)->port;
  *to_value = my_new(fd, port);
  return (*to_value ? SM_SUCCESS : SM_ERROR);
}

sm_status on_recv(sm_t sm, int fd, void *value,
    const char *buf, ssize_t length) {
  ws_t ws = ((my_t)value)->ws;
  return ws->on_recv(ws, buf, length);
}

sm_status on_close(sm_t sm, int fd, void *value, bool is_server) {
  if (!is_server) {
    my_free((my_t)value);
  }
  return SM_SUCCESS;
}

static int quit_flag = 0;

static void on_signal(int sig) {
  fprintf(stderr, "Exiting...\n");
  quit_flag++;
}

int main(int argc, char** argv) {
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  int port = 8080;

  int s_fd = sm_listen(port);
  if (s_fd < 0) {
    return -1;
  }

  sm_t sm = sm_new(4096);
  sm->on_accept = on_accept;
  sm->on_recv = on_recv;
  sm->on_close = on_close;

  my_sm_t my_sm = (my_sm_t)malloc(sizeof(struct my_sm_struct));
  my_sm->port = port;
  //sm->state = my_sm; // optional

  sm->add_fd(sm, s_fd, my_sm, true);

  int ret = 0;
  while (!quit_flag) {
    if (sm->select(sm, 2) < 0) {
      ret = -1;
      break;
    }
  }
  sm->cleanup(sm);
  free(my_sm);
  sm_free(sm);
  return ret;
}


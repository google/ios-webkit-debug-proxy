// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

//
// An example device_listener client
//

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ios-webkit-debug-proxy/device_listener.h"

struct my_struct {
  int fd;
};
typedef struct my_struct *my_t;

// callbacks:
dl_status my_send_packet(dl_t dl, const char *buf, size_t length) {
  int fd = ((my_t)dl->state)->fd;
  ssize_t send_bytes = send(fd, buf, length, 0);
  return (send_bytes == length ? DL_SUCCESS : DL_ERROR);
}
dl_status my_on_attach(dl_t dl, const char *device_id, int device_num) {
  printf("on_attach %s %d\n", device_id, device_num);
  return DL_SUCCESS;
}
dl_status my_on_detach(dl_t dl, const char *device_id, int device_num) {
  printf("on_detach %s %d\n", device_id, device_num);
  return DL_SUCCESS;
}

int main(int argc, char** argv) {
  int fd = dl_connect(3000);
  if (fd < 0) {
    return -1;
  }

  dl_t dl = dl_new();
  dl->send_packet = my_send_packet;
  dl->on_attach = my_on_attach;
  dl->on_detach = my_on_detach;
  my_t my = (my_t)malloc(sizeof(struct my_struct));
  my->fd = fd;
  dl->state = my;

  if (dl->start(dl)) {
    return -1;
  }

#define BUF_LEN 1024
  char buf[BUF_LEN];
  while (1) {
    ssize_t read_bytes = recv(fd, buf, BUF_LEN, 0);
    if (read_bytes < 0) {
      if (errno == EAGAIN) {
        continue;
      }
      break;
    }
    if (dl->on_recv(dl, buf, read_bytes)) {
      break;
    }
  }
  close(fd);
  free(dl->state);
  dl_free(dl);
  return 0;
}


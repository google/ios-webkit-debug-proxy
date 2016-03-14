// Google BSD license http://code.google.com/google_bsd_license.html
// Copyright 2012 Google Inc. wrightt@google.com

//
// A minimal websocket "echo" server
//
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ws_echo_common.h"
#include <iwdp/websocket.h>

#ifdef WIN32
#include <windows.h>
#include <winsock2.h>
#else
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#include <errno.h>
#include <unistd.h>

#define BUF_LEN 1024
#define PORT 8080

int main(int argc, char** argv) {
#ifdef WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2,2), &wsa_data) != ERROR_SUCCESS) {
    fprintf(stderr, "WSAStartup failed!\n");
    ExitProcess(-1);
  }
#endif
  int port = PORT;

  int sfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sfd < 0) {
    return -1;
  }

  struct sockaddr_in local;
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = INADDR_ANY;
  local.sin_port = htons(port);
  int on = 1;
  if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char *)&on,sizeof(on)) < 0 ||
      bind(sfd, (struct sockaddr*)&local, sizeof(local)) < 0 ||
      listen(sfd, 1)) {
    perror("Unable to bind");
#ifndef WIN32
    close(sfd);
#else
    closesocket(sfd);
#endif
    return -1;
  }

  int ret = 0;
  while (1) {
    int fd = accept(sfd, NULL, NULL);
    if (fd < 0) {
      perror("Accept failed");
      ret = -1;
      break;
    }

    my_t my = my_new(fd, port);
    if (!my) {
      ret = -1;
      break;
    }
    ws_t ws = my->ws;

    char buf[BUF_LEN];
    while (1) {
      ssize_t read_bytes = recv(fd, buf, BUF_LEN, 0);
      if (ws->on_recv(ws, buf, read_bytes)) {
        break;
      }
    }
#ifndef WIN32
    close(fd);
#else
    closesocket(fd);
#endif
    my_free(my);
  }

#ifndef WIN32
  close(sfd);
#else
  closesocket(sfd);
#endif
  return ret;
}

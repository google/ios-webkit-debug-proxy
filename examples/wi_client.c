// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

//
// A minimal webinspector client
//

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#include "ios-webkit-debug-proxy/webinspector.h"

// our state
struct my_wi_struct {
  char *device_id;
  int fd;
  wi_t wi;
};
typedef struct my_wi_struct *my_wi_t;

//
// inspector callbacks:
//

wi_status send_packet(wi_t wi, const char *packet, size_t length) {
  my_wi_t my_wi = (my_wi_t)wi->state;
  ssize_t sent_bytes = send(my_wi->fd, (void*)packet, length, 0);
  return (sent_bytes == length ? WI_SUCCESS : WI_ERROR);
}

wi_status recv_plist(wi_t wi, const plist_t rpc_dict) {
  char *xml = NULL;
  uint32_t length = 0;
  plist_to_xml(rpc_dict, &xml, &length);
  puts(xml);
  free(xml);
  return WI_SUCCESS;
}

//
// Main:
//

static int quit_flag = 0;

static void on_signal(int sig) {
  fprintf(stderr, "Exiting...\n");
  quit_flag++;
}

int main(int argc, char **argv) {
  // map ctrl-c to quit_flag=1
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

#ifdef WIN32
  WSADATA wsa_data;
  int res = WSAStartup(MAKEWORD(2,2), &wsa_data);
  if (res) {
    fprintf(stderr, "WSAStartup failed with error: %d\n", res);
    exit(1);
  }
#endif

  // parse args
  char *device_id = NULL;
  bool is_debug = false;
  int i = 0;
  for (i = 1; i < argc; i++) {
    if ((!strcmp(argv[i], "-u") || !strcmp(argv[i], "--udid")) &&
        i + 1 < argc) {
      free(device_id);
      device_id = strdup(argv[++i]);
    } else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug")) {
      is_debug = true;
    } else {
      bool is_help = (!strcmp(argv[i], "h") || !strcmp(argv[i], "--help"));
      char *name = strrchr(argv[0], '/');
      printf("Usage: %s OPTIONS\n"
          "Minimal iOS webinspector client.\n\n"
          "  -u, --udid UDID\ttarget device by its 40-digit device UDID\n"
          "  -d, --debug\t\tenable communication debugging\n",
          (name ? name + 1 : argv[0]));
      return (is_help ? 0 : 1);
    }
  }

  // connect to device
  char *device_id2 = NULL;
  int recv_timeout = 1000;
  int fd = wi_connect(device_id, &device_id2, NULL, NULL, NULL, recv_timeout);
  if (fd < 0) {
    return -1;
  }

  // create inspector
  my_wi_t my_wi = (my_wi_t)malloc(sizeof(struct my_wi_struct));
  wi_t wi = wi_new(false);
  memset(my_wi, 0, sizeof(struct my_wi_struct));
  my_wi->device_id = device_id2;
  my_wi->fd = fd;
  my_wi->wi = wi;
  wi->send_packet = send_packet;
  wi->recv_plist = recv_plist;
  wi->state = my_wi;
  wi->is_debug = &is_debug;

  // send "reportIdentifier"
  const char *xml = ""
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<plist version=\"1.0\"><dict>\n"
    "  <key>__selector</key><string>_rpc_reportIdentifier:</string>\n"
    "  <key>__argument</key><dict>\n"
    "    <key>WIRConnectionIdentifierKey</key>\n"
    "    <string>077BA242-564F-443B-B83A-EFBB337DAE35</string>\n"
    "</dict></dict></plist>";
  plist_t rpc_dict = NULL;
  plist_from_xml(xml, strlen(xml), &rpc_dict);
  wi->send_plist(wi, rpc_dict);
  plist_free(rpc_dict);

  // read responses until user presses ctrl-c
  char buf[1024];
  size_t buf_length = 1024;
  while (!quit_flag) {
    ssize_t read_bytes = recv(fd, buf, buf_length, 0);
    if (read_bytes < 0 && errno == EWOULDBLOCK) {
      continue;
    }
    if (wi->on_recv(wi, buf, read_bytes)) {
      break;
    }
  }

  // cleanup
  free(my_wi->device_id);
  wi_free(my_wi->wi);
  memset(my_wi, 0, sizeof(struct my_wi_struct));
  free(my_wi);
  if (fd >= 0) {
#ifdef WIN32
    closesocket(fd);
#else
    close(fd);
#endif
  }
#ifdef WIN32
  WSACleanup();
#endif
  return 0;
}

// Google BSD license http://code.google.com/google_bsd_license.html
// Copyright 2012 Google Inc. wrightt@google.com

//
// A minimal webinspector client
//
// We connect to the inspector, request a listing, and navigate to a page.
//

#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "webinspector.h"

static char *COMMANDS[] = {
  "{\"id\":1,\"method\":\"Page.navigate\",\"params\":"
    "{\"url\":\"http://www.google.com/\"}}",
  NULL,
};

void print_usage(int argc, char **argv) {
  char *name = strrchr(argv[0], '/');
  printf("Usage: %s OPTIONS\n", (name ? name + 1 : argv[0]));
  printf("Scripted iOS webinspector client.\n\n");
  printf
    ("  -U, --uuid UUID\tOptional iOS device 40-character UUID.\n"
     "  -h, --help\t\tprints usage information\n"
     "  -d, --debug\t\tenable communication debugging\n" "\n");
}

// our state
struct my_wi_struct {
  char *device_id;
  int fd;
  wi_t wi;
  char *connection_id;
  char *sender_id;
  uint8_t sent_fgl;
  uint8_t sent_fss;
  char *app_id;
  uint32_t page_id;
  int count;
};
typedef struct my_wi_struct *my_wi_t;
my_wi_t my_wi_new(char *device_id, int fd, bool *is_debug);
void my_wi_free(my_wi_t my_wi);

//
// inspector callbacks:
//

wi_status send_packet(wi_t wi, const char *packet, size_t length) {
  my_wi_t my_wi = (my_wi_t)wi->state;
  ssize_t sent_bytes = send(my_wi->fd, (void*)packet, length, 0);
  return (sent_bytes == length ? WI_SUCCESS : WI_ERROR);
}

wi_status on_reportSetup(wi_t wi) {
  my_wi_t my_wi = (my_wi_t)wi->state;
  return WI_SUCCESS;
}

wi_status on_reportConnectedApplicationList(wi_t wi, const wi_app_t *apps) {
  my_wi_t my_wi = (my_wi_t)wi->state;
  const wi_app_t *a;
  for (a = apps; *a; a++) {
    wi_app_t app = *a;
    if (!my_wi->sent_fgl) {
      printf("app %s\n", app->app_id);
      my_wi->sent_fgl = 1;
      wi->send_forwardGetListing(wi, my_wi->connection_id, app->app_id);
    }
  }
  return WI_SUCCESS;
}

wi_status on_applicationConnected(wi_t wi, const wi_app_t app) {
  my_wi_t my_wi = (my_wi_t)wi->state;
  if (!my_wi->sent_fgl && !strcmp(app->app_id,"com.apple.mobilesafari")) {
    printf("app %s\n", app->app_id);
    my_wi->sent_fgl = 1;
    wi->send_forwardGetListing(wi, my_wi->connection_id, app->app_id);
  }
  return WI_SUCCESS;
}

wi_status on_applicationDisconnected(wi_t wi, const wi_app_t app) {
  my_wi_t my_wi = (my_wi_t)wi->state;
  return WI_SUCCESS;
}

void send_next_command(wi_t wi) {
  my_wi_t my_wi = (my_wi_t)wi->state;
  char *data = COMMANDS[my_wi->count];
  if (data) {
    printf("send[%d] %s\n", my_wi->count, data);
    my_wi->count++;
    wi->send_forwardSocketData(wi, my_wi->connection_id,
        my_wi->app_id, my_wi->page_id, my_wi->sender_id,
        data, strlen(data));
  }
}

wi_status on_applicationSentListing(wi_t wi,
    const char *app_id, const wi_page_t *pages) {
  my_wi_t my_wi = (my_wi_t)wi->state;
  size_t i;
  const wi_page_t *p;
  for (p = pages; *p; p++) {
    wi_page_t page = *p;
    if (!my_wi->sent_fss) {
      my_wi->sent_fss = 1;
      my_wi->app_id = strdup(app_id);
      my_wi->page_id = page->page_id;
      printf("page %d: %s\n", page->page_id, page->url);
      wi_new_uuid(&my_wi->sender_id);
      wi->send_forwardSocketSetup(wi, my_wi->connection_id,
          app_id, page->page_id, my_wi->sender_id);

      send_next_command(wi);
    }
  }
  return WI_SUCCESS;
}

wi_status on_applicationSentData(wi_t wi,
    const char *app_id, const char *dest_id,
    const char *data, size_t length) {
  my_wi_t my_wi = (my_wi_t)wi->state;
  printf("Recv %.*s\n", (int)length, data);
  send_next_command(wi);
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

void parse_options(int argc, char **argv, char **to_device_id,
    bool *to_is_debug);

int main(int argc, char **argv) {
  // map ctrl-c to quit_flag=1
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  char *device_id = NULL;
  bool is_debug = false;
  parse_options(argc, argv, &device_id, &is_debug);

  char *device_id2 = NULL;
  int recv_timeout = 1000;
  int fd = wi_connect(device_id, &device_id2, NULL, recv_timeout);
  if (fd < 0) {
    return -1;
  }

  my_wi_t my_wi = my_wi_new(device_id2, fd, &is_debug);
  if (!my_wi) {
    return -1;
  }

  wi_t wi = my_wi->wi;
  wi_new_uuid(&my_wi->connection_id);
  if (wi->send_reportIdentifier(wi, my_wi->connection_id)) {
    return -1;
  }

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

  my_wi_free(my_wi);
  if (fd >= 0) {
    close(fd);
  }
  return 0;
}

void parse_options(int argc, char **argv, char **to_device_id,
    bool *to_is_debug) {
  static struct option longopts[] = {
    {"uuid", 1, NULL, 'U'},
    {"debug", 0, NULL, 'd'},
    {"help", 0, NULL, 'h'},

    // backwards compatibity, matches ideviceinfo
    {"udid", 1, NULL, 'u'},

    {NULL, 0, NULL, 0}
  };

  *to_device_id = NULL;
  *to_is_debug = false;
  while (1) {
    int c = getopt_long(argc, argv, "U:dhu:", longopts,
        (int *) 0);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 'h':
        print_usage(argc, argv);
        exit(0);
      case 'U':
      case 'u':
        if (strlen(optarg) != 40) {
          printf("%s: invalid UUID specified (length != 40)\n",
              argv[0]);
          print_usage(argc, argv);
          exit(2);
        }
        *to_device_id = strdup(optarg);
        break;
      case 'd':
        *to_is_debug = true;
        break;
      default:
        print_usage(argc, argv);
        exit(2);
    }
  }

  if ((argc - optind) > 0) {
    print_usage(argc, argv);
    exit(2);
  }
}

//
// Structs:
//

my_wi_t my_wi_new(char *device_id, int fd, bool *is_debug) {
  my_wi_t my_wi = (my_wi_t)malloc(sizeof(struct my_wi_struct));
  if (!my_wi) {
    return NULL;
  }
  wi_t wi = wi_new(false);
  if (!wi) {
    free(my_wi);
    return NULL;
  }
  memset(my_wi, 0, sizeof(struct my_wi_struct));
  my_wi->device_id = device_id;
  my_wi->fd = fd;
  my_wi->wi = wi;
  wi->send_packet = send_packet;
  wi->on_reportSetup = on_reportSetup;
  wi->on_reportConnectedApplicationList = on_reportConnectedApplicationList;
  wi->on_applicationConnected = on_applicationConnected;
  wi->on_applicationDisconnected = on_applicationDisconnected;
  wi->on_applicationSentListing = on_applicationSentListing;
  wi->on_applicationSentData = on_applicationSentData;
  wi->state = my_wi;
  wi->is_debug = is_debug;
  return my_wi;
}

void my_wi_free(my_wi_t my_wi) {
  if (my_wi) {
    free(my_wi->device_id);
    free(my_wi->connection_id);
    free(my_wi->sender_id);
    wi_free(my_wi->wi);
    memset(my_wi, 0, sizeof(struct my_wi_struct));
    free(my_wi);
  }
}


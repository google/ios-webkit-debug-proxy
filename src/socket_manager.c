// Google BSD license http://code.google.com/google_bsd_license.html
// Copyright 2012 Google Inc. wrightt@google.com

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "socket_manager.h"
#include "hash_table.h"

#ifdef __MACH__
#define SIZEOF_FD_SET sizeof(struct fd_set)
#define RECV_FLAGS 0
#else
#define SIZEOF_FD_SET sizeof(fd_set)
#define RECV_FLAGS MSG_DONTWAIT
#endif

struct sm_private {
  struct timeval timeout;
  fd_set *server_fds;
  fd_set *master_fds;
  fd_set *ready_fds;
  int max_fd;
  ht_t fd_to_value;
  char *buf;
  size_t buf_length;
};


int sm_listen(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  int opts = fcntl(fd, F_GETFL);
  struct sockaddr_in local;
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = INADDR_ANY;
  local.sin_port = htons(port);
  int ra = 1;
  int nb = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&ra,sizeof(ra)) < 0 ||
      opts < 0 ||
      ioctl(fd, FIONBIO, (char *)&nb) < 0 ||
      bind(fd, (struct sockaddr*)&local, sizeof(local)) < 0 ||
      listen(fd, 5)) {
    close(fd);
    return -1;
  }
  return fd;
}

int sm_connect(const char *hostname, int port) {
  int ret = -1;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res0;
  char *port_str = NULL;
  asprintf(&port_str, "%d", port);
  ret = getaddrinfo(hostname, port_str, &hints, &res0);
  free(port_str);
  if (ret) {
    perror("Unknown host");
    return (ret < 0 ? ret : -1);
  }
  struct addrinfo *res;
  for (res = res0; res; res = res->ai_next) {
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
      continue;
    }
    int opts = fcntl(fd, F_GETFL);
    if (opts < 0) {
      close(fd);
      continue;
    }
    // TODO use non-blocking connect:
    //   if (fcntl(fd, F_SETFL, (opts | O_NONBLOCK)) < 0) { ... }
    // but this causes a send error for reachable hosts:
    //   Socket is not connected
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0 &&
        errno != EINPROGRESS) {
      close(fd);
      continue;
    }
    ret = fd;
    break;
  }
  freeaddrinfo(res0);
  return ret;
}


sm_status sm_on_debug(sm_t self, const char *format, ...) {
  if (self->is_debug && *self->is_debug) {
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    va_end(args);
  }
  return SM_SUCCESS;
}

sm_status sm_add_fd(sm_t self, int fd, void *value, bool is_server) {
  sm_private_t my = self->private_state;
  if (FD_ISSET(fd, my->master_fds)) {
    return SM_ERROR;
  }
  if (ht_put(my->fd_to_value, HT_KEY(fd), value)) {
    // The above FD_ISSET(..master..) should prevent this
    return SM_ERROR;
  }
  // is_server == getsockopt(..., SO_ACCEPTCONN, ...)?
  sm_on_debug(self, "ss.add%s_fd(%d)", (is_server ? "_server" : ""), fd);
  FD_SET(fd, my->master_fds);
  FD_CLR(fd, my->ready_fds);
  if (is_server) {
    FD_SET(fd, my->server_fds);
  }
  if (fd > my->max_fd) {
    my->max_fd = fd;
  }
  return SM_SUCCESS;
}

sm_status sm_remove_fd(sm_t self, int fd) {
  sm_private_t my = self->private_state;
  if (!FD_ISSET(fd, my->master_fds)) {
    return SM_ERROR;
  }
  void *value = ht_put(my->fd_to_value, HT_KEY(fd), NULL);
  bool is_server = FD_ISSET(fd, my->server_fds);
  sm_on_debug(self, "ss.remove%s_fd(%d)", (is_server ? "_server" : ""), fd);
  sm_status ret = self->on_close(self, fd, value, is_server);
  close(fd);
  FD_CLR(fd, my->master_fds);
  if (is_server) {
    FD_CLR(fd, my->server_fds);
  }
  FD_CLR(fd, my->ready_fds);
  if (fd == my->max_fd) {
    while (my->max_fd >= 0 && !FD_ISSET(my->max_fd, my->master_fds)) {
      my->max_fd--;
    }
  }
  return ret;
}

int sm_select(sm_t self, int timeout_secs) {
  sm_private_t my = self->private_state;

  if (my->max_fd <= 0) {
    return -1;
  }

  my->timeout.tv_sec = timeout_secs;

  // see if any sockets are readable
  memcpy(my->ready_fds, my->master_fds, SIZEOF_FD_SET);
  int num_ready = select(my->max_fd + 1, my->ready_fds,
      NULL, NULL, &my->timeout);

  // see if any sockets are readable
  if (num_ready == 0) {
    return 0; // timeout, select again
  }
  if (num_ready < 0) {
    if (errno != EINTR && errno != EAGAIN) {
      // might want to sleep here?
      perror("select failed");
      return -errno;
    }
    return 0;
  }

  int num_left = num_ready;
  int fd;
  for (fd = 0; fd <= my->max_fd && num_left > 0; fd++) {
    if (!FD_ISSET(fd, my->ready_fds)) {
      continue;
    }
    num_left--;
    if (FD_ISSET(fd, my->server_fds)) {
      while (1) {
        int new_fd = accept(fd, NULL, NULL);
        if (new_fd < 0) {
          if (errno != EWOULDBLOCK) {
            perror("accept failed");
            return -errno;
          }
          break;
        }
        sm_on_debug(self, "ss.accept server=%d new_client=%d",
            fd, new_fd);
        void *value = ht_get_value(my->fd_to_value, HT_KEY(fd));
        void *new_value = NULL;
        if (self->on_accept(self, fd, value, new_fd, &new_value)) {
          close(new_fd);
        } else if (self->add_fd(self, new_fd, new_value, false)) {
          self->on_close(self, new_fd, new_value, false);
          close(new_fd);
        }
      }
    } else {
      while (1) {
        ssize_t read_bytes = recv(fd, my->buf, my->buf_length, RECV_FLAGS);
        if (read_bytes < 0) {
          if (errno != EWOULDBLOCK) {
            perror("recv failed");
            self->remove_fd(self, fd);
          }
          break;
        }
        sm_on_debug(self, "ss.recv fd=%d len=%zd", fd, read_bytes);
        void *value = ht_get_value(my->fd_to_value, HT_KEY(fd));
        if (read_bytes == 0 ||
            self->on_recv(self, fd, value, my->buf, read_bytes)) {
          self->remove_fd(self, fd);
          break;
        }
      }
    }
  }
  return num_ready;
}

sm_status sm_cleanup(sm_t self) {
  sm_private_t my = self->private_state;
  int fd;
  for (fd = 0; fd <= my->max_fd; fd++) {
    if (FD_ISSET(fd, my->master_fds)) {
      self->remove_fd(self, fd);
    }
  }
  return SM_SUCCESS;
}

//
// STRUCTS
//

void sm_private_free(sm_private_t my) {
  if (my) {
    if (my->master_fds) {
      free(my->master_fds);
    }
    if (my->server_fds) {
      free(my->server_fds);
    }
    if (my->ready_fds) {
      free(my->ready_fds);
    }
    ht_free(my->fd_to_value);
    if (my->buf) {
      free(my->buf);
    }
    memset(my, 0, sizeof(struct sm_private));
    free(my);
  }
}

sm_private_t sm_private_new(size_t buf_length) {
  sm_private_t my = (sm_private_t)malloc(sizeof(struct sm_private));
  if (!my) {
    return NULL;
  }
  memset(my, 0, sizeof(struct sm_private));
  my->master_fds = (fd_set *)malloc(SIZEOF_FD_SET);
  my->server_fds = (fd_set *)malloc(SIZEOF_FD_SET);
  my->ready_fds = (fd_set *)malloc(SIZEOF_FD_SET);
  my->fd_to_value = ht_new(HT_INT_KEYS);
  my->buf = (char *)calloc(buf_length, sizeof(char *));
  if (!my->buf || !my->master_fds || !my->server_fds || !my->ready_fds ||
      !my->fd_to_value) {
    sm_private_free(my);
    return NULL;
  }
  FD_ZERO(my->master_fds);
  FD_ZERO(my->server_fds);
  FD_ZERO(my->ready_fds);
  my->max_fd = -1;
  my->timeout.tv_sec = 5;
  my->timeout.tv_usec = 0;
  my->buf_length = buf_length;
  return my;
}

sm_t sm_new(size_t buf_length) {
  sm_private_t my = sm_private_new(buf_length);
  if (!my) {
    return NULL;
  }
  sm_t self = (sm_t)malloc(sizeof(struct sm_struct));
  if (!self) {
    sm_private_free(my);
    return NULL;
  }
  memset(self, 0, sizeof(struct sm_struct));
  self->add_fd = sm_add_fd;
  self->remove_fd = sm_remove_fd;
  self->select = sm_select;
  self->cleanup = sm_cleanup;
  self->private_state = my;
  return self;
}

void sm_free(sm_t self) {
  if (self) {
    sm_private_free(self->private_state);
    memset(self, 0, sizeof(struct sm_struct));
    free(self);
  }
}


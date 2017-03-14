// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
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
#include <unistd.h>

#include "char_buffer.h"
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
  // fds:
  fd_set *all_fds;
  int max_fd;  // max fd in all_fds
  // subsets of all_fds:
  fd_set *server_fds; // can on_accept, i.e. "is_server"
  fd_set *send_fds;   // blocked sends, same as fd_to_sendq.keys
  fd_set *recv_fds;   // can recv, same as all_fds - sendq.recv_fd's
  // fd to on_* callback
  ht_t fd_to_value;
  // fd to blocked sm_sendq_t, often empty
  ht_t fd_to_sendq;
  // temp recv buffer, for use in sm_select:
  char *tmp_buf;
  size_t tmp_buf_length;
  // temp fd sets, for use in sm_select:
  fd_set *tmp_send_fds;
  fd_set *tmp_recv_fds;
  fd_set *tmp_fail_fds;
  // current sm_select on_recv fd, only set when in sm_select loop
  int curr_recv_fd;
};

struct sm_sendq;
typedef struct sm_sendq *sm_sendq_t;
struct sm_sendq {
  void *value;  // for on_sent
  int recv_fd;  // the my->recv_fd that caused this blocked send
  char *begin;  // sm_send data
  char *head;
  char *tail;   // begin + sm_send length
  sm_sendq_t next;
};
sm_sendq_t sm_sendq_new(int recv_fd, void *value, const char *data,
    size_t length);
void sm_sendq_free(sm_sendq_t sendq);


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
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res0;
  char *port_str = NULL;
  if (asprintf(&port_str, "%d", port) < 0) {
    return -1;  // asprintf failed
  }
  int ret = getaddrinfo(hostname, port_str, &hints, &res0);
  free(port_str);
  if (ret) {
    perror("Unknown host");
    return (ret < 0 ? ret : -1);
  }
  ret = -1;
  int fd = 0;
  struct addrinfo *res;
  for (res = res0; res; res = res->ai_next) {
    if (fd > 0) {
      close(fd);
    }
    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
      continue;
    }
    // try non-blocking connect, usually succeeds even if unreachable
    int opts = fcntl(fd, F_GETFL);
    if (opts < 0 ||
        fcntl(fd, F_SETFL, (opts | O_NONBLOCK)) < 0 ||
        ((connect(fd, res->ai_addr, res->ai_addrlen) < 0) ==
         (errno != EINPROGRESS))) {
      continue;
    }
    // try blocking select to verify its reachable
    struct timeval to;
    to.tv_sec = 0;
    to.tv_usec= 500*1000; // arbitrary
    fd_set error_fds;
    FD_ZERO(&error_fds);
    FD_SET(fd, &error_fds);
    if (fcntl(fd, F_SETFL, opts) < 0) {
      continue;
    }
    int is_error = select(fd + 1, &error_fds, NULL, NULL, &to);
    if (is_error) {
      continue;
    }
    // success!  set back to non-blocking and return
    if (fcntl(fd, F_SETFL, (opts | O_NONBLOCK)) < 0) {
      continue;
    }
    ret = fd;
    break;
  }
  if (fd > 0 && ret <= 0) {
    close(fd);
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
  if (FD_ISSET(fd, my->all_fds)) {
    return SM_ERROR;
  }
  if (ht_put(my->fd_to_value, HT_KEY(fd), value)) {
    // The above FD_ISSET(..master..) should prevent this
    return SM_ERROR;
  }
  // is_server == getsockopt(..., SO_ACCEPTCONN, ...)?
  sm_on_debug(self, "ss.add%s_fd(%d)", (is_server ? "_server" : ""), fd);
  FD_SET(fd, my->all_fds);
  FD_CLR(fd, my->send_fds); // only set if blocked
  FD_SET(fd, my->recv_fds);
  FD_CLR(fd, my->tmp_send_fds);
  FD_CLR(fd, my->tmp_recv_fds);
  FD_CLR(fd, my->tmp_fail_fds);
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
  if (!FD_ISSET(fd, my->all_fds)) {
    return SM_ERROR;
  }
  void *value = ht_put(my->fd_to_value, HT_KEY(fd), NULL);
  bool is_server = FD_ISSET(fd, my->server_fds);
  sm_on_debug(self, "ss.remove%s_fd(%d)", (is_server ? "_server" : ""), fd);
  sm_status ret = self->on_close(self, fd, value, is_server);
  close(fd);
  FD_CLR(fd, my->all_fds);
  if (is_server) {
    FD_CLR(fd, my->server_fds);
  }
  FD_CLR(fd, my->send_fds);
  FD_CLR(fd, my->recv_fds);
  FD_CLR(fd, my->tmp_send_fds);
  FD_CLR(fd, my->tmp_recv_fds);
  FD_CLR(fd, my->tmp_fail_fds);
  if (fd == my->max_fd) {
    while (my->max_fd >= 0 && !FD_ISSET(my->max_fd, my->all_fds)) {
      my->max_fd--;
    }
  }
  if (ht_size(my->fd_to_sendq)) {
    sm_sendq_t *qs = (sm_sendq_t *)ht_values(my->fd_to_sendq);
    sm_sendq_t *q;
    for (q = qs; *q; q++) {
      sm_sendq_t sendq = *q;
      while (sendq) {
        if (sendq->recv_fd == fd) {
          sendq->recv_fd = 0;
          // don't abort this blocked send, even though the "cause" has ended
        }
        sendq = sendq->next;
      }
    }
    free(qs);
  }
  return ret;
}

sm_status sm_send(sm_t self, int fd, const char *data, size_t length,
    void* value) {
  sm_private_t my = self->private_state;
  sm_sendq_t sendq = (sm_sendq_t)ht_get_value(my->fd_to_sendq, HT_KEY(fd));
  const char *head = data;
  const char *tail = data + length;
  if (!sendq) {
    // send as much as we can without blocking
    while (1) {
      ssize_t sent_bytes = send(fd, (void*)head, (tail - head), 0);
      if (sent_bytes <= 0) {
        if (sent_bytes && errno != EWOULDBLOCK) {
          sm_on_debug(self, "ss.failed fd=%d", fd);
          perror("send failed");
          return SM_ERROR;
        }
        break;
      }
      head += sent_bytes;
      if (head >= tail) {
        self->on_sent(self, fd, value, data, length);
        return SM_SUCCESS; // this is the typical case
      }
    }
  }
  // we can't send this now, so queue it
  int curr_recv_fd = my->curr_recv_fd;
  sm_sendq_t newq = sm_sendq_new(curr_recv_fd, value, head, tail - head);
  if (sendq) {
    while (sendq->next) {
      sendq = sendq->next;
    }
    sendq->next = newq;
  } else {
    ht_put(my->fd_to_sendq, HT_KEY(fd), newq);
    FD_SET(fd, my->send_fds);
  }
  sm_on_debug(self, "ss.sendq<%p> new fd=%d recv_fd=%d length=%zd"
      ", prev=<%p>", newq, fd, curr_recv_fd, tail - head, sendq);
  if (curr_recv_fd && FD_ISSET(curr_recv_fd, my->recv_fds)) {
    // block the current recv_fd, to prevent our sendq from growing too large.
    // At worst our recv_fds are all trying to send to the same fd, in which
    // case we'll eventually block all of them until the first blocked send
    // succeeds.
    sm_on_debug(self, "ss.sendq<%p> disable recv_fd=%d", newq, curr_recv_fd);
    FD_CLR(curr_recv_fd, my->recv_fds);
    FD_CLR(curr_recv_fd, my->tmp_recv_fds);
  }
  return SM_SUCCESS;
}

void sm_accept(sm_t self, int fd) {
  sm_private_t my = self->private_state;
  while (1) {
    int new_fd = accept(fd, NULL, NULL);
    if (new_fd < 0) {
      if (errno != EWOULDBLOCK) {
        perror("accept failed");
        self->remove_fd(self, fd);
        return;
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
}

void sm_resend(sm_t self, int fd) {
  sm_private_t my = self->private_state;
  sm_sendq_t sendq = ht_get_value(my->fd_to_sendq, HT_KEY(fd));
  while (sendq) {
    char *head = sendq->head;
    char *tail = sendq->tail;
    // send as much as we can without blocking
    sm_on_debug(self, "ss.sendq<%p> resume send to fd=%d len=%zd", sendq, fd,
        (tail - head));
    while (head < tail) {
      ssize_t sent_bytes = send(fd, (void*)head, (tail - head), 0);
      if (sent_bytes <= 0) {
        if (sent_bytes && errno != EWOULDBLOCK) {
          perror("sendq retry failed");
          self->remove_fd(self, fd);
          return;
        }
        break;
      }
      head += sent_bytes;
    }
    sendq->head = head;
    if (head < tail) {
      // still have stuff to send
      sm_on_debug(self, "ss.sendq<%p> defer len=%zd", sendq, (tail - head));
      break;
    }
    self->on_sent(self, fd, sendq->value, sendq->begin, tail - sendq->begin);
    sm_sendq_t nextq = sendq->next;
    ht_put(my->fd_to_sendq, HT_KEY(fd), nextq);
    if (!nextq) {
      FD_CLR(fd, my->send_fds);
    }
    int recv_fd = sendq->recv_fd;
    if (recv_fd && FD_ISSET(recv_fd, my->all_fds)) {
      // if no other sendq's match this blocked recv_fd, re-enable it
      bool found = false;
      if (ht_size(my->fd_to_sendq)) {
        sm_sendq_t *qs = (sm_sendq_t *)ht_values(my->fd_to_sendq);
        sm_sendq_t *q;
        for (q = qs; *q && !found; q++) {
          sm_sendq_t sq;
          for (sq = *q; sq && !found; sq = sq->next) {
            found |= (sq->recv_fd == recv_fd);
          }
        }
        free(qs);
      }
      if (!found) {
        sm_on_debug(self, "ss.sendq<%p> re-enable recv_fd=%d", sendq, recv_fd);
        FD_SET(recv_fd, my->recv_fds);
        // don't FD_SET(tmp_recv_fds), since maybe there was no input
        // instead, let the next select loop pick it up
      }
    }
    sm_on_debug(self, "ss.sendq<%p> free, next=<%p>", sendq, nextq);
    sm_sendq_free(sendq);
    sendq = nextq;
  }
}

void sm_recv(sm_t self, int fd) {
  sm_private_t my = self->private_state;
  my->curr_recv_fd = fd;
  while (1) {
    ssize_t read_bytes = recv(fd, my->tmp_buf, my->tmp_buf_length, RECV_FLAGS);
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
        self->on_recv(self, fd, value, my->tmp_buf, read_bytes)) {
      self->remove_fd(self, fd);
      break;
    }
  }
  my->curr_recv_fd = 0;
}

int sm_select(sm_t self, int timeout_secs) {
  sm_private_t my = self->private_state;

  if (my->max_fd <= 0) {
    return -1;
  }

  my->timeout.tv_sec = timeout_secs;

  // copy into tmp
  memcpy(my->tmp_send_fds, my->send_fds, SIZEOF_FD_SET);
  memcpy(my->tmp_recv_fds, my->recv_fds, SIZEOF_FD_SET);
  memcpy(my->tmp_fail_fds, my->all_fds, SIZEOF_FD_SET);
  int num_ready = select(my->max_fd + 1, my->tmp_recv_fds,
      my->tmp_send_fds, my->tmp_fail_fds, &my->timeout);

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
    bool can_send = FD_ISSET(fd, my->tmp_send_fds);
    bool can_recv = FD_ISSET(fd, my->tmp_recv_fds);
    bool is_fail = FD_ISSET(fd, my->tmp_fail_fds);
    if (!can_send && !can_recv && !is_fail) {
      continue;
    }
    num_left--;
    if (is_fail) {
      self->remove_fd(self, fd);
    } else if (FD_ISSET(fd, my->server_fds)) {
      sm_accept(self, fd);
    } else {
      if (can_send) {
        sm_resend(self, fd);
      }
      if (can_recv) {
        sm_recv(self, fd);
      }
    }
  }
  return num_ready;
}

sm_status sm_cleanup(sm_t self) {
  sm_private_t my = self->private_state;
  int fd;
  for (fd = 0; fd <= my->max_fd; fd++) {
    if (FD_ISSET(fd, my->all_fds)) {
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
    free(my->all_fds);
    free(my->server_fds);
    free(my->send_fds);
    free(my->recv_fds);
    free(my->tmp_send_fds);
    free(my->tmp_recv_fds);
    free(my->tmp_fail_fds);
    ht_free(my->fd_to_value);
    ht_free(my->fd_to_sendq);
    free(my->tmp_buf);
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
  my->all_fds = (fd_set *)malloc(SIZEOF_FD_SET);
  my->server_fds = (fd_set *)malloc(SIZEOF_FD_SET);
  my->send_fds = (fd_set *)malloc(SIZEOF_FD_SET);
  my->recv_fds = (fd_set *)malloc(SIZEOF_FD_SET);
  my->tmp_send_fds = (fd_set *)malloc(SIZEOF_FD_SET);
  my->tmp_recv_fds = (fd_set *)malloc(SIZEOF_FD_SET);
  my->tmp_fail_fds = (fd_set *)malloc(SIZEOF_FD_SET);
  my->fd_to_value = ht_new(HT_INT_KEYS);
  my->fd_to_sendq = ht_new(HT_INT_KEYS);
  my->tmp_buf = (char *)calloc(buf_length, sizeof(char *));
  if (!my->tmp_buf || !my->all_fds || !my->server_fds ||
      !my->send_fds || !my->recv_fds ||
      !my->tmp_send_fds || !my->tmp_recv_fds || !my->tmp_fail_fds ||
      !my->fd_to_value || !my->fd_to_sendq) {
    sm_private_free(my);
    return NULL;
  }
  FD_ZERO(my->all_fds);
  FD_ZERO(my->server_fds);
  FD_ZERO(my->send_fds);
  FD_ZERO(my->recv_fds);
  FD_ZERO(my->tmp_send_fds);
  FD_ZERO(my->tmp_recv_fds);
  FD_ZERO(my->tmp_fail_fds);
  my->max_fd = -1;
  my->timeout.tv_sec = 5;
  my->timeout.tv_usec = 0;
  my->tmp_buf_length = buf_length;
  return my;
}

sm_sendq_t sm_sendq_new(int recv_fd, void *value, const char *data,
    size_t length) {
  sm_sendq_t ret = (sm_sendq_t)malloc(sizeof(struct sm_sendq));
  memset(ret, 0, sizeof(struct sm_sendq));
  ret->recv_fd = recv_fd;
  ret->value = value;
  ret->begin = (char *)malloc(length);
  memcpy(ret->begin, data, length);
  ret->head = ret->begin;
  ret->tail = ret->begin + length;
  return ret;
}

void sm_sendq_free(sm_sendq_t sendq) {
  if (sendq) {
    free(sendq->begin);
    memset(sendq, 0, sizeof(struct sm_sendq));
    free(sendq);
  }
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
  self->send = sm_send;
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


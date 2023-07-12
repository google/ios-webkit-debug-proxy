// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

//
// A generic select-based socket manager.
//

#ifndef SOCKET_SELECTOR_H
#define	SOCKET_SELECTOR_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

// Bind a server port, return the file descriptor (or -1 for error).
int sm_listen(int port);

// Connect to a server, return the file descriptor (or -1 for error).
int sm_connect(const char *socket_addr);


typedef uint8_t sm_status;
#define SM_ERROR 1
#define SM_SUCCESS 0


struct sm_private;
typedef struct sm_private *sm_private_t;

struct sm_struct;
typedef struct sm_struct *sm_t;
sm_t sm_new(size_t buffer_length);
void sm_free(sm_t self);

struct sm_struct {

  // Call these APIs:

  // @param value a value to associate with this fd, which will be passed
  // in future on_accept/on_recv/on_close callbacks.
  sm_status (*add_fd)(sm_t self, int fd, void *ssl_session, void *value, bool
      is_server);

  sm_status (*remove_fd)(sm_t self, int fd);

  // @param value a value for the on_sent callback
  sm_status (*send)(sm_t self, int fd, const char *data, size_t length,
      void* value);

  int (*select)(sm_t self, int timeout_secs);

  sm_status (*cleanup)(sm_t self);

  void *state;
  bool *is_debug;

  // Set these callbacks:

  // @param server_value specified in the add_fd call
  // @param to_value will be used in future on_recv calls
  sm_status (*on_accept)(sm_t self,
                         int server_fd, void *server_value,
                         int fd, void **to_value);

  sm_status (*on_sent)(sm_t self, int fd, void *value,
                       const char *buf, ssize_t length);

  sm_status (*on_recv)(sm_t self, int fd, void *value,
                       const char *buf, ssize_t length);

  sm_status (*on_close)(sm_t self, int fd, void *value, bool is_server);

  // For internal use only:
  sm_private_t private_state;
};

#ifdef	__cplusplus
}
#endif

#endif	/* SOCKET_SELECTOR_H */


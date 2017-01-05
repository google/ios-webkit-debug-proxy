// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libimobiledevice/installation_proxy.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>

#include "char_buffer.h"
#include "webinspector.h"


#define WI_DEBUG 1

// TODO figure out exact value
#define MAX_RPC_LEN 8096 - 500

// some arbitrarly limit, to catch bad packets
#define MAX_BODY_LENGTH 1<<24

struct wi_private {
  bool is_sim;
  cb_t in;
  cb_t partial;
  bool has_length;
  size_t body_length;
};

//
// CONNECT
//

wi_status idevice_connection_get_fd_wrapper(idevice_connection_t connection,
    int *to_fd) {

  int fd;
  idevice_error_t err = idevice_connection_get_fd(connection, &fd);
  if (err < 0) {
    return WI_ERROR;
  }

  struct stat fd_stat;
  if (fstat(fd, &fd_stat) < 0 || !S_ISSOCK(fd_stat.st_mode)) {
    perror("idevice_connection fd is not a socket?");
    return WI_ERROR;
  }
  
  *to_fd = fd;
  return WI_SUCCESS;
}

int wi_connect(const char *device_id, char **to_device_id,
    char **to_device_name, int recv_timeout) {
  int ret = -1;

  idevice_t phone = NULL;
  plist_t node = NULL;
  lockdownd_service_descriptor_t service = NULL;
  lockdownd_client_t client = NULL;
  idevice_connection_t connection = NULL;
  int fd = -1;

  // get phone
  if (idevice_new(&phone, device_id)) {
    perror("No iPhone found, is it plugged in?");
    goto leave_cleanup;
  }

  // connect to lockdownd
  if (lockdownd_client_new_with_handshake(
        phone, &client, "ios_webkit_debug_proxy")) {
    perror("Could not connect to lockdownd. Exiting.");
    goto leave_cleanup;
  }

  // get device info
  if (to_device_id &&
      !lockdownd_get_value(client, NULL, "UniqueDeviceID", &node)) {
    plist_get_string_val(node, to_device_id);
    plist_free(node);
    node = NULL;
  }
  if (to_device_name &&
      !lockdownd_get_value(client, NULL, "DeviceName", &node)) {
    plist_get_string_val(node, to_device_name);
  }

  // start webinspector, get port
  if (lockdownd_start_service(client, "com.apple.webinspector", &service) ||
      !service->port) {
    perror("Could not start com.apple.webinspector!");
    goto leave_cleanup;
  }

  // connect to webinspector
  if (idevice_connect(phone, service->port, &connection)) {
    perror("idevice_connect failed!");
    goto leave_cleanup;
  }

  if (client) {
    // not needed anymore
    lockdownd_client_free(client);
    client = NULL;
  }

  // extract the connection fd
  if (idevice_connection_get_fd_wrapper(connection, &fd)) {
    perror("Unable to get connection file descriptor.");
    goto leave_cleanup;
  }

  if (recv_timeout < 0) {
    int opts = fcntl(fd, F_GETFL);
    if (!opts || fcntl(fd, F_SETFL, (opts | O_NONBLOCK)) < 0) {
      perror("Could not set socket to non-blocking");
      goto leave_cleanup;
    }
  } else {
    long millis = (recv_timeout > 0 ? recv_timeout : 5000);
    struct timeval tv;
    tv.tv_sec = (time_t) (millis / 1000);
    tv.tv_usec = (time_t) ((millis - (tv.tv_sec * 1000)) * 1000);
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,
          sizeof(tv))) {
      perror("Could not set socket receive timeout");
      goto leave_cleanup;
    }
  }

  // success
  ret = fd;

leave_cleanup:
  if (ret < 0 && fd > 0) {
    close(fd);
  }
  // don't call usbmuxd_disconnect(fd)!
  //idevice_disconnect(connection);
  free(connection);
  lockdownd_client_free(client);
  plist_free(node);
  idevice_free(phone);
  return ret;
}

//
// SEND
//

wi_status wi_on_error(wi_t self, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  return WI_ERROR;
}

wi_status wi_on_debug(wi_t self, const char *message,
    const char *buf, size_t length) {
  if (self->is_debug && *self->is_debug) {
    char *text;
    cb_asprint(&text, buf, length, 80, 30);
    printf("%s[%zd]:\n%s\n", message, length, text);
    free(text);
  }
  return WI_SUCCESS;
}

/*
   WIRFinalMessageKey
   __selector
   __argument
 */
wi_status wi_send_plist(wi_t self, plist_t rpc_dict) {
  wi_private_t my = self->private_state;
  char *rpc_bin = NULL;
  uint32_t rpc_len = 0;
  plist_to_bin(rpc_dict, &rpc_bin, &rpc_len);
  // if our message is <8k, we'll send a single final_msg,
  // otherwise we'll send <8k partial_msg "chunks" then a final_msg "chunk"
  wi_status ret = WI_ERROR;
  uint32_t i;
  for (i = 0; ; i += MAX_RPC_LEN) {
    bool is_partial = false;
    char *data = NULL;
    uint32_t data_len = 0;
    if (my->is_sim) {
      data = rpc_bin;
      data_len = rpc_len;
      rpc_bin = NULL;
    } else {
      is_partial = (rpc_len - i > MAX_RPC_LEN);
      plist_t wi_dict = plist_new_dict();
      plist_t wi_rpc = plist_new_data(rpc_bin + i,
          (is_partial ? MAX_RPC_LEN : rpc_len - i));
      plist_dict_set_item(wi_dict,
          (is_partial ? "WIRPartialMessageKey" : "WIRFinalMessageKey"), wi_rpc);
      plist_to_bin(wi_dict, &data, &data_len);
      plist_free(wi_dict);
      wi_dict = NULL;
      wi_rpc = NULL; // freed by wi_dict
      if (!data) {
        break;
      }
    }

    size_t length = data_len + 4;
    char *out_head = (char*)malloc(length * sizeof(char));
    if (!out_head) {
      if (!my->is_sim) {
        free(data);
      }
      break;
    }
    char *out_tail = out_head;

    // write big-endian int
    *out_tail++ = ((data_len >> 24) & 0xFF);
    *out_tail++ = ((data_len >> 16) & 0xFF);
    *out_tail++ = ((data_len >> 8) & 0xFF);
    *out_tail++ = (data_len & 0xFF);

    // write data
    memcpy(out_tail, data, data_len);
    free(data);

    wi_on_debug(self, "wi.send_packet", out_head, length);
    wi_status not_sent = self->send_packet(self, out_head, length);
    free(out_head);
    if (not_sent) {
      break;
    }

    if (!is_partial) {
      ret = WI_SUCCESS;
      break;
    }
  }
  free(rpc_bin);
  return ret;
}

//
// RECV
//

wi_status wi_parse_length(wi_t self, const char *buf, size_t *to_length) {
  if (!buf || !to_length) {
    return WI_ERROR;
  }
  *to_length = (
      ((((unsigned char) buf[0]) & 0xFF) << 24) |
      ((((unsigned char) buf[1]) & 0xFF) << 16) |
      ((((unsigned char) buf[2]) & 0xFF) << 8) |
      (((unsigned char) buf[3]) & 0xFF));
  if (MAX_BODY_LENGTH > 0 && *to_length > MAX_BODY_LENGTH) {
#define TO_CHAR(c) ((c) >= ' ' && (c) < '~' ? (c) : '.')
    return self->on_error(self, "Invalid packet header "
        "0x%x%x%x%x == %c%c%c%c == %zd",
        buf[0], buf[1], buf[2], buf[3],
        TO_CHAR(buf[0]), TO_CHAR(buf[1]),
        TO_CHAR(buf[2]), TO_CHAR(buf[3]),
        *to_length);
  }
  return WI_SUCCESS;
}

wi_status wi_parse_plist(wi_t self, const char *from_buf, size_t length,
    plist_t *to_rpc_dict, bool *to_is_partial) {
  wi_private_t my = self->private_state;
  *to_is_partial = false;
  *to_rpc_dict = NULL;

  if (my->is_sim) {
    plist_from_bin(from_buf, length, to_rpc_dict);
  } else {
    plist_t wi_dict = NULL;
    plist_from_bin(from_buf, length, &wi_dict);
    if (!wi_dict) {
      return WI_ERROR;
    }
    plist_t wi_rpc = plist_dict_get_item(wi_dict, "WIRFinalMessageKey");
    if (!wi_rpc) {
      wi_rpc = plist_dict_get_item(wi_dict, "WIRPartialMessageKey");
      if (!wi_rpc) {
        return WI_ERROR;
      }
      *to_is_partial = true;
    }

    uint64_t rpc_len = 0;
    char *rpc_bin = NULL;
    plist_get_data_val(wi_rpc, &rpc_bin, &rpc_len);
    plist_free(wi_dict); // also frees wi_rpc
    if (!rpc_bin) {
      return WI_ERROR;
    }
    // assert rpc_len < MAX_RPC_LEN?

    size_t p_length = my->partial->tail - my->partial->head;
    if (*to_is_partial || p_length) {
      if (cb_ensure_capacity(my->partial, rpc_len)) {
        return self->on_error(self, "Out of memory");
      }
      memcpy(my->partial->tail, rpc_bin, rpc_len);
      my->partial->tail += rpc_len;
      p_length += rpc_len;
      free(rpc_bin);
      if (*to_is_partial) {
        return WI_SUCCESS;
      }
    }

    if (p_length) {
      plist_from_bin(my->partial->head, (uint32_t)p_length, to_rpc_dict);
      cb_clear(my->partial);
    } else {
      plist_from_bin(rpc_bin, (uint32_t)rpc_len, to_rpc_dict);
      free(rpc_bin);
    }
  }

  return (*to_rpc_dict ? WI_SUCCESS : WI_ERROR);
}

wi_status wi_recv_packet(wi_t self, const char *packet, ssize_t length) {
  wi_on_debug(self, "wi.recv_packet", packet, length);

  size_t body_length = 0;
  plist_t rpc_dict = NULL;
  bool is_partial = false;
  if (!packet || length < 4 || wi_parse_length(self, packet, &body_length) ||
      //TODO (body_length != length - 4) ||
      wi_parse_plist(self, packet + 4, body_length, &rpc_dict, &is_partial)) {
    // invalid packet
    char *text = NULL;
    if (body_length != length - 4) {
      if (asprintf(&text, "size %zd != %zd - 4", body_length, length) < 0) {
        return self->on_error(self, "asprintf failed");
      }
    } else {
      cb_asprint(&text, packet, length, 80, 50);
    }
    wi_status ret = self->on_error(self, "Invalid packet %s\n", text);
    free(text);
    return ret;
  }

  if (is_partial) {
    return WI_SUCCESS;
  }
  wi_status ret = self->recv_plist(self, rpc_dict);
  plist_free(rpc_dict);
  return ret;
}

wi_status wi_recv_loop(wi_t self) {
  wi_private_t my = self->private_state;
  wi_status ret;
  const char *in_head = my->in->in_head;
  const char *in_tail = my->in->in_tail;
  while (1) {
    size_t in_length = in_tail - in_head;
    if (!my->has_length && in_length >= 4) {
      // can read body_length now
      size_t len;
      ret = wi_parse_length(self, in_head, &len);
      if (ret) {
        in_head += 4;
        break;
      }
      my->body_length = len;
      my->has_length = true;
      // don't advance in_head yet
    } else if (my->has_length && in_length >= my->body_length + 4) {
      // can read body now
      ret = self->recv_packet(self, in_head, my->body_length + 4);
      in_head += my->body_length + 4;
      my->has_length = false;
      my->body_length = 0;
      if (ret) {
        break;
      }
    } else {
      // need more input
      ret = WI_SUCCESS;
      break;
    }
  }
  my->in->in_head = in_head;
  return ret;
}

wi_status wi_on_recv(wi_t self, const char *buf, ssize_t length) {
  wi_private_t my = self->private_state;
  if (length < 0) {
    return WI_ERROR;
  } else if (length == 0) {
    return WI_SUCCESS;
  }
  wi_on_debug(self, "wi.recv", buf, length);
  if (cb_begin_input(my->in, buf, length)) {
    return self->on_error(self, "begin_input buffer error");
  }
  wi_status ret = wi_recv_loop(self);
  if (cb_end_input(my->in)) {
    return self->on_error(self, "end_input buffer error");
  }
  return ret;
}

//
// STRUCTS
//

void wi_private_free(wi_private_t my) {
  if (my) {
    cb_free(my->in);
    cb_free(my->partial);
    memset(my, 0, sizeof(struct wi_private));
    free(my);
  }
}
wi_private_t wi_private_new() {
  wi_private_t my = (wi_private_t)malloc(sizeof(
        struct wi_private));
  if (my) {
    memset(my, 0, sizeof(struct wi_private));
    my->in = cb_new();
    my->partial = cb_new();
    if (!my->in || !my->partial) {
      wi_private_free(my);
      return NULL;
    }
  }
  return my;
}


void wi_free(wi_t self) {
  if (self) {
    wi_private_free(self->private_state);
    memset(self, 0, sizeof(struct wi_struct));
    free(self);
  }
}
wi_t wi_new(bool is_sim) {
  wi_t self = (wi_t)malloc(sizeof(struct wi_struct));
  if (!self) {
    return NULL;
  }
  memset(self, 0, sizeof(struct wi_struct));
  self->on_recv = wi_on_recv;
  self->send_plist = wi_send_plist;
  self->recv_packet = wi_recv_packet;
  self->on_error = wi_on_error;
  self->private_state = wi_private_new();
  if (!self->private_state) {
    wi_free(self);
    return NULL;
  }
  self->private_state->is_sim = is_sim;
  return self;
}

// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <resolv.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <plist/plist.h>

#include "char_buffer.h"
#include "hash_table.h"
#include "device_listener.h"

//
// We can't use libusbmuxd's
//     int usbmuxd_subscribe(usbmuxd_event_cb_t callback, void *user_data)
// because it's threaded and does blocking reads, but we want a
// select-friendly fd that we can loop-unroll.  Fortunately this is relatively
// straight-forward.
//

#define USBMUXD_FILE_PATH "/var/run/usbmuxd"
#define TYPE_PLIST 8

struct dl_private {
  cb_t in;
  ht_t device_num_to_device_id;
  bool has_length;
  size_t body_length;
};

int dl_connect(int recv_timeout) {
  const char *filename = USBMUXD_FILE_PATH;
  int fd = -1;
  struct stat fst;
  if (stat(filename, &fst) ||
      !S_ISSOCK(fst.st_mode) ||
      (fd = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
    return -1;
  }

  struct sockaddr_un name;
  name.sun_family = AF_LOCAL;
  strncpy(name.sun_path, filename, sizeof(name.sun_path));
  name.sun_path[sizeof(name.sun_path) - 1] = 0;
  size_t size = SUN_LEN(&name);
  if (connect(fd, (struct sockaddr *)&name, size) < 0) {
    close(fd);
    return -1;
  }

  if (recv_timeout < 0) {
    int opts = fcntl(fd, F_GETFL);
    if (!opts || fcntl(fd, F_SETFL, (opts | O_NONBLOCK)) < 0) {
      perror("Could not set socket to non-blocking");
    }
  } else {
    long millis = (recv_timeout > 0 ? recv_timeout : 5000);
    struct timeval tv;
    tv.tv_sec = (time_t) (millis / 1000);
    tv.tv_usec = (time_t) ((millis - (tv.tv_sec * 1000)) * 1000);
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,
          sizeof(tv))) {
      perror("Could not set socket receive timeout");
    }
  }
  return fd;
}

char *dl_sprintf_uint32(char *buf, uint32_t value) {
  char *tail = buf;
  int8_t i;
  for (i = 0; i < 4; i++) {
    *tail++ = (unsigned char)((value >> (i<<3)) & 0xFF);
  }
  return tail;
}

dl_status dl_start(dl_t self) {
  // Assume usbmuxd supports proto_version 1.  If not then we'd need to
  // send a binary listen request, check for failure, then retry this:
  plist_t dict = plist_new_dict();
  plist_dict_set_item(dict, "ClientVersionString", plist_new_string(
        "device_listener"));
  if (plist_dict_get_size(dict) != 1) {
    perror("Detected an old copy of libplist?!  For a fix, see:\n"
        "https://github.com/libimobiledevice/libimobiledevice/issues/"
        "68#issuecomment-38994545");
    return DL_ERROR;
  }
  plist_dict_set_item(dict, "MessageType", plist_new_string("Listen"));
  plist_dict_set_item(dict, "ProgName", plist_new_string("libusbmuxd"));
  char *xml = NULL;
  uint32_t xml_length = 0;
  plist_to_xml(dict, &xml, &xml_length);
  plist_free(dict);

  size_t length = 16 + xml_length;
  char *packet = (char *)calloc(length, sizeof(char));
  if (!packet) {
    return DL_ERROR;
  }
  char *tail = packet;
  tail = dl_sprintf_uint32(tail, length);
  tail = dl_sprintf_uint32(tail, 1); // version: 1
  tail = dl_sprintf_uint32(tail, TYPE_PLIST); // type: plist
  tail = dl_sprintf_uint32(tail, 1); // tag: 1
  strncpy(tail, xml, xml_length);
  free(xml);

  dl_status ret = self->send_packet(self, packet, length);
  free(packet);
  return ret;
}

uint32_t dl_sscanf_uint32(const char *buf) {
  uint32_t ret = 0;
  const char *tail = buf;
  int8_t i;
  for (i = 0; i < 4; i++) {
    ret |= ((((unsigned char) *tail++) & 0xFF) << (i<<3));
  }
  return ret;
}

dl_status dl_recv_packet(dl_t self, const char *packet, size_t length) {
  dl_private_t my = self->private_state;

  const char *tail = packet;
  uint32_t len = dl_sscanf_uint32(tail);
  tail += 4;
  if (len != length || len < 16) {
    return DL_ERROR;
  }
  uint32_t version = dl_sscanf_uint32(tail);
  tail += 4;
  uint32_t type = dl_sscanf_uint32(tail);
  tail += 4;
  (void)dl_sscanf_uint32(tail);
  tail += 4;
  const char *xml = tail;
  size_t xml_length = length - 16;

  if (version != 1 || type != TYPE_PLIST) {
    return DL_SUCCESS; // ignore?
  }

  plist_t dict = NULL;
  plist_from_xml(xml, xml_length, &dict);
  char *message = NULL;
  if (dict) {
    plist_t node = plist_dict_get_item(dict, "MessageType");
    if (plist_get_node_type(node) == PLIST_STRING) {
      plist_get_string_val(node, &message);
    }
  }

  dl_status ret = DL_ERROR;
  if (!message) {
    ret = DL_ERROR;
  } else if (!strcmp(message, "Result")) {
    plist_t node = plist_dict_get_item(dict, "Number");
    if (node) {
      uint64_t value = 0;
      plist_get_uint_val(node, &value);
      // just an ack of our Listen?
      ret = (value ? DL_ERROR : DL_SUCCESS);
    }
  } else if (!strcmp(message, "Attached")) {
    plist_t props = plist_dict_get_item(dict, "Properties");
    if (props) {
      uint64_t device_num = 0;
      plist_t node = plist_dict_get_item(props, "DeviceID");
      plist_get_uint_val(node, &device_num);

      uint64_t product_id = 0;
      node = plist_dict_get_item(props, "ProductID");
      plist_get_uint_val(node, &product_id);

      char *device_id = NULL;
      node = plist_dict_get_item(props, "SerialNumber");
      if (node) {
        plist_get_string_val(node, &device_id);
      }

      uint64_t location = 0;
      node = plist_dict_get_item(props, "LocationID");
      plist_get_uint_val(node, &location);

      ht_t d_ht = my->device_num_to_device_id;
      ht_put(d_ht, HT_KEY(device_num), device_id);
      ret = self->on_attach(self, device_id, (int)device_num);
    }
  } else if (strcmp(message, "Detached") == 0) {
    plist_t node = plist_dict_get_item(dict, "DeviceID");
    if (node) {
      uint64_t device_num = 0;
      plist_get_uint_val(node, &device_num);

      ht_t d_ht = my->device_num_to_device_id;
      char *device_id = (char *)ht_remove(d_ht, HT_KEY(device_num));
      if (device_id) {
        ret = self->on_detach(self, device_id, (int)device_num);
        free(device_id);
      }
    }
  }
  free(message);
  plist_free(dict);
  return ret;
}

dl_status dl_recv_loop(dl_t self) {
  dl_private_t my = self->private_state;
  dl_status ret;
  const char *in_head = my->in->in_head;
  const char *in_tail = my->in->in_tail;
  while (1) {
    size_t in_length = in_tail - in_head;
    if (!my->has_length && in_length >= 4) {
      // can read body_length now
      size_t len = dl_sscanf_uint32(in_head);
      my->body_length = len;
      my->has_length = true;
      // don't advance in_head yet
    } else if (my->has_length && in_length >= my->body_length) {
      // can read body now
      ret = dl_recv_packet(self, in_head, my->body_length);
      in_head += my->body_length;
      my->has_length = false;
      my->body_length = 0;
      if (ret) {
        break;
      }
    } else {
      // need more input
      ret = DL_SUCCESS;
      break;
    }
  }
  my->in->in_head = in_head;
  return ret;
}

dl_status dl_on_recv(dl_t self, const char *buf, ssize_t length) {
  dl_private_t my = self->private_state;
  if (length < 0) {
    return DL_ERROR;
  } else if (length == 0) {
    return DL_SUCCESS;
  }
  if (cb_begin_input(my->in, buf, length)) {
    return DL_ERROR;
  }
  dl_status ret = dl_recv_loop(self);
  if (cb_end_input(my->in)) {
    return DL_ERROR;
  }
  return ret;
}


dl_t dl_new() {
  dl_t self = (dl_t)malloc(sizeof(struct dl_struct));
  dl_private_t my = (dl_private_t)malloc(sizeof(struct dl_private));
  cb_t in = cb_new();
  ht_t d_ht = ht_new(HT_INT_KEYS);
  if (!self || !my || !in || !d_ht) {
    free(self);
    free(my);
    free(in);
    return NULL;
  }
  memset(self, 0, sizeof(struct dl_struct));
  memset(my, 0, sizeof(struct dl_private));
  self->start = dl_start;
  self->on_recv = dl_on_recv;
  self->private_state = my;
  my->in = in;
  my->device_num_to_device_id = d_ht;
  return self;
}

void dl_free(dl_t self) {
  if (self) {
    dl_private_t my = self->private_state;
    if (my) {
      cb_free(my->in);
      ht_free(my->device_num_to_device_id);
      memset(my, 0, sizeof(struct dl_private));
      free(my);
    }
    memset(self, 0, sizeof(struct dl_struct));
    free(self);
  }
}

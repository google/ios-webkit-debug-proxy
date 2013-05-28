// Google BSD license http://code.google.com/google_bsd_license.html
// Copyright 2012 Google Inc. wrightt@google.com

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

#ifdef __MACH__
#include <uuid/uuid.h>
#endif

#include <libimobiledevice/installation_proxy.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>

#include "char_buffer.h"
#include "webinspector.h"


#define WI_DEBUG 1

// TODO figure out exact value
#define MAX_RPC_LEN 8096 - 500

// some arbitrarly limit, to catch bad packets
#define MAX_BODY_LENGTH 1<<20

struct wi_private {
  bool is_sim;
  cb_t in;
  cb_t partial;
  bool has_length;
  size_t body_length;
};


wi_status wi_parse_app(const plist_t node, wi_app_t *app);
void wi_free_app(wi_app_t app);

wi_status wi_parse_apps(const plist_t node, wi_app_t **to_apps);
void wi_free_apps(wi_app_t *apps);

wi_status wi_parse_pages(const plist_t node, wi_page_t **to_pages);
void wi_free_pages(wi_page_t *pages);

wi_status wi_args_to_xml(wi_t self,
    const void *args_obj, char **to_xml, bool should_trim);

wi_status wi_dict_get_required_string(const plist_t node, const char *key,
    char **to_value);
wi_status wi_dict_get_optional_string(const plist_t node, const char *key,
    char **to_value);
wi_status wi_dict_get_required_bool(const plist_t node, const char *key,
    bool *to_value);
wi_status wi_dict_get_optional_bool(const plist_t node, const char *key,
    bool *to_value);
wi_status wi_dict_get_required_uint(const plist_t node, const char *key,
    uint32_t *to_value);
wi_status wi_dict_get_required_data(const plist_t node, const char *key,
    char **to_value, size_t *to_length);


//
// CONNECT
//

// based on libimobiledevice/src/idevice.h
enum connection_type {
  CONNECTION_USBMUXD = 1
};
struct idevice_connection_private {
  enum connection_type type;
  void *data;
  void *ssl_data;
};

wi_status idevice_connection_get_fd(idevice_connection_t connection,
    int *to_fd) {
  if (!connection || !to_fd) {
    return WI_ERROR;
  }
  // extract the connection fd
  idevice_connection_private *connection_private = 
    (idevice_connection_private *) connection;
  if (!connection ||
      sizeof(*connection) != sizeof(idevice_connection_private) ||
      connection_private->type != CONNECTION_USBMUXD ||
      connection_private->data <= 0 ||
      connection_private->ssl_data) {
    perror("Invalid idevice_connection struct?");
    return WI_ERROR;
  }
  int fd = (int)(long)connection_private->data;
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
  if (idevice_connection_get_fd(connection, &fd)) {
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
// UUID
//

wi_status wi_new_uuid(char **to_uuid) {
  if (!to_uuid) {
    return WI_ERROR;
  }
#ifdef __MACH__
  *to_uuid = (char *)malloc(37);
  uuid_t uuid;
  uuid_generate(uuid);
  uuid_unparse_upper(uuid, *to_uuid);
#else
  // see stackoverflow.com/questions/2174768/clinuxgenerating-uuids-in-linux
  static bool seeded = false;
  if (!seeded) {
    seeded = true;
    srand(time(NULL));
  }
  asprintf(to_uuid, "%x%x-%x-%x-%x-%x%x%x", 
      rand(), rand(), rand(),
      ((rand() & 0x0fff) | 0x4000),
      rand() % 0x3fff + 0x8000,
      rand(), rand(), rand());
#endif
  return WI_SUCCESS;
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


plist_t wi_new_args(const char *connection_id) {
  plist_t ret = plist_new_dict();
  if (connection_id) {
    plist_dict_insert_item(ret, "WIRConnectionIdentifierKey",
        plist_new_string(connection_id));
  }
  return ret;
}

/*
   WIRFinalMessageKey
   __selector
   __argument
 */
wi_status wi_send_msg(wi_t self, const char *selector, plist_t args) {
  wi_private_t my = self->private_state;
  if (!selector || !args) {
    return WI_ERROR;
  }
  plist_t rpc_dict = plist_new_dict();
  plist_dict_insert_item(rpc_dict, "__selector", 
      plist_new_string(selector));
  plist_dict_insert_item(rpc_dict, "__argument", plist_copy(args));
  char *rpc_bin = NULL;
  uint32_t rpc_len = 0;
  plist_to_bin(rpc_dict, &rpc_bin, &rpc_len);
  plist_free(rpc_dict);
  rpc_dict = NULL;
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
      plist_dict_insert_item(wi_dict,
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

/*
_rpc_reportIdentifier:
<key>WIRConnectionIdentifierKey</key>
<string>4B2550E4-13D6-4902-A48E-B45D5B23215B</string>
 */
wi_status wi_send_reportIdentifier(wi_t self, const char *connection_id) {
  if (!connection_id) {
    return WI_ERROR;
  }
  char *selector = "_rpc_reportIdentifier:";
  plist_t args = wi_new_args(connection_id);
  wi_status ret = wi_send_msg(self, selector, args);
  plist_free(args);
  return ret;
}

/*
_rpc_getConnectedApplications:
<key>WIRConnectionIdentifierKey</key>
<string>4B2550E4-13D6-4902-A48E-B45D5B23215B</string>
 */
wi_status wi_send_getConnectedApplications(wi_t self,
    const char *connection_id) {
  if (!connection_id) {
    return WI_ERROR;
  }
  char *selector = "_rpc_getConnectedApplications:";
  plist_t args = wi_new_args(connection_id);
  wi_status ret = wi_send_msg(self, selector, args);
  plist_free(args);
  return ret;
}

/*
_rpc_forwardGetListing:
<key>WIRApplicationIdentifierKey</key>
<string>com.apple.mobilesafari</string>
<key>WIRConnectionIdentifierKey</key>
<string>4B2550E4-13D6-4902-A48E-B45D5B23215B</string>
 */
wi_status wi_send_forwardGetListing(wi_t self, const char *connection_id,
    const char *app_id) {
  if (!connection_id || !app_id) {
    return WI_ERROR;
  }
  char *selector = "_rpc_forwardGetListing:";
  plist_t args = wi_new_args(connection_id);
  plist_dict_insert_item(args, "WIRApplicationIdentifierKey",
      plist_new_string(app_id));
  wi_status ret = wi_send_msg(self, selector, args);
  plist_free(args);
  return ret;
}

/*
_rpc_forwardIndicateWebView:
<key>WIRApplicationIdentifierKey</key> <string>com.apple.mobilesafari</string>
<key>WIRConnectionIdentifierKey</key>
<string>4B2550E4-13D6-4902-A48E-B45D5B23215B</string>
<key>WIRIndicateEnabledKey</key>  <true/>
<key>WIRPageIdentifierKey</key>   <integer>1</integer>
 */
wi_status wi_send_forwardIndicateWebView(wi_t self, const char *connection_id,
    const char *app_id, uint32_t page_id, bool is_enabled) {
  if (!connection_id || !app_id) {
    return WI_ERROR;
  }
  char *selector = "_rpc_forwardIndicateWebView:";
  plist_t args = wi_new_args(connection_id);
  plist_dict_insert_item(args, "WIRApplicationIdentifierKey",
      plist_new_string(app_id));
  plist_dict_insert_item(args, "WIRPageIdentifierKey",
      plist_new_uint(page_id));
  plist_dict_insert_item(args, "WIRIndicateEnabledKey",
      plist_new_bool(is_enabled));
  wi_status ret = wi_send_msg(self, selector, args);
  plist_free(args);
  return ret;
}

/*
_rpc_forwardSocketSetup:
<key>WIRApplicationIdentifierKey</key> <string>com.apple.mobilesafari</string>
<key>WIRConnectionIdentifierKey</key>
<string>4B2550E4-13D6-4902-A48E-B45D5B23215B</string>
<key>WIRPageIdentifierKey</key>   <integer>1</integer>
<key>WIRSenderKey</key>
<string>C1EAD225-D6BC-44B9-9089-2D7CC2D2204C</string>
 */
wi_status wi_send_forwardSocketSetup(wi_t self, const char *connection_id,
    const char *app_id, uint32_t page_id, const char *sender_id) {
  if (!connection_id || !app_id || !sender_id) {
    return WI_ERROR;
  }
  char *selector = "_rpc_forwardSocketSetup:";
  plist_t args = wi_new_args(connection_id);
  plist_dict_insert_item(args, "WIRApplicationIdentifierKey",
      plist_new_string(app_id));
  plist_dict_insert_item(args, "WIRPageIdentifierKey",
      plist_new_uint(page_id));
  plist_dict_insert_item(args, "WIRSenderKey",
      plist_new_string(sender_id));
  wi_status ret = wi_send_msg(self, selector, args);
  plist_free(args);
  return ret;
}

/*
_rpc_forwardSocketData:
<key>WIRApplicationIdentifierKey</key>
<string>com.apple.mobilesafari</string>
<key>WIRConnectionIdentifierKey</key>
<string>4B2550E4-13D6-4902-A48E-B45D5B23215B</string>
<key>WIRPageIdentifierKey</key>
<integer>1</integer>
<key>WIRSenderKey</key>
<string>C1EAD225-D6BC-44B9-9089-2D7CC2D2204C</string>
<key>WIRSocketDataKey **data**</key>
<data>
{"method":"Debugger.causesRecompilation","id":1}
</data>
 */
wi_status wi_send_forwardSocketData(wi_t self, const char *connection_id,
    const char *app_id, uint32_t page_id, const char *sender_id,
    const char *data, size_t length) {
  if (!connection_id || !app_id || !sender_id || !data) {
    return WI_ERROR;
  }
  char *selector = "_rpc_forwardSocketData:";
  plist_t args = wi_new_args(connection_id);
  plist_dict_insert_item(args, "WIRApplicationIdentifierKey",
      plist_new_string(app_id));
  plist_dict_insert_item(args, "WIRPageIdentifierKey",
      plist_new_uint(page_id));
  plist_dict_insert_item(args, "WIRSenderKey",
      plist_new_string(sender_id));
  plist_dict_insert_item(args, "WIRSocketDataKey",
      plist_new_data(data, length));
  wi_status ret = wi_send_msg(self, selector, args);
  plist_free(args);
  return ret;
}

/*
_rpc_forwardDidClose:
?
 */
wi_status wi_send_forwardDidClose(wi_t self, const char *connection_id,
    const char *app_id, uint32_t page_id, const char *sender_id) {
  if (!connection_id || !app_id || !sender_id) {
    return WI_ERROR;
  }
  char *selector = "_rpc_forwardDidClose:";
  plist_t args = wi_new_args(connection_id);
  plist_dict_insert_item(args, "WIRApplicationIdentifierKey",
      plist_new_string(app_id));
  plist_dict_insert_item(args, "WIRPageIdentifierKey",
      plist_new_uint(page_id));
  plist_dict_insert_item(args, "WIRSenderKey",
      plist_new_string(sender_id));
  wi_status ret = wi_send_msg(self, selector, args);
  plist_free(args);
  return ret;
}


//
// RECV
//

/*
 */
wi_status wi_recv_reportSetup(wi_t self, const plist_t args) {
  if (plist_get_node_type(args) != PLIST_DICT) {
    return WI_ERROR;
  }
  return self->on_reportSetup(self);
}

/*
   <key>WIRApplicationDictionaryKey</key>
   <dict>
   <key>com.apple.mobilesafari</key>
   <dict>
   <key>WIRApplicationIdentifierKey</key>
   <string>com.apple.mobilesafari</string>
   <key>WIRApplicationNameKey</key>    <string>Safari</string>
   <key>WIRIsApplicationProxyKey</key> <false/>
   </dict>
   </dict>
 */
wi_status wi_recv_reportConnectedApplicationList(wi_t self,
    const plist_t args) {
  plist_t item = plist_dict_get_item(args, "WIRApplicationDictionaryKey");
  wi_app_t *apps = NULL;
  wi_status ret = wi_parse_apps(item, &apps);
  if (!ret) {
    ret = self->on_reportConnectedApplicationList(self, apps);
    wi_free_apps(apps);
  }
  return ret;
}

/*
 */
wi_status wi_recv_applicationConnected(wi_t self, const plist_t args) {
  wi_app_t app = NULL;
  wi_status ret = wi_parse_app(args, &app);
  if (!ret) {
    ret = self->on_applicationConnected(self, app);
    wi_free_app(app);
  }
  return ret;
}

/*
 */
wi_status wi_recv_applicationDisconnected(wi_t self, const plist_t args) {
  wi_app_t app = NULL;
  wi_status ret = wi_parse_app(args, &app);
  if (!ret) {
    ret = self->on_applicationDisconnected(self, app);
    wi_free_app(app);
  }
  return ret;
}

/*
_rpc_applicationSentListing:
string appId = args[“WIRApplicationIdentifierKey”]
for val in args[“WIRListingKey”].values():
string pageId = val[“WIRPageIdentifierKey”]
pageInfo.title = val[“WIRTitleKey”]
pageInfo.url = val[“WIRURLKey”]
pageInfo.connectionId = val[“WIRConnectionIdentifierKey”]
<key>WIRApplicationIdentifierKey</key>  <string>com.apple.mobilesafari</string>
<key>WIRListingKey</key>
<dict>
<key>1</key>
<dict>
<key>WIRConnectionIdentifierKey</key>
<string>4B2550E4-13D6-4902-A48E-B45D5B23215B</string>
<key>WIRPageIdentifierKey</key>   <integer>1</integer>
<key>WIRTitleKey</key>   <string>Cannot Open Page</string>
<key>WIRURLKey</key>
<string>x-appbundle:///StandardError.html</string>
</dict>
</dict>
 */
wi_status wi_recv_applicationSentListing(wi_t self, const plist_t args) {
  char *app_id = NULL;
  wi_page_t *pages = NULL;
  plist_t item = plist_dict_get_item(args, "WIRListingKey");
  wi_status ret;
  if (!wi_dict_get_required_string(args, "WIRApplicationIdentifierKey",
        &app_id) &&
      !wi_parse_pages(item, &pages) &&
      !self->on_applicationSentListing(self, app_id, pages)) {
    ret = WI_SUCCESS;
  } else {
    ret = WI_ERROR;
  }
  free(app_id);
  wi_free_pages(pages);
  return ret;
}

/*
_rpc_applicationSentData:
string appId = args[“WIRApplicationIdentifierKey”]
string socketKey = args[“WIRDestinationKey”]
string jsonData = args[“WIRMessageDataKey”]
<key>WIRApplicationIdentifierKey</key>
<string>com.apple.mobilesafari</string>
<key>WIRDestinationKey</key>
<string>C1EAD225-D6BC-44B9-9089-2D7CC2D2204C</string>
<key>WIRMessageDataKey</key>
<data>
{"result":{"result":true},"id":1}
</data>
 */
wi_status wi_recv_applicationSentData(wi_t self, const plist_t args) {
  char *app_id = NULL;
  char *dest_id = NULL;
  char *data = NULL;
  size_t length = 0;
  wi_status ret;
  if (!wi_dict_get_required_string(args, "WIRApplicationIdentifierKey",
        &app_id) &&
      !wi_dict_get_required_string(args, "WIRDestinationKey",
        &dest_id) &&
      !wi_dict_get_required_data(args, "WIRMessageDataKey",
        &data, &length) &&
      !self->on_applicationSentData(self,
        app_id, dest_id, data, length)) {
    ret = WI_SUCCESS;
  } else {
    ret = WI_ERROR;
  }
  free(app_id);
  free(dest_id);
  free(data);
  return ret;
}

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

wi_status wi_parse_msg(wi_t self, const char *from_buf, size_t length,
    char **to_selector, plist_t *to_args, bool *to_is_partial) {
  wi_private_t my = self->private_state;
  *to_selector = NULL;
  *to_args = NULL;
  *to_is_partial = false;

  plist_t rpc_dict = NULL;
  if (my->is_sim) {
    plist_from_bin(from_buf, length, &rpc_dict);
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
      plist_from_bin(my->partial->head, (uint32_t)p_length, &rpc_dict);
      cb_clear(my->partial);
    } else {
      plist_from_bin(rpc_bin, (uint32_t)rpc_len, &rpc_dict);
      free(rpc_bin);
    }
  }
  if (!rpc_dict) {
    return WI_ERROR;
  }

  plist_t sel_item = plist_dict_get_item(rpc_dict, "__selector");
  if (sel_item) {
    plist_get_string_val(sel_item, to_selector);
  }
  plist_t args = plist_dict_get_item(rpc_dict, "__argument");
  if (args) {
    *to_args = plist_copy(args);
  }
  plist_free(rpc_dict);

  return (*to_selector && *to_args ? WI_SUCCESS : WI_ERROR);
}


wi_status wi_recv_msg(wi_t self, const char *selector, const plist_t args) {
  if (!selector) {
    return WI_ERROR;
  }
  if (!strcmp(selector, "_rpc_reportSetup:")) {
    if (!wi_recv_reportSetup(self, args)) {
      return WI_SUCCESS;
    }
  } else if (!strcmp(selector, "_rpc_reportConnectedApplicationList:")) {
    if (!wi_recv_reportConnectedApplicationList(self, args)) {
      return WI_SUCCESS;
    }
  } else if (!strcmp(selector, "_rpc_applicationConnected:")) {
    if (!wi_recv_applicationConnected(self, args)) {
      return WI_SUCCESS;
    }
  } else if (!strcmp(selector, "_rpc_applicationDisconnected:")) {
    if (!wi_recv_applicationDisconnected(self, args)) {
      return WI_SUCCESS;
    }
  } else if (!strcmp(selector, "_rpc_applicationSentListing:")) {
    if (!wi_recv_applicationSentListing(self, args)) {
      return WI_SUCCESS;
    }
  } else if (!strcmp(selector, "_rpc_applicationSentData:")) {
    if (!wi_recv_applicationSentData(self, args)) {
      return WI_SUCCESS;
    }
  }

  // invalid msg
  char *args_xml = NULL;
  wi_args_to_xml(self, args, &args_xml, true);
  wi_status ret = self->on_error(self, "Invalid message %s %s",
      selector, args_xml);
  free(args_xml);
  return ret;
}

wi_status wi_recv_packet(wi_t self, const char *packet, size_t length) {
  wi_on_debug(self, "wi.recv_packet", packet, length);

  size_t body_length = 0;
  char *selector = NULL;
  plist_t args = NULL;
  bool is_partial = false;
  if (packet && length >= 4 &&
      !wi_parse_length(self, packet, &body_length) &&
      //TODO (body_length == length - 4) &&
      !wi_parse_msg(self, packet + 4, body_length,
        &selector, &args, &is_partial)) {
    if (is_partial) {
      return WI_SUCCESS;
    }
    wi_status ret = wi_recv_msg(self, selector, args);
    free(selector);
    plist_free(args);
    return ret;
  }

  // invalid packet
  char *text = NULL;
  if (body_length != length - 4) {
    asprintf(&text, "size %zd != %zd - 4", body_length, length);
  } else {
    cb_asprint(&text, packet, length, 80, 50);
  }
  wi_status ret = self->on_error(self, "Invalid packet %s\n", text);
  free(text);
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
      ret = wi_recv_packet(self, in_head, my->body_length + 4);
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
  self->send_reportIdentifier = wi_send_reportIdentifier;
  self->send_getConnectedApplications = wi_send_getConnectedApplications;
  self->send_forwardGetListing = wi_send_forwardGetListing;
  self->send_forwardIndicateWebView = wi_send_forwardIndicateWebView;
  self->send_forwardSocketSetup = wi_send_forwardSocketSetup;
  self->send_forwardSocketData = wi_send_forwardSocketData;
  self->send_forwardDidClose = wi_send_forwardDidClose;
  self->on_recv = wi_on_recv;
  self->on_error = wi_on_error;
  self->private_state = wi_private_new();
  if (!self->private_state) {
    wi_free(self);
    return NULL;
  }
  self->private_state->is_sim = is_sim;
  return self;
}

wi_app_t wi_new_app() {
  wi_app_t app = (wi_app_t)malloc(sizeof(struct wi_app_struct));
  if (app) {
    memset(app, 0, sizeof(struct wi_app_struct));
  }
  return app;
}
void wi_free_app(wi_app_t app) {
  if (app) {
    free(app->app_id);
    free(app->app_name);
    memset(app, 0, sizeof(struct wi_app_struct));
    free(app);
  }
}
wi_status wi_parse_app(const plist_t node, wi_app_t *to_app) {
  wi_app_t app = (to_app ? wi_new_app() : NULL);
  if (!app ||
      wi_dict_get_required_string(node, "WIRApplicationIdentifierKey",
        &app->app_id) ||
      wi_dict_get_optional_string(node, "WIRApplicationNameKey",
        &app->app_name) ||
      wi_dict_get_optional_bool(node, "WIRIsApplicationProxyKey",
        &app->is_proxy)) {
    wi_free_app(app);
    if (to_app) {
      *to_app = NULL;
    }
    return WI_ERROR;
  }
  *to_app = app;
  return WI_SUCCESS;
}


void wi_free_apps(wi_app_t *apps) {
  if (apps) {
    wi_app_t *a = apps;
    while (*a) {
      wi_free_app(*a++);
    }
    free(apps);
  }
}
wi_status wi_parse_apps(const plist_t node, wi_app_t **to_apps) {
  if (!to_apps) {
    return WI_ERROR;
  }
  *to_apps = NULL;
  if (plist_get_node_type(node) != PLIST_DICT) {
    return WI_ERROR;
  }
  size_t length = plist_dict_get_size(node);
  wi_app_t *apps = (wi_app_t *)calloc(length + 1, sizeof(wi_app_t));
  if (!apps) {
    return WI_ERROR;
  }
  plist_dict_iter iter = NULL;
  plist_dict_new_iter(node, &iter);
  int8_t is_ok = (iter != NULL);
  size_t i;
  for (i = 0; i < length && is_ok; i++) {
    char *key = NULL;
    plist_t value = NULL;
    plist_dict_next_item(node, iter, &key, &value);
    wi_app_t app = NULL;
    is_ok = (key && !wi_parse_app(value, &app) &&
        !strcmp(key, app->app_id));
    apps[i] = app;
    free(key);
  }
  free(iter);
  if (!is_ok) {
    wi_free_apps(apps);
    return WI_ERROR;
  }
  *to_apps = apps;
  return WI_SUCCESS;
}

wi_page_t wi_new_page() {
  wi_page_t page = (wi_page_t)malloc(sizeof(struct wi_page_struct));
  if (page) {
    memset(page, 0, sizeof(struct wi_page_struct));
  }
  return page;
}
void wi_free_page(wi_page_t page) {
  if (page) {
    page->page_id = 0;
    free(page->connection_id);
    free(page->title);
    free(page->url);
    memset(page, 0, sizeof(struct wi_page_struct));
    free(page);
  }
}
wi_status wi_parse_page(const plist_t node, wi_page_t *to_page) {
  wi_page_t page = (to_page ? wi_new_page() : NULL);
  if (!page ||
      wi_dict_get_required_uint(node, "WIRPageIdentifierKey",
        &page->page_id) ||
      wi_dict_get_optional_string(node, "WIRConnectionIdentifierKey",
        &page->connection_id) ||
      wi_dict_get_optional_string(node, "WIRTitleKey",
        &page->title) ||
      wi_dict_get_optional_string(node, "WIRURLKey",
        &page->url)) {
    wi_free_page(page);
    if (to_page) {
      *to_page = NULL;
    }
    return WI_ERROR;
  }
  *to_page = page;
  return WI_SUCCESS;
}

void wi_free_pages(wi_page_t *pages) {
  if (pages) {
    wi_page_t *p = pages;
    while (*p) {
      wi_free_page(*p++);
    }
    free(pages);
  }
}
wi_status wi_parse_pages(const plist_t node, wi_page_t **to_pages) {
  if (!node || !to_pages ||
      plist_get_node_type(node) != PLIST_DICT) {
    return WI_ERROR;
  }
  *to_pages = NULL;
  size_t length = plist_dict_get_size(node);
  wi_page_t *pages = (wi_page_t *)calloc(length + 1, sizeof(wi_page_t));
  if (!pages) {
    return WI_ERROR;
  }
  plist_dict_iter iter = NULL;
  plist_dict_new_iter(node, &iter);
  int is_ok = (iter != NULL);
  size_t i;
  for (i = 0; i < length && is_ok; i++) {
    char *key = NULL;
    plist_t value = NULL;
    plist_dict_next_item(node, iter, &key, &value);
    wi_page_t page = NULL;
    is_ok = (key && !wi_parse_page(value, &page) &&
        page->page_id == strtol(key, NULL, 0));
    pages[i] = page;
    free(key);
  }
  free(iter);
  if (!is_ok) {
    wi_free_pages(pages);
    return WI_ERROR;
  }
  *to_pages = pages;
  return WI_SUCCESS;
}


/*
 */
wi_status wi_args_to_xml(wi_t self,
    const void *args_obj, char **to_xml, bool should_trim) {
  if (!args_obj || !to_xml) {
    return WI_ERROR;
  }
  *to_xml = NULL;
  uint32_t length = 0;
  plist_to_xml((plist_t)args_obj, to_xml, &length);
  if (!*to_xml || !length) {
    return self->on_error(self, "plist_to_xml failed");
  }
  if (should_trim) {
    char *head = strstr(*to_xml, "<plist");
    head = (head ? strchr(head, '>') : NULL);
    if (head) {
      while (*++head == '\n') {
      }
      char *tail = *to_xml + length;
      while (tail > head && (!*tail || *tail == '\n')) {
        tail--;
      }
      if (tail-head >= 8 && !strncmp(tail-7, "</plist>", 8)) {
        tail -= 8;
        char *new_xml = (char *)malloc((tail - head + 1) * sizeof(char));
        strncpy(new_xml, head, tail - head);
        new_xml[tail - head] = '\0';
        free(*to_xml);
        *to_xml = new_xml;
      }
    }
  }
  return WI_SUCCESS;
}


wi_status wi_dict_get_required_string(const plist_t node, const char *key,
    char **to_value) {
  if (!node || !key || !to_value) {
    return WI_ERROR;
  }
  plist_t item = plist_dict_get_item(node, key);
  if (plist_get_node_type(item) != PLIST_STRING) {
    return WI_ERROR;
  }
  plist_get_string_val(item, to_value);
  return WI_SUCCESS;
}

wi_status wi_dict_get_optional_string(const plist_t node, const char *key,
    char **to_value) {
  if (!node || !key || !to_value) {
    return WI_ERROR;
  }
  return (plist_dict_get_item(node, key) ?
      wi_dict_get_required_string(node, key, to_value) : WI_SUCCESS);
}

wi_status wi_dict_get_required_bool(const plist_t node, const char *key,
    bool *to_value) {
  if (!node || !key || !to_value) {
    return WI_ERROR;
  }
  plist_t item = plist_dict_get_item(node, key);
  if (plist_get_node_type(item) != PLIST_BOOLEAN) {
    return WI_ERROR;
  }
  uint8_t value = 0;
  plist_get_bool_val(item, &value);
  *to_value = (value ? true : false);
  return WI_SUCCESS;
}

wi_status wi_dict_get_optional_bool(const plist_t node, const char *key,
    bool *to_value) {
  if (!node || !key || !to_value) {
    return WI_ERROR;
  }
  return (plist_dict_get_item(node, key) ?
      wi_dict_get_required_bool(node, key, to_value) : WI_SUCCESS);
}

wi_status wi_dict_get_required_uint(const plist_t node, const char *key,
    uint32_t *to_value) {
  if (!node || !key || !to_value) {
    return WI_ERROR;
  }
  plist_t item = plist_dict_get_item(node, key);
  if (plist_get_node_type(item) != PLIST_UINT) {
    return WI_ERROR;
  }
  uint64_t value;
  plist_get_uint_val(item, &value);
  if (value > UINT32_MAX) {
    return WI_ERROR;
  }
  *to_value = (uint32_t)value;
  return WI_SUCCESS;
}

wi_status wi_dict_get_required_data(const plist_t node, const char *key,
    char **to_value, size_t *to_length) {
  if (!node || !key || !to_value || !to_length) {
    return WI_ERROR;
  }
  *to_value = NULL;
  *to_length = 0;
  plist_t item = plist_dict_get_item(node, key);
  if (plist_get_node_type(item) != PLIST_DATA) {
    return WI_ERROR;
  }
  char *data = NULL;
  uint64_t length = 0;
  plist_get_data_val(item, &data, &length);
  if (length > UINT32_MAX) {
    free(data);
    return WI_ERROR;
  }
  *to_value = data;
  *to_length = (size_t)length;
  return WI_SUCCESS;
}


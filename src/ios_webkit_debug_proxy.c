// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "char_buffer.h"
#include "device_listener.h"
#include "hash_table.h"
#include "ios_webkit_debug_proxy.h"
#include "rpc.h"
#include "webinspector.h"
#include "websocket.h"
#include "strndup.h"


struct iwdp_idl_struct;
typedef struct iwdp_idl_struct *iwdp_idl_t;

struct iwdp_private {
  // our device listener
  iwdp_idl_t idl;

  // our null-id registry (:9221) plus per-device ports (:9222-...)
  ht_t device_id_to_iport;

  // frontend url, e.g. "http://bar.com/devtools.html" or "/foo/inspector.html"
  char *frontend;
  char *sim_wi_socket_addr;
};


#define TYPE_IDL   1
#define TYPE_IPORT 2
#define TYPE_IWI   3
#define TYPE_IWS   4
#define TYPE_IFS   5

/*!
 * Struct type id, for iwdp_on_recv/etc "switch" use.
 *
 * Each sub-struct has a "*_fd".
 */
typedef struct {
  int type;
} iwdp_type_struct;
typedef iwdp_type_struct *iwdp_type_t;

/*!
 * Device add/remove listener.
 */
struct iwdp_idl_struct {
  iwdp_type_struct type;
  iwdp_t self;

  dl_t dl;
  int dl_fd;
};
iwdp_idl_t iwdp_idl_new();
void iwdp_idl_free(iwdp_idl_t idl);

struct iwdp_iwi_struct;
typedef struct iwdp_iwi_struct *iwdp_iwi_t;

/*!
 * browser listener.
 */
struct iwdp_iport_struct {
  iwdp_type_struct type;
  iwdp_t self;

  // browser port, e.g. 9222
  int port;
  int s_fd;

  // true if iwdp_on_attach has succeeded and we should restore this port
  // if the device is reattach
  bool is_sticky;

  // all websocket clients on this port
  // key owned by iws->ws_id
  ht_t ws_id_to_iws;

  // iOS device_id, e.g. ddc86a518cd948e13bbdeadbeef00788ea35fcf9
  char *device_id;
  char *device_name;
  int device_os_version;

  // null if the device is detached
  iwdp_iwi_t iwi;
};

typedef struct iwdp_iport_struct *iwdp_iport_t;
iwdp_iport_t iwdp_iport_new();
void iwdp_iport_free(iwdp_iport_t iport);
char *iwdp_iports_to_text(iwdp_iport_t *iports, bool want_json,
    const char *host);

/*!
 * WebInpsector.
 */
struct iwdp_iwi_struct {
  iwdp_type_struct type;
  iwdp_iport_t iport; // owner

  // webinspector
  wi_t wi;
  int wi_fd;
  char *connection_id;

  rpc_t rpc;  // plist parser
  rpc_app_t app;

  bool connected;
  uint32_t max_page_num; // > 0
  ht_t app_id_to_true;   // set of app_ids
  ht_t page_num_to_ipage;
};

iwdp_iwi_t iwdp_iwi_new(bool is_sim, bool partials_supported, bool *is_debug);
void iwdp_iwi_free(iwdp_iwi_t iwi);

struct iwdp_ifs_struct;
typedef struct iwdp_ifs_struct *iwdp_ifs_t;

struct iwdp_ipage_struct;
typedef struct iwdp_ipage_struct *iwdp_ipage_t;

/*!
 * WebSocket connection.
 */
struct iwdp_iws_struct {
  iwdp_type_struct type;
  iwdp_iport_t iport; // owner

  // browser client
  int ws_fd;
  ws_t ws;
  char *ws_id; // devtools sender_id

  // set if the resource is /devtools/page/<page_num>
  uint32_t page_num;

  // assert (!page_num ||
  //     (ipage && ipage->page_num == page_num && ipage->iws == this))
  //
  // shortcut pointer to the page with our page_num, but only if
  // we own that page (i.e. ipage->iws == this).  Another iws can "steal" our
  // page away and set ipage == NULL, but we keep our page_num so we can report
  // a useful error.
  iwdp_ipage_t ipage; // owner is iwi->page_num_to_ipage

  // set if the resource is /devtools/<non-page>
  iwdp_ifs_t ifs;
};
typedef struct iwdp_iws_struct *iwdp_iws_t;
iwdp_iws_t iwdp_iws_new(bool *is_debug);
void iwdp_iws_free(iwdp_iws_t iws);

/*!
 * Static file-system page request.
 */
struct iwdp_ifs_struct {
  iwdp_type_struct type;
  iwdp_iws_t iws; // owner

  // static server
  int fs_fd;
};

iwdp_ifs_t iwdp_ifs_new();
void iwdp_ifs_free(iwdp_ifs_t ifs);


// page info
struct iwdp_ipage_struct {
  // browser
  uint32_t page_num;

  // webinspector, which can lag re: on_applicationSentListing
  char *app_id;
  uint32_t page_id;
  char *connection_id;
  char *title;
  char *url;
  char *sender_id;

  // set if being inspected, limit one client per page
  // owner is iport->ws_id_to_iws
  iwdp_iws_t iws;
};

iwdp_ipage_t iwdp_ipage_new();
void iwdp_ipage_free(iwdp_ipage_t ipage);
int iwdp_ipage_cmp(const void *a, const void *b);
char *iwdp_ipages_to_text(iwdp_ipage_t *ipages, bool want_json,
    const char *device_id, const char *device_name,
    const char *frontend_url, const char *host, int port);

// file extension to Content-Type
const char *EXT_TO_MIME[][2] = {
  {"css", "text/css"},
  {"gif", "image/gif; charset=binary"},
  {"html", "text/html; charset=UTF-8"},
  {"ico", "image/x-icon"},
  {"js", "application/javascript"},
  {"json", "application/json; charset=UTF-8"},
  {"png", "image/png; charset=binary"},
  {"txt", "text/plain"},
};
iwdp_status iwdp_get_content_type(const char *path, bool is_local,
    char **to_mime);

ws_status iwdp_start_devtools(iwdp_ipage_t ipage, iwdp_iws_t iws);
ws_status iwdp_stop_devtools(iwdp_ipage_t ipage);

int iwdp_update_string(char **old_value, const char *new_value);

//
// logging
//

iwdp_status iwdp_on_error(iwdp_t self, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  return IWDP_ERROR;
}

void iwdp_log_connect(iwdp_iport_t iport) {
  if (iport->device_id) {
    printf("Connected :%d to %s (%s)\n", iport->port, iport->device_name,
        iport->device_id);
  } else {
    printf("Listing devices on :%d\n", iport->port);
  }
}

void iwdp_log_disconnect(iwdp_iport_t iport) {
  if (iport->iwi && iport->iwi->connected) {
    printf("Disconnected :%d from %s (%s)\n", iport->port,
        iport->device_name, iport->device_id);
  } else {
    printf("Unable to connect to %s (%s)\n  Please"
        " verify that Settings > Safari > Advanced > Web Inspector = ON\n",
        iport->device_name, iport->device_id);
  }
}


//
// device_listener
//

dl_status iwdp_listen(iwdp_t self, const char *device_id) {
  iwdp_private_t my = self->private_state;

  // see if this device was previously attached
  ht_t iport_ht = my->device_id_to_iport;
  iwdp_iport_t iport = (iwdp_iport_t)ht_get_value(iport_ht, device_id);
  if (iport && iport->s_fd > 0) {
    return self->on_error(self, "%s already on :%d", device_id,
        iport->port);
  }
  int port = (iport ? iport->port : -1);

  // select new port
  int min_port = -1;
  int max_port = -1;
  if (self->select_port && self->select_port(self, device_id,
        &port, &min_port, &max_port)) {
    return (device_id ? DL_ERROR : DL_SUCCESS);
  }
  if (port < 0 && (min_port < 0 || max_port < min_port)) {
    return (device_id ? DL_ERROR : DL_SUCCESS); // ignore this device
  }
  if (!iport) {
    iport = iwdp_iport_new();
    iport->device_id = (device_id ? strdup(device_id) : NULL);
    ht_put(iport_ht, iport->device_id, iport);
  }
  iport->self = self;

  // listen for browser clients
  int s_fd = -1;
  if (port > 0) {
    s_fd = self->listen(self, port);
  }
  if (s_fd < 0 && (min_port > 0 && max_port >= min_port)) {
    iwdp_iport_t *iports = (iwdp_iport_t *)ht_values(iport_ht);
    int p;
    for (p = min_port; p <= max_port; p++) {
      bool is_taken = false;
      iwdp_iport_t *ipp;
      for (ipp = iports; *ipp; ipp++) {
        if ((*ipp)->port == p) {
          is_taken = true;
          break;
        }
      }
      if (!is_taken && p != port) {
        s_fd = self->listen(self, p);
        if (s_fd > 0) {
          port = p;
          break;
        }
      }
    }
    free(iports);
  }
  if (s_fd < 0) {
    return self->on_error(self, "Unable to bind %s on port %d-%d",
        (device_id ? device_id : "\"devices list\""),
        min_port, max_port);
  }
  if (self->add_fd(self, s_fd, NULL, iport, true)) {
    return self->on_error(self, "add_fd s_fd=%d failed", s_fd);
  }
  iport->s_fd = s_fd;
  iport->port = port;
  if (!device_id) {
    iwdp_log_connect(iport);
  }
  return DL_SUCCESS;
}

iwdp_status iwdp_start(iwdp_t self) {
  iwdp_private_t my = self->private_state;
  if (my->idl) {
    return self->on_error(self, "Already started?");
  }

  if (iwdp_listen(self, NULL)) {
    // Okay, keep going
  }

  iwdp_idl_t idl = iwdp_idl_new();
  idl->self = self;

  int dl_fd = self->subscribe(self);
  if (dl_fd < 0) {  // usbmuxd isn't running
    return self->on_error(self, "No device found, is it plugged in?");
  }
  idl->dl_fd = dl_fd;

  if (self->add_fd(self, dl_fd, NULL, idl, false)) {
    return self->on_error(self, "add_fd failed");
  }

  dl_t dl = idl->dl;
  if (dl->start(dl)) {
    return self->on_error(self, "Unable to start device_listener");
  }

  // TODO add iOS simulator listener
  // for now we'll fake a callback
  dl->on_attach(dl, "SIMULATOR", -1);

  return IWDP_SUCCESS;
}

dl_status iwdp_send_to_dl(dl_t dl, const char *buf, size_t length) {
  iwdp_idl_t idl = (iwdp_idl_t)dl->state;
  iwdp_t self = idl->self;
  int dl_fd = idl->dl_fd;
  return self->send(self, dl_fd, buf, length);
}

dl_status iwdp_on_attach(dl_t dl, const char *device_id, int device_num) {
  iwdp_t self = ((iwdp_idl_t)dl->state)->self;
  if (!device_id) {
    return self->on_error(self, "Null device_id");
  }

  if (iwdp_listen(self, device_id)) {
    // Couldn't bind browser port, or we're simply ignoring this device
    return DL_SUCCESS;
  }
  iwdp_private_t my = self->private_state;

  // Return "success" on most errors, otherwise we'll kill our
  // device_listener and, via iwdp_idl_close, all our iports!

  ht_t iport_ht = my->device_id_to_iport;
  iwdp_iport_t iport = (iwdp_iport_t)ht_get_value(iport_ht, device_id);
  if (!iport) {
    return self->on_error(self, "Internal error: !iport %s", device_id);
  }
  if (iport->iwi) {
    self->on_error(self, "%s already on :%d", device_id, iport->port);
    return DL_SUCCESS;
  }
  char *device_name = iport->device_name;
  int device_os_version = 0;

  // connect to inspector
  int wi_fd;
  void *ssl_session = NULL;
  bool is_sim = !strcmp(device_id, "SIMULATOR");
  if (is_sim) {
    // TODO launch webinspectord
    // For now we'll assume Safari starts it for us.
    //
    // `launchctl list` shows:
    //   com.apple.iPhoneSimulator:com.apple.webinspectord
    // so the launch is probably something like:
    //   xpc_connection_create[_mach_service](...webinspectord, ...)?
    wi_fd = self->connect(self, my->sim_wi_socket_addr);
  } else {
    wi_fd = self->attach(self, device_id, NULL,
      (device_name ? NULL : &device_name), &device_os_version, &ssl_session);
  }
  if (wi_fd < 0) {
    self->remove_fd(self, iport->s_fd);
    if (!is_sim) {
      self->on_error(self, "Unable to attach %s inspector", device_id);
    }
    return DL_SUCCESS;
  }
  iport->device_name = (device_name ? device_name : strdup(device_id));
  iport->device_os_version = device_os_version;
  iwdp_iwi_t iwi = iwdp_iwi_new(is_sim, !is_sim && device_os_version < 0xb0000,
      self->is_debug);
  iwi->iport = iport;
  iport->iwi = iwi;
  if (self->add_fd(self, wi_fd, ssl_session, iwi, false)) {
    self->remove_fd(self, iport->s_fd);
    return self->on_error(self, "add_fd wi_fd=%d failed", wi_fd);
  }
  iwi->wi_fd = wi_fd;

  // start inspector
  rpc_new_uuid(&iwi->connection_id);
  rpc_t rpc = iwi->rpc;
  if (rpc->send_reportIdentifier(rpc, iwi->connection_id)) {
    self->remove_fd(self, iport->s_fd);
    self->on_error(self, "Unable to report to inspector %s",
        device_id);
    return DL_SUCCESS;
  }

  iport->is_sticky = true;
  return DL_SUCCESS;
}

dl_status iwdp_on_detach(dl_t dl, const char *device_id, int device_num) {
  iwdp_idl_t idl = (iwdp_idl_t)dl->state;
  iwdp_t self = idl->self;
  iwdp_private_t my = self->private_state;
  iwdp_iport_t iport = (iwdp_iport_t)ht_get_value(my->device_id_to_iport,
      device_id);
  if (iport && iport->s_fd > 0) {
    self->remove_fd(self, iport->s_fd);
  }
  return IWDP_SUCCESS;
}

//
// socket I/O
//

iwdp_status iwdp_iport_accept(iwdp_t self, iwdp_iport_t iport, int ws_fd,
    iwdp_iws_t *to_iws) {
  iwdp_iws_t iws = iwdp_iws_new(self->is_debug);
  iws->iport = iport;
  iws->ws_fd = ws_fd;
  rpc_new_uuid(&iws->ws_id);
  ht_put(iport->ws_id_to_iws, iws->ws_id, iws);
  *to_iws = iws;
  return IWDP_SUCCESS;
}

iwdp_status iwdp_on_accept(iwdp_t self, int s_fd, void *value,
    int fd, void **to_value) {
  int type = ((iwdp_type_t)value)->type;
  if (type == TYPE_IPORT) {
    return iwdp_iport_accept(self, (iwdp_iport_t)value, fd,
        (iwdp_iws_t*)to_value);
  } else {
    return self->on_error(self, "Unexpected accept type %d", type);
  }
}

iwdp_status iwdp_on_recv(iwdp_t self, int fd, void *value,
    const char *buf, ssize_t length) {
  int type = ((iwdp_type_t)value)->type;
  switch (type) {
    case TYPE_IDL:
      {
        dl_t dl = ((iwdp_idl_t)value)->dl;
        return dl->on_recv(dl, buf, length);
      }
    case TYPE_IWI:
      {
        wi_t wi = ((iwdp_iwi_t)value)->wi;
        return wi->on_recv(wi, buf, length);
      }
    case TYPE_IWS:
      {
        ws_t ws = ((iwdp_iws_t)value)->ws;
        return ws->on_recv(ws, buf, length);
      }
    case TYPE_IFS:
      {
        int ws_fd = ((iwdp_ifs_t)value)->iws->ws_fd;
        iwdp_status ret = self->send(self, ws_fd, buf, length);
        if (ret) {
          self->remove_fd(self, ws_fd);
        }
        return ret;
      }
    default:
      return self->on_error(self, "Unexpected recv type %d", type);
  }
}

iwdp_status iwdp_iport_close(iwdp_t self, iwdp_iport_t iport) {
  iwdp_private_t my = self->private_state;
  // check pointer to this iport
  const char *device_id = iport->device_id;
  ht_t iport_ht = my->device_id_to_iport;
  iwdp_iport_t old_iport = (iwdp_iport_t)ht_get_value(iport_ht, device_id);
  if (old_iport != iport) {
    return self->on_error(self, "Internal iport mismatch?");
  }
  // close clients
  iwdp_iws_t *iwss = (iwdp_iws_t *)ht_values(iport->ws_id_to_iws);
  iwdp_iws_t *iws;
  for (iws = iwss; *iws; iws++) {
    if ((*iws)->ws_fd > 0) {
      self->remove_fd(self, (*iws)->ws_fd);
    }
  }
  free(iwss);
  ht_clear(iport->ws_id_to_iws);
  // close iwi
  iwdp_iwi_t iwi = iport->iwi;
  if (iwi) {
    iwdp_log_disconnect(iport);
    iwi->iport = NULL;
    iport->iwi = NULL;
    if (iwi->wi_fd > 0) {
      self->remove_fd(self, iwi->wi_fd);
    }
  }
  if (iport->is_sticky) {
    // keep iport so we can restore the port if this device is reattached
    iport->s_fd = -1;
  } else {
    ht_remove(iport_ht, device_id);
    iwdp_iport_free(iport);
  }
  return IWDP_SUCCESS;
}

iwdp_status iwdp_iws_close(iwdp_t self, iwdp_iws_t iws) {
  // clear pointer to this iws
  iwdp_ipage_t ipage = iws->ipage;
  if (ipage) {
    if (ipage->sender_id && ipage->iws == iws) {
      iwdp_stop_devtools(ipage);
    } // else internal error?
  }
  iwdp_iport_t iport = iws->iport;
  if (iport) {
    ht_t iws_ht = iport->ws_id_to_iws;
    char *ws_id = iws->ws_id;
    iwdp_iws_t iws2 = (iwdp_iws_t)ht_get_value(iws_ht, ws_id);
    if (ws_id && iws2 == iws) {
      ht_remove(iws_ht, ws_id);
    } // else internal error?
  }
  iwdp_ifs_t ifs = iws->ifs;
  if (ifs) {
    ifs->iws = NULL;
    if (ifs->fs_fd > 0) {
      self->remove_fd(self, ifs->fs_fd);
    } // else internal error?
  }
  iwdp_iws_free(iws);
  return IWDP_SUCCESS;
}

iwdp_status iwdp_iwi_close(iwdp_t self, iwdp_iwi_t iwi) {
  iwdp_iport_t iport = iwi->iport;
  if (iport) {
    iwdp_log_disconnect(iport);
    // clear pointer to this iwi
    if (iport->iwi) {
      iport->iwi = NULL;
    }
  }
  // free pages
  ht_t ipage_ht = iwi->page_num_to_ipage;
  iwdp_ipage_t *ipages = (iwdp_ipage_t *)ht_values(ipage_ht);
  ht_clear(ipage_ht);
  iwdp_ipage_t *ipp;
  for (ipp = ipages; *ipp; ipp++) {
    iwdp_ipage_free((iwdp_ipage_t)*ipp);
  }
  free(ipages);
  iwdp_iwi_free(iwi);
  // close browser listener, which will close all clients
  if (iport && iport->s_fd > 0) {
    self->remove_fd(self, iport->s_fd);
  }
  return IWDP_SUCCESS;
}

iwdp_status iwdp_ifs_close(iwdp_t self, iwdp_ifs_t ifs) {
  iwdp_iws_t iws = ifs->iws;
  // clear pointer to this ifs
  if (iws && iws->ifs == ifs) {
    iws->ifs = NULL;
  }
  iwdp_ifs_free(ifs);
  // close client
  if (iws && iws->ws_fd > 0) {
    self->remove_fd(self, iws->ws_fd);
  }
  return IWDP_SUCCESS;
}

iwdp_status iwdp_idl_close(iwdp_t self, iwdp_idl_t idl) {
  // TODO rm_fd all device_id_to_iport s_fds?!
  return IWDP_SUCCESS;
}

iwdp_status iwdp_on_close(iwdp_t self, int fd, void *value, bool is_server) {
  int type = ((iwdp_type_t)value)->type;
  switch (type) {
    case TYPE_IDL:
      return iwdp_idl_close(self, (iwdp_idl_t)value);
    case TYPE_IPORT:
      return iwdp_iport_close(self, (iwdp_iport_t)value);
    case TYPE_IWI:
      return iwdp_iwi_close(self, (iwdp_iwi_t)value);
    case TYPE_IWS:
      return iwdp_iws_close(self, (iwdp_iws_t)value);
    case TYPE_IFS:
      return iwdp_ifs_close(self, (iwdp_ifs_t)value);
    default:
      return self->on_error(self, "Unknown close type %d", type);
  }
}

//
// websocket
//

ws_status iwdp_send_data(ws_t ws, const char *data, size_t length) {
  iwdp_iws_t iws = (iwdp_iws_t)ws->state;
  iwdp_t self = iws->iport->self;
  return (self->send(self, iws->ws_fd, data, length) ?
      ws->on_error(ws, "Unable to send %zd bytes of data", length) :
      WS_SUCCESS);
}

ws_status iwdp_send_http(ws_t ws, bool is_head, const char *status,
    const char *resource, const char *content) {
  char *ctype;
  iwdp_get_content_type(resource, false, &ctype);
  char *data;
  if (asprintf(&data,
      "HTTP/1.1 %s\r\n"
      "Content-length: %zd\r\n"
      "Connection: close\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Access-Control-Allow-Methods: GET, HEAD"
      "%s%s\r\n\r\n%s",
      status, (content ? strlen(content) : 0),
      (ctype ? "\r\nContent-Type: " : ""), (ctype ? ctype : ""),
      (content && !is_head ? content : "")) < 0) {
    return ws->on_error(ws, "asprintf failed");
  }
  free(ctype);
  ws_status ret = ws->send_data(ws, data, strlen(data));
  free(data);
  return ret;
}

ws_status iwdp_on_list_request(ws_t ws, bool is_head, bool want_json,
    const char *host) {
  iwdp_iws_t iws = (iwdp_iws_t)ws->state;
  iwdp_iport_t iport = iws->iport;
  iwdp_t self = iport->self;
  iwdp_private_t my = self->private_state;
  char *content;
  if (iport->device_id) {
    const char *fe_url = my->frontend;
    char *frontend_url = NULL;
    if (fe_url && !strncasecmp(fe_url, "chrome-devtools://", 18)) {
      // allow chrome-devtools links, even though Chrome's sandbox blocks them:
      //   Not allowed to load local resource: chrome-devtools://...
      // Maybe a future Chrome flag (TBD?) will permit this.
      frontend_url = strdup(fe_url);
    } else if (fe_url) {
      const char *fe_proto = strstr(fe_url, "://");
      const char *fe_path = (fe_proto ? fe_proto + 3 : fe_url);
      const char *fe_sep = strrchr(fe_path, '/');
      const char *fe_file = (fe_sep ? (strlen(fe_sep) > 1 ? fe_sep + 1 : NULL) :
          fe_path);
      if (!fe_file) {
        self->on_error(self, "Ignoring invalid frontend: %s\n", fe_url);
      }
      if (asprintf(&frontend_url, "/devtools/%s", fe_file) < 0) {
        return self->on_error(self, "asprintf failed");
      }
    }
    ht_t ipage_ht = (iport->iwi ? iport->iwi->page_num_to_ipage : NULL);
    iwdp_ipage_t *ipages = (iwdp_ipage_t *)ht_values(ipage_ht);

    content = iwdp_ipages_to_text(ipages, want_json,
        iport->device_id, iport->device_name, frontend_url, host, iport->port);
    free(ipages);
    free(frontend_url);
  } else {
    iwdp_iport_t *iports = (iwdp_iport_t *)ht_values(my->device_id_to_iport);
    content = iwdp_iports_to_text(iports, want_json, host);
    free(iports);
  }
  ws_status ret = iwdp_send_http(ws, is_head, "200 OK",
      (want_json ? ".json" : ".html"), content);
  free(content);
  return ret;
}

ws_status iwdp_on_not_found(ws_t ws, bool is_head, const char *resource,
    const char *details) {
  char *content;
  if (asprintf(&content,
      "<html><title>Error 404 (Not Found)</title>\n"
      "<p><b>404.</b> <ins>That's an error.</ins>\n"
      "<p>The requested URL <code>%s</code> was not found.\n"
      "%s</html>", resource, (details ? details : "")) < 0) {
    return ws->on_error(ws, "asprintf failed");
  }
  ws_status ret = iwdp_send_http(ws, is_head, "404 Not Found", ".html",
      content);
  free(content);
  return ret;
}

ws_status iwdp_on_devtools_request(ws_t ws, const char *resource) {
  iwdp_iws_t iws = (iwdp_iws_t)ws->state;
  if (!resource || strncmp(resource, "/devtools/page/", 15)) {
    return ws->on_error(ws, "Internal error: %s", resource);
  }
  // parse page_num
  const char *s = resource + 15;
  char *end = NULL;
  int page_num = strtol(s, &end, 0);
  if (*end != '\0' || *s == '\0') {
    page_num = -1;
  }
  // find page
  iwdp_iwi_t iwi = iws->iport->iwi;
  iwdp_ipage_t p =
    (iwi && page_num > 0 && page_num <= iwi->max_page_num ?
     (iwdp_ipage_t)ht_get_value(iwi->page_num_to_ipage,
       HT_KEY(page_num)) : NULL);
  if (!p) {
    return iwdp_on_not_found(ws, false, resource, "Unknown page id");
  }
  return iwdp_start_devtools(p, iws);
}

ws_status iwdp_get_frontend_path(const char *fe_path, const char *resource,
    char **to_path) {
  if (!to_path) {
    return IWDP_ERROR;
  }
  *to_path = NULL;

  // trim frontend "/qux/inspector.html" to "/qux/"
  if (!fe_path) {
    return IWDP_ERROR;
  }
  const char *fe_file = strrchr(fe_path, '/');
  fe_file = (fe_file ? fe_file + 1 : NULL);
  size_t fe_path_len = (fe_file ? (fe_file - fe_path) : 0);

  // trim resource "/devtools/foo/bar.html?q" to "foo/bar.html"
  // this might be too restrictive, but at least it's secure
  if (!resource || strncmp(resource, "/devtools/", 10)) {
    return IWDP_ERROR;
  }
  const char *res = resource + 10;
  const char *res_tail = res - 1;
  while (*++res_tail == '/') { // deny root via !fe_path_len && res[0]=='/'
  }
  char ch;
  for (ch = *res_tail;
       ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || (ch && strchr("-./_", ch)));
       ch = *++res_tail) {
  }
  size_t res_len = res_tail - res;
  if (strnstr(res, "..", res_len)) {
    return IWDP_ERROR;
  }
  if (!res_len && fe_file) {
    res = fe_file;
    res_len = strlen(fe_file);
  }

  // concat them into "/qux/foo/bar.html"
  if (asprintf(to_path, "%.*s%.*s", (int)fe_path_len, fe_path, (int)res_len, res) < 0) {
    return IWDP_ERROR;
  }
  return IWDP_SUCCESS;
}

ws_status iwdp_on_static_request_for_file(ws_t ws, bool is_head,
    const char *resource, const char *fe_path, bool *to_keep_alive) {
  iwdp_iws_t iws = (iwdp_iws_t)ws->state;
  iwdp_t self = iws->iport->self;

  // TODO if fe_path is "/blah/resources.pak#devtools.html", do something like:
  //   if not my->path2pos and exists "/blah/resources.pak":
  //     read pathToOffset from pak, e.g. [(22000,3500), (22001,5000), ...]
  //     if exists "/blah/resources.dat":
  //       read path2id from text file, e.g. "22000 devtools.html\n22001 ..."
  //     elif exists chrome binary:  // ugly hack :(
  //       find '\0devtools.html\0' in binary
  //       read path2id from strings until a non-path, assume ids 22000-and-up
  //     join into my->path2pos, e.g. {'devtools.html':(3500,1500), ...}
  //   if my->path2pos:
  //     offset, length = my->path2pos[resource + 10]
  //     seek to offset in "/blah/resources.pak", read length bytes, ws->send

  char *path;
  iwdp_get_frontend_path(fe_path, resource, &path);
  if (!path) {
    return iwdp_send_http(ws, is_head, "403 Forbidden", ".txt", "Invalid path");
  }

  int fs_fd = open(path, O_RDONLY);
  if (fs_fd < 0) {
    // file doesn't exist.  Provide help if this is a "*.js" with a matching
    // "*.qrc", e.g. WebKit's qresource-compiled "InspectorBackendCommands.js"
    bool is_qrc = false;
    if (strlen(path) > 3 && !strcasecmp(path + strlen(path) - 3, ".js")) {
      char *qrc_path;
      if (asprintf(&qrc_path, "%.*sqrc", (int)(strlen(path) - 2), path) < 0) {
        return self->on_error(self, "asprintf failed");
      }
      int qrc_fd = open(qrc_path, O_RDONLY);
      free(qrc_path);
      if (qrc_fd >= 0) {
        is_qrc = true;
        close(qrc_fd);
      }
    }
    if (is_qrc) {
      const char *fe_sep = strrchr(fe_path, '/');
      size_t fe_path_len = (fe_sep ? (fe_sep - fe_path) : strlen(fe_path));
      self->on_error(self, "Missing code-generated WebKit file:\n"
          "  %s\n"
          "A matching \".qrc\" exists, so try generating the \".js\":\n"
          "  cd %.*s/..\n"
          "  mkdir -p tmp\n"
          "  ./CodeGeneratorInspector.py Inspector.json "
          "--output_h_dir tmp --output_cpp_dir tmp\n"
          "  mv tmp/*.js %.*s\n", path, fe_path_len, fe_path,
          fe_path_len, fe_path);
    }
    free(path);
    return iwdp_on_not_found(ws, is_head, resource,
        (is_qrc ? "Missing code-generated WebKit file" : NULL));
  }
  char *ctype = NULL;
  iwdp_get_content_type(path, true, &ctype);
  free(path);
  struct stat fs_stat;
  if (fstat(fs_fd, &fs_stat) || !(fs_stat.st_mode & S_IFREG)) {
    free(ctype);
    close(fs_fd);
    return iwdp_send_http(ws, is_head, "403 Forbidden", ".txt", "Not a file");
  }
  size_t length = fs_stat.st_size;
  char *data = NULL;
  if (asprintf(&data,
      "HTTP/1.1 200 OK\r\n"
      "Content-length: %zd\r\n"
      "Connection: close"
      "%s%s\r\n\r\n",
      length, (ctype ? "\r\nContent-Type: " : ""), (ctype ? ctype : "")) < 0) {
    return self->on_error(self, "asprintf failed");
  }
  free(ctype);
  ws_status ret = ws->send_data(ws, data, strlen(data));
  free(data);
  if (ret || is_head || !length) {
    close(fs_fd);
    return ret;
  }
  // bummer, can't self->add_fd this non-socket fd to selectable :(
  size_t max_len = 4096;
  size_t buf_len = (length > max_len ? max_len : length);
  char *buf = (char *)calloc(buf_len, sizeof(char));
  ssize_t sent_bytes = 0;
  while (true) {
    ssize_t read_bytes = read(fs_fd, buf, buf_len);
    if (read_bytes <= 0) {
      break;
    }
    if (ws->send_data(ws, buf, read_bytes)) {
      break;
    }
    sent_bytes += read_bytes;
  }
  close(fs_fd);
  return (sent_bytes == length ? WS_SUCCESS : WS_ERROR);
}

ws_status iwdp_on_static_request_for_http(ws_t ws, bool is_head,
    const char *resource, bool *to_keep_alive) {
  iwdp_iws_t iws = (iwdp_iws_t)ws->state;
  iwdp_t self = iws->iport->self;
  const char *fe_url = self->private_state->frontend;

  if (!resource || !fe_url || strncasecmp(fe_url, "http://", 7)) {
    return IWDP_ERROR; // internal error
  }

  const char *fe_host = fe_url + 7;
  const char *fe_path = strchr(fe_host, '/');
  if (!fe_path) {
    return iwdp_send_http(ws, is_head, "500 Server Error", ".txt",
        "Invalid frontend URL?");
  }
  char *path;
  iwdp_get_frontend_path(fe_path, resource, &path);
  if (!path) {
    return iwdp_send_http(ws, is_head, "403 Forbidden", ".txt", "Invalid path");
  }
  const char *fe_port = strchr(fe_host, ':');
  fe_port = (fe_port && fe_port <= fe_path ? fe_port :
      NULL); // e.g. "http://foo.com/bar:x"
  size_t fe_host_len = ((fe_port ? fe_port : fe_path) - fe_host);
  char *host = strndup(fe_host, fe_host_len);

  char *host_with_port;
  char *port = NULL;
  if (fe_port) {
    port = strndup(fe_port, fe_path - fe_port);
  }
  if (asprintf(&host_with_port, "%s%s", host, port ? port : ":80") < 0) {
    return self->on_error(self, "asprintf failed");
  };
  free(port);

  int fs_fd = self->connect(self, host_with_port);
  if (fs_fd < 0) {
    char *error;
    if (asprintf(&error, "Unable to connect to %s", host_with_port) < 0) {
      return self->on_error(self, "asprintf failed");
    }
    free(host_with_port);
    free(host);
    free(path);
    ws_status ret = iwdp_send_http(ws, is_head, "500 Server Error", ".txt",
        error);
    free(error);
    return ret;
  }
  iwdp_ifs_t ifs = iwdp_ifs_new();
  ifs->iws = iws;
  ifs->fs_fd = fs_fd;
  iws->ifs = ifs;
  if (self->add_fd(self, fs_fd, NULL, ifs, false)) {
    free(host_with_port);
    free(host);
    free(path);
    return self->on_error(self, "Unable to add fd %d", fs_fd);
  }
  char *data;
  if (asprintf(&data,
      "%s %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Connection: close\r\n" // keep-alive?
      "Accept: */*\r\n"
      "\r\n",
      (is_head ? "HEAD" : "GET"), path, host) < 0) {
    return self->on_error(self, "asprintf failed");
  }
  free(host_with_port);
  free(host);
  free(path);
  size_t length = strlen(data);
  iwdp_status ret = self->send(self, fs_fd, data, length);
  free(data);
  *to_keep_alive = true;
  return ret;

  /*
  // redirect
  char *data;
  asprintf(&data,
  "HTTP/1.1 302 Found\r\n"
  "Connection: close\r\n"
  "Location: %s%s\r\n"
  "\r\n",
  frontend, resource + 10);
  ws_status ret = ws->send_data(ws, data, strlen(data));
  free(data);
  return ret;
   */
}

ws_status iwdp_on_static_request(ws_t ws, bool is_head, const char *resource,
    bool *to_keep_alive) {
  iwdp_iws_t iws = (iwdp_iws_t)ws->state;
  iwdp_t self = iws->iport->self;
  if (!resource || strncmp(resource, "/devtools/", 10)) {
    return self->on_error(self, "Internal error: %s", resource);
  }

  iwdp_private_t my = self->private_state;
  const char *fe_url = my->frontend;
  if (!fe_url) {
    return iwdp_on_not_found(ws, is_head, resource, "Frontend is disabled.");
  }
  bool is_file = !strstr(fe_url, "://");
  if (is_file || !strncasecmp(fe_url, "file://", 7)) {
    return iwdp_on_static_request_for_file(ws, is_head, resource,
        fe_url + (is_file ? 0 : 7), to_keep_alive);
  } else if (!strncasecmp(fe_url, "http://", 7)) {
    return iwdp_on_static_request_for_http(ws, is_head, resource,
        to_keep_alive);
  }
  return iwdp_on_not_found(ws, is_head, resource, "Invalid frontend URL?");
}

ws_status iwdp_on_http_request(ws_t ws,
    const char *method, const char *resource, const char *version,
    const char *host, const char *headers, size_t headers_length,
    bool is_websocket, bool *to_keep_alive) {
  bool is_get = !strcmp(method, "GET");
  bool is_head = !is_get && !strcmp(method, "HEAD");
  if (is_websocket) {
    if (is_get && !strncmp(resource, "/devtools/page/", 15)) {
      return iwdp_on_devtools_request(ws, resource);
    }
  } else {
    if (!is_get && !is_head) {
      return iwdp_on_not_found(ws, is_head, resource, "Method Not Allowed");
    }

    if (!strlen(resource) || !strcmp(resource, "/")) {
      return iwdp_on_list_request(ws, is_head, false, host);
    } else if (!strcmp(resource, "/json") || !strcmp(resource, "/json/list")) {
      return iwdp_on_list_request(ws, is_head, true, host);
    } else if (!strncmp(resource, "/devtools/", 10)) {
      return iwdp_on_static_request(ws, is_head, resource,
          to_keep_alive);
    }
    // Chrome's devtools_http_handler_impl.cc also supports:
    //   /json/version*  -- version info
    //   /json/new*      -- open page
    //   /json/close/*   -- close page
    //   /thumb/*        -- get page thumbnail png
  }
  return iwdp_on_not_found(ws, is_head, resource, NULL);
}

ws_status iwdp_on_upgrade(ws_t ws,
    const char *resource, const char *protocol,
    int version, const char *sec_key) {
  return ws->send_upgrade(ws);
}

ws_status iwdp_on_frame(ws_t ws,
    bool is_fin, uint8_t opcode, bool is_masking,
    const char *payload_data, size_t payload_length,
    bool *to_keep) {
  iwdp_iws_t iws = (iwdp_iws_t)ws->state;
  switch (opcode) {
    case OPCODE_TEXT:
    case OPCODE_BINARY:
      if (!is_fin) {
        // wait for full data
        *to_keep = true;
        return WS_SUCCESS;
      }
      if (!is_masking) {
        return ws->send_close(ws, CLOSE_PROTOCOL_ERROR,
            "Clients must mask");
      }
      iwdp_iport_t iport = iws->iport;
      iwdp_iwi_t iwi = iport->iwi;
      if (!iwi) {
        return ws->send_close(ws, CLOSE_GOING_AWAY, "inspector closed?");
      }
      iwdp_ipage_t ipage = iws->ipage;
      if (!ipage) {
        // someone stole our page?
        iwdp_ipage_t p = (iws->page_num ? (iwdp_ipage_t)ht_get_value(
            iwi->page_num_to_ipage, HT_KEY(iws->page_num)) : NULL);
        char *s;
        if (asprintf(&s, "Page %d/%d %s%s", iport->port, iws->page_num,
            (p ? "claimed by " : "not found"),
            (p ? (p->iws ? "local" : "remote") : "" )) < 0) {
          return ws->on_error(ws, "asprintf failed");
        }
        ws->on_error(ws, "%s", s);
        ws_status ret = ws->send_close(ws, CLOSE_GOING_AWAY, s);
        free(s);
        return ret;
      }
      rpc_t rpc = iwi->rpc;
      return rpc->send_forwardSocketData(rpc,
          iwi->connection_id,
          ipage->app_id, ipage->page_id, ipage->sender_id,
          payload_data, payload_length);

    case OPCODE_CLOSE:
      // ack close
      return ws->send_close(ws, CLOSE_NORMAL, NULL);

    case OPCODE_PING:
      // ack ping
      return ws->send_frame(ws,
          true, OPCODE_PONG, false,
          payload_data, payload_length);

    case OPCODE_PONG:
      return WS_SUCCESS;

    default:
      return WS_ERROR;
  }
}

//
// webinspector
//

wi_status iwdp_send_packet(wi_t wi, const char *packet, size_t length) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)wi->state;
  iwdp_t self = iwi->iport->self;
  return (self->send(self, iwi->wi_fd, packet, length) ?
      self->on_error(self, "Unable to send %zd bytes to inspector", length) :
      WI_SUCCESS);
}

wi_status iwdp_recv_plist(wi_t wi, const plist_t rpc_dict) {
  rpc_t rpc = ((iwdp_iwi_t)wi->state)->rpc;
  return rpc->recv_plist(rpc, rpc_dict);
}

rpc_status iwdp_send_plist(rpc_t rpc, const plist_t rpc_dict) {
  wi_t wi = ((iwdp_iwi_t)rpc->state)->wi;
  return wi->send_plist(wi, rpc_dict);
}

rpc_status iwdp_on_reportSetup(rpc_t rpc) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)rpc->state;
  iwi->connected = true;
  iwdp_log_connect(iwi->iport);
  return RPC_SUCCESS;
}

rpc_status iwdp_add_app_id(rpc_t rpc, const char *app_id) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)rpc->state;
  ht_t app_id_ht = iwi->app_id_to_true;
  if (ht_get_value(app_id_ht, app_id)) {
    return RPC_SUCCESS;
  }
  ht_put(app_id_ht, strdup(app_id), HT_VALUE(1));
  return rpc->send_forwardGetListing(rpc, iwi->connection_id, app_id);
}

void rpc_set_app(rpc_t rpc, const rpc_app_t app) {
    iwdp_iwi_t iwi = (iwdp_iwi_t)rpc->state;
    rpc_app_t to_app = NULL;
    rpc_copy_app(app, &to_app);
    iwi->app = to_app;
}

rpc_status iwdp_on_applicationConnected(rpc_t rpc, const rpc_app_t app) {
  rpc_set_app(rpc, app);
  return iwdp_add_app_id(rpc, app->app_id);
}

ws_status iwdp_start_devtools(iwdp_ipage_t ipage, iwdp_iws_t iws) {
  if (!ipage || !iws) {
    return WS_ERROR;
  }
  iwdp_iwi_t iwi = iws->iport->iwi;
  if (!iwi) {
    return WS_ERROR; // internal error?
  }
  iwdp_iport_t iport = iwi->iport;
  iwdp_t self = (iport ? iport->self : NULL);
  iwdp_iws_t iws2 = ipage->iws;
  if (iws2) {
    // steal this page from our other client, as if the page went away
    self->on_error(self, "Taking page %d/%d from local %s to %s",
        iport->port, ipage->page_num, iws2->ws_id, iws->ws_id);
    iwdp_stop_devtools(ipage);
    iws2->page_num = ipage->page_num;
  }
  iws->ipage = ipage;
  iws->page_num = ipage->page_num;
  ipage->iws = iws;
  ipage->sender_id = strdup(iws->ws_id);
  if (ipage->connection_id && iwi->connection_id &&
       strcmp(ipage->connection_id, iwi->connection_id)) {
    // steal this page from the other (not-us, maybe dead?) inspector.
    // We also might need to send_forwardDidClose on their behalf...
    self->on_error(self, "Taking page %d/%d from remote %s",
        iport->port, ipage->page_num, ipage->connection_id);
  }
  rpc_t rpc = iwi->rpc;
  return rpc->send_forwardSocketSetup(rpc,
      iwi->connection_id,
      ipage->app_id, ipage->page_id, ipage->sender_id);
}

ws_status iwdp_stop_devtools(iwdp_ipage_t ipage) {
  iwdp_iws_t iws = ipage->iws;
  if (!iws) {
    return WS_SUCCESS;
  }
  if (iws->ipage != ipage) {
    return WS_ERROR; // internal error?
  }
  char *sender_id = ipage->sender_id;
  if (!sender_id) {
    return WS_ERROR; // internal error?
  }
  iwdp_iport_t iport = iws->iport;
  iwdp_iws_t iws2 = ht_get_value(iport->ws_id_to_iws, sender_id);
  if (iws != iws2) {
    return WS_ERROR; // internal error?
  }
  iwdp_iwi_t iwi = iport->iwi;
  if (iwi && iwi->connection_id && (!ipage->connection_id ||
        !strcmp(ipage->connection_id, iwi->connection_id))) {
    // if ipage->connection_id is NULL, it's likely a normal lag between our
    // send_forwardSocketSetup and the on_applicationSentListing ack.
    rpc_t rpc = iwi->rpc;
    rpc->send_forwardDidClose(rpc,
        iwi->connection_id, ipage->app_id,
        ipage->page_id, ipage->sender_id);
  }
  // close the ws_fd?
  iws->ipage = NULL;
  iws->page_num = 0;
  ipage->iws = NULL;
  ipage->sender_id = NULL;
  free(sender_id);
  return WS_SUCCESS;
}

rpc_status iwdp_remove_app_id(rpc_t rpc, const char *app_id) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)rpc->state;
  ht_t app_id_ht = iwi->app_id_to_true;
  char *old_app_id = ht_get_key(app_id_ht, app_id);
  if (!old_app_id) {
    return RPC_SUCCESS;
  }
  ht_remove(app_id_ht, app_id);
  // remove pages with this app_id
  ht_t ipage_ht = iwi->page_num_to_ipage;
  iwdp_ipage_t *ipages = (iwdp_ipage_t *)ht_values(ipage_ht);
  iwdp_ipage_t *ipp;
  for (ipp = ipages; *ipp; ipp++) {
    iwdp_ipage_t ipage = *ipp;
    if (!strcmp(app_id, ipage->app_id)) {
      iwdp_stop_devtools(ipage);
      ht_remove(ipage_ht, HT_KEY(ipage->page_num));
      iwdp_ipage_free(ipage);
    }
  }
  free(ipages);
  // free this last, in case old_app_id == app_id
  free(old_app_id);
  return RPC_SUCCESS;
}

rpc_status iwdp_on_applicationDisconnected(rpc_t rpc, const rpc_app_t app) {
  return iwdp_remove_app_id(rpc, app->app_id);
}

rpc_status iwdp_on_reportConnectedApplicationList(rpc_t rpc, const rpc_app_t *apps) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)rpc->state;
  ht_t app_id_ht = iwi->app_id_to_true;

  // rpc_reportSetup never comes from iOS >= 11.3
  if (!iwi->connected) {
    iwi->connected = true;
    iwdp_log_connect(iwi->iport);
  }

  if (*apps == NULL) {
    return RPC_SUCCESS;
  }

  // remove old apps
  char **old_app_ids = (char **)ht_keys(app_id_ht);
  char **oa;
  for (oa = old_app_ids; *oa; oa++) {
    const rpc_app_t *a;
    for (a = apps; *a && strcmp((*a)->app_id, *oa); a++) {
    }
    if (!*a) {
      iwdp_remove_app_id(rpc, *oa);
    }
  }
  free(old_app_ids);

  // add new apps
  const rpc_app_t *a;
  for (a = apps; *a; a++) {
    rpc_set_app(rpc, *a);
    iwdp_add_app_id(rpc, (*a)->app_id);
  }
  return RPC_SUCCESS;
}

rpc_status iwdp_on_applicationSentListing(rpc_t rpc,
    const char *app_id, const rpc_page_t *pages) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)rpc->state;
  iwdp_iport_t iport = (iwi ? iwi->iport : NULL);
  iwdp_t self = (iport ? iport->self : NULL);
  if (!self) {
    return RPC_ERROR;  // Inspector closed?
  }
  if (!ht_get_value(iwi->app_id_to_true, app_id)) {
    iwdp_iwi_t iwi = (iwdp_iwi_t)rpc->state;
    rpc_app_t app = iwi->app;
    if (app) {
      return rpc->send_forwardGetListing(rpc, iwi->connection_id, app->app_id);
    }
    return self->on_error(self, "Unknown app_id %s", app_id);
  }
  ht_t ipage_ht = iwi->page_num_to_ipage;
  iwdp_ipage_t *ipages = (iwdp_ipage_t *)ht_values(ipage_ht);

  // add new pages
  const rpc_page_t *pp;
  for (pp = pages; *pp; pp++) {
    const rpc_page_t page = *pp;
    // find page with this app_id & page_id
    iwdp_ipage_t ipage = NULL;
    iwdp_ipage_t *ipp;
    for (ipp = ipages; *ipp; ipp++) {
      if ((*ipp)->page_id == page->page_id &&
          !strcmp(app_id, (*ipp)->app_id)) {
        ipage = *ipp;
        break;
      }
    }
    if (!ipage) {
      // new page
      ipage = iwdp_ipage_new();
      ipage->app_id = strdup(app_id);
      ipage->page_id = page->page_id;
      ipage->page_num = ++iwi->max_page_num;
      ht_put(ipage_ht, HT_KEY(ipage->page_num), ipage);
    }
    iwdp_update_string(&ipage->title, page->title);
    iwdp_update_string(&ipage->url, page->url);
    if (ipage->iws && page->connection_id && iwi->connection_id &&
        strcmp(iwi->connection_id, page->connection_id)) {
      // a remote inspector stole stole our page?
      char *s;
      if (asprintf(&s, "Page %d/%d claimed by remote %s",
          iport->port, ipage->page_id, page->connection_id) < 0) {
        return self->on_error(self, "asprintf failed");
      }
      self->on_error(self, "%s", s);
      free(s);
      ipage->iws->ipage = NULL;
    }
    iwdp_update_string(&ipage->connection_id, page->connection_id);
  }

  // remove old pages
  iwdp_ipage_t *ipp;
  for (ipp = ipages; *ipp; ipp++) {
    iwdp_ipage_t ipage = *ipp;
    if (strcmp(ipage->app_id, app_id)) {
      continue;
    }
    const rpc_page_t *pp;
    for (pp = pages; *pp && (*pp)->page_id != ipage->page_id; pp++) {
    }
    if (!*pp) {
      iwdp_stop_devtools(ipage);
      ht_remove(ipage_ht, HT_KEY(ipage->page_num));
      iwdp_ipage_free(ipage);
    }
  }
  free(ipages);

  return RPC_SUCCESS;
}

rpc_status iwdp_on_applicationSentData(rpc_t rpc,
    const char *app_id, const char *dest_id,
    const char *data, const size_t length) {
  iwdp_iport_t iport = ((iwdp_iwi_t)rpc->state)->iport;
  iwdp_iws_t iws = ht_get_value(iport->ws_id_to_iws, dest_id);
  if (!iws) {
    return RPC_SUCCESS;  // error but don't kill the inspector!
  }
  ws_t ws = iws->ws;
  return ws->send_frame(ws,
      true, OPCODE_TEXT, false,
      data, length);
}

rpc_status iwdp_on_applicationUpdated(rpc_t rpc,
    const char *app_id, const char *dest_id) {
  return iwdp_add_app_id(rpc, dest_id);
}

//
// STRUCTS
//

void iwdp_free(iwdp_t self) {
  if (self) {
    iwdp_private_t my = self->private_state;
    if (my) {
      ht_free(my->device_id_to_iport);
      free(my->frontend);
      free(my->sim_wi_socket_addr);
      memset(my, 0, sizeof(struct iwdp_private));
      free(my);
    }
    memset(self, 0, sizeof(struct iwdp_struct));
    free(self);
  }
}

iwdp_t iwdp_new(const char *frontend, const char *sim_wi_socket_addr) {
  iwdp_t self = (iwdp_t)malloc(sizeof(struct iwdp_struct));
  iwdp_private_t my = (iwdp_private_t)malloc(sizeof(struct iwdp_private));
  if (!self || !my) {
    iwdp_free(self);
    return NULL;
  }
  memset(self, 0, sizeof(struct iwdp_struct));
  memset(my, 0, sizeof(struct iwdp_private));
  self->start = iwdp_start;
  self->on_accept = iwdp_on_accept;
  self->on_recv = iwdp_on_recv;
  self->on_close = iwdp_on_close;
  self->on_error = iwdp_on_error;
  self->private_state = my;
  my->frontend = (frontend ? strdup(frontend) : NULL);
  my->sim_wi_socket_addr = strdup(sim_wi_socket_addr);
  my->device_id_to_iport = ht_new(HT_STRING_KEYS);
  if (!my->device_id_to_iport) {
    iwdp_free(self);
    return NULL;
  }
  return self;
}

void iwdp_idl_free(iwdp_idl_t idl) {
  if (idl) {
    dl_free(idl->dl);
    memset(idl, 0, sizeof(struct iwdp_idl_struct));
    free(idl);
  }
}

iwdp_idl_t iwdp_idl_new() {
  iwdp_idl_t idl = (iwdp_idl_t)malloc(sizeof(struct iwdp_idl_struct));
  dl_t dl = dl_new();
  if (!idl || !dl) {
    free(idl);
    return NULL;
  }
  memset(idl, 0, sizeof(struct iwdp_idl_struct));
  idl->type.type = TYPE_IDL;
  idl->dl = dl;
  dl->send_packet = iwdp_send_to_dl;
  dl->on_attach = iwdp_on_attach;
  dl->on_detach = iwdp_on_detach;
  dl->state = idl;
  return idl;
}

void iwdp_iport_free(iwdp_iport_t iport) {
  if (iport) {
    free(iport->device_id);
    free(iport->device_name);
    ht_free(iport->ws_id_to_iws);
    memset(iport, 0, sizeof(struct iwdp_iport_struct));
    free(iport);
  }
}

iwdp_iport_t iwdp_iport_new() {
  iwdp_iport_t iport = (iwdp_iport_t)malloc(sizeof(struct iwdp_iport_struct));
  if (!iport) {
    return NULL;
  }
  memset(iport, 0, sizeof(struct iwdp_iport_struct));
  iport->type.type = TYPE_IPORT;
  iport->ws_id_to_iws = ht_new(HT_STRING_KEYS);
  if (!iport->ws_id_to_iws) {
    iwdp_iport_free(iport);
    return NULL;
  }
  return iport;
}

int iwdp_iport_cmp(const void *a, const void *b) {
  const iwdp_iport_t ipa = *((iwdp_iport_t *)a);
  const iwdp_iport_t ipb = *((iwdp_iport_t *)b);
  if (ipa == ipb || !ipa || !ipb) {
    return (ipa == ipb ? 0 : ipa ? -1 : 1);
  }
  uint32_t pa = ipa->port;
  uint32_t pb = ipb->port;
  return (pa == pb ? 0 : pa < pb ? -1 : 1);
}

/*
 * Escape string value for json output
 */
char *iwdp_escape_json_string_val(const char *str) {
  int len = strlen(str);
  char* res = (char*)malloc(len * 6 + 1);

  int i, j;
  for (i = 0, j = 0; i < len; i++, j++) {
    if (str[i] >= 0 && str[i] < 32) {
      sprintf(res + j, "\\u%04d", str[i]);
      j += 5;
    } else {
      if (str[i] == '"' || str[i] == '\\') {
        res[j++] = '\\';
      }
      res[j] = str[i];
    }
  }
  res[j] = '\0';

  return res;
}

char *iwdp_iports_to_text(iwdp_iport_t *iports, bool want_json,
    const char *host) {
  // count ports
  size_t n = 0;
  const iwdp_iport_t *ipp;
  for (ipp = iports; *ipp; ipp++) {
    n++;
  }

  // sort by port
  qsort(iports, n, sizeof(iwdp_iport_t), iwdp_iport_cmp);

  // get each port as text
  char **items = (char **)calloc(n+1, sizeof(char *));
  if (!items) {
    return NULL;
  }
  size_t sum_len = 0;
  char **item = items;
  for (ipp = iports; *ipp; ipp++) {
    iwdp_iport_t iport = *ipp;
    if (!iport->device_id) {
      continue; // skip registry port
    }
    // Escape/encode device_id & device_name?
    char *s = NULL;
    if (want_json) {
      if (iport->iwi) {
        char* escaped_device_id = iwdp_escape_json_string_val(
            iport->device_id ? iport->device_id : "");
        char* escaped_device_name = iwdp_escape_json_string_val(
            iport->device_name ? iport->device_name : "");

        int os_version_major = (iport->device_os_version >> 16) & 0xff;
        int os_version_minor = (iport->device_os_version >> 8) & 0xff;
        int os_version_patch = iport->device_os_version & 0xff;

        int res = asprintf(&s,
            "%s{\n"
            "   \"deviceId\": \"%s\",\n"
            "   \"deviceName\": \"%s\",\n"
            "   \"deviceOSVersion\": \"%d.%d.%d\",\n"
            "   \"url\": \"%s:%d\"\n"
            "}",
            (sum_len ? "," : ""), escaped_device_id, escaped_device_name,
            os_version_major, os_version_minor, os_version_patch,
            (host ? host : "localhost"), iport->port);

        free(escaped_device_id);
        free(escaped_device_name);

        if (res < 0) {
          free(items);
          return NULL;  // asprintf failed
        }
      }
    } else {
      // TODO use relative urls instead of "localhost", see:
      //   http://stackoverflow.com/questions/6016120
      char *href = NULL;
      if (iport->iwi) {
        if (asprintf(&href, " href=\"http://%s:%d/\"",
            (host ? host : "localhost"), iport->port) < 0) {
          free(items);
          return NULL;  // asprintf failed
        }
      }
      if (asprintf(&s,
          "<li><a%s>%s:%d</a> - <a title=\"%s\">%s</a></li>\n",
          (href ? href : ""), (host ? host : "localhost"),
          iport->port, iport->device_id,
          (iport->device_name ? iport->device_name : "?")) < 0) {
        free(items);
        return NULL;  // asprintf failed
      }
      free(href);
    }
    if (s) {
      sum_len += strlen(s);
      *item++ = s;
    }
  }

  const char *header =
    (want_json ? "[" : "<html><head><title>iOS Devices</title></head>"
     "<body>iOS Devices:<p><ol>\n");
  const char *footer = (want_json ? "]" : "</ol></body></html>");

  // concat
  size_t length = strlen(header) + sum_len + strlen(footer);
  char *ret = (char *)calloc(length+1, sizeof(char));
  if (ret) {
    char *tail = ret;
    strcpy(tail, header);
    tail += strlen(header);
    for (item = items; *item; item++) {
      strcpy(tail, *item);
      tail += strlen(*item);
      free(*item);
    }
    strcpy(tail, footer);
  }
  free(items);
  return ret;
}

void iwdp_iwi_free(iwdp_iwi_t iwi) {
  if (iwi) {
    wi_free(iwi->wi);
    rpc_free(iwi->rpc);
    rpc_free_app(iwi->app);
    // TODO free ht_values?
    free(iwi->connection_id);
    ht_free(iwi->app_id_to_true);
    ht_free(iwi->page_num_to_ipage);
    memset(iwi, 0, sizeof(struct iwdp_iwi_struct));
    free(iwi);
  }
}

iwdp_iwi_t iwdp_iwi_new(bool is_sim, bool partials_supported, bool *is_debug) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)malloc(sizeof(struct iwdp_iwi_struct));
  if (!iwi) {
    return NULL;
  }
  memset(iwi, 0, sizeof(struct iwdp_iwi_struct));
  iwi->type.type = TYPE_IWI;
  iwi->app_id_to_true = ht_new(HT_STRING_KEYS);
  iwi->page_num_to_ipage = ht_new(HT_INT_KEYS);
  rpc_t rpc = rpc_new();
  wi_t wi = wi_new(is_sim, partials_supported);
  if (!rpc || !wi || !iwi->page_num_to_ipage || !iwi->app_id_to_true) {
    iwdp_iwi_free(iwi);
    return NULL;
  }
  rpc->on_reportSetup = iwdp_on_reportSetup;
  rpc->on_reportConnectedApplicationList =
    iwdp_on_reportConnectedApplicationList;
  rpc->on_applicationConnected = iwdp_on_applicationConnected;
  rpc->on_applicationDisconnected = iwdp_on_applicationDisconnected;
  rpc->on_applicationSentListing = iwdp_on_applicationSentListing;
  rpc->on_applicationSentData = iwdp_on_applicationSentData;
  rpc->on_applicationUpdated = iwdp_on_applicationUpdated;
  rpc->send_plist = iwdp_send_plist;
  rpc->state = iwi;
  iwi->rpc = rpc;
  wi->send_packet = iwdp_send_packet;
  wi->recv_plist = iwdp_recv_plist;
  wi->state = iwi;
  wi->is_debug = is_debug;
  iwi->wi = wi;
  return iwi;
}

void iwdp_iws_free(iwdp_iws_t iws) {
  if (iws) {
    ws_free(iws->ws);
    free(iws->ws_id);
    memset(iws, 0, sizeof(struct iwdp_iws_struct));
    free(iws);
  }
}

iwdp_iws_t iwdp_iws_new(bool *is_debug) {
  iwdp_iws_t iws = (iwdp_iws_t)malloc(sizeof(struct iwdp_iws_struct));
  if (!iws) {
    return NULL;
  }
  memset(iws, 0, sizeof(struct iwdp_iws_struct));
  iws->type.type = TYPE_IWS;
  iws->ws = ws_new();
  if (iws->ws) {
    ws_t ws = iws->ws;
    ws->send_data = iwdp_send_data;
    ws->on_http_request = iwdp_on_http_request;
    ws->on_upgrade = iwdp_on_upgrade;
    ws->on_frame = iwdp_on_frame;
    ws->state = iws;
    ws->is_debug = is_debug;
  }

  if (!iws->ws) {
    iwdp_iws_free(iws);
    return NULL;
  }
  return iws;
}

void iwdp_ifs_free(iwdp_ifs_t ifs) {
  if (ifs) {
    memset(ifs, 0, sizeof(struct iwdp_ifs_struct));
    free(ifs);
  }
}

iwdp_ifs_t iwdp_ifs_new() {
  iwdp_ifs_t ifs = (iwdp_ifs_t)malloc(sizeof(struct iwdp_ifs_struct));
  if (ifs) {
    memset(ifs, 0, sizeof(struct iwdp_ifs_struct));
    ifs->type.type = TYPE_IFS;
  }
  return ifs;
}

void iwdp_ipage_free(iwdp_ipage_t ipage) {
  if (ipage) {
    free(ipage->app_id);
    free(ipage->connection_id);
    free(ipage->title);
    free(ipage->url);
    free(ipage->sender_id);
    memset(ipage, 0, sizeof(struct iwdp_ipage_struct));
    free(ipage);
  }
}

iwdp_ipage_t iwdp_ipage_new() {
  iwdp_ipage_t ipage = (iwdp_ipage_t)malloc(sizeof(struct iwdp_ipage_struct));
  if (ipage) {
    memset(ipage, 0, sizeof(struct iwdp_ipage_struct));
  }
  return ipage;
}

/*!
 * @result compare by page_num
 */
int iwdp_ipage_cmp(const void *a, const void *b) {
  const iwdp_ipage_t ipa = *((iwdp_ipage_t *)a);
  const iwdp_ipage_t ipb = *((iwdp_ipage_t *)b);
  if (ipa == ipb || !ipa || !ipb) {
    return (ipa == ipb ? 0 : ipa ? -1 : 1);
  }
  uint32_t pna = ipa->page_num;
  uint32_t pnb = ipb->page_num;
  return (pna == pnb ? 0 : pna < pnb ? -1 : 1);
}

/*
   [{
   "devtoolsFrontendUrl": "/devtools/devtools.html?host=localhost:9222&page=7",
   "faviconUrl": "",
   "thumbnailUrl": "/thumb/http://www.google.com/",
   "title": "Google",
   "url": "http://www.google.com/",
   "webSocketDebuggerUrl": "ws://localhost:9222/devtools/page/7"
   }]
 */
char *iwdp_ipages_to_text(iwdp_ipage_t *ipages, bool want_json,
    const char *device_id, const char *device_name,
    const char *frontend_url, const char *host, int port) {
  // count pages
  size_t n = 0;
  const iwdp_ipage_t *ipp;
  for (ipp = ipages; *ipp; ipp++) {
    n++;
  }

  // sort by page_num
  qsort(ipages, n, sizeof(iwdp_ipage_t), iwdp_ipage_cmp);

  // get each page as text
  char **items = (char **)calloc(n+1, sizeof(char *));
  if (!items) {
    return NULL;
  }
  size_t sum_len = 0;
  char **item = items;
  for (ipp = ipages; *ipp; ipp++) {
    iwdp_ipage_t ipage = *ipp;
    char *href = NULL;
    if (frontend_url) {
      if (asprintf(&href, "%s?ws=%s:%d/devtools/page/%d", frontend_url,
          (host ? host : "localhost"), port, ipage->page_num) < 0) {
        return NULL;  // asprintf failed
      }
    }
    char *s = NULL;
    if (want_json) {
      char* escaped_title = iwdp_escape_json_string_val(
          ipage->title ? ipage->title : "");
      char* escaped_app_id = iwdp_escape_json_string_val(
          ipage->app_id ? ipage->app_id : "");
      char* escaped_page_url = iwdp_escape_json_string_val(
          ipage->url ? ipage->url : "");

      int res = asprintf(&s,
          "%s{\n"
          "   \"devtoolsFrontendUrl\": \"%s\",\n"
          "   \"faviconUrl\": \"\",\n"
          "   \"thumbnailUrl\": \"/thumb/%s\",\n"
          "   \"title\": \"%s\",\n"
          "   \"url\": \"%s\",\n"
          "   \"webSocketDebuggerUrl\": \"ws://%s:%d/devtools/page/%d\",\n"
          "   \"appId\": \"%s\"\n"
          "}",
          (sum_len ? "," : ""), (href && !ipage->iws ? href : ""),
          escaped_page_url, escaped_title, escaped_page_url,
          (host ? host : "localhost"), port, ipage->page_num, escaped_app_id);

      free(escaped_title);
      free(escaped_app_id);
      free(escaped_page_url);

      if (res < 0) {
        free(href);
        free(items);
        return NULL;  // asprintf failed
      }
    } else {
      if (asprintf(&s,
          "<li value=\"%d\"><a%s%s%s title=\"%s\">%s</a></li>\n",
          ipage->page_num,
          (href ? (ipage->iws ? " alt=\"" : " href=\"") : ""),
          (href ? href : ""),
          (href ? "\"" : ""),
          (ipage->title ? ipage->title : "?"),
          (ipage->url ? ipage->url : "?")) < 0) { // encodeURI?
        free(href);
        free(items);
        return NULL;  // asprintf failed
      }
    }
    free(href);
    if (s) {
      sum_len += strlen(s);
      *item++ = s;
    }
  }

  char *header = (char *)"[";
  char *footer = (char *)"]";
  if (!want_json) {
    if (asprintf(&header,
        "<html><head><title>%s</title></head>"
        "<body>Inspectable pages for <a title=\"%s\">%s</a>:<p><ol>\n",
        device_name, device_id, device_name) < 0) {
      return NULL;  // asprintf failed
    }
    bool is_chrome_dev = (sum_len > 0 && frontend_url &&
        !strncasecmp(frontend_url, "chrome-devtools://", 18));
    if (asprintf(&footer, "</ol>%s</body></html>", (is_chrome_dev ?
        "<p><b>Note:</b> Your browser may block<sup><a href=\""
        "https://code.google.com/p/chromium/issues/detail?id=87815"
        "\"1\">1,</a><a href=\""
        "https://codereview.chromium.org/12621008#msg11"
        "\">2</a></sup> the above links with JavaScript console error:<br><tt>"
        "&nbsp;&nbsp;Not allowed to load local resource: chrome-devtools://..."
        "</tt><br>To open a link: right-click on the link (control-click on"
        " Mac), 'Copy Link Address', and paste it into address bar." : "")) < 0) {
      return NULL;  // asprintf failed
    }
  }

  // concat
  size_t length = strlen(header) + sum_len + strlen(footer);
  char *ret = (char *)calloc(length+1, sizeof(char));
  if (ret) {
    char *tail = ret;
    strcpy(tail, header);
    tail += strlen(header);
    for (item = items; *item; item++) {
      strcpy(tail, *item);
      tail += strlen(*item);
      free(*item);
    }
    strcpy(tail, footer);
  }
  if (!want_json) {
    free(header);
    free(footer);
  }
  free(items);
  return ret;
}

int iwdp_update_string(char **old_value, const char *new_value) {
  if (*old_value) {
    if (new_value && !strcmp(*old_value, new_value)) {
      return 0;
    }
    free(*old_value);
    *old_value = NULL;
  }
  if (new_value) {
    *old_value = strdup(new_value);
    if (!*old_value) {
      return -1;
    }
  }
  return 0;
}

iwdp_status iwdp_get_content_type(const char *path, bool is_local,
    char **to_mime) {
  const char *mime = NULL;
  if (is_local) {
#ifdef MAGIC_MIME
    magic_t fs_m = magic_open(MAGIC_MIME);
    if (fs_m) {
      if (!magic_load(fs_m, NULL)) {
        mime = magic_file(fs_m, path);
      }
      magic_close(fs_m);
    }
#endif
  }
  if (!mime) {
    char *fext = strrchr(path, '.');
    if (fext) {
      ++fext;
      size_t n = (sizeof(EXT_TO_MIME) / sizeof(EXT_TO_MIME[0]));
      size_t i;
      for (i = 0; i < n; i++) {
        if (!strcasecmp(fext, EXT_TO_MIME[i][0])) {
          mime = EXT_TO_MIME[i][1];
          break;
        }
      }
    }
  }
  *to_mime = (mime ? strdup(mime) : NULL);
  return (mime ? IWDP_SUCCESS : IWDP_ERROR);
}

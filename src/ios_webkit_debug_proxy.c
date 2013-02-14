// Google BSD license http://code.google.com/google_bsd_license.html
// Copyright 2012 Google Inc. wrightt@google.com

//
//
//

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_listener.h"
#include "hash_table.h"
#include "ios_webkit_debug_proxy.h"
#include "webinspector.h"
#include "websocket.h"


struct iwdp_idl_struct;
typedef struct iwdp_idl_struct *iwdp_idl_t;

struct iwdp_private {
  // our device listener
  iwdp_idl_t idl;

  // our null-id registry (:9221) plus per-device ports (:9222-...)
  ht_t device_id_to_iport;

  // static file server base url, e.g.
  //   host "chrome-devtools-frontend.appspot.com"
  //   path "/static/18.0.1025.74/"
  char *frontend_host;
  char *frontend_path;
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

  // all websocket clients on this port
  // key owned by iws->ws_id
  ht_t ws_id_to_iws;

  // iOS device_id, e.g. ddc86a518cd948e13bbdeadbeef00788ea35fcf9
  char *device_id;
  char *device_name;

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

  uint32_t max_page_num; // > 0
  ht_t app_id_to_true;   // set of app_ids
  ht_t page_num_to_ipage;
};
iwdp_iwi_t iwdp_iwi_new(bool *is_debug);
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
  // owner is iwi->page_num_to_ipage
  iwdp_ipage_t ipage;

  // sef if the resource is /devtools/<non-page>
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

  // webinspector
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
    const char *frontend, const char *host, int port);

ws_status iwdp_start_devtools(iwdp_ipage_t ipage, iwdp_iws_t iws);
ws_status iwdp_stop_devtools(iwdp_ipage_t ipage);

int iwdp_update_string(char **old_value, const char *new_value);

//
// device_listener
//

iwdp_status iwdp_on_error(iwdp_t self, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  return IWDP_ERROR;
}

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
    return self->on_error(self, "select_port(%) failed", device_id);
  }
  if (port < 0 && (min_port < 0 || max_port < min_port)) {
    return DL_ERROR; // ignore this device
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
    return self->on_error(self, "Unable to bind %s on port %d-%d", device_id,
        min_port, max_port);
  }
  if (self->add_fd(self, s_fd, iport, true)) {
    return self->on_error(self, "add_fd s_fd=%d failed", s_fd);
  }
  iport->s_fd = s_fd;
  iport->port = port;
  return DL_SUCCESS;
}

iwdp_status iwdp_start(iwdp_t self) {
  iwdp_private_t my = self->private_state;
  if (my->idl) {
    return self->on_error(self, "Already started?");
  }

  if (iwdp_listen(self, NULL)) {
    self->on_error(self, "Unable to create registry");
    // Okay, keep going
  }

  iwdp_idl_t idl = iwdp_idl_new();
  idl->self = self;

  int dl_fd = self->subscribe(self);
  if (dl_fd < 0) {
    return self->on_error(self, "Unable to subscribe to"
        " device add/remove events");
  }
  idl->dl_fd = dl_fd;

  if (self->add_fd(self, dl_fd, idl, false)) {
    return self->on_error(self, "add_fd failed");
  }

  dl_t dl = idl->dl;
  if (dl->start(dl)) {
    return self->on_error(self, "Unable to start device_listener");
  }

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

  // connect to inspector
  int wi_fd = self->attach(self, device_id, NULL,
      (device_name ? NULL : &device_name));
  if (wi_fd < 0) {
    self->remove_fd(self, iport->s_fd);
    self->on_error(self, "Unable to attach %s inspector", device_id);
    return DL_SUCCESS;
  }
  iport->device_name = device_name;
  iwdp_iwi_t iwi = iwdp_iwi_new(self->is_debug);
  iwi->iport = iport;
  iport->iwi = iwi;
  if (self->add_fd(self, wi_fd, iwi, false)) {
    self->remove_fd(self, iport->s_fd);
    return self->on_error(self, "add_fd wi_fd=%d failed", wi_fd);
  }
  iwi->wi_fd = wi_fd;

  // start inspector
  wi_new_uuid(&iwi->connection_id);
  wi_t wi = iwi->wi;
  if (wi->send_reportIdentifier(wi, iwi->connection_id)) {
    self->remove_fd(self, iport->s_fd);
    self->on_error(self, "Unable to report to inspector %s",
        device_id);
    return DL_SUCCESS;
  }
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
  wi_new_uuid(&iws->ws_id);
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
  char *device_id = iport->device_id;
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
    iwi->iport = NULL;
    iport->iwi = NULL;
    if (iwi->wi_fd > 0) {
      self->remove_fd(self, iwi->wi_fd);
    }
  }
  // keep iport so we can restore the port if this device is reattached
  iport->s_fd = -1;
  return IWDP_SUCCESS;
}

iwdp_status iwdp_iws_close(iwdp_t self, iwdp_iws_t iws) {
  // clear pointer to this iws
  iwdp_iport_t iport = iws->iport;
  if (iport) {
    ht_t iws_ht = iport->ws_id_to_iws;
    char *ws_id = iws->ws_id;
    iwdp_iws_t iws2 = (iwdp_iws_t)ht_get_value(iws_ht, ws_id);
    if (ws_id && iws2 == iws) {
      ht_remove(iws_ht, ws_id);
    } // else internal error?
  }
  iwdp_ipage_t ipage = iws->ipage;
  if (ipage) {
    if (ipage->sender_id && ipage->iws == iws) {
      iwdp_stop_devtools(ipage);
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
  iwdp_private_t my = self->private_state;
  // clear pointer to this iwi
  iwdp_iport_t iport = iwi->iport;
  if (iport && iport->iwi) {
    iport->iwi = NULL;
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
  return self->send(self, iws->ws_fd, data, length);
}

ws_status iwdp_on_list_request(ws_t ws, bool is_head, bool want_json) {
  iwdp_iws_t iws = (iwdp_iws_t)ws->state;
  iwdp_iport_t iport = iws->iport;
  char *content;
  if (iport->device_id) {
    ht_t ipage_ht = (iport->iwi ? iport->iwi->page_num_to_ipage : NULL);
    iwdp_ipage_t *ipages = (iwdp_ipage_t *)ht_values(ipage_ht);
    content = iwdp_ipages_to_text(ipages, want_json,
        iport->device_id, iport->device_name, NULL, NULL,
        iport->port);
    free(ipages);
  } else {
    iwdp_iport_t *iports = (iwdp_iport_t *)ht_values(
        iport->self->private_state->device_id_to_iport);
    content = iwdp_iports_to_text(iports, want_json, NULL);
    free(iports);
  }
  char *data;
  asprintf(&data,
      "HTTP/1.1 200 OK\r\n"
      "Content-length: %zd\r\n"
      "Connection: close\r\n"
      "Content-Type: %s\r\n"
      "\r\n%s",
      strlen(content),
      (want_json ? "application/json" : "text/html; charset=UTF-8"),
      (is_head ? "" : content));
  free(content);
  ws_status ret = ws->send_data(ws, data, strlen(data));
  free(data);
  return ret;
}

ws_status iwdp_on_not_found(ws_t ws, bool is_head, const char *resource) {
  bool is_html = true; // TODO examine resource extension?
  char *content = NULL;
  if (is_html) {
    asprintf(&content,
        "<html><title>Error 404 (Not Found)</title>\n"
        "<p><b>404.</b> <ins>That's an error.</ins>\n"
        "<p>The requested URL <code>%s</code> was not found.\n"
        "</html>", resource);
  }
  char *data;
  asprintf(&data,
      "HTTP/1.1 404 Not Found\r\n"
      "Content-length: %zd\r\n"
      "Connection: close\r\n"
      "Content-Type: %s\r\n"
      "\r\n%s",
      (content ? strlen(content) : 0),
      "text/html; charset=UTF-8",
      (is_head || !content ? "" : content));
  if (content) {
    free(content);
  }
  ws_status ret = ws->send_data(ws, data, strlen(data));
  free(data);
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
    return iwdp_on_not_found(ws, false, resource);
  }
  return iwdp_start_devtools(p, iws);
}

ws_status iwdp_on_static_request(ws_t ws, bool is_head, const char *resource,
    bool *to_keep_alive) {
  iwdp_iws_t iws = (iwdp_iws_t)ws->state;
  iwdp_t self = iws->iport->self;
  if (!resource || strncmp(resource, "/devtools/", 10)) {
    return self->on_error(self, "Internal error: %s", resource);
  }

  // This proxy is awkward but necessary, since that's what Android's
  // devtools_http_protocol_handler.cc does :(
  //
  // Ideally we could also read Chrome's "resources.pak" file.
  iwdp_private_t my = self->private_state;
  char *hostname = my->frontend_host;
  char *path = my->frontend_path;

  int fs_fd = self->connect(self, hostname, 80);
  if (fs_fd < 0) {
    return self->on_error(self, "Unable to connect to %s", hostname);
  }
  iwdp_ifs_t ifs = iwdp_ifs_new();
  ifs->iws = iws;
  ifs->fs_fd = fs_fd;
  iws->ifs = ifs;
  if (self->add_fd(self, fs_fd, ifs, false)) {
    return self->on_error(self, "Unable to add fd %d", fs_fd);
  }
  char *data;
  asprintf(&data,
      "%s %s%s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Connection: close\r\n" // keep-alive?
      "Accept: */*\r\n"
      "\r\n",
      (is_head ? "HEAD" : "GET"), path, resource + 10, hostname);
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

ws_status iwdp_on_http_request(ws_t ws,
    const char *method, const char *resource, const char *version,
    const char *headers, size_t headers_length, bool is_websocket,
    bool *to_keep_alive) {
  bool is_get = !strcmp(method, "GET");
  bool is_head = !is_get && !strcmp(method, "HEAD");
  if (is_websocket) {
    if (is_get && !strncmp(resource, "/devtools/page/", 15)) {
      return iwdp_on_devtools_request(ws, resource);
    }
  } else {
    if (!is_get && !is_head) {
      return iwdp_on_not_found(ws, is_head, resource);
    }
    if (!strlen(resource) || !strcmp(resource, "/")) {
      return iwdp_on_list_request(ws, is_head, false);
    } else if (!strcmp(resource, "/json")) {
      return iwdp_on_list_request(ws, is_head, true);
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
  return iwdp_on_not_found(ws, is_head, resource);
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
      iwdp_ipage_t ipage = iws->ipage;
      iwdp_iwi_t iwi = iws->iport->iwi;
      if (!ipage || !iwi) {
        return ws->send_close(ws, CLOSE_GOING_AWAY,
            (ipage ? "webinspector closed" : "page closed"));
      }
      wi_t wi = iwi->wi;
      return wi->send_forwardSocketData(wi,
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
  return self->send(self, iwi->wi_fd, packet, length);
}

wi_status iwdp_on_reportSetup(wi_t wi) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)wi->state;
  return WI_SUCCESS;
}

wi_status iwdp_add_app_id(wi_t wi, const char *app_id) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)wi->state;
  ht_t app_id_ht = iwi->app_id_to_true;
  if (ht_get_value(app_id_ht, app_id)) {
    return WI_SUCCESS;
  }
  ht_put(app_id_ht, strdup(app_id), HT_VALUE(1));
  return wi->send_forwardGetListing(wi, iwi->connection_id, app_id);
}
wi_status iwdp_on_applicationConnected(wi_t wi, const wi_app_t app) {
  return iwdp_add_app_id(wi, app->app_id);
}

ws_status iwdp_start_devtools(iwdp_ipage_t ipage, iwdp_iws_t iws) {
  if (!ipage || !iws) {
    return WS_ERROR;
  }
  if (ipage->iws) {
    // abort our other client, as if the page went away
    iwdp_stop_devtools(ipage);
  }
  iws->ipage = ipage;
  ipage->iws = iws;
  ipage->sender_id = strdup(iws->ws_id);
  iwdp_iwi_t iwi = iws->iport->iwi;
  wi_t wi = iwi->wi;
  return wi->send_forwardSocketSetup(wi,
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
  if (iwi) {
    wi_t wi = iwi->wi;
    wi_status ret = wi->send_forwardDidClose(wi,
        ipage->connection_id, ipage->app_id,
        ipage->page_id, ipage->sender_id);
  }
  // close the ws_fd?
  iws->ipage = NULL;
  ipage->iws = NULL;
  ipage->sender_id = NULL;
  free(sender_id);
  return WS_SUCCESS;
}

wi_status iwdp_remove_app_id(wi_t wi, const char *app_id) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)wi->state;
  ht_t app_id_ht = iwi->app_id_to_true;
  char *old_app_id = ht_get_key(app_id_ht, app_id);
  if (!old_app_id) {
    return WI_SUCCESS;
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
  return WI_SUCCESS;
}
wi_status iwdp_on_applicationDisconnected(wi_t wi, const wi_app_t app) {
  return iwdp_remove_app_id(wi, app->app_id);
}

wi_status iwdp_on_reportConnectedApplicationList(wi_t wi, const wi_app_t *apps) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)wi->state;
  ht_t app_id_ht = iwi->app_id_to_true;

  // remove old apps
  char **old_app_ids = (char **)ht_keys(app_id_ht);
  char **oa;
  for (oa = old_app_ids; *oa; oa++) {
    const wi_app_t *a;
    for (a = apps; *a && strcmp((*a)->app_id, *oa); a++) {
    }
    if (!*a) {
      iwdp_remove_app_id(wi, *oa);
    }
  }
  free(old_app_ids);

  // add new apps
  const wi_app_t *a;
  for (a = apps; *a; a++) {
    iwdp_add_app_id(wi, (*a)->app_id);
  }
  return WI_SUCCESS;
}

wi_status iwdp_on_applicationSentListing(wi_t wi,
    const char *app_id, const wi_page_t *pages) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)wi->state;
  if (!ht_get_value(iwi->app_id_to_true, app_id)) {
    return wi->on_error(wi, "Unknown app_id %s", app_id);;
  }
  ht_t ipage_ht = iwi->page_num_to_ipage;
  iwdp_ipage_t *ipages = (iwdp_ipage_t *)ht_values(ipage_ht);

  // add new pages
  const wi_page_t *pp;
  for (pp = pages; *pp; pp++) {
    const wi_page_t page = *pp;
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
    if (page->connection_id && 
        (!ipage->connection_id ||
         strcmp(ipage->connection_id, page->connection_id)) &&
        ipage->iws) {
      // Verify that this iwdp_open_devtools ack is us
      iwdp_iwi_t iwi = ipage->iws->iport->iwi;
      char *iwdp_connection_id = (iwi ? iwi->connection_id : NULL);
      if (!iwdp_connection_id || strcmp(
            iwdp_connection_id, page->connection_id)) {
        // Some other client stole our page?
      }
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
    const wi_page_t *pp;
    for (pp = pages; *pp && (*pp)->page_id != ipage->page_id; pp++) {
    }
    if (!*pp) {
      iwdp_stop_devtools(ipage);
      ht_remove(ipage_ht, HT_KEY(ipage->page_num));
      iwdp_ipage_free(ipage);
    }
  }
  free(ipages);

  return WI_SUCCESS;
}

wi_status iwdp_on_applicationSentData(wi_t wi,
    const char *app_id, const char *dest_id,
    const char *data, const size_t length) {
  iwdp_iport_t iport = ((iwdp_iwi_t)wi->state)->iport;
  iwdp_iws_t iws = ht_get_value(iport->ws_id_to_iws, dest_id);
  if (!iws) {
    // error but don't kill the inspector!
    return WI_SUCCESS;
  }
  ws_t ws = iws->ws;
  return ws->send_frame(ws,
      true, OPCODE_TEXT, false,
      data, length);
}

//
// STRUCTS
//

void iwdp_free(iwdp_t self) {
  if (self) {
    iwdp_private_t my = self->private_state;
    if (my) {
      ht_free(my->device_id_to_iport);
      memset(my, 0, sizeof(struct iwdp_private));
      free(my);
    }
    memset(self, 0, sizeof(struct iwdp_struct));
    free(self);
  }
}
iwdp_t iwdp_new() {
  iwdp_t self = (iwdp_t)malloc(sizeof(struct iwdp_struct));
  iwdp_private_t my = (iwdp_private_t)malloc(sizeof(struct iwdp_private));
  if (!self || !my) {
    free(self);
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
  my->device_id_to_iport = ht_new(HT_STRING_KEYS);
  my->frontend_host = "chrome-devtools-frontend.appspot.com";
  my->frontend_path = "/static/18.0.1025.74/";
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
        asprintf(&s,
            "%s{\n"
            "   \"deviceId\": \"%s\",\n"
            "   \"deviceName\": \"%s\",\n"
            "   \"url\": \"%s:%d\"\n"
            "}",
            (sum_len ? "," : ""), iport->device_id,
            (iport->device_name ? iport->device_name : ""),
            (host ? host : "localhost"), iport->port);
      }
    } else {
      char *href = NULL;
      if (iport->iwi) {
        asprintf(&href, " href=\"http://%s:%d/\"",
            (host ? host : "localhost"), iport->port);
      }
      asprintf(&s,
          "<li><a%s>%s:%d</a> - <a title=\"%s\">%s</a></li>\n",
          (href ? href : ""), (host ? host : "localhost"),
          iport->port, iport->device_id,
          (iport->device_name ? iport->device_name : "?"));
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
    tail = stpcpy(tail, header);
    for (item = items; *item; item++) {
      tail = stpcpy(tail, *item);
      free(*item);
    }
    tail = stpcpy(tail, footer);
  }
  free(items);
  return ret;
}

void iwdp_iwi_free(iwdp_iwi_t iwi) {
  if (iwi) {
    wi_free(iwi->wi);
    // TODO free ht_values?
    free(iwi->connection_id);
    ht_free(iwi->app_id_to_true);
    ht_free(iwi->page_num_to_ipage);
    memset(iwi, 0, sizeof(struct iwdp_iwi_struct));
    free(iwi);
  }
}
iwdp_iwi_t iwdp_iwi_new(bool *is_debug) {
  iwdp_iwi_t iwi = (iwdp_iwi_t)malloc(sizeof(struct iwdp_iwi_struct));
  if (!iwi) {
    return NULL;
  }
  memset(iwi, 0, sizeof(struct iwdp_iwi_struct));
  iwi->type.type = TYPE_IWI;
  iwi->app_id_to_true = ht_new(HT_STRING_KEYS);
  iwi->page_num_to_ipage = ht_new(HT_INT_KEYS);
  wi_t wi = wi_new();
  if (!wi || !iwi->page_num_to_ipage || !iwi->app_id_to_true) {
    iwdp_iwi_free(iwi);
    return NULL;
  }
  iwi->wi = wi;
  wi->send_packet = iwdp_send_packet;
  wi->on_reportSetup = iwdp_on_reportSetup;
  wi->on_reportConnectedApplicationList = 
    iwdp_on_reportConnectedApplicationList;
  wi->on_applicationConnected = iwdp_on_applicationConnected;
  wi->on_applicationDisconnected = iwdp_on_applicationDisconnected;
  wi->on_applicationSentListing = iwdp_on_applicationSentListing;
  wi->on_applicationSentData = iwdp_on_applicationSentData;
  wi->state = iwi;
  wi->is_debug = is_debug;
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
    const char *frontend, const char *host, int port) {
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
    asprintf(&href, "%s?host=%s:%d&page=%d",
        (frontend ? frontend : "/devtools/devtools.html"),
        (host ? host : "localhost"), port, ipage->page_num);
    char *s = NULL;
    if (want_json) {
      asprintf(&s, 
          "%s{\n"
          "   \"devtoolsFrontendUrl\": \"%s\",\n"
          "   \"faviconUrl\": \"\",\n"
          "   \"thumbnailUrl\": \"/thumb/%s\",\n"
          "   \"title\": \"%s\",\n"
          "   \"url\": \"%s\",\n"
          "   \"webSocketDebuggerUrl\": \"ws://%s:%d/devtools/page/%d\"\n"
          "}",
          (sum_len ? "," : ""), (ipage->iws ? "" : href),
          (ipage->url ? ipage->url : ""),
          (ipage->title ? ipage->title : ""),
          (ipage->url ? ipage->url : ""),
          (host ? host : "localhost"), port, ipage->page_num);
    } else {
      asprintf(&s,
          "<li value=\"%d\"><a %s=\"%s\" title=\"%s\">%s</a></li>\n",
          ipage->page_num,
          (ipage->iws ? "alt" : "href"), href,
          (ipage->title ? ipage->title : "?"),
          (ipage->url ? ipage->url : "?")); // encodeURI?
    }
    free(href);
    if (s) {
      sum_len += strlen(s);
      *item++ = s;
    }
  }

  char *header = NULL;
  if (want_json) {
    header = "[";
  } else {
    asprintf(&header,
        "<html><head><title>%s</title></head>"
        "<body>Inspectable pages for <a title=\"%s\">%s</a>:<p><ol>\n",
        device_name, device_id, device_name);
  }
  const char *footer = (want_json ? "]" : "</ol></body></html>");

  // concat
  size_t length = strlen(header) + sum_len + strlen(footer);
  char *ret = (char *)calloc(length+1, sizeof(char));
  if (ret) {
    char *tail = ret;
    tail = stpcpy(tail, header);
    for (item = items; *item; item++) {
      tail = stpcpy(tail, *item);
      free(*item);
    }
    tail = stpcpy(tail, footer);
  }
  if (!want_json) {
    free(header);
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


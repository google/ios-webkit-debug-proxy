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

#ifdef __MACH__
#include <uuid/uuid.h>
#endif

#include "webinspector.h"


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
}

//
// STRUCTS
//

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


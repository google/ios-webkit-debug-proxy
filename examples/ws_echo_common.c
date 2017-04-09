// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

//
// A minimal websocket "echo" server
//

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "ws_echo_common.h"

// websocket callbacks:

ws_status send_data(ws_t ws, const char *data, size_t length) {
  int fd = ((my_t)ws->state)->fd;
  ssize_t sent_bytes = send(fd, (void*)data, length, 0);
  return (sent_bytes == length ? WS_SUCCESS : WS_ERROR);
}

char *create_root_response(int port, int count) {
  char *html = NULL;
  if (asprintf(&html,
      "<html><head><script type=\"text/javascript\">\n"
      "function WebSocketTest() {\n"
      "  if (\"WebSocket\" in window) {\n"
      "    var ws = new WebSocket(\"ws://localhost:%d/\");\n"
      "    var count = %d;\n"
      "    ws.onopen = function() {\n"
      "      alert(\"Sending \"+count);\n"
      "      ws.send(\"count[\"+count+\"]\");\n"
      "    };\n"
      "    ws.onmessage = function (evt) {\n"
      "      alert(\"Received (\"+evt.data+\"), sending \"+\n"
      "           (count > 1 ? (count-1) : \"close\"));\n"
      "      if (count > 1) {\n"
      "        ws.send(\"count[\"+(--count)+\"]\");\n"
      "      } else {\n"
      "        ws.close();\n"
      "      }\n"
      "    };\n"
      "    ws.onclose = function() { alert(\"Closed\"); };\n"
      "    ws.onerror = function(e) { alert(\"Error: \"+e.data); };\n"
      "  } else {\n"
      "    alert(\"WebSocket NOT supported by your Browser!\");\n"
      "  }\n"
      "}\n"
      "</script></head><body><div id=\"sse\">\n"
      "  <a href=\"javascript:WebSocketTest()\">Run WebSocket</a>\n"
      "</div></body></html>\n", port, count) < 0) {
    return NULL;  // asprintf failed
  }
  char *ret = NULL;
  if (asprintf(&ret,
      "HTTP/1.1 200 OK\r\n"
      "Content-length: %zd\r\n"
      "Connection: close\r\n"
      "Content-Type: text/html; charset=UTF-8\r\n"
      "\r\n%s",
      (html ? strlen(html) : 0), html) < 0) {
    return NULL;  // asprintf failed
  }
  free(html);
  return ret;
}

ws_status on_http_request(ws_t ws,
    const char *method, const char *resource, const char *version,
    const char *headers, size_t headers_length, bool is_websocket,
    bool *to_keep_alive) {
  if (strcmp(method, "GET") || strcmp(resource, "/")) {
    return WS_ERROR;
  }
  if (!is_websocket) {
    char *data = create_root_response(((my_t)ws->state)->port, 3);
    ws_status ret = ws->send_data(ws, data, strlen(data));
    free(data);
    return ret;
  }
  return WS_SUCCESS;
}

ws_status on_upgrade(ws_t ws,
    const char *resource, const char *protocol,
    int version, const char *sec_key) {
  return ws->send_upgrade(ws);
}

ws_status on_frame(ws_t ws,
    bool is_fin, uint8_t opcode, bool is_masking,
    const char *payload_data, size_t payload_length,
    bool *to_keep) {
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
      // echo
      return ws->send_frame(ws,
          true, opcode, false,
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

// struct:

my_t my_new(int fd, int port) {
  my_t my = (my_t)malloc(sizeof(struct my_struct));
  ws_t ws = ws_new();
  if (!ws || !my) {
    free(ws);
    return NULL;
  }
  memset(my, 0, sizeof(struct my_struct));
  my->fd = fd;
  my->port = port;
  my->ws = ws;
  ws->send_data = send_data;
  ws->on_http_request = on_http_request;
  ws->on_upgrade = on_upgrade;
  ws->on_frame = on_frame;
  ws->state = my;
  return my;
}

void my_free(my_t my) {
  if (my) {
    ws_free(my->ws);
    memset(my, 0, sizeof(struct my_struct));
    free(my);
  }
}


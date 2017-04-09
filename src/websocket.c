// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "websocket.h"
#include "char_buffer.h"

#include "base64.h"
#include "sha1.h"

#include "validate_utf8.h"
#include "strndup.h"
#include "strcasestr.h"

typedef int8_t ws_state;
#define STATE_ERROR 1
#define STATE_READ_HTTP_REQUEST 2
#define STATE_READ_HTTP_HEADERS 3
#define STATE_KEEP_ALIVE 4
#define STATE_READ_FRAME_LENGTH 5
#define STATE_READ_FRAME 6
#define STATE_CLOSED 7


struct ws_private {
  ws_state state;

  cb_t in;
  cb_t out;
  cb_t data;

  char *method;
  char *resource;
  char *http_version;

  char *protocol;
  int version;
  char *sec_key;
  bool is_websocket;

  char *sec_answer;

  size_t needed_length;
  size_t frame_length;

  uint8_t continued_opcode;
  bool sent_close;
};


ws_status ws_on_error(ws_t self, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  return WS_ERROR;
}

ws_status ws_on_debug(ws_t self, const char *message,
    const char *buf, size_t length) {
  //ws_private_t my = self->private_state;
  if (self->is_debug && *self->is_debug) {
    char *text;
    cb_asprint(&text, buf, length, 80, 50);
    printf("%s[%zd]:\n%s\n", message, length, text);
    free(text);
  }
  return WS_SUCCESS;
}

//
// SEND
//

static char *ws_compute_answer(const char *sec_key) {
  if (!sec_key) {
    return NULL;
  }

  // concat sec_key + magic
  static char *MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  size_t text_length = (strlen(sec_key) + strlen(MAGIC) + 1);
  char *text = (char *)malloc(text_length * sizeof(char));
  if (!text) {
    return NULL;
  }
  sprintf(text, "%s%s", sec_key, MAGIC);

  // SHA-1 hash
  unsigned char hash[20];
  sha1_context ctx;
  sha1_starts(&ctx);
  sha1_update(&ctx, (const unsigned char *)text, text_length-1);
  sha1_finish(&ctx, hash);
  free(text);
  text = NULL;

  // base64 encode
  size_t length = 0;
  base64_encode(NULL, &length, NULL, 20);
  char *ret = (char *)malloc(length);
  if (!ret) {
    return NULL;
  }
  if (base64_encode((unsigned char *)ret, &length, hash, 20)) {
    free(ret);
    return NULL;
  }

  return ret;
}

void ws_random_buf(char *buf, size_t len) {
#ifdef __MACH__
  arc4random_buf(buf, len);
#else
  static bool seeded = false;
  if (!seeded) {
    seeded = true;
    // could fread from /dev/random
    srand(time(NULL));
  }
  size_t i;
  for (i = 0; i < len; i++) {
    buf[i] = (char)rand();
  }
#endif
}

ws_status ws_send_connect(ws_t self,
    const char *resource, const char *protocol,
    const char *host, const char *origin) {
  ws_private_t my = self->private_state;

  if (!resource) {
    return self->on_error(self, "Null arg");
  }

  char sec_ukey[20];
  ws_random_buf(sec_ukey, 20);
  size_t key_length = 0;
  base64_encode(NULL, &key_length, NULL, 20);
  char *sec_key = (char *)malloc(key_length);
  if (!sec_key) {
    return self->on_error(self, "Out of memory");
  }
  if (base64_encode((unsigned char *)sec_key, &key_length,
      (const unsigned char *)sec_ukey, 20)) {
    free(sec_key);
    return self->on_error(self, "base64_encode failed");
  }

  size_t needed = (1024 + strlen(resource) + strlen(sec_key) +
      (protocol ? strlen(protocol) : 0) +
      (host ? strlen(host) : 0) +
      (origin ? strlen(origin) : 0));
  cb_clear(my->out);
  if (cb_ensure_capacity(my->out, needed)) {
    return self->on_error(self, "Output %zd exceeds buffer capacity",
        needed);
  }
  char *out_tail = my->out->tail;

  out_tail += sprintf(out_tail,
      "GET %s HTTP/1.1\r\n"
      "Upgrade: WebSocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Key: %s\r\n", resource, sec_key);
  if (protocol) {
    out_tail += sprintf(out_tail, "Sec-WebSocket-Protocol: %s\r\n",
        protocol);
  }
  if (host) {
    out_tail += sprintf(out_tail, "Host: %s\r\n", host);
  }
  if (origin) {
    out_tail += sprintf(out_tail, "Origin: %s\r\n", origin);
  }
  out_tail += sprintf(out_tail, "\r\n");

  size_t out_length = out_tail - my->out->tail;
  ws_on_debug(self, "ws.send_connect", my->out->tail, out_length);
  ws_status ret = self->send_data(self, my->out->tail, out_length);
  my->out->tail = out_tail;
  return ret;
}

ws_status ws_send_upgrade(ws_t self) {
  ws_private_t my = self->private_state;

  if (!my->resource) {
    return self->on_error(self, "Missing HTTP resource");
  }
  if (!my->sec_key) {
    return self->on_error(self, "Missing WebSocket headers");
  }

  my->sec_answer = ws_compute_answer(my->sec_key);
  if (!my->sec_answer) {
    return self->on_error(self, "Unable to compute answer for %s",
        my->sec_key);
  }

  size_t needed = (1024 + strlen(my->sec_answer) +
      (my->protocol ? strlen(my->protocol) : 0));
  cb_clear(my->out);
  if (cb_ensure_capacity(my->out, needed)) {
    return self->on_error(self, "Out of memory");
  }
  char *out_tail = my->out->tail;

  out_tail += sprintf(out_tail,
      "HTTP/1.1 101 WebSocket Protocol Handshake\r\n"
      "Upgrade: WebSocket\r\n"
      "Connection: Upgrade\r\n");
  if (my->protocol) {
    out_tail += sprintf(out_tail, "Sec-WebSocket-Protocol: %s\r\n",
        my->protocol);
  }
  out_tail += sprintf(out_tail, "Sec-WebSocket-Accept: %s\r\n",
      my->sec_answer);
  out_tail += sprintf(out_tail, "\r\n");

  size_t out_length = out_tail - my->out->tail;
  ws_on_debug(self, "ws.sending_upgrade", my->out->tail, out_length);
  ws_status ret = self->send_data(self, my->out->tail, out_length);
  my->out->tail = out_tail;
  return ret;
}

ws_status ws_send_frame(ws_t self,
    bool is_fin, uint8_t opcode, bool is_masking,
    const char *payload_data, size_t payload_length) {
  ws_private_t my = self->private_state;

  if (my->sent_close) {
    return self->on_error(self, "Already sent close_frame");
  }

  if (!payload_data) {
    return self->on_error(self, "Null arg");
  }

  if (opcode != OPCODE_CONTINUATION && opcode != OPCODE_TEXT &&
      opcode != OPCODE_BINARY && opcode != OPCODE_CLOSE &&
      opcode != OPCODE_PING && opcode != OPCODE_PONG) {
    return self->on_error(self, "Invalid opcode 0x%x", opcode);
  }
  bool is_control = (opcode >= OPCODE_CLOSE ? true : false);
  if (is_control) {
    if (!is_fin) {
      return self->on_error(self, "Control 0x%x not fin", opcode);
    }
    if (payload_length > 125) {
      return self->on_error(self, "Control 0x%x payload_length %zd > 125",
          opcode, payload_length);
    }
  }

  uint8_t opcode2 = opcode;
  if (!is_control && my->continued_opcode) {
    if (my->continued_opcode != opcode) {
      return self->on_error(self, "Expecting continue of 0x%x not 0x%x",
          my->continued_opcode, opcode);
    }
    opcode2 = OPCODE_CONTINUATION;
  }

  size_t i;
  bool is_utf8 = (opcode2 == OPCODE_TEXT ? true : false);
  if (is_utf8) {
    unsigned int utf8_state = UTF8_VALID;
    const char *payload_head = payload_data;
    for (i = 0; i < payload_length; i++) {
      unsigned char ch = *payload_head++;
      utf8_state = validate_utf8[utf8_state + ch];
      if (utf8_state == UTF8_INVALID) {
        return self->on_error(self,
            "Invalid %sUTF8 character 0x%x at %zd",
            (is_masking ? "masked " :""), ch,
            payload_head-1 - payload_data);
      }
    }
  }

  int8_t payload_n = (payload_length < 126 ? 0 :
      payload_length < UINT16_MAX ? 2 : 8);

  size_t needed = (2 + payload_n + (is_masking ? 4 : 0) + payload_length);
  cb_clear(my->out);
  if (cb_ensure_capacity(my->out, needed)) {
    return self->on_error(self, "Out of memory");
  }
  char *out_tail = my->out->tail;

  *out_tail++ = ((is_fin ? 0x80 : 0) | (opcode2 & 0x0F));

  *out_tail++ = ((is_masking ? 0x80 : 0) | (!payload_n ? payload_length :
        payload_n == 2 ? 126: 127));


  int8_t j;
  int8_t payload_mem_size = sizeof(payload_length);
  for (j = payload_n - 1; j >= 0; j--) {
    *out_tail++ = j >= payload_mem_size ? 0 : (unsigned char)((payload_length >> (j<<3)) & 0xFF);
  }

  if (is_masking) {
    char mask[4];
    ws_random_buf(mask, 4);
    for (i = 0; i < 4; i++) {
      *out_tail++ = (is_masking ? mask[i] : 0);
    }
    const char *payload_head = payload_data;
    uint32_t mask_offset = 0;
    for (i = 0; i < payload_length; i++) {
      unsigned char ch = *payload_head++;
      ch = (ch ^ mask[mask_offset++ & 3]);
      *out_tail++ = ch;
    }
  } else {
    memcpy(out_tail, payload_data, payload_length);
    out_tail += payload_length;
  }

  if (!is_fin && !my->continued_opcode) {
    my->continued_opcode = opcode;
  }

  size_t out_length = out_tail - my->out->tail;
  ws_on_debug(self, "ws.sending_frame", my->out->tail, out_length);
  ws_status ret = self->send_data(self, my->out->tail, out_length);
  if (!ret && opcode == OPCODE_CLOSE) {
    my->sent_close = true;
  }
  my->out->tail = out_tail;
  return ret;
}

ws_status ws_send_close(ws_t self, ws_close close_code, const char *reason) {
  size_t length = 2 + (reason ? strlen(reason) : 0);
  char *data = (char *)calloc(length+1, sizeof(char));
  if (!data) {
    return WS_ERROR;
  }
  data[0] = ((close_code >> 8) & 0xFF);
  data[1] = (close_code & 0xFF);
  if (reason) {
    strcpy(data+2, reason);
  }
  ws_status ret = self->send_frame(self,
      true, OPCODE_CLOSE, false,
      data, length);
  free(data);
  return ret;
}


//
// RECV
//

ws_status ws_read_http_request(ws_t self) {
  ws_private_t my = self->private_state;
  const char *in_head = my->in->in_head;
  size_t in_length = my->in->in_tail - in_head;

  char *line_end = strnstr(in_head, "\r\n", in_length);
  if (!line_end) {
    return self->on_error(self, "Missing \\r\\n");
  }

  char *trio[3];
  size_t i;
  for (i = 0; i < 3; i++) {
    while (in_head < line_end && *in_head == ' ') {
      in_head++;
    }
    const char *s = in_head;
    while (in_head < line_end && *in_head != ' ') {
      in_head++;
    }
    trio[i] = (s < in_head ? strndup(s, in_head - s) : NULL);
  }
  my->method = trio[0];
  my->resource = trio[1];
  my->http_version = trio[2];

  // Keep the request tail '\r\n', in case our client doesn't send
  // any headers, so ws_recv_headers can simply check for "\r\n\r\n".
  my->in->in_head = line_end;
  if (!my->http_version) {
    return self->on_error(self, "Invalid HTTP header");
  }
  return WS_SUCCESS;
}

ws_status ws_read_http_header(ws_t self,
    char **to_key, char **to_val) {
  ws_private_t my = self->private_state;
  const char *in_head = my->in->in_head;
  size_t in_length = my->in->in_tail - in_head;

  *to_key = NULL;
  *to_val = NULL;
  char *line_end = strnstr(in_head, "\r\n", in_length);
  if (!line_end) {
    return self->on_error(self, "Missing \\r\\n");
  }
  if (in_head < line_end) {
    const char *k_start = in_head;
    if (*k_start == ' ') {
      return self->on_error(self, "TODO header continuation");
    }
    const char *k_end = k_start;
    while (++k_end < line_end && *k_end != ':') {
    }
    const char *v_start = k_end;
    while (++v_start < line_end && *v_start == ' ') {
    }
    const char *v_end = line_end;
    while (v_end > v_start && v_end[-1] == ' ') {
      v_end--;
    }
    *to_key = strndup(k_start, k_end - k_start);
    *to_val = strndup(v_start, v_end - v_start);
  }
  my->in->in_head = line_end + 2;
  return WS_SUCCESS;
}

ws_status ws_read_headers(ws_t self) {
  ws_private_t my = self->private_state;

  bool is_connection = false;
  bool is_upgrade = false;
  while (1) {
    char *key;
    char *val;
    if (ws_read_http_header(self, &key, &val) || !key) {
      break;
    }
    if (!strcasecmp(key, "Connection")) {
      // firefox uses "keep-alive, Upgrade"
      is_connection = (strcasestr(val, "Upgrade") ? 1 : 0);
    } else if (!strcasecmp(key, "Upgrade")) {
      is_upgrade = !strcasecmp(val, "WebSocket");
    } else if (!strcasecmp(key, "Sec-WebSocket-Protocol")) {
      free(my->protocol);
      my->protocol = strdup(val);
    } else if (!strcasecmp(key, "Sec-WebSocket-Version")) {
      my->version = strtol(val, NULL, 0);
    } else if (!strcasecmp(key, "Sec-WebSocket-Key")) {
      free(my->sec_key);
      my->sec_key = strdup(val);
    }
    free(key);
    free(val);
  }

  my->is_websocket = (is_connection && is_upgrade && my->sec_key);
  return WS_SUCCESS;
}

ws_status ws_read_frame_length(ws_t self) {
  ws_private_t my = self->private_state;
  const char *in_head = my->in->in_head;
  size_t in_length = my->in->in_tail - in_head;

  my->needed_length = 0;
  my->frame_length = 0;

  if (in_length < 2) {
    my->needed_length = 2;
    return WS_SUCCESS;
  }

  bool is_fin = ((*in_head & 0x80) ? true : false);
  uint8_t reserved_flags = (*in_head & 0x70);
  uint8_t opcode = (*in_head & 0x0F);
  bool is_control = (opcode >= OPCODE_CLOSE ? true : false);

  // error check
  if (reserved_flags) {
    return self->on_error(self, "Reserved flags 0x%x in 0x%x",
        reserved_flags, *in_head);
  }
  if (opcode != OPCODE_CONTINUATION && opcode != OPCODE_TEXT &&
      opcode != OPCODE_BINARY && opcode != OPCODE_CLOSE &&
      opcode != OPCODE_PING && opcode != OPCODE_PONG) {
    return self->on_error(self, "Unknown opcode 0x%x in 0x%x",
        opcode, *in_head);
  }
  if (is_control && !is_fin) {
    return self->on_error(self, "Control 0x%x not fin", opcode);
  }
  if (opcode == OPCODE_CONTINUATION) {
    if (!my->continued_opcode) {
      return self->on_error(self, "Continue but prev was fin");
    }
  } else if (!is_control && my->continued_opcode) {
    return self->on_error(self,
        "Expecting continue (of 0x%x), not 0x%x",
        my->continued_opcode, opcode);
  }
  in_head++;

  bool is_masking = ((*in_head & 0x80) ? true : false);
  size_t payload_length = (*in_head & 0x7f);
  if (is_control && payload_length > 125) {
    return self->on_error(self,
        "Control 0x%x payload_length %zd > 125",
        opcode, payload_length);
  }
  in_head++;

  uint8_t payload_n = (payload_length < 126 ? 0 :
      payload_length < 127 ? 2 : 8);
  if (in_length < 2 + payload_n) {
    my->needed_length = 2 + payload_n;
    return WS_SUCCESS;
  }
  if (payload_n > 0) {
    uint8_t j;
    payload_length = 0;
    for (j = 0; j < payload_n; j++) {
      payload_length <<= 8;
      payload_length |= (unsigned char)*in_head++;
    }
  }
  my->frame_length = 2 + payload_n + (is_masking ? 4 : 0) + payload_length;

  // don't advance my->in->in_head yet
  return WS_SUCCESS;
}

ws_status ws_read_frame(ws_t self,
    bool *to_is_fin, uint8_t *to_opcode, bool *to_is_masking) {
  ws_private_t my = self->private_state;
  const char *in_head = my->in->in_head;
  size_t in_length = my->in->in_tail - in_head;
  ws_on_debug(self, "ws.recv_frame", in_head, in_length);

  size_t frame_length = my->frame_length;
  if (my->needed_length || !frame_length || in_length < frame_length) {
    return self->on_error(self, "Invalid partial frame");
  }

  // the above "ws_read_frame_length" checks the opcode,
  // control flags, etc, so we won't repeat that here.

  bool is_fin = ((*in_head & 0x80) ? true : false);
  uint8_t opcode = (*in_head & 0x0F);
  //bool is_control = (opcode >= OPCODE_CLOSE ? true : false);

  bool is_continue = (opcode == OPCODE_CONTINUATION ? true : false);
  uint8_t opcode2 = (is_continue ? my->continued_opcode : opcode);
  in_head++;

  bool is_masking = ((*in_head & 0x80) ? true : false);
  size_t payload_length = (*in_head & 0x7f);
  in_head++;

  int payload_n = (payload_length < 126 ? 0 : payload_length < 127 ? 2 : 8);
  if (payload_n > 0) {
    payload_length = frame_length - (2 + (is_masking ? 4 : 0) + payload_n);
    in_head += payload_n;
  }

  // the "on_frame" callback will assert (is_masking == is_client)
  size_t i;
  unsigned char mask[4];
  if (is_masking) {
    is_masking = false;
    for (i = 0; i < 4; i++) {
      if (*in_head) {
        is_masking = true;
      }
      mask[i] = *in_head++;
    }
  }

  if (cb_ensure_capacity(my->data, payload_length)) {
    return self->on_error(self,
        "Payload %zd exceeds buffer capacity", payload_length);
  }
  char *data_tail = my->data->tail;

  // no extension, so no extension data

  if (is_masking) {
    uint32_t mask_offset = 0;
    for (i = 0; i < payload_length; i++) {
      unsigned char ch = *in_head++;
      ch = (ch ^ mask[mask_offset++ & 3]);
      *data_tail++ = ch;
    }
  } else {
    memcpy(data_tail, in_head, payload_length);
    data_tail += payload_length;
  }
  my->in->in_head = in_head;

  bool is_utf8 = (opcode2 == OPCODE_TEXT ? true : false);
  if (is_utf8) {
    unsigned int utf8_state = UTF8_VALID;
    char *dt = my->data->tail;
    for (i = 0; i < payload_length; i++) {
      unsigned char ch = *dt++;
      utf8_state = validate_utf8[utf8_state + ch];
      if (utf8_state == UTF8_INVALID) {
        return self->on_error(self,
            "Invalid %sUTF8 character 0x%x at %zd",
            (is_masking ? "masked " :""), ch,
            dt-1 - my->data->tail);
      }
    }
  }

  *to_is_fin = is_fin;
  *to_opcode = opcode2;
  *to_is_masking = is_masking;
  my->data->tail = data_tail;
  return WS_SUCCESS;
}


ws_state ws_recv_request(ws_t self) {
  ws_private_t my = self->private_state;
  const char *in_head = my->in->in_head;
  size_t in_length = my->in->in_tail - in_head;

  if (!strnstr(in_head, "\r\n", in_length)) {
    // still waiting for header
    return -1;
  }

  if (ws_read_http_request(self)) {
    return STATE_ERROR;
  }

  return STATE_READ_HTTP_HEADERS;
}

ws_state ws_recv_headers(ws_t self) {
  ws_private_t my = self->private_state;
  const char *in_head = my->in->in_head;
  size_t in_length = my->in->in_tail - in_head;

  if (!strnstr(in_head, "\r\n\r\n", in_length)) {
    return -1;
  }
  my->in->in_head += 2; // skip the request tail

  if (ws_read_headers(self)) {
    return STATE_ERROR;
  }

  bool keep_alive = false;
  if (self->on_http_request(self, my->method, my->resource,
        my->http_version, in_head, my->in->head - in_head,
        my->is_websocket, &keep_alive)) {
    return STATE_ERROR;
  }
  if (!my->is_websocket) {
    // keep-alive assumes no content-length!
    return (keep_alive ? STATE_READ_HTTP_REQUEST : STATE_CLOSED);
  }

  if (self->on_upgrade(self,
        my->resource, my->protocol,
        my->version, my->sec_key)) {
    return STATE_ERROR;
  }

  return STATE_READ_FRAME_LENGTH;
}

ws_state ws_recv_frame_length(ws_t self) {
  ws_private_t my = self->private_state;

  if (ws_read_frame_length(self)) {
    return STATE_ERROR;
  }
  if (my->needed_length || !my->frame_length) {
    return -1;
  }
  return STATE_READ_FRAME;
}

ws_state ws_recv_frame(ws_t self) {
  ws_private_t my = self->private_state;

  if (my->needed_length || !my->frame_length ||
      my->in->in_tail - my->in->in_head < my->frame_length) {
    return -1;
  }

  bool is_fin;
  uint8_t opcode;
  bool is_masking;
  if (ws_read_frame(self, &is_fin, &opcode, &is_masking)) {
    return STATE_ERROR;
  }

  bool should_keep = 1;
  if (self->on_frame(self, is_fin, opcode, is_masking,
        my->data->begin, my->data->tail - my->data->begin,
        &should_keep)) {
    return STATE_ERROR;
  }
  if (is_fin || !should_keep) {
    cb_clear(my->data);
  }

  if (is_fin) {
    my->continued_opcode = 0;
  } else if (opcode != OPCODE_CONTINUATION) {
    my->continued_opcode = opcode;
  }

  if (opcode == OPCODE_CLOSE) {
    return STATE_CLOSED;
  }
  return STATE_READ_FRAME_LENGTH;
}

ws_status ws_recv_loop(ws_t self) {
  ws_private_t my = self->private_state;
  while (1) {
    ws_state new_state;
    switch (my->state) {
      case STATE_READ_HTTP_REQUEST:
        new_state = ws_recv_request(self);
        break;

      case STATE_READ_HTTP_HEADERS:
        new_state = ws_recv_headers(self);
        break;

      case STATE_KEEP_ALIVE:
        // discard non-ws content
        my->in->in_tail = my->in->in_head;
        new_state = -1;
        break;

      case STATE_READ_FRAME_LENGTH:
        new_state = ws_recv_frame_length(self);
        break;

      case STATE_READ_FRAME:
        new_state = ws_recv_frame(self);
        break;

      case STATE_CLOSED:
      case STATE_ERROR:
      default:
        return WS_ERROR;
    }
    if (new_state < 0) {
      return WS_SUCCESS;
    }
    my->state = new_state;
    if (new_state == STATE_CLOSED || new_state == STATE_ERROR) {
      return WS_ERROR;
    }
    if (my->in->in_tail == my->in->in_head) {
      return WS_SUCCESS;
    }
  }
}

ws_status ws_on_recv(ws_t self, const char *buf, ssize_t length) {
  ws_private_t my = self->private_state;
  if (length < 0) {
    return WS_ERROR;
  } else if (length == 0) {
    return WS_SUCCESS;
  }
  ws_on_debug(self, "ws.recv", buf, length);
  if (cb_begin_input(my->in, buf, length)) {
    return self->on_error(self, "begin_input buffer error");
  }
  ws_status ret = ws_recv_loop(self);
  if (cb_end_input(my->in)) {
    return self->on_error(self, "end_input buffer error");
  }
  return ret;
}


//
// STRUCTS
//

ws_private_t ws_private_new() {
  ws_private_t my = (ws_private_t)malloc(sizeof(struct ws_private));
  if (my) {
    memset(my, 0, sizeof(struct ws_private));
    my->in = cb_new();
    my->out = cb_new();
    my->data = cb_new();
    my->state = STATE_READ_HTTP_REQUEST;
  }
  return my;
}
void ws_private_free(ws_private_t my) {
  if (my) {
    cb_free(my->in);
    cb_free(my->out);
    cb_free(my->data);
    free(my->method);
    free(my->resource);
    free(my->http_version);
    free(my->protocol);
    free(my->sec_key);
    free(my->sec_answer);
    memset(my, 0, sizeof(struct ws_private));
    free(my);
  }
}

ws_t ws_new() {
  ws_private_t my = ws_private_new();
  if (!my) {
    return NULL;
  }
  ws_t self = (ws_t)malloc(sizeof(struct ws_struct));
  if (!self) {
    ws_private_free(my);
    return NULL;
  }
  memset(self, 0, sizeof(struct ws_struct));
  self->send_connect = ws_send_connect;
  self->send_upgrade = ws_send_upgrade;
  self->send_frame = ws_send_frame;
  self->send_close = ws_send_close;
  self->on_recv = ws_on_recv;
  self->on_error = ws_on_error;
  self->private_state = my;
  return self;
}
void ws_free(ws_t self) {
  if (self) {
    ws_private_free(self->private_state);
    memset(self, 0, sizeof(struct ws_struct));
    free(self);
  }
}


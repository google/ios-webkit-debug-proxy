// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2026 Google Inc.

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "char_buffer.h"
#include "websocket.h"

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
          #cond);                                                       \
      exit(1);                                                          \
    }                                                                   \
  } while (0)

struct test_capture {
  int error_count;

  uint8_t sent_hdr[16];
  size_t sent_total_len;
  uint8_t sent_opcodes[8];
  bool sent_fins[8];
  size_t sent_count;

  uint8_t *recv_buf;
  size_t recv_len;
  uint8_t recv_opcode;
  bool recv_fin;
  bool recv_is_masking;
  size_t recv_count;
};

static ws_status test_on_error(ws_t self, const char *format, ...) {
  (void)format;
  struct test_capture *cap = (struct test_capture *)self->state;
  cap->error_count++;
  return WS_ERROR;
}

static ws_status test_send_data(ws_t self, const char *data, size_t length) {
  struct test_capture *cap = (struct test_capture *)self->state;
  if (length >= 2 && cap->sent_count < 8) {
    cap->sent_fins[cap->sent_count] = ((uint8_t)data[0] & 0x80) != 0;
    cap->sent_opcodes[cap->sent_count] = (uint8_t)data[0] & 0x0f;
    cap->sent_count++;
  }
  size_t hdr_copy = (length < sizeof(cap->sent_hdr) ? length : sizeof(cap->sent_hdr));
  if (hdr_copy > 0) {
    memcpy(cap->sent_hdr, data, hdr_copy);
  }
  cap->sent_total_len = length;
  return WS_SUCCESS;
}

static ws_status test_on_http(ws_t self, const char *method,
    const char *resource, const char *version, const char *host,
    const char *headers, size_t headers_length, bool is_websocket,
    bool *to_keep_alive) {
  (void)self;
  (void)method;
  (void)resource;
  (void)version;
  (void)host;
  (void)headers;
  (void)headers_length;
  (void)is_websocket;
  (void)to_keep_alive;
  return WS_SUCCESS;
}

static ws_status test_on_upgrade(ws_t self, const char *resource,
    const char *protocol, int version, const char *sec_key) {
  (void)resource;
  (void)protocol;
  (void)version;
  (void)sec_key;
  return self->send_upgrade(self);
}

static ws_status test_on_frame(ws_t self, bool is_fin, ws_opcode opcode,
    bool is_masking, const char *payload_data, size_t payload_length,
    bool *to_keep) {
  struct test_capture *cap = (struct test_capture *)self->state;
  if (!is_fin) {
    *to_keep = true;
    return WS_SUCCESS;
  }
  free(cap->recv_buf);
  cap->recv_buf = payload_length ? (uint8_t *)malloc(payload_length) : NULL;
  if (payload_length) {
    CHECK(cap->recv_buf != NULL);
    memcpy(cap->recv_buf, payload_data, payload_length);
  }
  cap->recv_len = payload_length;
  cap->recv_opcode = opcode;
  cap->recv_fin = is_fin;
  cap->recv_is_masking = is_masking;
  cap->recv_count++;
  return WS_SUCCESS;
}

static ws_t create_upgraded_ws(struct test_capture *cap) {
  memset(cap, 0, sizeof(*cap));
  ws_t ws = ws_new();
  CHECK(ws != NULL);
  ws->state = cap;
  ws->on_error = test_on_error;
  ws->send_data = test_send_data;
  ws->on_http_request = test_on_http;
  ws->on_upgrade = test_on_upgrade;
  ws->on_frame = test_on_frame;

  const char *req =
      "GET /devtools/page/1 HTTP/1.1\r\n"
      "Host: localhost:9222\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";
  CHECK(ws->on_recv(ws, req, (ssize_t)strlen(req)) == WS_SUCCESS);
  cap->sent_count = 0;
  cap->sent_total_len = 0;
  return ws;
}

static void destroy_ws(ws_t ws, struct test_capture *cap) {
  ws_free(ws);
  free(cap->recv_buf);
  cap->recv_buf = NULL;
}

static void write_be64(uint8_t *dst, uint64_t val) {
  for (int i = 7; i >= 0; i--) {
    dst[7 - i] = (uint8_t)((val >> (i * 8)) & 0xff);
  }
}

static void test_char_buffer_overflow(void) {
  cb_t cb = cb_new();
  CHECK(cb != NULL);
  CHECK(cb_ensure_capacity(cb, 64) == 0);
  cb->tail += 16;
  // Previously (used + needed) wrapped around SIZE_MAX and clamped to 1536.
  CHECK(cb_ensure_capacity(cb, SIZE_MAX - 3) == -1);
  CHECK(cb_ensure_capacity(cb, SIZE_MAX - 15) == -1);
  cb_free(cb);
}

static void test_frame_length_overflow_single_and_fragmented(void) {
  struct test_capture cap;
  ws_t ws = create_upgraded_ws(&cap);

  // Two-frame heap overflow trigger (non-FIN fragment + wrapped length).
  uint8_t frame1[2 + 4 + 16] = {
      0x02,       // FIN=0, BINARY
      0x80 | 16,  // MASK=1, len=16
      0x11, 0x22, 0x33, 0x44,
  };
  memset(frame1 + 6, 'A', 16);
  CHECK(ws->on_recv(ws, (const char *)frame1, sizeof(frame1)) == WS_SUCCESS);

  uint8_t frame2[64];
  memset(frame2, 'B', sizeof(frame2));
  frame2[0] = 0x80;        // FIN=1, CONTINUATION
  frame2[1] = 0x80 | 127;  // MASK=1, 8-byte length
  write_be64(frame2 + 2, UINT64_MAX - 3);
  frame2[10] = 0x01;
  frame2[11] = 0x02;
  frame2[12] = 0x03;
  frame2[13] = 0x04;
  CHECK(ws->on_recv(ws, (const char *)frame2, sizeof(frame2)) == WS_ERROR);
  CHECK(cap.error_count == 1);
  destroy_ws(ws, &cap);

  // Reject MSB-set 64-bit length (RFC 6455 §5.2), 32-bit-wrapping 4 GiB+10,
  // and >64 MiB single frames.
  const uint64_t bad_lengths[] = {
      (uint64_t)1 << 63,
      ((uint64_t)1 << 32) + 10,
      ((uint64_t)64 << 20) + 1,
  };
  for (size_t i = 0; i < sizeof(bad_lengths) / sizeof(bad_lengths[0]); i++) {
    ws = create_upgraded_ws(&cap);
    uint8_t hdr[24] = {0x82, 0x80 | 127};
    write_be64(hdr + 2, bad_lengths[i]);
    CHECK(ws->on_recv(ws, (const char *)hdr, sizeof(hdr)) == WS_ERROR);
    destroy_ws(ws, &cap);
  }
}

static void test_fragment_accumulation_limit_and_control_bypass(void) {
  struct test_capture cap;
  ws_t ws = create_upgraded_ws(&cap);

  // Buffer a full 64 MiB first fragment (right at WS_MAX_MESSAGE_PAYLOAD).
  size_t frag1_len = (size_t)64 << 20;
  uint8_t *frag1 = (uint8_t *)calloc(1, 14 + frag1_len);
  CHECK(frag1 != NULL);
  frag1[0] = 0x02;        // FIN=0, BINARY
  frag1[1] = 0x80 | 127;  // MASK=1, 8-byte len
  write_be64(frag1 + 2, frag1_len);
  CHECK(ws->on_recv(ws, (const char *)frag1, (ssize_t)(14 + frag1_len)) ==
         WS_SUCCESS);
  free(frag1);

  // Interleaved PING must still be accepted even with 64 MiB buffered.
  uint8_t ping[2 + 4 + 2] = {0x89, 0x82, 0x01, 0x02, 0x03, 0x04, 'o' ^ 0x01, 'k' ^ 0x02};
  CHECK(ws->on_recv(ws, (const char *)ping, sizeof(ping)) == WS_SUCCESS);
  CHECK(cap.recv_count == 1 && cap.recv_opcode == OPCODE_PING);
  CHECK(cap.recv_len == 2 && memcmp(cap.recv_buf, "ok", 2) == 0);

  // Data continuation pushing total buffered payload to 64 MiB + 1 fails.
  uint8_t frag2[14 + 1] = {0x80, 0x81};
  frag2[2] = 0; frag2[3] = 0; frag2[4] = 0; frag2[5] = 0; frag2[6] = 'X';
  CHECK(ws->on_recv(ws, (const char *)frag2, sizeof(frag2)) == WS_ERROR);
  destroy_ws(ws, &cap);
}

static void test_unmasked_and_zero_mask_flags_and_advance(void) {
  struct test_capture cap;
  ws_t ws = create_upgraded_ws(&cap);

  // Frame 1: 00 00 00 00 mask on TEXT must advance in_head AND preserve
  // is_masking == true so callers like iwdp_on_frame do not reject it.
  uint8_t masked_zero[] = {0x81, 0x83, 0x00, 0x00, 0x00, 0x00, 'a', 'b', 'c'};
  // Frame 2: unmasked TEXT back-to-back must advance in_head and report
  // is_masking == false.
  uint8_t unmasked[] = {0x81, 0x03, 'x', 'y', 'z'};
  uint8_t wire[sizeof(masked_zero) + sizeof(unmasked)];
  memcpy(wire, masked_zero, sizeof(masked_zero));
  memcpy(wire + sizeof(masked_zero), unmasked, sizeof(unmasked));

  CHECK(ws->on_recv(ws, (const char *)wire, sizeof(masked_zero)) == WS_SUCCESS);
  CHECK(cap.recv_count == 1 && cap.recv_is_masking == true);
  CHECK(cap.recv_len == 3 && memcmp(cap.recv_buf, "abc", 3) == 0);

  // Also test back-to-back parsing in a single on_recv call.
  destroy_ws(ws, &cap);
  ws = create_upgraded_ws(&cap);
  CHECK(ws->on_recv(ws, (const char *)wire, sizeof(wire)) == WS_SUCCESS);
  CHECK(cap.recv_count == 2 && cap.recv_is_masking == false);
  CHECK(cap.recv_len == 3 && memcmp(cap.recv_buf, "xyz", 3) == 0);

  // Empty 0-byte PING on fresh socket must not trigger null+0 UB.
  uint8_t empty_ping[] = {0x89, 0x80, 0x11, 0x22, 0x33, 0x44};
  CHECK(ws->on_recv(ws, (const char *)empty_ping, sizeof(empty_ping)) == WS_SUCCESS);
  CHECK(cap.recv_count == 3 && cap.recv_opcode == OPCODE_PING && cap.recv_len == 0);
  destroy_ws(ws, &cap);
}

static void test_independent_send_and_recv_continued_opcode(void) {
  struct test_capture cap;
  ws_t ws = create_upgraded_ws(&cap);

  // Client starts a fragmented BINARY receive message.
  uint8_t recv_frag1[2 + 4 + 3] = {
      0x02, 0x83, 0x01, 0x02, 0x03, 0x04,
      0x10 ^ 0x01, 0x20 ^ 0x02, 0x30 ^ 0x03,
  };
  CHECK(ws->on_recv(ws, (const char *)recv_frag1, sizeof(recv_frag1)) ==
         WS_SUCCESS);
  CHECK(cap.recv_count == 0);

  // While recv is mid-fragment, server sends a fragmented TEXT message with
  // a PING control frame in between. Must not collide with recv_continued_opcode.
  CHECK(ws->send_frame(ws, false, OPCODE_TEXT, false, "hel", 3) == WS_SUCCESS);
  CHECK(ws->send_frame(ws, true, OPCODE_PING, false, "p", 1) == WS_SUCCESS);
  CHECK(ws->send_frame(ws, true, OPCODE_TEXT, false, "lo", 2) == WS_SUCCESS);

  CHECK(cap.sent_count == 3);
  CHECK(cap.sent_opcodes[0] == OPCODE_TEXT && !cap.sent_fins[0]);
  CHECK(cap.sent_opcodes[1] == OPCODE_PING && cap.sent_fins[1]);
  CHECK(cap.sent_opcodes[2] == OPCODE_CONTINUATION && cap.sent_fins[2]);

  // Interleaved incoming PING mid-fragment must preserve partial BINARY data
  // and recv_continued_opcode.
  uint8_t recv_ping[2 + 4 + 2] = {
      0x89, 0x82, 0x01, 0x02, 0x03, 0x04,
      'h' ^ 0x01, 'i' ^ 0x02,
  };
  CHECK(ws->on_recv(ws, (const char *)recv_ping, sizeof(recv_ping)) ==
         WS_SUCCESS);
  CHECK(cap.recv_count == 1 && cap.recv_opcode == OPCODE_PING);
  CHECK(cap.recv_len == 2 && memcmp(cap.recv_buf, "hi", 2) == 0);

  // Client finishes the incoming BINARY fragment.
  uint8_t recv_frag2[2 + 4 + 2] = {
      0x80, 0x82, 0x01, 0x02, 0x03, 0x04,
      0x40 ^ 0x01, 0x50 ^ 0x02,
  };
  CHECK(ws->on_recv(ws, (const char *)recv_frag2, sizeof(recv_frag2)) ==
         WS_SUCCESS);
  CHECK(cap.recv_count == 2 && cap.recv_opcode == OPCODE_BINARY);
  CHECK(cap.recv_len == 5);
  uint8_t expected[5] = {0x10, 0x20, 0x30, 0x40, 0x50};
  CHECK(memcmp(cap.recv_buf, expected, 5) == 0);
  destroy_ws(ws, &cap);
}

static void test_utf8_across_continuations_and_65535_encoding(void) {
  struct test_capture cap;

  // 1) ws_send_frame intentionally skips UTF-8 validation (endpoints enforce
  // their own UTF-8 rules; a malformed device string must not drop the whole
  // USB device connection), including >1 MiB and split continuation chunks.
  ws_t ws = create_upgraded_ws(&cap);
  size_t large_len = (1U << 20) + 16;
  char *raw_text = (char *)malloc(large_len);
  CHECK(raw_text != NULL);
  memset(raw_text, 'a', large_len);
  raw_text[large_len - 1] = (char)0xff;
  CHECK(ws->send_frame(ws, true, OPCODE_TEXT, false, raw_text, large_len) ==
         WS_SUCCESS);
  free(raw_text);
  const char split_a[2] = {'a', (char)0xe2};
  const char split_b[2] = {(char)0x82, (char)0xac};
  CHECK(ws->send_frame(ws, false, OPCODE_TEXT, false, split_a, sizeof(split_a)) ==
         WS_SUCCESS);
  CHECK(ws->send_frame(ws, true, OPCODE_PING, false, "ok", 2) == WS_SUCCESS);
  CHECK(ws->send_frame(ws, true, OPCODE_TEXT, false, split_b, sizeof(split_b)) ==
         WS_SUCCESS);
  destroy_ws(ws, &cap);

  // 2) Incoming split multi-byte UTF-8 ("🎉" = F0 9F 8E 89) across recv
  // continuation frames (plus interleaved PING) must succeed at FIN.
  ws = create_upgraded_ws(&cap);
  uint8_t rf1[] = {0x01, 0x82, 0, 0, 0, 0, 'a', 0xf0};
  uint8_t rping[] = {0x89, 0x81, 0, 0, 0, 0, 'p'};
  uint8_t rf2[] = {0x00, 0x82, 0, 0, 0, 0, 0x9f, 0x8e};
  uint8_t rf3[] = {0x80, 0x81, 0, 0, 0, 0, 0x89};
  CHECK(ws->on_recv(ws, (const char *)rf1, sizeof(rf1)) == WS_SUCCESS);
  CHECK(ws->on_recv(ws, (const char *)rping, sizeof(rping)) == WS_SUCCESS);
  CHECK(ws->on_recv(ws, (const char *)rf2, sizeof(rf2)) == WS_SUCCESS);
  CHECK(ws->on_recv(ws, (const char *)rf3, sizeof(rf3)) == WS_SUCCESS);
  CHECK(cap.recv_count == 2 && cap.recv_opcode == OPCODE_TEXT);
  uint8_t expected_emoji[5] = {'a', 0xf0, 0x9f, 0x8e, 0x89};
  CHECK(cap.recv_len == 5 && memcmp(cap.recv_buf, expected_emoji, 5) == 0);
  destroy_ws(ws, &cap);

  // 3) Incoming invalid or truncated UTF-8 at recv FIN MUST be rejected.
  ws = create_upgraded_ws(&cap);
  uint8_t rf_trunc[] = {0x81, 0x82, 0, 0, 0, 0, 'a', 0xe2};
  CHECK(ws->on_recv(ws, (const char *)rf_trunc, sizeof(rf_trunc)) == WS_ERROR);
  destroy_ws(ws, &cap);

  ws = create_upgraded_ws(&cap);
  uint8_t rf_bad1[] = {0x01, 0x82, 0, 0, 0, 0, 'a', 0xe2};
  uint8_t rf_bad2[] = {0x80, 0x82, 0, 0, 0, 0, 0xff, 'c'};
  CHECK(ws->on_recv(ws, (const char *)rf_bad1, sizeof(rf_bad1)) == WS_SUCCESS);
  CHECK(ws->on_recv(ws, (const char *)rf_bad2, sizeof(rf_bad2)) == WS_ERROR);
  destroy_ws(ws, &cap);

  // 4) RFC 6455 §5.2 minimal length encoding at UINT16_MAX (65535 -> 126, 2-byte).
  ws = create_upgraded_ws(&cap);
  char *buf65k = (char *)malloc(UINT16_MAX);
  CHECK(buf65k != NULL);
  memset(buf65k, 'z', UINT16_MAX);
  CHECK(ws->send_frame(ws, true, OPCODE_TEXT, false, buf65k, UINT16_MAX) ==
         WS_SUCCESS);
  CHECK((cap.sent_hdr[1] & 0x7f) == 126);
  CHECK(cap.sent_total_len == 4 + UINT16_MAX);
  free(buf65k);
  destroy_ws(ws, &cap);
}

int main(void) {
  test_char_buffer_overflow();
  test_frame_length_overflow_single_and_fragmented();
  test_fragment_accumulation_limit_and_control_bypass();
  test_unmasked_and_zero_mask_flags_and_advance();
  test_independent_send_and_recv_continued_opcode();
  test_utf8_across_continuations_and_65535_encoding();
  printf("All websocket tests passed.\n");
  return 0;
}

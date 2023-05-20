// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <time.h>
#ifdef WIN32
#include <windows.h>
#endif

#include <usbmuxd.h>

#include "idevice_ext.h"

typedef struct {
  unsigned char *data;
  unsigned int size;
} key_data_t;

int read_pair_record(const char *udid, plist_t *pair_record) {
  char* record_data = NULL;
  uint32_t record_size = 0;

  int res = usbmuxd_read_pair_record(udid, &record_data, &record_size);
  if (res < 0) {
    free(record_data);
    return -1;
  }

  *pair_record = NULL;
#if LIBPLIST_VERSION_MAJOR >= 2 && LIBPLIST_VERSION_MINOR >= 3
  plist_from_memory(record_data, record_size, pair_record, NULL);
#else
  plist_from_memory(record_data, record_size, pair_record);
#endif
  free(record_data);

  if (!*pair_record) {
    return -1;
  }

  return 0;
}

int pair_record_get_item_as_key_data(plist_t pair_record, const char* name, key_data_t *value) {
  char* buffer = NULL;
  uint64_t length = 0;
  plist_t node = plist_dict_get_item(pair_record, name);

  if (node && plist_get_node_type(node) == PLIST_DATA) {
    plist_get_data_val(node, &buffer, &length);
    value->data = (unsigned char*)malloc(length+1);
    memcpy(value->data, buffer, length);
    value->data[length] = '\0';
    value->size = length+1;
    free(buffer);
    return 0;
  }

  return -1;
}

int idevice_ext_connection_enable_ssl(const char *device_id, int fd, SSL **to_session) {
  plist_t pair_record = NULL;
  if (read_pair_record(device_id, &pair_record)) {
    fprintf(stderr, "Failed to read pair record\n");
    return -1;
  }

  key_data_t root_cert = { NULL, 0 };
  key_data_t root_privkey = { NULL, 0 };
  pair_record_get_item_as_key_data(pair_record, "RootCertificate", &root_cert);
  pair_record_get_item_as_key_data(pair_record, "RootPrivateKey", &root_privkey);
  plist_free(pair_record);

  BIO *ssl_bio = BIO_new(BIO_s_socket());
  if (!ssl_bio) {
    fprintf(stderr, "Could not create SSL bio\n");
    return -1;
  }

  BIO_set_fd(ssl_bio, fd, BIO_NOCLOSE);
  SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_method());
  if (ssl_ctx == NULL) {
    fprintf(stderr, "Could not create SSL context\n");
    BIO_free(ssl_bio);
  }

  SSL_CTX_set_security_level(ssl_ctx, 0);
  SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_VERSION);

  BIO* membp;
  X509* rootCert = NULL;
  membp = BIO_new_mem_buf(root_cert.data, root_cert.size);
  PEM_read_bio_X509(membp, &rootCert, NULL, NULL);
  BIO_free(membp);
  SSL_CTX_use_certificate(ssl_ctx, rootCert);
  X509_free(rootCert);
  free(root_cert.data);

  EVP_PKEY* rootPrivKey = NULL;
  membp = BIO_new_mem_buf(root_privkey.data, root_privkey.size);
  PEM_read_bio_PrivateKey(membp, &rootPrivKey, NULL, NULL);
  BIO_free(membp);
  SSL_CTX_use_PrivateKey(ssl_ctx, rootPrivKey);
  EVP_PKEY_free(rootPrivKey);

  free(root_privkey.data);

  SSL *ssl = SSL_new(ssl_ctx);
  if (!ssl) {
    fprintf(stderr, "Could not create SSL object\n");
    BIO_free(ssl_bio);
    SSL_CTX_free(ssl_ctx);
    return -1;
  }

  SSL_set_connect_state(ssl);
  SSL_set_verify(ssl, 0, NULL);
  SSL_set_bio(ssl, ssl_bio, ssl_bio);

  int ssl_error = 0;
  while (1) {
    ssl_error = SSL_get_error(ssl, SSL_do_handshake(ssl));
    if (ssl_error == 0 || ssl_error != SSL_ERROR_WANT_READ) {
      break;
    }
#ifdef WIN32
    Sleep(100);
#else
    struct timespec ts = { 0, 100000000 };
    nanosleep(&ts, NULL);
#endif
  }

  if (ssl_error != 0) {
    SSL_free(ssl);
    SSL_CTX_free(ssl_ctx);
    return ssl_error;
  }

  *to_session = ssl;
  return 0;
}

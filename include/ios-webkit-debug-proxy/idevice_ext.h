// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

//
// libimobildevice extensions
//

#ifndef IDEVICE_EXT_H
#define	IDEVICE_EXT_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <openssl/ssl.h>
#include <libimobiledevice/libimobiledevice.h>

int idevice_ext_connection_enable_ssl(const char *device_id, int fd, SSL **to_session);

#ifdef	__cplusplus
}
#endif

#endif	/* IDEVICE_EXT_H */

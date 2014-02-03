// Google BSD license http://code.google.com/google_bsd_license.html
// Copyright 2012 Google Inc. wrightt@google.com

//
// iOS WebInspector
//

#ifndef WEBINSPECTOR_H
#define	WEBINSPECTOR_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>


typedef uint8_t wi_status;
#define WI_ERROR 1
#define WI_SUCCESS 0


// Create a webinspector connection.
//
// @param device_id iOS 40-character device id, or NULL for any device
// @param to_device_id selected device_id (copy of device_id if set)
// @param to_device_name selected device name
// @param recv_timeout Set the socket receive timeout for future recv calls:
//    negative for non-blocking,
//    zero for the system default (5000 millis), or
//    positive for milliseconds.
// @result fd, or -1 for error
int wi_connect(const char *device_id, char **to_device_id,
               char **to_device_name, int recv_timeout);

struct wi_struct;
typedef struct wi_struct *wi_t;
wi_t wi_new(bool is_sim);
void wi_free(wi_t self);

struct wi_private;
typedef struct wi_private *wi_private_t;

// iOS WebInspector.
struct wi_struct {

    //
    // Use these APIs:
    //

    wi_status (*on_recv)(wi_t self, const char *buf, ssize_t length);

    void *state;
    bool *is_debug;

    //
    // Set these callbacks:
    //

    wi_status (*send_packet)(wi_t self, const char *packet, size_t length);

    // For internal use only:
    wi_status (*on_error)(wi_t self, const char *format, ...);
    wi_private_t private_state;
};


#ifdef	__cplusplus
}
#endif

#endif	/* WEBINSPECTOR_H */

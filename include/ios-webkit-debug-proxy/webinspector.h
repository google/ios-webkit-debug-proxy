// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

//
// iOS WebInspector
//

#ifndef WEBINSPECTOR_H
#define	WEBINSPECTOR_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <plist/plist.h>
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
               char **to_device_name, int *to_device_os_version,
               void **to_ssl_session, int recv_timeout);

struct wi_struct;
typedef struct wi_struct *wi_t;
wi_t wi_new(bool is_sim, bool partials_supported);
void wi_free(wi_t self);

struct wi_private;
typedef struct wi_private *wi_private_t;

// iOS WebInspector.
struct wi_struct {

    //
    // Call these APIs:
    //

    // Appends an arbitrary number of bytes to our input buffer,
    // calls recv_packet when the buffer contains one or more packets.
    wi_status (*on_recv)(wi_t self, const char *buf, ssize_t length);

    // Calls recv_plist if the packet is a full plist, otherwise appends
    // the partial packet to our pending buffer.
    wi_status (*recv_packet)(wi_t self, const char *packet, ssize_t length);

    // Calls send_packet with the serialized rpc packet(s).
    wi_status (*send_plist)(wi_t self, const plist_t rpc_dict);

    // Optional state for use in your callbacks.
    void *state;
    bool *is_debug;

    //
    // Set these callbacks:
    //

    // Send a serialized rpc (full or partial).
    wi_status (*send_packet)(wi_t self, const char *packet, size_t length);

    // Receive a deserialized full rpc.
    wi_status (*recv_plist)(wi_t self, const plist_t rpc_dict);

    // For internal use only:
    wi_status (*on_error)(wi_t self, const char *format, ...);
    wi_private_t private_state;
};


#ifdef	__cplusplus
}
#endif

#endif	/* WEBINSPECTOR_H */

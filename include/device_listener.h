// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

//
// iOS device add/remove listener.
//

#ifndef DEVICE_LISTENER_H
#define	DEVICE_LISTENER_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>


typedef uint8_t dl_status;
#define DL_ERROR 1
#define DL_SUCCESS 0


// Create a device add/remove connection.
// @param recv_timeout milliseconds, negative for non_blocking
// @result fd, or -1 for error
int dl_connect(int recv_timeout);


struct dl_struct;
typedef struct dl_struct *dl_t;
dl_t dl_new();
void dl_free(dl_t self);

struct dl_private;
typedef struct dl_private *dl_private_t;

// iOS device add/remove listener.
struct dl_struct {

    //
    // Use these API:
    //

    // Call once after startup.
    dl_status (*start)(dl_t self);

    // Call to append data, calls on_attach/on_detach when we have a full
    // input packet.
    dl_status (*on_recv)(dl_t self, const char *buf, ssize_t length);

    void *state;
    bool *is_debug;

    //
    // Set these callbacks:
    //

    // Called to send "listen" and other output packets.
    dl_status (*send_packet)(dl_t self, const char *buf, size_t length);

    // Called by on_recv.
    // @param device_id 40-character hex iOS device identifier.
    // @param device_num usbmuxd device identifier
    dl_status (*on_attach)(dl_t self, const char *device_id, int device_num);

    dl_status (*on_detach)(dl_t self, const char *device_id, int device_num);

    // For internal use only:
    dl_private_t private_state;
};


#ifdef	__cplusplus
}
#endif

#endif	/* DEVICE_LISTENER_H */


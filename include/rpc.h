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


// Create a UUID, e.g. "4B2550E4-13D6-4902-A48E-B45D5B23215B".
wi_status wi_new_uuid(char **to_uuid);


struct wi_app_struct {
  char *app_id;
  char *app_name;
  bool is_proxy;
};
typedef struct wi_app_struct *wi_app_t;


struct wi_page_struct {
  uint32_t page_id;
  char *connection_id;
  char *title;
  char *url;
};
typedef struct wi_page_struct *wi_page_t;


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

    wi_status (*send_reportIdentifier)(wi_t self,
            const char *connection_id);

    wi_status (*send_getConnectedApplications)(wi_t self,
            const char *connection_id);

    wi_status (*send_forwardGetListing)(wi_t self,
            const char *connection_id, const char *app_id);

    wi_status (*send_forwardIndicateWebView)(wi_t self,
            const char *connection_id, const char *app_id,
            uint32_t page_id, bool is_enabled);

    wi_status (*send_forwardSocketSetup)(wi_t self,
            const char *connection_id, const char *app_id,
            uint32_t page_id, const char *sender_id);

    wi_status (*send_forwardSocketData)(wi_t self,
            const char *connection_id, const char *app_id,
            uint32_t page_id, const char *sender_id,
            const char *data, size_t length);

    wi_status (*send_forwardDidClose)(wi_t self,
            const char *connection_id, const char *app_id,
            uint32_t page_id, const char *sender_id);

    void *state;
    bool *is_debug;

    //
    // Set these callbacks:
    //

    wi_status (*send_packet)(wi_t self, const char *packet, size_t length);

    wi_status (*on_reportSetup)(wi_t self);

    wi_status (*on_reportConnectedApplicationList)(wi_t self,
            const wi_app_t *apps);

    wi_status (*on_applicationConnected)(wi_t self,
            const wi_app_t app);

    wi_status (*on_applicationDisconnected)(wi_t self,
            const wi_app_t app);

    wi_status (*on_applicationSentListing)(wi_t self,
            const char *app_id, const wi_page_t *pages);

    wi_status (*on_applicationSentData)(wi_t self,
            const char *app_id, const char *dest_id,
            const char *data, size_t length);


    // For internal use only:
    wi_status (*on_error)(wi_t self, const char *format, ...);
    wi_private_t private_state;
};


#ifdef	__cplusplus
}
#endif

#endif	/* WEBINSPECTOR_H */

Google BSD license <https://developers.google.com/google-bsd-license>
Copyright 2012 Google Inc.  <wrightt@google.com>

Example Proxy Clients
---------------------

- Browser HTML
   \- <http://localhost:9221> supports HTML-formatted list of devices, pages, and DevTools UI.

- Browser JSON
   \- <http://localhost:9221/json> supports JSON-formatted output.

- Browser JS
   \- [wdp_client.html](wdp_client.html) demonstrates DevTools command I/O, e.g. "Page.navigate".

- NodeJS
   \- [wdp_client.js](wdp_client.js) is the command-line equivalent of [wdp_client.html](wdp_client.html).

- C
   \- [wi_client.c](wi_client.c) a minimal webinspector client.


Example System Clients
----------------------

- Device attach/remove listener
   \- [dl_client.c](dl_client.c)

- WebSocket "echo" client (for ws_echo* testing)
   \- [ws_client.html](ws_client.html)

- WebSocket "echo" servers
   \- [ws_echo1.c](ws_echo1.c) uses blocking I/O
   \- [ws_echo2.c](ws_echo2.c) uses non-blocking I/O

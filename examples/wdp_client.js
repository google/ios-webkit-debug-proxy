#!/usr/bin/env node
// Google BSD license https://developers.google.com/google-bsd-license
// Copyright 2012 Google Inc. wrightt@google.com

// Node.js example client
//
// Requires `npm install optimist ws`

var argv = require('optimist')
    .default({ port: 9222, page: 1, url: 'http://www.google.com' })
    .usage('Usage: $0 [OPTIONS]\n\nSends a Page.navigate command to the proxy.')
    .boolean('help')
    .check(function() { return !arguments[0].help; })
    .argv

var url = 'ws://localhost:' + argv.port + '/devtools/page/' + argv.page
var commands = [
    '{"id": 1, "method": "Page.navigate", "params":{"url": "' + argv.url + '"}}'
]

var WebSocket = require('ws');
console.log('open '+url);
var ws = new WebSocket(url)
var count = 0

ws.on('open', function() {
    console.log('connected');
    if (count < commands.length) {
        console.log('send '+commands[count])
        ws.send(commands[count++])
    }
});
ws.on('close', function() {
    console.log('disconnected');
    ws.close()
});
ws.on('message', function(data, flags) {
    console.log('recv %s', data)
    if (count < commands.length) {
       console.log('send '+commands[count])
       ws.send(commands[count++])
    } else {
       // if we sent Page.enable, we could listen for Page.loadEventFired to
       // see if our nav succeeded.
       ws.close()
    }
});

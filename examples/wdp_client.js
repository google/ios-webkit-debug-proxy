#!/usr/bin/env node
// Google BSD license http://code.google.com/google_bsd_license.html
// Copyright 2012 Google Inc. wrightt@google.com

// Node.js example client
//
// Requires `npm install optimist ws`

var argv = require('optimist')
    .default({ port : 9222, page : 1 })
    .argv

var url = 'ws://localhost:' + argv.port + '/devtools/page/' + argv.page
var commands = [
    '{"id": 1, "method": "Page.navigate", "params":{"url": "http://www.google.com"}}'
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
       ws.close()
    }
});

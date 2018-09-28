const events = require('events');
const util = require('util');

const NobleWinrt = require('../build/Release/noble-winrt-native').NobleWinrt;

util.inherits(NobleWinrt, events.EventEmitter);

module.exports = new NobleWinrt();

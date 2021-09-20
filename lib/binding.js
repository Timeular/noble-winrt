const events = require('events');
const util   = require('util');

const NobleWinrt =  require('bindings')('noble_winrt').NobleWinrt;

util.inherits(NobleWinrt, events.EventEmitter);

module.exports = new NobleWinrt();

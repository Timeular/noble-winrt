'use strict';

const os = require('os');

// noble-winrt acts as a shim to noble.
if (os.platform() === 'win32') {
	const ver = os.release().split('.').map(Number);
	if (!(ver[0] > 10 ||
		(ver[0] === 10 && ver[1] > 0) ||
		(ver[0] === 10 && ver[1] === 0 && ver[2] >= 15063))) {
		module.exports = require('@abandonware/noble')
	} else {
		const Noble = require('@abandonware/noble/lib/noble');
		const winrtBindings = require('./lib/binding.js');
		var nobleInstance = new Noble(winrtBindings);
		module.exports = nobleInstance;
	}
} else {
	module.exports = require('@abandonware/noble');
}

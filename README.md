# Noble (Node.js Bluetooth LE) for Windows 10

_This is a rewrite of [noble-uwp](https://github.com/jasongin/noble-uwp) using the [C++/WinRT](https://docs.microsoft.com/en-us/windows/uwp/cpp-and-winrt-apis/intro-to-using-cpp-with-winrt) API._

## System Requirements
 * Node.js v6.14.2 or later.
 * Windows 10 build 10.0.15063 or later
 * Windows 10 SDK build 10.0.17134.0

## Usage
Simply require `noble-winrt` instead of `noble`:
```javascript
const noble = require('noble-winrt');
```
On non-Windows platforms or Windows versions lower than 10.0.15063 this will use the regular [noble](https://github.com/sandeepmistry/noble/blob/master/README.md) implementation and on Windows version 10.0.15063 or later it will use the native binding using the C++/WinRT API.

## Implementation Status
Everything should work that also works with the regular noble bindings except:
 * Writing/Reading to descriptor handles is not supported
 * Broadcast is not supported

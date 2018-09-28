// this can be tested with https://github.com/WebBluetoothCG/ble-test-peripheral-android

const noble = require('../index');

const BATTERY_SERVICE_UUID = "180f"

noble.on('stateChange', function(state) {
  console.log(state)
  if (state === 'poweredOn') {
      noble.startScanning([BATTERY_SERVICE_UUID]);
  } else {
    noble.stopScanning();
  }
})

noble.on('discover', function(peripheral) {
  noble.stopScanning();
  console.log('peripheral with ID ' + peripheral.id + ' ' + peripheral.advertisement.localName + ' found: ' + peripheral.advertisement.serviceUuids);
  peripheral.connect(function(error) {
    console.log('connected to peripheral: ' + peripheral.uuid);
    peripheral.discoverServices(['180f'], function(error, services) {
      var batteryService = services[0];
      console.log('discoveredBatter service');

      batteryService.discoverCharacteristics(['2a19'], function(error, characteristics) {
        var batteryLevelCharacteristic = characteristics[0];
        console.log('discovered Battery Level characteristic');

        batteryLevelCharacteristic.on('data', function(data, isNotification) {
          console.log('battery level is now: ', data.readUInt8(0) + '%');
        });

        // to enable notify
        batteryLevelCharacteristic.subscribe(function(error) {
          console.log('battery level notification on');
        });
      });
    });
  });
  peripheral.on('disconnect', () => {
    console.log('disconnected')
    process.exit(0)
  })
});

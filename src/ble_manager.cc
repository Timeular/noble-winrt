//
//  ble_manager.cc
//  noble-winrt-native
//
//  Created by Georg Vienna on 03.09.18.
//

#include "ble_manager.h"
#include "winrt_cpp.h"

#include <winrt/Windows.Storage.Streams.h>
using winrt::Windows::Devices::Bluetooth::BluetoothCacheMode;
using winrt::Windows::Devices::Bluetooth::BluetoothConnectionStatus;
using winrt::Windows::Storage::Streams::DataReader;
using winrt::Windows::Storage::Streams::DataWriter;

template <typename T> auto inFilter(std::vector<T> filter, T object)
{
    return filter.empty() || std::find(filter.begin(), filter.end(), object) != filter.end();
}

template <typename O, typename M, class... Types> auto bind2(O* object, M method, Types&... args)
{
    return std::bind(method, object, std::placeholders::_1, std::placeholders::_2, args...);
}

#define LOGE(message, ...) printf("WINRT BLE API::BLE MANAGER:: " __FUNCTION__ ": " message "\n", __VA_ARGS__)

#define CHECK_DEVICE()                                     \
    if (mDeviceMap.find(uuid) == mDeviceMap.end())         \
    {                                                      \
        LOGE("device with id %s not found", uuid.c_str()); \
        return false;                                      \
    }

#define IFDEVICE(_device, _uuid)                     \
    PeripheralWinrt& peripheral = mDeviceMap[_uuid]; \
    if (!peripheral.device.has_value())              \
    {                                                \
        LOGE("device not connected");                \
        return false;                                \
    }                                                \
    BluetoothLEDevice& _device = *peripheral.device;

template <class T>
bool CheckResult(T _result)
{
    if (!_result)
    {
        LOGE("result is null");
        return false;
    } else {
        auto _commStatus = _result.Status();
        auto _protError  = _result.ProtocolError();
        if (_commStatus != GattCommunicationStatus::Success) 
        {
            LOGE("ommunication status: %d", _commStatus);
            LOGE("protocol error: %d", _protError);
            return false;
        }
    }

    return true;
}

#define FOR(object, vector)       \
    auto& _vector = vector;       \
    if (!_vector)                 \
    {                             \
        LOGE(#vector " is null"); \
        return;                   \
    }                             \
    else                          \
        for (auto&& object : _vector)

BLEManager::BLEManager(const Napi::Value& receiver, const Napi::Function& callback)
{
    mRadioState = AdapterState::Initial;
    mEmit.Wrap(receiver, callback);
    auto onRadio = std::bind(&BLEManager::OnRadio, this, std::placeholders::_1);
    mWatcher.Start(onRadio);
    mAdvertismentWatcher.ScanningMode(BluetoothLEScanningMode::Active);
    auto onReceived  = bind2(this, &BLEManager::OnScanResult);
    mReceivedRevoker = mAdvertismentWatcher.Received(winrt::auto_revoke, onReceived);
    auto onStopped   = bind2(this, &BLEManager::OnScanStopped);
    mStoppedRevoker  = mAdvertismentWatcher.Stopped(winrt::auto_revoke, onStopped);
}

const char* adapterStateToString(AdapterState state)
{
    switch (state)
    {
    case AdapterState::Unsupported:
        return "unsupported";
    case AdapterState::On:
        return "poweredOn";
        break;
    case AdapterState::Off:
        return "poweredOff";
        break;
    case AdapterState::Disabled:
        return "poweredOff";
        break;
    default:
        return "unknown";
    }
}

void BLEManager::OnRadio(Radio& radio)
{
    auto state = AdapterState::Unsupported;
    if (radio)
    {
        state = (AdapterState)radio.State();
    }
    if (state != mRadioState)
    {
        mRadioState = state;
        mEmit.RadioState(adapterStateToString(state));
    }
}

void BLEManager::Scan(const std::vector<winrt::guid>& serviceUUIDs, bool allowDuplicates)
{
    mAdvertismentMap.clear();
    mAllowDuplicates = allowDuplicates;
    BluetoothLEAdvertisementFilter filter = BluetoothLEAdvertisementFilter();
    BluetoothLEAdvertisement advertisment = BluetoothLEAdvertisement();
    auto& services = advertisment.ServiceUuids();
    for (auto& uuid : serviceUUIDs)
    {
        services.Append(uuid);
    }
    filter.Advertisement(advertisment);
    mAdvertismentWatcher.AdvertisementFilter(filter);
    mAdvertismentWatcher.Start();
    mEmit.ScanState(true);
}

void BLEManager::OnScanResult(BluetoothLEAdvertisementWatcher watcher,
                              const BluetoothLEAdvertisementReceivedEventArgs& args)
{
    uint64_t bluetoothAddress = args.BluetoothAddress();
    std::string uuid = formatBluetoothUuid(bluetoothAddress);
    int16_t rssi = args.RawSignalStrengthInDBm();
    auto advertismentType = args.AdvertisementType();

    if (mDeviceMap.find(uuid) == mDeviceMap.end())
    {
        mAdvertismentMap.insert(uuid);
        auto peripheral =
            PeripheralWinrt(bluetoothAddress, advertismentType, rssi, args.Advertisement());
        mEmit.Scan(uuid, rssi, peripheral);
        mDeviceMap.emplace(std::make_pair(uuid, std::move(peripheral)));
    }
    else
    {
        PeripheralWinrt& peripheral = mDeviceMap[uuid];
        peripheral.Update(rssi, args.Advertisement());
        if (mAllowDuplicates || mAdvertismentMap.find(uuid) == mAdvertismentMap.end())
        {
            mAdvertismentMap.insert(uuid);
            mEmit.Scan(uuid, rssi, peripheral);
        }
    }
}

void BLEManager::StopScan()
{
    mAdvertismentWatcher.Stop();
}

void BLEManager::OnScanStopped(BluetoothLEAdvertisementWatcher watcher,
                               const BluetoothLEAdvertisementWatcherStoppedEventArgs& args)
{
    mEmit.ScanState(false);
}

bool BLEManager::Connect(const std::string& uuid)
{
    if (mDeviceMap.find(uuid) == mDeviceMap.end())
    {
        mEmit.Connected(uuid, "device not found");
        return false;
    }
    PeripheralWinrt& peripheral = mDeviceMap[uuid];
    if (!peripheral.device.has_value())
    {
        auto completed = bind2(this, &BLEManager::OnConnected, uuid);
        BluetoothLEDevice::FromBluetoothAddressAsync(peripheral.bluetoothAddress).Completed(completed);
    }
    else
    {
        mEmit.Connected(uuid);
    }
    return true;
}

void BLEManager::OnConnected(IAsyncOperation<BluetoothLEDevice> asyncOp, AsyncStatus& status,
                             const std::string uuid)
{
    if (status == AsyncStatus::Completed)
    {
        BluetoothLEDevice& device = asyncOp.GetResults();
        // device can be null if the connection failed
        if (device)
        {
            auto onChanged = bind2(this, &BLEManager::OnConnectionStatusChanged);
            auto token = device.ConnectionStatusChanged(onChanged);
            auto uuid = formatBluetoothUuid(device.BluetoothAddress());
            PeripheralWinrt& peripheral = mDeviceMap[uuid];
            peripheral.device = device;
            peripheral.connectionToken = token;
            mEmit.Connected(uuid);
        }
        else
        {
            mEmit.Connected(uuid, "could not connect to device: result is null");
        }
    }
    else
    {
        mEmit.Connected(uuid, "could not connect to device");
    }
}

bool BLEManager::Disconnect(const std::string& uuid)
{
    CHECK_DEVICE();
    PeripheralWinrt& peripheral = mDeviceMap[uuid];
    peripheral.Disconnect();
    mNotifyMap.Remove(uuid);
    mEmit.Disconnected(uuid);
    return true;
}

void BLEManager::OnConnectionStatusChanged(BluetoothLEDevice device,
                                           winrt::Windows::Foundation::IInspectable inspectable)
{
    if (device.ConnectionStatus() == BluetoothConnectionStatus::Disconnected)
    {
        auto uuid = formatBluetoothUuid(device.BluetoothAddress());
        if (mDeviceMap.find(uuid) == mDeviceMap.end())
        {
            LOGE("device with id %s not found", uuid.c_str());
            return;
        }
        PeripheralWinrt& peripheral = mDeviceMap[uuid];
        peripheral.Disconnect();
        mNotifyMap.Remove(uuid);
        mEmit.Disconnected(uuid);
    }
}

bool BLEManager::UpdateRSSI(const std::string& uuid)
{
    CHECK_DEVICE();

    PeripheralWinrt& peripheral = mDeviceMap[uuid];
    // no way to get the rssi while we are connected, return the last value of advertisement
    mEmit.RSSI(uuid, peripheral.rssi);
    return true;
}

bool BLEManager::DiscoverServices(const std::string& uuid,
                                  const std::vector<winrt::guid>& serviceUUIDs)
{
    CHECK_DEVICE();
    IFDEVICE(device, uuid)
    {
        auto completed = bind2(this, &BLEManager::OnServicesDiscovered, uuid, serviceUUIDs);
        device.GetGattServicesAsync(BluetoothCacheMode::Uncached).Completed(completed);
        return true;
    }
}

void BLEManager::OnServicesDiscovered(IAsyncOperation<GattDeviceServicesResult> asyncOp,
                                      AsyncStatus status, const std::string uuid,
                                      const std::vector<winrt::guid> serviceUUIDs)
{
    std::vector<std::string> serviceUuids;
    if (status == AsyncStatus::Completed)
    { 
        GattDeviceServicesResult& result = asyncOp.GetResults();
        if(CheckResult(result))
        {
            FOR(service, result.Services())
            {
                auto id = service.Uuid();
                if (inFilter(serviceUUIDs, id))
                {
                    serviceUuids.push_back(toStr(id));
                }
            }
        }
        else
        {
            LOGE("GattDeviceServicesResult:: failed to discover any services.");
        }
    }
    else
    {
        LOGE("AsyncStatus failed: %d", status);
    }

    mEmit.ServicesDiscovered(uuid, serviceUuids);
}

bool BLEManager::DiscoverIncludedServices(const std::string& uuid, const winrt::guid& serviceUuid,
                                          const std::vector<winrt::guid>& serviceUUIDs)
{
    CHECK_DEVICE();
    IFDEVICE(device, uuid)
    {
        peripheral.GetService(serviceUuid, [=](std::optional<GattDeviceService> service) {
            if (service)
            {
                std::string serviceId = toStr(serviceUuid);
                service->GetIncludedServicesAsync(BluetoothCacheMode::Uncached)
                    .Completed(bind2(this, &BLEManager::OnIncludedServicesDiscovered, uuid,
                                     serviceId, serviceUUIDs));
            }
            else
            {
                LOGE("GetService error");
            }
        });
        return true;
    }
}

void BLEManager::OnIncludedServicesDiscovered(IAsyncOperation<GattDeviceServicesResult> asyncOp,
                                              AsyncStatus status, const std::string uuid,
                                              const std::string serviceId,
                                              const std::vector<winrt::guid> serviceUUIDs)
{
    std::vector<std::string> servicesUuids;
    if (status == AsyncStatus::Completed)
    {
        auto& result = asyncOp.GetResults();
        if(CheckResult(result))
        {
            FOR(service, result.Services())
            {
                auto id = service.Uuid();
                if (inFilter(serviceUUIDs, id))
                {
                    servicesUuids.push_back(toStr(id));
                }
            }
        }
        else
        {
            LOGE("GattDeviceServicesResult:: Failed to discover included services.");
        }
    }
    else
    {
        LOGE("AsyncStatus status: %d", status);
    }

    mEmit.IncludedServicesDiscovered(uuid, serviceId, servicesUuids);
}

bool BLEManager::DiscoverCharacteristics(const std::string& uuid, const winrt::guid& serviceUuid,
                                         const std::vector<winrt::guid>& characteristicUUIDs)
{
    CHECK_DEVICE();
    IFDEVICE(device, uuid)
    {
        peripheral.GetService(serviceUuid, [=](std::optional<GattDeviceService> service) {
            if (service)
            {
                std::string serviceId = toStr(serviceUuid);
                service->GetCharacteristicsAsync(BluetoothCacheMode::Uncached)
                    .Completed(bind2(this, &BLEManager::OnCharacteristicsDiscovered, uuid,
                                     serviceId, characteristicUUIDs));
            }
            else
            {
                LOGE("GetService error");
            }
        });
        return true;
    }
}

void BLEManager::OnCharacteristicsDiscovered(IAsyncOperation<GattCharacteristicsResult> asyncOp,
                                             AsyncStatus status, const std::string uuid,
                                             const std::string serviceId,
                                             const std::vector<winrt::guid> characteristicUUIDs)
{
    std::vector<std::pair<std::string, std::vector<std::string>>> characteristicsUuids;
    if (status == AsyncStatus::Completed)
    {
        auto& result = asyncOp.GetResults();
        if(CheckResult(result))
        {
            FOR(characteristic, result.Characteristics())
            {
                auto id = characteristic.Uuid();
                if (inFilter(characteristicUUIDs, id))
                {
                    auto props = characteristic.CharacteristicProperties();
                    characteristicsUuids.push_back({ toStr(id), toPropertyArray(props) });
                }
            }
        }
        else
        {
            LOGE("GattCharacteristicsResult:: Failed to discover any characteristics.");
        }
    }
    else
    {
        LOGE("AsyncStatus status: %d", status);
    }

    mEmit.CharacteristicsDiscovered(uuid, serviceId, characteristicsUuids);
}

bool BLEManager::Read(const std::string& uuid, const winrt::guid& serviceUuid,
                      const winrt::guid& characteristicUuid)
{
    CHECK_DEVICE();
    IFDEVICE(device, uuid)
    {
        peripheral.GetCharacteristic(
            serviceUuid, characteristicUuid, [=](std::optional<GattCharacteristic> characteristic) {
                if (characteristic)
                {
                    std::string serviceId = toStr(serviceUuid);
                    std::string characteristicId = toStr(characteristicUuid);
                    characteristic->ReadValueAsync(BluetoothCacheMode::Uncached)
                        .Completed(
                            bind2(this, &BLEManager::OnRead, uuid, serviceId, characteristicId));
                }
                else
                {
                    LOGE("GetCharacteristic error");
                }
            });
        return true;
    }
}

void BLEManager::OnRead(IAsyncOperation<GattReadResult> asyncOp, AsyncStatus status,
                        const std::string uuid, const std::string serviceId,
                        const std::string characteristicId)
{
    Data data(0);
    if (status == AsyncStatus::Completed)
    {
        GattReadResult& result = asyncOp.GetResults();
        if(CheckResult(result))
        {
            auto& value = result.Value();
            if (value)
            {
                auto& reader = DataReader::FromBuffer(value);
                Data data(reader.UnconsumedBufferLength());
                reader.ReadBytes(data);
                mEmit.Read(uuid, serviceId, characteristicId, data, false);
            }
            else
            {
                LOGE("GattReadResult::Value is null");
            }
        }
        else
        {
            LOGE("GattReadResult:: failed to read");
        }
    }
    else
    {
        LOGE("AsyncStatus failed: %d", status);
    }

    mEmit.Read(uuid, serviceId, characteristicId, data, false);
}

bool BLEManager::Write(const std::string& uuid, const winrt::guid& serviceUuid,
                       const winrt::guid& characteristicUuid, const Data& data,
                       bool withoutResponse)
{
    CHECK_DEVICE();
    IFDEVICE(device, uuid)
    {
        peripheral.GetCharacteristic(
            serviceUuid, characteristicUuid, [=](std::optional<GattCharacteristic> characteristic) {
                if (characteristic)
                {
                    std::string serviceId = toStr(serviceUuid);
                    std::string characteristicId = toStr(characteristicUuid);
                    auto writer = DataWriter();
                    writer.WriteBytes(data);
                    auto& value = writer.DetachBuffer();
                    GattWriteOption option = withoutResponse ? GattWriteOption::WriteWithoutResponse
                                                             : GattWriteOption::WriteWithResponse;
                    characteristic->WriteValueWithResultAsync(value, option)
                        .Completed(
                            bind2(this, &BLEManager::OnWrite, uuid, serviceId, characteristicId));
                }
                else
                {
                    LOGE("GetCharacteristic error");
                }
            });
        return true;
    }
}

void BLEManager::OnWrite(IAsyncOperation<GattWriteResult> asyncOp, AsyncStatus status,
                         const std::string uuid, const std::string serviceId,
                         const std::string characteristicId)
{
    if (status == AsyncStatus::Completed)
    {
        mEmit.Write(uuid, serviceId, characteristicId);
    }
    else
    {
        auto errorCode = asyncOp.ErrorCode().value;

        LOGE("Write:Status: %d", status);
        LOGE("GattWriteError: %d", errorCode);
    }
}

GattClientCharacteristicConfigurationDescriptorValue
GetDescriptorValue(GattCharacteristicProperties properties)
{
    if ((properties & GattCharacteristicProperties::Indicate) ==
        GattCharacteristicProperties::Indicate)
    {
        return GattClientCharacteristicConfigurationDescriptorValue::Indicate;
    }
    else
    {
        return GattClientCharacteristicConfigurationDescriptorValue::Notify;
    }
}

bool BLEManager::Notify(const std::string& uuid, const winrt::guid& serviceUuid,
                        const winrt::guid& characteristicUuid, bool on)
{
    CHECK_DEVICE();
    IFDEVICE(device, uuid)
    {
        auto onCharacteristic = [=](std::optional<GattCharacteristic> characteristic) {
            if (characteristic)
            {
                std::string serviceId = toStr(serviceUuid);
                std::string characteristicId = toStr(characteristicUuid);
                bool subscribed = mNotifyMap.IsSubscribed(uuid, *characteristic);

                if (on)
                {
                    if (subscribed)
                    {
                        // already listening
                        mEmit.Notify(uuid, serviceId, characteristicId, true);
                        return;
                    }
                    auto descriptorValue =
                        GetDescriptorValue(characteristic->CharacteristicProperties());

                    auto completed = bind2(this, &BLEManager::OnNotify, *characteristic, uuid,
                                           serviceId, characteristicId, on);
                    characteristic
                        ->WriteClientCharacteristicConfigurationDescriptorWithResultAsync(
                            descriptorValue)
                        .Completed(completed);
                }
                else
                {
                    if (!subscribed)
                    {
                        // already not listening
                        mEmit.Notify(uuid, serviceId, characteristicId, false);
                        return;
                    }

                    mNotifyMap.Unsubscribe(uuid, *characteristic);
                    auto descriptorValue =
                        GattClientCharacteristicConfigurationDescriptorValue::None;
                    auto completed = bind2(this, &BLEManager::OnNotify, *characteristic, uuid,
                                           serviceId, characteristicId, on);
                    characteristic
                        ->WriteClientCharacteristicConfigurationDescriptorWithResultAsync(
                            descriptorValue)
                        .Completed(completed);
                }
            }
            else
            {
                LOGE("GetCharacteristic error");
            }
        };
        peripheral.GetCharacteristic(serviceUuid, characteristicUuid, onCharacteristic);
        return true;
    }
}

void BLEManager::OnNotify(IAsyncOperation<GattWriteResult> asyncOp, AsyncStatus status,
                          const GattCharacteristic characteristic, const std::string uuid,
                          const std::string serviceId, const std::string characteristicId,
                          const bool state)
{
    if (status == AsyncStatus::Completed)
    {
        if (state == true)
        {
            auto onChanged = bind2(this, &BLEManager::OnValueChanged, uuid);
            auto token = characteristic.ValueChanged(onChanged);
            mNotifyMap.Add(uuid, characteristic, token);
        }
        mEmit.Notify(uuid, serviceId, characteristicId, state);
    }
    else
    {
        LOGE("status: %d", status);
    }
}

void BLEManager::OnValueChanged(GattCharacteristic characteristic,
                                const GattValueChangedEventArgs& args, std::string deviceUuid)
{
    auto& reader = DataReader::FromBuffer(args.CharacteristicValue());
    Data data(reader.UnconsumedBufferLength());
    reader.ReadBytes(data);
    auto characteristicUuid = toStr(characteristic.Uuid());
    auto serviceUuid = toStr(characteristic.Service().Uuid());
    mEmit.Read(deviceUuid, serviceUuid, characteristicUuid, data, true);
}

bool BLEManager::DiscoverDescriptors(const std::string& uuid, const winrt::guid& serviceUuid,
                                     const winrt::guid& characteristicUuid)
{
    CHECK_DEVICE();
    IFDEVICE(device, uuid)
    {
        peripheral.GetCharacteristic(
            serviceUuid, characteristicUuid, [=](std::optional<GattCharacteristic> characteristic) {
                if (characteristic)
                {
                    std::string serviceId = toStr(serviceUuid);
                    std::string characteristicId = toStr(characteristicUuid);
                    auto completed = bind2(this, &BLEManager::OnDescriptorsDiscovered, uuid,
                                           serviceId, characteristicId);
                    characteristic->GetDescriptorsAsync(BluetoothCacheMode::Uncached)
                        .Completed(completed);
                }
                else
                {
                    LOGE("GetCharacteristic error");
                }
            });
        return true;
    }
}

void BLEManager::OnDescriptorsDiscovered(IAsyncOperation<GattDescriptorsResult> asyncOp,
                                         AsyncStatus status, const std::string uuid,
                                         const std::string serviceId,
                                         const std::string characteristicId)
{
    std::vector<std::string> descriptorUuids;
    if (status == AsyncStatus::Completed)
    {
        auto& result = asyncOp.GetResults();
        if(CheckResult(result))
        {
            FOR(descriptor, result.Descriptors())
            {
                descriptorUuids.push_back(toStr(descriptor.Uuid()));
            }
        }
        else
        {
            LOGE("GattDescriptorsResult:: Failed to discover any descryptors.");
        }
    }
    else
    {
        LOGE("AsyncStatus failed: %d", status);
    }

    mEmit.DescriptorsDiscovered(uuid, serviceId, characteristicId, descriptorUuids);
}

bool BLEManager::ReadValue(const std::string& uuid, const winrt::guid& serviceUuid,
                           const winrt::guid& characteristicUuid, const winrt::guid& descriptorUuid)
{
    CHECK_DEVICE();
    IFDEVICE(device, uuid)
    {
        peripheral.GetDescriptor(
            serviceUuid, characteristicUuid, descriptorUuid,
            [=](std::optional<GattDescriptor> descriptor) {
                if (descriptor)
                {
                    std::string serviceId = toStr(serviceUuid);
                    std::string characteristicId = toStr(characteristicUuid);
                    std::string descriptorId = toStr(descriptorUuid);
                    auto completed = bind2(this, &BLEManager::OnReadValue, uuid, serviceId,
                                           characteristicId, descriptorId);
                    descriptor->ReadValueAsync(BluetoothCacheMode::Uncached).Completed(completed);
                }
                else
                {
                    LOGE("descriptor not found");
                }
            });
        return true;
    }
}

void BLEManager::OnReadValue(IAsyncOperation<GattReadResult> asyncOp, AsyncStatus status,
                             const std::string uuid, const std::string serviceId,
                             const std::string characteristicId, const std::string descriptorId)
{
    Data data(0);
    if (status == AsyncStatus::Completed)
    {
        GattReadResult& result = asyncOp.GetResults();
        if(CheckResult(result))
        {
            auto& value = result.Value();
            if (value)
            {
                auto& reader = DataReader::FromBuffer(value);
                Data data(reader.UnconsumedBufferLength());
                reader.ReadBytes(data);
                mEmit.ReadValue(uuid, serviceId, characteristicId, descriptorId, data);
            }
            else
            {
                LOGE("GattReadResult::Value is null");
            }
        }
        else
        {
            LOGE("GattReadResult:: failed to read");
        }
    }
    else
    {
        LOGE("AsyncStatus failed: %d", status);
    }

    mEmit.ReadValue(uuid, serviceId, characteristicId, descriptorId, data);
}

bool BLEManager::WriteValue(const std::string& uuid, const winrt::guid& serviceUuid,
                            const winrt::guid& characteristicUuid,
                            const winrt::guid& descriptorUuid, const Data& data)
{
    CHECK_DEVICE();
    IFDEVICE(device, uuid)
    {
        auto onDescriptor = [=](std::optional<GattDescriptor> descriptor) {
            if (descriptor)
            {
                std::string serviceId = toStr(serviceUuid);
                std::string characteristicId = toStr(characteristicUuid);
                std::string descriptorId = toStr(descriptorUuid);
                auto writer = DataWriter();
                writer.WriteBytes(data);
                auto& value = writer.DetachBuffer();
                auto& asyncOp = descriptor->WriteValueWithResultAsync(value);
                asyncOp.Completed(bind2(this, &BLEManager::OnWriteValue, uuid, serviceId,
                                        characteristicId, descriptorId));
            }
            else
            {
                LOGE("descriptor not found");
            }
        };
        peripheral.GetDescriptor(serviceUuid, characteristicUuid, descriptorUuid, onDescriptor);
        return true;
    }
}

void BLEManager::OnWriteValue(IAsyncOperation<GattWriteResult> asyncOp, AsyncStatus status,
                              const std::string uuid, const std::string serviceId,
                              const std::string characteristicId, const std::string descriptorId)
{
    if (status == AsyncStatus::Completed)
    {
        mEmit.WriteValue(uuid, serviceId, characteristicId, descriptorId);
    }
    else
    {
        LOGE("WriteValue:Status: %d", status);
    }
}

bool BLEManager::ReadHandle(const std::string& uuid, int handle)
{
    CHECK_DEVICE();
    IFDEVICE(device, uuid)
    {
        LOGE("not available");
        return true;
    }
}

void BLEManager::OnReadHandle(IAsyncOperation<GattReadResult> asyncOp, AsyncStatus status,
                              const std::string uuid, const int handle)
{
    Data data(0);
    if (status == AsyncStatus::Completed)
    {
        GattReadResult& result = asyncOp.GetResults();
        if(CheckResult(result))
        {
            auto& value = result.Value();
            if (value)
            {
                auto& reader = DataReader::FromBuffer(value);
                Data data(reader.UnconsumedBufferLength());
                reader.ReadBytes(data);
                mEmit.ReadHandle(uuid, handle, data);
            }
            else
            {
                LOGE("GattReadResult::Value is null");
            }
        }
        else
        {
            LOGE("GattReadResult:: failed to read");
        }
    }
    else
    {
        LOGE("AsyncStatus failed: %d", status);
    }

    mEmit.ReadHandle(uuid, handle, data);
}

bool BLEManager::WriteHandle(const std::string& uuid, int handle, Data data)
{
    CHECK_DEVICE();
    IFDEVICE(device, uuid)
    {
        LOGE("not available");
        return true;
    }
}

void BLEManager::OnWriteHandle(IAsyncOperation<GattWriteResult> asyncOp, AsyncStatus status,
                               const std::string uuid, const int handle)
{
    if (status == AsyncStatus::Completed)
    {
        mEmit.WriteHandle(uuid, handle);
    }
    else
    {
        LOGE("status %d", status);
    }
}

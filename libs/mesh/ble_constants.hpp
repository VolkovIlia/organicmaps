#pragma once

#include <cstdint>
#include <string>

namespace mesh
{
/// \brief BLE service and characteristic UUIDs for P2P traffic sharing.
/// Using custom UUIDs to avoid conflicts with standard BLE services.
namespace BleUuid
{
// Service UUID: Organic Maps P2P Traffic (128-bit)
// Format: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
constexpr char kServiceUuid[] = "0000FF01-0000-1000-8000-00805F9B34FB";

// Characteristic: Traffic Data (read/notify)
constexpr char kTrafficDataUuid[] = "0000FF02-0000-1000-8000-00805F9B34FB";

// Characteristic: Gossip Gradients (read/write)
constexpr char kGossipGradientsUuid[] = "0000FF03-0000-1000-8000-00805F9B34FB";

// Characteristic: Device Info (read)
constexpr char kDeviceInfoUuid[] = "0000FF04-0000-1000-8000-00805F9B34FB";
}  // namespace BleUuid

/// \brief BLE advertisement constants.
namespace BleAdvertisement
{
// Advertisement interval (milliseconds)
constexpr uint32_t kMinIntervalMs = 100;
constexpr uint32_t kMaxIntervalMs = 500;

// Scan window/interval (milliseconds)
constexpr uint32_t kScanIntervalMs = 1000;
constexpr uint32_t kScanWindowMs = 200;

// Connection timeout (seconds)
constexpr uint32_t kConnectionTimeoutSec = 10;

// Max connections at once
constexpr uint8_t kMaxSimultaneousConnections = 3;

// Manufacturer ID for advertisement data (using 0xFFFF for development)
constexpr uint16_t kManufacturerId = 0xFFFF;
}  // namespace BleAdvertisement

/// \brief BLE message size limits.
namespace BleMessageSize
{
// BLE 4.2 MTU default
constexpr size_t kDefaultMtu = 23;

// BLE 5.0 extended MTU
constexpr size_t kExtendedMtu = 247;

// Max advertisement payload
constexpr size_t kMaxAdvertisementPayload = 31;

// Max characteristic value
constexpr size_t kMaxCharacteristicValue = 512;
}  // namespace BleMessageSize

/// \brief Connection state for BLE peers.
enum class ConnectionState : uint8_t
{
  Disconnected = 0,
  Connecting = 1,
  Connected = 2,
  Disconnecting = 3
};

std::string DebugPrint(ConnectionState state);
}  // namespace mesh

#pragma once

#include "mesh/ble_constants.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mesh
{
/// \brief Discovered BLE device information.
struct DiscoveredDevice
{
  std::string deviceId;
  std::string name;
  int rssi = 0;
  std::vector<uint8_t> advertisementData;
  uint64_t timestamp = 0;
};

/// \brief Abstract interface for BLE scanning.
/// Platform-specific implementations will be provided for Android/iOS.
class IBleScanner
{
public:
  using OnDeviceDiscovered = std::function<void(DiscoveredDevice const &)>;

  virtual ~IBleScanner() = default;

  /// \brief Start scanning for P2P traffic service.
  /// \return True if scanning started successfully.
  virtual bool StartScanning() = 0;

  /// \brief Stop scanning.
  virtual void StopScanning() = 0;

  /// \brief Check if currently scanning.
  virtual bool IsScanning() const = 0;

  /// \brief Set scan parameters.
  /// \param intervalMs Scan interval in milliseconds.
  /// \param windowMs Scan window in milliseconds.
  virtual void SetScanParameters(uint32_t intervalMs, uint32_t windowMs) = 0;

  /// \brief Set callback for device discovery.
  virtual void SetOnDeviceDiscovered(OnDeviceDiscovered callback) = 0;

  /// \brief Filter devices by service UUID.
  /// Only devices advertising the P2P traffic service will be reported.
  virtual void SetServiceFilter(std::string const & serviceUuid) = 0;
};
}  // namespace mesh

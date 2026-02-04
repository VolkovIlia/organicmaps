#pragma once

#include "mesh/ble_constants.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace mesh
{
/// \brief Abstract interface for BLE advertising.
/// Platform-specific implementations will be provided for Android/iOS.
class IBleAdvertiser
{
public:
  virtual ~IBleAdvertiser() = default;

  /// \brief Start advertising with the P2P traffic service UUID.
  /// \return True if advertising started successfully.
  virtual bool StartAdvertising() = 0;

  /// \brief Stop advertising.
  virtual void StopAdvertising() = 0;

  /// \brief Check if currently advertising.
  virtual bool IsAdvertising() const = 0;

  /// \brief Set advertisement data.
  /// \param data Custom data to include in advertisement (max 31 bytes).
  virtual void SetAdvertisementData(std::vector<uint8_t> const & data) = 0;

  /// \brief Set advertisement interval.
  /// \param minMs Minimum interval in milliseconds.
  /// \param maxMs Maximum interval in milliseconds.
  virtual void SetAdvertisementInterval(uint32_t minMs, uint32_t maxMs) = 0;
};
}  // namespace mesh

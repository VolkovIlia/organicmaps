#pragma once

#include "traffic/speed_groups.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace p2p
{
/// \brief Compact traffic data for P2P sharing.
/// Total size: 15 bytes (fits in single BLE advertisement).
struct AggregatedTrafficShare
{
  uint64_t m_h3Cell = 0;           // H3 cell index (8 bytes)
  uint8_t m_speedBucket = 0;       // SpeedGroup value (1 byte)
  uint8_t m_confidence = 0;        // Sample count, max 255 (1 byte)
  uint32_t m_timestampMinutes = 0; // Minutes since epoch / 5 (4 bytes)
  uint8_t m_hopCount = 0;          // Remaining hops (1 byte)
  // Total: 15 bytes

  /// \brief Check if share is valid.
  [[nodiscard]] bool IsValid() const;

  /// \brief Check if share has expired.
  [[nodiscard]] bool IsExpired(uint32_t currentMinutes, uint32_t ttlMinutes) const;

  /// \brief Get SpeedGroup from bucket.
  [[nodiscard]] traffic::SpeedGroup GetSpeedGroup() const;

  /// \brief Set speed bucket from SpeedGroup.
  void SetSpeedGroup(traffic::SpeedGroup group);

  /// \brief Increment confidence (capped at 255).
  void IncrementConfidence();

  /// \brief Decrement hop count for forwarding.
  /// \return true if can still be forwarded.
  [[nodiscard]] bool DecrementHop();
};

/// \brief Serialization format for AggregatedTrafficShare.
/// Wire format: [h3Cell:8][speedBucket:1][confidence:1][timestamp:4][hopCount:1]
class TrafficShareSerializer
{
public:
  static constexpr size_t kSerializedSize = 15;
  using SerializedData = std::array<uint8_t, kSerializedSize>;

  /// \brief Serialize to bytes.
  [[nodiscard]] static SerializedData Serialize(AggregatedTrafficShare const & share);

  /// \brief Deserialize from bytes.
  [[nodiscard]] static std::optional<AggregatedTrafficShare> Deserialize(
      uint8_t const * data, size_t size);

  /// \brief Deserialize from array.
  [[nodiscard]] static std::optional<AggregatedTrafficShare> Deserialize(
      SerializedData const & data);
};

/// \brief Builder for creating traffic shares.
class TrafficShareBuilder
{
public:
  TrafficShareBuilder & SetCell(uint64_t h3Cell);
  TrafficShareBuilder & SetSpeedGroup(traffic::SpeedGroup group);
  TrafficShareBuilder & SetConfidence(uint8_t confidence);
  TrafficShareBuilder & SetTimestamp(uint32_t minutes);
  TrafficShareBuilder & SetHopCount(uint8_t hops);

  [[nodiscard]] AggregatedTrafficShare Build() const;

  /// \brief Create share with current timestamp.
  [[nodiscard]] static AggregatedTrafficShare CreateNow(
      uint64_t h3Cell, traffic::SpeedGroup group, uint8_t confidence);

private:
  AggregatedTrafficShare m_share;
};

/// \brief Get current timestamp in 5-minute intervals.
[[nodiscard]] uint32_t GetCurrentTimestampMinutes();

std::string DebugPrint(AggregatedTrafficShare const & share);
}  // namespace p2p

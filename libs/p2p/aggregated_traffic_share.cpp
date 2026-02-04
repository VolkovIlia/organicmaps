#include "p2p/aggregated_traffic_share.hpp"

#include "p2p/privacy_settings.hpp"

#include <chrono>
#include <cstring>
#include <sstream>

namespace p2p
{
bool AggregatedTrafficShare::IsValid() const
{
  return m_h3Cell != 0 && m_confidence > 0;
}

bool AggregatedTrafficShare::IsExpired(uint32_t currentMinutes, uint32_t ttlMinutes) const
{
  if (m_timestampMinutes > currentMinutes)
    return false;  // Future timestamp (clock skew)
  return (currentMinutes - m_timestampMinutes) > ttlMinutes;
}

traffic::SpeedGroup AggregatedTrafficShare::GetSpeedGroup() const
{
  if (m_speedBucket > static_cast<uint8_t>(traffic::SpeedGroup::TempBlock))
    return traffic::SpeedGroup::Unknown;
  return static_cast<traffic::SpeedGroup>(m_speedBucket);
}

void AggregatedTrafficShare::SetSpeedGroup(traffic::SpeedGroup group)
{
  m_speedBucket = static_cast<uint8_t>(group);
}

void AggregatedTrafficShare::IncrementConfidence()
{
  if (m_confidence < 255)
    ++m_confidence;
}

bool AggregatedTrafficShare::DecrementHop()
{
  if (m_hopCount == 0)
    return false;
  --m_hopCount;
  return true;
}

// TrafficShareSerializer

TrafficShareSerializer::SerializedData TrafficShareSerializer::Serialize(
    AggregatedTrafficShare const & share)
{
  SerializedData data{};
  size_t offset = 0;

  // H3 cell (8 bytes, little-endian)
  std::memcpy(data.data() + offset, &share.m_h3Cell, 8);
  offset += 8;

  // Speed bucket (1 byte)
  data[offset++] = share.m_speedBucket;

  // Confidence (1 byte)
  data[offset++] = share.m_confidence;

  // Timestamp (4 bytes, little-endian)
  std::memcpy(data.data() + offset, &share.m_timestampMinutes, 4);
  offset += 4;

  // Hop count (1 byte)
  data[offset] = share.m_hopCount;

  return data;
}

std::optional<AggregatedTrafficShare> TrafficShareSerializer::Deserialize(
    uint8_t const * data, size_t size)
{
  if (!data || size < kSerializedSize)
    return std::nullopt;

  AggregatedTrafficShare share;
  size_t offset = 0;

  std::memcpy(&share.m_h3Cell, data + offset, 8);
  offset += 8;

  share.m_speedBucket = data[offset++];
  share.m_confidence = data[offset++];

  std::memcpy(&share.m_timestampMinutes, data + offset, 4);
  offset += 4;

  share.m_hopCount = data[offset];

  return share;
}

std::optional<AggregatedTrafficShare> TrafficShareSerializer::Deserialize(
    SerializedData const & data)
{
  return Deserialize(data.data(), data.size());
}

// TrafficShareBuilder

TrafficShareBuilder & TrafficShareBuilder::SetCell(uint64_t h3Cell)
{
  m_share.m_h3Cell = h3Cell;
  return *this;
}

TrafficShareBuilder & TrafficShareBuilder::SetSpeedGroup(traffic::SpeedGroup group)
{
  m_share.SetSpeedGroup(group);
  return *this;
}

TrafficShareBuilder & TrafficShareBuilder::SetConfidence(uint8_t confidence)
{
  m_share.m_confidence = confidence;
  return *this;
}

TrafficShareBuilder & TrafficShareBuilder::SetTimestamp(uint32_t minutes)
{
  m_share.m_timestampMinutes = minutes;
  return *this;
}

TrafficShareBuilder & TrafficShareBuilder::SetHopCount(uint8_t hops)
{
  m_share.m_hopCount = hops;
  return *this;
}

AggregatedTrafficShare TrafficShareBuilder::Build() const
{
  return m_share;
}

AggregatedTrafficShare TrafficShareBuilder::CreateNow(
    uint64_t h3Cell, traffic::SpeedGroup group, uint8_t confidence)
{
  return TrafficShareBuilder()
      .SetCell(h3Cell)
      .SetSpeedGroup(group)
      .SetConfidence(confidence)
      .SetTimestamp(GetCurrentTimestampMinutes())
      .SetHopCount(PrivacyConfig::kMaxHopCount)
      .Build();
}

uint32_t GetCurrentTimestampMinutes()
{
  auto const now = std::chrono::system_clock::now();
  auto const epoch = now.time_since_epoch();
  auto const minutes = std::chrono::duration_cast<std::chrono::minutes>(epoch);
  return static_cast<uint32_t>(minutes.count() / 5);  // 5-minute intervals
}

std::string DebugPrint(AggregatedTrafficShare const & share)
{
  std::ostringstream oss;
  oss << "TrafficShare ["
      << "cell=0x" << std::hex << share.m_h3Cell << std::dec
      << ", speed=" << traffic::DebugPrint(share.GetSpeedGroup())
      << ", conf=" << static_cast<int>(share.m_confidence)
      << ", time=" << share.m_timestampMinutes
      << ", hops=" << static_cast<int>(share.m_hopCount)
      << "]";
  return oss.str();
}
}  // namespace p2p

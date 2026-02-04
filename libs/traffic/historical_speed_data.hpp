#pragma once

#include "traffic/speed_groups.hpp"

#include "coding/reader.hpp"
#include "coding/writer.hpp"

#include "base/assert.hpp"

#include <array>
#include <cstdint>
#include <ctime>
#include <optional>
#include <vector>

namespace traffic
{
// Number of time slots: 24 hours * 7 days = 168
constexpr uint32_t kHoursPerDay = 24;
constexpr uint32_t kDaysPerWeek = 7;
constexpr uint32_t kTimeSlots = kHoursPerDay * kDaysPerWeek;

// Speed is stored as percentage of free-flow speed (0-200%)
// 0 = no data, 1-200 = percentage, 255 = blocked
using SpeedPercentage = uint8_t;
constexpr SpeedPercentage kNoSpeedData = 0;
constexpr SpeedPercentage kBlockedSpeed = 255;

/// \brief Converts day of week (0=Sunday) and hour (0-23) to time slot index.
inline uint32_t TimeSlotIndex(uint8_t dayOfWeek, uint8_t hour)
{
  ASSERT_LESS(dayOfWeek, kDaysPerWeek, ());
  ASSERT_LESS(hour, kHoursPerDay, ());
  return static_cast<uint32_t>(dayOfWeek) * kHoursPerDay + hour;
}

/// \brief Converts time_t to time slot index using local time.
uint32_t TimeSlotIndexFromTime(std::time_t time);

/// \brief Extracts day of week (0=Sunday) and hour from time slot index.
inline void TimeSlotToComponents(uint32_t slot, uint8_t & dayOfWeek, uint8_t & hour)
{
  ASSERT_LESS(slot, kTimeSlots, ());
  dayOfWeek = static_cast<uint8_t>(slot / kHoursPerDay);
  hour = static_cast<uint8_t>(slot % kHoursPerDay);
}

/// \brief Historical speed pattern for a single road segment.
/// Stores 168 speed values (one per hour for each day of the week).
/// Speed is stored as percentage of free-flow speed to save space.
class SegmentSpeedPattern
{
public:
  SegmentSpeedPattern() { m_speeds.fill(kNoSpeedData); }

  /// \brief Get speed percentage for given time slot.
  /// \return Speed as percentage of free-flow (1-200), 0 if no data, 255 if blocked.
  SpeedPercentage GetSpeed(uint32_t timeSlot) const
  {
    ASSERT_LESS(timeSlot, kTimeSlots, ());
    return m_speeds[timeSlot];
  }

  /// \brief Get speed percentage for given day and hour.
  SpeedPercentage GetSpeed(uint8_t dayOfWeek, uint8_t hour) const
  {
    return GetSpeed(TimeSlotIndex(dayOfWeek, hour));
  }

  /// \brief Set speed percentage for given time slot.
  void SetSpeed(uint32_t timeSlot, SpeedPercentage speed)
  {
    ASSERT_LESS(timeSlot, kTimeSlots, ());
    m_speeds[timeSlot] = speed;
  }

  /// \brief Set speed percentage for given day and hour.
  void SetSpeed(uint8_t dayOfWeek, uint8_t hour, SpeedPercentage speed)
  {
    SetSpeed(TimeSlotIndex(dayOfWeek, hour), speed);
  }

  /// \brief Check if this pattern has any data.
  bool HasData() const
  {
    for (auto speed : m_speeds)
    {
      if (speed != kNoSpeedData)
        return true;
    }
    return false;
  }

  /// \brief Get raw speed array for serialization.
  std::array<SpeedPercentage, kTimeSlots> const & GetRawSpeeds() const { return m_speeds; }

  /// \brief Set raw speed array from deserialization.
  void SetRawSpeeds(std::array<SpeedPercentage, kTimeSlots> const & speeds) { m_speeds = speeds; }

  bool operator==(SegmentSpeedPattern const & rhs) const { return m_speeds == rhs.m_speeds; }

private:
  std::array<SpeedPercentage, kTimeSlots> m_speeds;
};

/// \brief Converts SpeedPercentage to SpeedGroup for rendering.
SpeedGroup SpeedPercentageToGroup(SpeedPercentage percentage);

/// \brief Converts SpeedGroup to approximate SpeedPercentage.
SpeedPercentage SpeedGroupToPercentage(SpeedGroup group);

/// \brief Container for historical speed patterns indexed by feature segment.
/// Uses compact storage with delta-encoding compression.
class HistoricalSpeedData
{
public:
  static uint8_t const kLatestVersion;

  HistoricalSpeedData() = default;

  /// \brief Add or update pattern for a segment.
  /// \param featureId Feature ID in MWM.
  /// \param segmentIdx Segment index within feature.
  /// \param isForward Direction flag (true = forward).
  /// \param pattern Speed pattern to store.
  void SetPattern(uint32_t featureId, uint16_t segmentIdx, bool isForward,
                  SegmentSpeedPattern const & pattern);

  /// \brief Get pattern for a segment if available.
  [[nodiscard]] std::optional<SegmentSpeedPattern> GetPattern(uint32_t featureId, uint16_t segmentIdx,
                                                               bool isForward) const;

  /// \brief Check if data exists for a segment.
  [[nodiscard]] bool HasPattern(uint32_t featureId, uint16_t segmentIdx, bool isForward) const;

  /// \brief Get speed for a segment at specific time.
  /// \return Speed percentage (1-200), 0 if no data.
  [[nodiscard]] SpeedPercentage GetSpeed(uint32_t featureId, uint16_t segmentIdx, bool isForward,
                                          std::time_t time) const;

  /// \brief Get speed group for a segment at specific time (for rendering).
  [[nodiscard]] SpeedGroup GetSpeedGroup(uint32_t featureId, uint16_t segmentIdx, bool isForward,
                                          std::time_t time) const;

  /// \brief Get number of segments with patterns.
  size_t GetSegmentCount() const { return m_patterns.size(); }

  /// \brief Clear all data.
  void Clear() { m_patterns.clear(); }

  /// \brief Iterate over all patterns with callback.
  /// \param callback Function called with (featureId, segmentIdx, isForward, pattern).
  template <typename Callback>
  void ForEachPattern(Callback && callback) const
  {
    for (auto const & [key, pattern] : m_patterns)
    {
      callback(key.m_featureId, key.m_segmentIdx, key.m_isForward, pattern);
    }
  }

  /// \brief Serialize to binary format with delta-encoding compression.
  template <typename Sink>
  void Serialize(Sink & sink) const;

  /// \brief Deserialize from binary format.
  template <typename Source>
  void Deserialize(Source & source);

private:
  /// Key for segment lookup: combines feature ID, segment index, and direction.
  struct SegmentKey
  {
    uint32_t m_featureId;
    uint16_t m_segmentIdx;
    bool m_isForward;

    bool operator<(SegmentKey const & rhs) const
    {
      if (m_featureId != rhs.m_featureId)
        return m_featureId < rhs.m_featureId;
      if (m_segmentIdx != rhs.m_segmentIdx)
        return m_segmentIdx < rhs.m_segmentIdx;
      return m_isForward < rhs.m_isForward;
    }

    bool operator==(SegmentKey const & rhs) const
    {
      return m_featureId == rhs.m_featureId &&
             m_segmentIdx == rhs.m_segmentIdx &&
             m_isForward == rhs.m_isForward;
    }
  };

  /// Sorted vector of (key, pattern) pairs for efficient lookup and serialization.
  std::vector<std::pair<SegmentKey, SegmentSpeedPattern>> m_patterns;

  /// Binary search for segment key.
  std::vector<std::pair<SegmentKey, SegmentSpeedPattern>>::const_iterator
  FindPattern(SegmentKey const & key) const;

  std::vector<std::pair<SegmentKey, SegmentSpeedPattern>>::iterator
  FindPattern(SegmentKey const & key);
};

// Template implementations

template <typename Sink>
void HistoricalSpeedData::Serialize(Sink & sink) const
{
  // Version
  WriteToSink(sink, kLatestVersion);

  // Number of patterns
  uint32_t const count = static_cast<uint32_t>(m_patterns.size());
  WriteToSink(sink, count);

  if (count == 0)
    return;

  // Delta-encode feature IDs
  uint32_t prevFeatureId = 0;
  for (auto const & [key, pattern] : m_patterns)
  {
    // Write delta-encoded feature ID
    uint32_t const delta = key.m_featureId - prevFeatureId;
    WriteToSink(sink, delta);
    prevFeatureId = key.m_featureId;

    // Write segment index and direction
    WriteToSink(sink, key.m_segmentIdx);
    WriteToSink(sink, static_cast<uint8_t>(key.m_isForward ? 1 : 0));

    // Write speed pattern with simple RLE compression
    auto const & speeds = pattern.GetRawSpeeds();

    // Simple compression: run-length encode sequences of same values
    uint32_t i = 0;
    while (i < kTimeSlots)
    {
      SpeedPercentage const value = speeds[i];
      uint8_t runLength = 1;

      // Count consecutive same values (max 255)
      while (i + runLength < kTimeSlots &&
             speeds[i + runLength] == value &&
             runLength < 255)
      {
        ++runLength;
      }

      // Write value and run length
      WriteToSink(sink, value);
      WriteToSink(sink, runLength);

      i += runLength;
    }
  }
}

template <typename Source>
void HistoricalSpeedData::Deserialize(Source & source)
{
  m_patterns.clear();

  // Version check
  uint8_t const version = ReadPrimitiveFromSource<uint8_t>(source);
  if (version != kLatestVersion)
  {
    LOG(LWARNING, ("Unsupported historical speed data version:", version));
    return;
  }

  // Number of patterns
  uint32_t const count = ReadPrimitiveFromSource<uint32_t>(source);
  m_patterns.reserve(count);

  uint32_t prevFeatureId = 0;
  for (uint32_t n = 0; n < count; ++n)
  {
    SegmentKey key;

    // Read delta-encoded feature ID
    uint32_t const delta = ReadPrimitiveFromSource<uint32_t>(source);
    key.m_featureId = prevFeatureId + delta;
    prevFeatureId = key.m_featureId;

    // Read segment index and direction
    key.m_segmentIdx = ReadPrimitiveFromSource<uint16_t>(source);
    key.m_isForward = ReadPrimitiveFromSource<uint8_t>(source) != 0;

    // Read RLE-compressed speed pattern
    SegmentSpeedPattern pattern;
    std::array<SpeedPercentage, kTimeSlots> speeds;
    speeds.fill(kNoSpeedData);  // Initialize with no-data

    uint32_t i = 0;
    while (i < kTimeSlots)
    {
      SpeedPercentage const value = ReadPrimitiveFromSource<SpeedPercentage>(source);
      uint8_t const runLength = ReadPrimitiveFromSource<uint8_t>(source);

      // Check for malformed data: runLength would exceed remaining slots
      if (i + runLength > kTimeSlots)
      {
        LOG(LWARNING, ("Malformed RLE data: runLength", runLength,
                       "exceeds remaining slots at position", i, "- truncating"));
      }

      for (uint8_t j = 0; j < runLength && i < kTimeSlots; ++j, ++i)
        speeds[i] = value;
    }

    pattern.SetRawSpeeds(speeds);
    m_patterns.emplace_back(key, std::move(pattern));
  }
}

std::string DebugPrint(SegmentSpeedPattern const & pattern);
std::string DebugPrint(HistoricalSpeedData const & data);

}  // namespace traffic

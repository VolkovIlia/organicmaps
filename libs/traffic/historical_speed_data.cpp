#include "traffic/historical_speed_data.hpp"

#include "base/logging.hpp"

#include <algorithm>
#include <ctime>
#include <sstream>

namespace traffic
{
uint8_t const HistoricalSpeedData::kLatestVersion = 1;

uint32_t TimeSlotIndexFromTime(std::time_t time)
{
  std::tm localTime;
#ifdef _WIN32
  if (localtime_s(&localTime, &time) != 0)
    return 0;
#else
  if (localtime_r(&time, &localTime) == nullptr)
    return 0;
#endif

  // tm_wday: 0 = Sunday, which matches our convention
  uint8_t const dayOfWeek = static_cast<uint8_t>(localTime.tm_wday);
  uint8_t const hour = static_cast<uint8_t>(localTime.tm_hour);

  return TimeSlotIndex(dayOfWeek, hour);
}

SpeedGroup SpeedPercentageToGroup(SpeedPercentage percentage)
{
  if (percentage == kNoSpeedData)
    return SpeedGroup::Unknown;

  if (percentage == kBlockedSpeed)
    return SpeedGroup::TempBlock;

  // Map percentage to SpeedGroup using thresholds from speed_groups.hpp
  // G0: 0-8%, G1: 8-16%, G2: 16-33%, G3: 33-58%, G4: 58-83%, G5: 83-100%
  if (percentage <= 8)
    return SpeedGroup::G0;
  if (percentage <= 16)
    return SpeedGroup::G1;
  if (percentage <= 33)
    return SpeedGroup::G2;
  if (percentage <= 58)
    return SpeedGroup::G3;
  if (percentage <= 83)
    return SpeedGroup::G4;
  return SpeedGroup::G5;
}

SpeedPercentage SpeedGroupToPercentage(SpeedGroup group)
{
  switch (group)
  {
  case SpeedGroup::G0: return 4;    // Middle of 0-8%
  case SpeedGroup::G1: return 12;   // Middle of 8-16%
  case SpeedGroup::G2: return 25;   // Middle of 16-33%
  case SpeedGroup::G3: return 45;   // Middle of 33-58%
  case SpeedGroup::G4: return 70;   // Middle of 58-83%
  case SpeedGroup::G5: return 92;   // Middle of 83-100%
  case SpeedGroup::TempBlock: return kBlockedSpeed;
  case SpeedGroup::Unknown:
  case SpeedGroup::Count:
  default:
    return kNoSpeedData;
  }
}

void HistoricalSpeedData::SetPattern(uint32_t featureId, uint16_t segmentIdx, bool isForward,
                                     SegmentSpeedPattern const & pattern)
{
  SegmentKey const key{featureId, segmentIdx, isForward};

  auto it = FindPattern(key);
  if (it != m_patterns.end() && it->first == key)
  {
    // Update existing pattern
    it->second = pattern;
  }
  else
  {
    // Insert new pattern maintaining sorted order
    m_patterns.insert(it, {key, pattern});
  }
}

std::optional<SegmentSpeedPattern> HistoricalSpeedData::GetPattern(uint32_t featureId,
                                                                    uint16_t segmentIdx,
                                                                    bool isForward) const
{
  SegmentKey const key{featureId, segmentIdx, isForward};
  auto it = FindPattern(key);
  if (it != m_patterns.end() && it->first == key)
    return it->second;
  return std::nullopt;
}

bool HistoricalSpeedData::HasPattern(uint32_t featureId, uint16_t segmentIdx, bool isForward) const
{
  SegmentKey const key{featureId, segmentIdx, isForward};
  auto it = FindPattern(key);
  return it != m_patterns.end() && it->first == key;
}

SpeedPercentage HistoricalSpeedData::GetSpeed(uint32_t featureId, uint16_t segmentIdx,
                                               bool isForward, std::time_t time) const
{
  auto pattern = GetPattern(featureId, segmentIdx, isForward);
  if (!pattern)
    return kNoSpeedData;

  uint32_t const timeSlot = TimeSlotIndexFromTime(time);
  return pattern->GetSpeed(timeSlot);
}

SpeedGroup HistoricalSpeedData::GetSpeedGroup(uint32_t featureId, uint16_t segmentIdx,
                                               bool isForward, std::time_t time) const
{
  SpeedPercentage const speed = GetSpeed(featureId, segmentIdx, isForward, time);
  return SpeedPercentageToGroup(speed);
}

std::vector<std::pair<HistoricalSpeedData::SegmentKey, SegmentSpeedPattern>>::const_iterator
HistoricalSpeedData::FindPattern(SegmentKey const & key) const
{
  return std::lower_bound(
      m_patterns.begin(), m_patterns.end(), key,
      [](auto const & pair, SegmentKey const & k) { return pair.first < k; });
}

std::vector<std::pair<HistoricalSpeedData::SegmentKey, SegmentSpeedPattern>>::iterator
HistoricalSpeedData::FindPattern(SegmentKey const & key)
{
  return std::lower_bound(
      m_patterns.begin(), m_patterns.end(), key,
      [](auto const & pair, SegmentKey const & k) { return pair.first < k; });
}

std::string DebugPrint(SegmentSpeedPattern const & pattern)
{
  std::ostringstream oss;
  oss << "SegmentSpeedPattern [";
  auto const & speeds = pattern.GetRawSpeeds();
  size_t nonZeroCount = 0;
  for (auto s : speeds)
  {
    if (s != kNoSpeedData)
      ++nonZeroCount;
  }
  oss << nonZeroCount << "/" << kTimeSlots << " slots with data]";
  return oss.str();
}

std::string DebugPrint(HistoricalSpeedData const & data)
{
  std::ostringstream oss;
  oss << "HistoricalSpeedData [" << data.GetSegmentCount() << " segments]";
  return oss.str();
}

}  // namespace traffic

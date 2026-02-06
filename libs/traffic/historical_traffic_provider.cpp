#include "traffic/historical_traffic_provider.hpp"

#include "defines.hpp"

#include "routing_common/car_model.hpp"

#include "indexer/feature.hpp"
#include "indexer/feature_processor.hpp"
#include "indexer/ftypes_matcher.hpp"

#include "coding/reader.hpp"

#include "base/logging.hpp"

namespace traffic
{
namespace
{
// Rush hour patterns (percentage of free-flow speed)
// Weekday morning rush: 7-9 AM
// Weekday evening rush: 5-7 PM
// Weekend: Generally higher speeds

struct TimePattern
{
  uint8_t hourStart;
  uint8_t hourEnd;
  uint8_t weekdaySpeedPercent;   // Mon-Fri
  uint8_t weekendSpeedPercent;   // Sat-Sun
};

// Default patterns for different road types
TimePattern const kMajorRoadPatterns[] = {
    {0, 5, 95, 95},     // Night: nearly free flow
    {5, 7, 75, 90},     // Early morning: light traffic weekdays
    {7, 9, 55, 85},     // Morning rush: heavy weekdays, light weekends
    {9, 12, 70, 85},    // Mid-morning: moderate
    {12, 14, 65, 80},   // Lunch: moderate
    {14, 16, 70, 80},   // Afternoon: moderate
    {16, 19, 50, 75},   // Evening rush: heaviest weekdays
    {19, 22, 75, 85},   // Evening: lighter
    {22, 24, 90, 90},   // Late evening: light
};

TimePattern const kMinorRoadPatterns[] = {
    {0, 6, 95, 95},     // Night
    {6, 8, 80, 90},     // Morning
    {8, 10, 70, 85},    // Late morning
    {10, 17, 80, 85},   // Daytime
    {17, 19, 65, 80},   // Evening rush (less impacted)
    {19, 22, 85, 90},   // Evening
    {22, 24, 95, 95},   // Late evening
};

TimePattern const kResidentialPatterns[] = {
    {0, 7, 95, 95},     // Night/early morning
    {7, 9, 75, 85},     // Morning (school drop-off)
    {9, 15, 90, 90},    // Daytime
    {15, 17, 75, 85},   // Afternoon (school pickup)
    {17, 19, 80, 85},   // Evening
    {19, 24, 90, 90},   // Late evening
};

uint8_t GetPatternSpeed(TimePattern const * patterns, size_t patternCount, uint8_t hour,
                        bool isWeekend)
{
  for (size_t i = 0; i < patternCount; ++i)
  {
    if (hour >= patterns[i].hourStart && hour < patterns[i].hourEnd)
      return isWeekend ? patterns[i].weekendSpeedPercent : patterns[i].weekdaySpeedPercent;
  }
  return 85;  // Default
}

bool IsMajorRoad(routing::HighwayType hwType)
{
  switch (hwType)
  {
  case routing::HighwayType::HighwayMotorway:
  case routing::HighwayType::HighwayMotorwayLink:
  case routing::HighwayType::HighwayTrunk:
  case routing::HighwayType::HighwayTrunkLink:
  case routing::HighwayType::HighwayPrimary:
  case routing::HighwayType::HighwayPrimaryLink:
    return true;
  default:
    return false;
  }
}

bool IsMinorRoad(routing::HighwayType hwType)
{
  switch (hwType)
  {
  case routing::HighwayType::HighwaySecondary:
  case routing::HighwayType::HighwaySecondaryLink:
  case routing::HighwayType::HighwayTertiary:
  case routing::HighwayType::HighwayTertiaryLink:
  case routing::HighwayType::HighwayUnclassified:
    return true;
  default:
    return false;
  }
}

bool IsResidentialRoad(routing::HighwayType hwType)
{
  switch (hwType)
  {
  case routing::HighwayType::HighwayResidential:
  case routing::HighwayType::HighwayLivingStreet:
  case routing::HighwayType::HighwayService:
    return true;
  default:
    return false;
  }
}

/// @brief Apply deterministic spatial variation to a speed value using feature ID hash.
/// Uses Fibonacci hashing for good distribution. Returns clamped to [5, 100].
uint8_t ApplySpatialVariation(uint8_t baseSpeed, uint32_t featureId, uint8_t maxVariation)
{
  // Fibonacci hash for good distribution across feature IDs
  uint32_t const hash = featureId * 2654435761u;
  // Map hash to [-maxVariation, +maxVariation] range
  int const variation = static_cast<int>(hash % (2 * maxVariation + 1)) - maxVariation;
  int const result = static_cast<int>(baseSpeed) + variation;
  return static_cast<uint8_t>(std::max(5, std::min(100, result)));
}

SegmentSpeedPattern GeneratePatternForRoadType(routing::HighwayType hwType)
{
  SegmentSpeedPattern pattern;

  for (uint8_t dayOfWeek = 0; dayOfWeek < kDaysPerWeek; ++dayOfWeek)
  {
    bool const isWeekend = (dayOfWeek == 0 || dayOfWeek == 6);  // Sunday or Saturday

    for (uint8_t hour = 0; hour < kHoursPerDay; ++hour)
    {
      uint8_t speedPercent;

      if (IsMajorRoad(hwType))
      {
        speedPercent = GetPatternSpeed(kMajorRoadPatterns,
                                       std::size(kMajorRoadPatterns), hour, isWeekend);
      }
      else if (IsMinorRoad(hwType))
      {
        speedPercent = GetPatternSpeed(kMinorRoadPatterns,
                                       std::size(kMinorRoadPatterns), hour, isWeekend);
      }
      else if (IsResidentialRoad(hwType))
      {
        speedPercent = GetPatternSpeed(kResidentialPatterns,
                                       std::size(kResidentialPatterns), hour, isWeekend);
      }
      else
      {
        // Other roads: minimal variation
        speedPercent = 90;
      }

      pattern.SetSpeed(dayOfWeek, hour, speedPercent);
    }
  }

  return pattern;
}

}  // namespace
std::optional<TrafficInfo::Coloring> HistoricalTrafficProvider::GetColoring(
    MwmSet::MwmId const & mwmId, time_t timestamp) const
{
  if (!mwmId.IsAlive())
    return std::nullopt;

  // Use current time if not specified
  if (timestamp == 0)
    timestamp = std::time(nullptr);

  std::lock_guard<std::mutex> lock(m_mutex);

  // Check cache first
  auto it = m_cache.find(mwmId);
  if (it == m_cache.end())
  {
    // Try to load
    if (!LoadHistoricalData(mwmId))
      return std::nullopt;
    it = m_cache.find(mwmId);
    if (it == m_cache.end())
      return std::nullopt;
  }

  return ConvertToColoring(*it->second, timestamp);
}

bool HistoricalTrafficProvider::HasData(MwmSet::MwmId const & mwmId) const
{
  if (!mwmId.IsAlive())
    return false;

  // We can always generate synthetic data, so return true for any valid MWM
  return true;
}

void HistoricalTrafficProvider::ClearCache(MwmSet::MwmId const & mwmId)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_cache.erase(mwmId);
}

void HistoricalTrafficProvider::Clear()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_cache.clear();
}

size_t HistoricalTrafficProvider::GetCachedMwmCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_cache.size();
}

bool HistoricalTrafficProvider::LoadHistoricalData(MwmSet::MwmId const & mwmId) const
{
  if (!mwmId.IsAlive())
    return false;

  auto const & mwmValue = *mwmId.GetInfo();
  std::string const mwmPath = mwmValue.GetLocalFile().GetPath(MapFileType::Map);

  try
  {
    FilesContainerR container(mwmPath);
    if (!container.IsExist(HISTORICAL_SPEEDS_FILE_TAG))
    {
      LOG(LINFO, ("No historical speeds data in", mwmPath, ", generating synthetic data"));
      return GenerateSyntheticData(mwmId);
    }

    auto reader = container.GetReader(HISTORICAL_SPEEDS_FILE_TAG);
    ReaderSource<FilesContainerR::TReader> source(reader);

    auto data = std::make_shared<HistoricalSpeedData>();
    data->Deserialize(source);

    m_cache[mwmId] = std::move(data);

    LOG(LINFO, ("Loaded historical speeds for", mwmPath,
                "segments:", m_cache[mwmId]->GetSegmentCount()));
    return true;
  }
  catch (RootException const & e)
  {
    LOG(LWARNING, ("Error loading historical speeds from", mwmPath, ":", e.Msg()));
    return false;
  }
}

bool HistoricalTrafficProvider::GenerateSyntheticData(MwmSet::MwmId const & mwmId) const
{
  if (!mwmId.IsAlive())
    return false;

  auto const & mwmValue = *mwmId.GetInfo();
  std::string const mwmPath = mwmValue.GetLocalFile().GetPath(MapFileType::Map);

  LOG(LINFO, ("Generating synthetic historical speeds for:", mwmPath));

  try
  {
    auto data = std::make_shared<HistoricalSpeedData>();
    auto const & carModel = routing::CarModel::AllLimitsInstance();

    size_t segmentCount = 0;
    size_t roadCount = 0;

    feature::ForEachFeature(mwmPath, [&](FeatureType & ft, uint32_t const fid) {
      feature::TypesHolder const types(ft);

      if (!carModel.IsRoad(types))
        return;

      ++roadCount;

      auto const hwType = carModel.GetHighwayType(types);
      if (!hwType)
        return;

      // Skip residential/service roads — they clutter the traffic layer
      // and are almost always green (free-flow). Saves ~50-70% memory.
      if (IsResidentialRoad(*hwType))
        return;

      ft.ParseGeometry(FeatureType::BEST_GEOMETRY);
      auto const numPoints = static_cast<uint16_t>(ft.GetPointsCount());
      bool const isOneWay = carModel.IsOneWay(types);

      // Generate base pattern for this road type, then apply per-feature variation
      SegmentSpeedPattern pattern = GeneratePatternForRoadType(*hwType);

      // Apply spatial variation: major roads ±8pp, minor roads ±12pp
      uint8_t const maxVariation = IsMajorRoad(*hwType) ? 8 : 12;
      for (uint8_t dayOfWeek = 0; dayOfWeek < kDaysPerWeek; ++dayOfWeek)
      {
        for (uint8_t hour = 0; hour < kHoursPerDay; ++hour)
        {
          uint8_t const base = pattern.GetSpeed(dayOfWeek, hour);
          uint8_t const varied = ApplySpatialVariation(base, fid, maxVariation);
          pattern.SetSpeed(dayOfWeek, hour, varied);
        }
      }

      // Add pattern for each segment
      for (uint16_t i = 0; i + 1 < numPoints; ++i)
      {
        data->SetPattern(fid, i, true /* forward */, pattern);
        ++segmentCount;

        if (!isOneWay)
        {
          data->SetPattern(fid, i, false /* backward */, pattern);
          ++segmentCount;
        }
      }
    });

    LOG(LINFO, ("Generated patterns for", segmentCount, "segments from", roadCount, "roads"));

    if (segmentCount == 0)
    {
      LOG(LWARNING, ("No road segments found in MWM for synthetic generation"));
      return false;
    }

    m_cache[mwmId] = std::move(data);
    return true;
  }
  catch (RootException const & e)
  {
    LOG(LERROR, ("Failed to generate synthetic historical speeds:", e.Msg()));
    return false;
  }
}

TrafficInfo::Coloring HistoricalTrafficProvider::ConvertToColoring(
    HistoricalSpeedData const & data, time_t timestamp) const
{
  TrafficInfo::Coloring coloring;

  // Get time components
  struct tm timeInfo;
#ifdef _WIN32
  localtime_s(&timeInfo, &timestamp);
#else
  localtime_r(&timestamp, &timeInfo);
#endif

  uint8_t const dayOfWeek = static_cast<uint8_t>(timeInfo.tm_wday);  // 0 = Sunday
  uint8_t const hour = static_cast<uint8_t>(timeInfo.tm_hour);

  // Iterate through all patterns and convert to coloring
  data.ForEachPattern([&](uint32_t featureId, uint16_t segmentIdx, bool forward,
                          SegmentSpeedPattern const & pattern) {
    SpeedPercentage const speed = pattern.GetSpeed(dayOfWeek, hour);
    if (speed == kNoSpeedData)
      return;

    SpeedGroup const group = SpeedPercentageToGroup(speed);
    if (group == SpeedGroup::Unknown)
      return;

    TrafficInfo::RoadSegmentId segId(
        featureId, segmentIdx,
        forward ? TrafficInfo::RoadSegmentId::kForwardDirection
                : TrafficInfo::RoadSegmentId::kReverseDirection);
    coloring[segId] = group;
  });

  return coloring;
}

TrafficInfo CreateHistoricalTrafficInfo(MwmSet::MwmId const & mwmId,
                                        TrafficInfo::Coloring && coloring)
{
  return TrafficInfo::BuildForTesting(std::move(coloring));
}

}  // namespace traffic

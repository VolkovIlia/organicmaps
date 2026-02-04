#include "generator/historical_speeds_generator.hpp"

#include "traffic/historical_speed_data.hpp"
#include "traffic/osm_speed_inference.hpp"
#include "traffic/traffic_info.hpp"

#include "routing_common/car_model.hpp"

#include "indexer/feature_algo.hpp"
#include "indexer/feature_processor.hpp"

#include "coding/file_reader.hpp"
#include "coding/file_writer.hpp"
#include "coding/files_container.hpp"

#include "base/logging.hpp"
#include "base/string_utils.hpp"

#include "defines.hpp"

#include <fstream>
#include <sstream>
#include <vector>

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

bool GenerateHistoricalSpeedsFromCSV(std::string const & mwmPath,
                                      std::string const & speedPatternsPath)
{
  LOG(LINFO, ("Generating historical speeds from CSV:", speedPatternsPath));

  try
  {
    HistoricalSpeedData data;

    std::ifstream file(speedPatternsPath);
    if (!file)
    {
      LOG(LERROR, ("Failed to open speed patterns file:", speedPatternsPath));
      return false;
    }

    std::string line;
    size_t lineNum = 0;
    size_t segmentCount = 0;

    // Skip header line
    if (std::getline(file, line))
      ++lineNum;

    while (std::getline(file, line))
    {
      ++lineNum;

      std::istringstream iss(line);
      std::string token;

      // Parse: feature_id, segment_idx, direction, speed_0, speed_1, ..., speed_167
      std::vector<std::string> tokens;
      while (std::getline(iss, token, ','))
        tokens.push_back(token);

      if (tokens.size() < 3 + kTimeSlots)
      {
        LOG(LWARNING, ("Invalid line", lineNum, "- expected", 3 + kTimeSlots, "columns, got",
                       tokens.size()));
        continue;
      }

      uint32_t featureId;
      uint16_t segmentIdx;
      uint8_t direction;

      if (!strings::to_uint32(tokens[0], featureId) ||
          !strings::to_uint(tokens[1], segmentIdx) ||
          !strings::to_uint(tokens[2], direction))
      {
        LOG(LWARNING, ("Failed to parse segment info at line", lineNum));
        continue;
      }

      SegmentSpeedPattern pattern;
      for (uint32_t i = 0; i < kTimeSlots; ++i)
      {
        uint32_t speed;
        if (strings::to_uint32(tokens[3 + i], speed))
          pattern.SetSpeed(i, static_cast<SpeedPercentage>(std::min(speed, 200u)));
      }

      bool const isForward = (direction == 0);
      data.SetPattern(featureId, segmentIdx, isForward, pattern);
      ++segmentCount;
    }

    LOG(LINFO, ("Loaded", segmentCount, "segment patterns from CSV"));

    // Serialize and write to MWM
    std::vector<uint8_t> buffer;
    MemWriter<std::vector<uint8_t>> writer(buffer);
    data.Serialize(writer);

    FilesContainerW container(mwmPath, FileWriter::OP_WRITE_EXISTING);
    auto mwmWriter = container.GetWriter(HISTORICAL_SPEEDS_FILE_TAG);
    mwmWriter->Write(buffer.data(), buffer.size());

    LOG(LINFO, ("Written historical speeds section:", buffer.size(), "bytes"));
    return true;
  }
  catch (RootException const & e)
  {
    LOG(LERROR, ("Failed to generate historical speeds:", e.Msg()));
    return false;
  }
}

bool GenerateHistoricalSpeedsFromUberMovement(std::string const & mwmPath,
                                               std::string const & uberMovementPath,
                                               std::string const & mappingPath)
{
  LOG(LINFO, ("Generating historical speeds from Uber Movement data"));
  LOG(LWARNING, ("Uber Movement integration not yet implemented - using synthetic data"));

  // For now, fall back to synthetic generation
  // TODO: Implement Uber Movement data parsing when data format is defined
  return GenerateSyntheticHistoricalSpeeds(mwmPath);
}

bool GenerateSyntheticHistoricalSpeeds(std::string const & mwmPath)
{
  LOG(LINFO, ("Generating synthetic historical speeds for:", mwmPath));

  try
  {
    HistoricalSpeedData data;
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

      ft.ParseGeometry(FeatureType::BEST_GEOMETRY);
      auto const numPoints = static_cast<uint16_t>(ft.GetPointsCount());
      bool const isOneWay = carModel.IsOneWay(types);

      // Generate pattern for this road type
      SegmentSpeedPattern const pattern = GeneratePatternForRoadType(*hwType);

      // Add pattern for each segment
      for (uint16_t i = 0; i + 1 < numPoints; ++i)
      {
        data.SetPattern(fid, i, true /* forward */, pattern);
        ++segmentCount;

        if (!isOneWay)
        {
          data.SetPattern(fid, i, false /* backward */, pattern);
          ++segmentCount;
        }
      }
    });

    LOG(LINFO, ("Generated patterns for", segmentCount, "segments from", roadCount, "roads"));

    if (segmentCount == 0)
    {
      LOG(LWARNING, ("No road segments found in MWM"));
      return true;  // Not an error, just empty
    }

    // Serialize and write to MWM
    std::vector<uint8_t> buffer;
    MemWriter<std::vector<uint8_t>> writer(buffer);
    data.Serialize(writer);

    FilesContainerW container(mwmPath, FileWriter::OP_WRITE_EXISTING);
    auto mwmWriter = container.GetWriter(HISTORICAL_SPEEDS_FILE_TAG);
    mwmWriter->Write(buffer.data(), buffer.size());

    LOG(LINFO, ("Written historical speeds section:", buffer.size(), "bytes for", segmentCount,
                "segments"));
    return true;
  }
  catch (RootException const & e)
  {
    LOG(LERROR, ("Failed to generate synthetic historical speeds:", e.Msg()));
    return false;
  }
}

bool HasHistoricalSpeedsSection(std::string const & mwmPath)
{
  try
  {
    FilesContainerR container(mwmPath);
    return container.IsExist(HISTORICAL_SPEEDS_FILE_TAG);
  }
  catch (RootException const &)
  {
    return false;
  }
}

}  // namespace traffic

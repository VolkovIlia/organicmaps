#include "traffic/traffic_ml_model.hpp"

#include <cmath>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace traffic
{
// Road type category indices for one-hot encoding:
// 0: Motorway (HighwayMotorway, HighwayMotorwayLink)
// 1: Trunk (HighwayTrunk, HighwayTrunkLink)
// 2: Primary (HighwayPrimary, HighwayPrimaryLink)
// 3: Secondary (HighwaySecondary, HighwaySecondaryLink)
// 4: Tertiary (HighwayTertiary, HighwayTertiaryLink)
// 5: Residential (HighwayResidential, HighwayLivingStreet)
// 6: Service (HighwayService)
// 7: Other (all others)

size_t TrafficMLFeatureBuilder::GetRoadTypeCategory(routing::HighwayType hwType)
{
  using routing::HighwayType;
  switch (hwType)
  {
  case HighwayType::HighwayMotorway:
  case HighwayType::HighwayMotorwayLink:
    return 0;  // Motorway

  case HighwayType::HighwayTrunk:
  case HighwayType::HighwayTrunkLink:
    return 1;  // Trunk

  case HighwayType::HighwayPrimary:
  case HighwayType::HighwayPrimaryLink:
    return 2;  // Primary

  case HighwayType::HighwaySecondary:
  case HighwayType::HighwaySecondaryLink:
    return 3;  // Secondary

  case HighwayType::HighwayTertiary:
  case HighwayType::HighwayTertiaryLink:
    return 4;  // Tertiary

  case HighwayType::HighwayResidential:
  case HighwayType::HighwayLivingStreet:
    return 5;  // Residential

  case HighwayType::HighwayService:
    return 6;  // Service

  default:
    return 7;  // Other
  }
}

std::array<float, TrafficMLFeatureBuilder::kRoadTypeCategories>
TrafficMLFeatureBuilder::EncodeHighwayType(routing::HighwayType hwType)
{
  std::array<float, kRoadTypeCategories> oneHot = {};
  size_t const category = GetRoadTypeCategory(hwType);
  if (category < kRoadTypeCategories)
    oneHot[category] = 1.0f;
  return oneHot;
}

std::pair<float, float> TrafficMLFeatureBuilder::EncodeHour(uint8_t hour)
{
  // Cyclic encoding: hour (0-23) -> angle (0 to 2*pi)
  double const angle = 2.0 * M_PI * static_cast<double>(hour) / 24.0;
  return {static_cast<float>(std::sin(angle)), static_cast<float>(std::cos(angle))};
}

std::pair<float, float> TrafficMLFeatureBuilder::EncodeDay(uint8_t dayOfWeek)
{
  // Cyclic encoding: day (0-6) -> angle (0 to 2*pi)
  double const angle = 2.0 * M_PI * static_cast<double>(dayOfWeek) / 7.0;
  return {static_cast<float>(std::sin(angle)), static_cast<float>(std::cos(angle))};
}

TrafficMLFeatures TrafficMLFeatureBuilder::Build(routing::HighwayType hwType, bool isInCity,
                                                  uint8_t hour, uint8_t dayOfWeek,
                                                  float historicalSpeedKmph, float personalSpeedKmph,
                                                  float freeFlowSpeedKmph)
{
  TrafficMLFeatures features;
  features.m_highwayType = hwType;
  features.m_isInCity = isInCity;
  features.m_hour = hour % 24;           // Clamp to valid range
  features.m_dayOfWeek = dayOfWeek % 7;  // Clamp to valid range

  // Normalize speeds to 0-1 range using free-flow as reference
  if (freeFlowSpeedKmph > 0.0f)
  {
    features.m_historicalSpeed = historicalSpeedKmph / freeFlowSpeedKmph;
    features.m_personalBaseline = personalSpeedKmph / freeFlowSpeedKmph;
    features.m_freeFlowSpeed = freeFlowSpeedKmph;
  }
  else
  {
    features.m_historicalSpeed = 0.0f;
    features.m_personalBaseline = 0.0f;
    features.m_freeFlowSpeed = 0.0f;
  }

  return features;
}

std::vector<float> TrafficMLFeatureBuilder::ToVector(TrafficMLFeatures const & features)
{
  std::vector<float> result;
  result.reserve(kInputSize);

  // Road type one-hot encoding (8 floats)
  auto const roadTypeOneHot = EncodeHighwayType(features.m_highwayType);
  for (float val : roadTypeOneHot)
    result.push_back(val);

  // Hour cyclic encoding (2 floats: sin, cos)
  auto const [hourSin, hourCos] = EncodeHour(features.m_hour);
  result.push_back(hourSin);
  result.push_back(hourCos);

  // Day of week cyclic encoding (2 floats: sin, cos)
  auto const [daySin, dayCos] = EncodeDay(features.m_dayOfWeek);
  result.push_back(daySin);
  result.push_back(dayCos);

  // Historical speed normalized (1 float)
  result.push_back(features.m_historicalSpeed);

  // Personal baseline normalized (1 float)
  result.push_back(features.m_personalBaseline);

  // Is in city flag (1 float)
  result.push_back(features.m_isInCity ? 1.0f : 0.0f);

  return result;
}

std::string DebugPrint(TrafficMLFeatures const & features)
{
  std::ostringstream oss;
  oss << "TrafficMLFeatures {"
      << " hwType: " << DebugPrint(features.m_highwayType)
      << ", isInCity: " << (features.m_isInCity ? "true" : "false")
      << ", hour: " << static_cast<int>(features.m_hour)
      << ", dayOfWeek: " << static_cast<int>(features.m_dayOfWeek)
      << ", historicalSpeed: " << features.m_historicalSpeed
      << ", personalBaseline: " << features.m_personalBaseline
      << ", freeFlowSpeed: " << features.m_freeFlowSpeed << " }";
  return oss.str();
}

std::string DebugPrint(TrafficMLPrediction const & prediction)
{
  std::ostringstream oss;
  oss << "TrafficMLPrediction {"
      << " speedMultiplier: " << prediction.m_speedMultiplier
      << ", confidence: " << prediction.m_confidence
      << ", isValid: " << (prediction.m_isValid ? "true" : "false") << " }";
  return oss.str();
}

}  // namespace traffic

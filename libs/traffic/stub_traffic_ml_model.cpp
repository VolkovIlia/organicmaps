#include "traffic/stub_traffic_ml_model.hpp"

#include <algorithm>

namespace traffic
{
TrafficMLPrediction StubTrafficMLModel::Predict(TrafficMLFeatures const & features) const
{
  TrafficMLPrediction result;
  result.m_isValid = true;
  result.m_confidence = 0.5f;  // Lower confidence than real model

  float multiplier = 1.0f;

  // Apply time-based factors
  if (IsRushHour(features.m_hour, features.m_dayOfWeek))
  {
    // Rush hour: slower traffic (0.6-0.8)
    multiplier = 0.7f;
  }
  else if (IsNightTime(features.m_hour))
  {
    // Night: faster traffic (1.1-1.2)
    multiplier = 1.15f;
  }
  else if (IsWeekend(features.m_dayOfWeek))
  {
    // Weekend daytime: slightly slower (0.9-1.0)
    multiplier = 0.95f;
  }

  // Apply road type factor
  float const roadFactor = GetRoadTypeMultiplier(features.m_highwayType, features.m_isInCity);
  multiplier *= roadFactor;

  // Use personal/historical data to refine if available
  if (features.m_personalBaseline > 0.0f)
  {
    // Blend with personal baseline (20% weight to personal data)
    multiplier = multiplier * 0.8f + features.m_personalBaseline * 0.2f;
    result.m_confidence = 0.6f;  // Slightly higher confidence with personal data
  }
  else if (features.m_historicalSpeed > 0.0f)
  {
    // Blend with historical (10% weight to historical)
    multiplier = multiplier * 0.9f + features.m_historicalSpeed * 0.1f;
    result.m_confidence = 0.55f;
  }

  // Clamp to reasonable range
  result.m_speedMultiplier = std::clamp(multiplier, 0.3f, 1.5f);
  return result;
}

bool StubTrafficMLModel::IsRushHour(uint8_t hour, uint8_t dayOfWeek) const
{
  // Weekdays only (Monday=1 to Friday=5)
  if (IsWeekend(dayOfWeek))
    return false;

  // Morning rush: 7-9 AM
  if (hour >= 7 && hour < 9)
    return true;

  // Evening rush: 5-7 PM (17-19)
  if (hour >= 17 && hour < 19)
    return true;

  return false;
}

bool StubTrafficMLModel::IsNightTime(uint8_t hour) const
{
  // Night: 10 PM (22) to 6 AM
  return hour >= 22 || hour < 6;
}

bool StubTrafficMLModel::IsWeekend(uint8_t dayOfWeek) const
{
  // Sunday=0, Saturday=6
  return dayOfWeek == 0 || dayOfWeek == 6;
}

float StubTrafficMLModel::GetRoadTypeMultiplier(routing::HighwayType hwType, bool isInCity) const
{
  using routing::HighwayType;

  float baseFactor = 1.0f;

  // City roads are generally slower due to traffic lights, pedestrians, etc.
  if (isInCity)
    baseFactor = 0.9f;

  // Adjust based on road type
  switch (hwType)
  {
  case HighwayType::HighwayMotorway:
  case HighwayType::HighwayMotorwayLink:
    // Motorways are typically at free-flow unless congested
    return baseFactor * 1.0f;

  case HighwayType::HighwayTrunk:
  case HighwayType::HighwayTrunkLink:
    return baseFactor * 0.98f;

  case HighwayType::HighwayPrimary:
  case HighwayType::HighwayPrimaryLink:
    return baseFactor * 0.95f;

  case HighwayType::HighwaySecondary:
  case HighwayType::HighwaySecondaryLink:
    return baseFactor * 0.92f;

  case HighwayType::HighwayTertiary:
  case HighwayType::HighwayTertiaryLink:
    return baseFactor * 0.88f;

  case HighwayType::HighwayResidential:
  case HighwayType::HighwayLivingStreet:
    return baseFactor * 0.85f;

  case HighwayType::HighwayService:
    return baseFactor * 0.8f;

  default:
    return baseFactor * 0.85f;
  }
}

}  // namespace traffic

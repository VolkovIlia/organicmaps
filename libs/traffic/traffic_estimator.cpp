#include "traffic/traffic_estimator.hpp"

#include "base/logging.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace traffic
{
using namespace routing;

// TrafficEstimate implementation

void TrafficEstimate::CombineWith(TrafficEstimate const & other, double otherWeight)
{
  if (!other.IsValid())
    return;

  if (!m_isValid)
  {
    *this = other;
    return;
  }

  // Weighted average of speeds
  double const totalWeight = m_confidence + otherWeight * other.m_confidence;
  if (totalWeight > 0.0)
  {
    m_speedKmph = (m_speedKmph * m_confidence + other.m_speedKmph * otherWeight * other.m_confidence) /
                  totalWeight;
    m_confidence = std::min(1.0, totalWeight);
  }

  // Recalculate derived values
  if (m_speedKmph > 0.0)
  {
    // Keep the source from the higher priority (lower enum value)
    if (other.m_source < m_source)
      m_source = other.m_source;

    // Note: m_percentage is not recalculated here because it requires
    // the highway type context (free-flow speed). The m_speedGroup is
    // derived from the original percentage and may be slightly stale
    // after combining. This is acceptable as the combined speed is the
    // primary value used for routing calculations.
    m_speedGroup = SpeedPercentageToGroup(m_percentage);
  }
}

// TrafficEstimator implementation

TrafficEstimator::TrafficEstimator()
  : m_config()
  , m_osmInference(std::make_shared<OSMSpeedInference>())
{
}

TrafficEstimator::TrafficEstimator(Config const & config)
  : m_config(config)
  , m_osmInference(std::make_shared<OSMSpeedInference>())
{
}

void TrafficEstimator::SetHistoricalProvider(std::shared_ptr<IHistoricalSpeedProvider> provider)
{
  m_historicalProvider = std::move(provider);
}

void TrafficEstimator::SetOSMInference(std::shared_ptr<OSMSpeedInference> inference)
{
  m_osmInference = std::move(inference);
}

void TrafficEstimator::SetPersonalStorage(std::shared_ptr<PersonalSpeedStorage> storage)
{
  m_personalStorage = std::move(storage);
}

TrafficEstimate TrafficEstimator::GetEstimate(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                               uint16_t segmentIdx, bool isForward,
                                               HighwayType hwType, bool isInCity,
                                               std::time_t time) const
{
  // Use current time if not specified
  if (time == 0)
    time = std::time(nullptr);

  TrafficEstimate result;

  // Try sources in priority order

  // 0. Personal history (highest priority)
  auto personal = GetPersonalEstimate(featureId, segmentIdx, isForward, hwType, isInCity, time);
  if (personal && personal->m_confidence >= m_config.m_earlyReturnThreshold)
  {
    LOG(LDEBUG, ("Using personal speed estimate:", DebugPrint(*personal)));
    return *personal;
  }

  if (personal)
    result = *personal;

  // 1. Historical patterns
  if (m_config.m_useHistoricalPatterns)
  {
    auto historical = GetHistoricalEstimate(mwmId, featureId, segmentIdx, isForward, hwType, isInCity, time);
    if (historical)
    {
      if (result.IsValid())
        result.CombineWith(*historical, m_config.m_historicalPatternWeight);
      else
      {
        result = *historical;
        // Early return for high-confidence historical data (>= 0.8).
        if (result.m_confidence >= 0.8)
          return result;
      }
    }
  }

  // 2. OSM inference (fallback)
  if (m_config.m_useOSMInference && !result.IsValid())
  {
    auto osm = GetOSMEstimate(hwType, isInCity);
    if (osm)
    {
      if (result.IsValid())
        result.CombineWith(*osm, m_config.m_osmInferenceWeight);
      else
        result = *osm;
    }
  }

  // 3. Default fallback
  if (!result.IsValid())
    result = GetDefaultEstimate(hwType, isInCity);

  return result;
}

SpeedGroup TrafficEstimator::GetSpeedGroup(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                            uint16_t segmentIdx, bool isForward,
                                            HighwayType hwType, bool isInCity,
                                            std::time_t time) const
{
  auto estimate = GetEstimate(mwmId, featureId, segmentIdx, isForward, hwType, isInCity, time);
  return estimate.m_speedGroup;
}

double TrafficEstimator::GetSpeedKmph(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                       uint16_t segmentIdx, bool isForward,
                                       HighwayType hwType, bool isInCity,
                                       std::time_t time) const
{
  auto estimate = GetEstimate(mwmId, featureId, segmentIdx, isForward, hwType, isInCity, time);
  return estimate.m_speedKmph;
}

double TrafficEstimator::GetTrafficFactor(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                           uint16_t segmentIdx, bool isForward,
                                           HighwayType hwType, bool isInCity,
                                           std::time_t time) const
{
  auto estimate = GetEstimate(mwmId, featureId, segmentIdx, isForward, hwType, isInCity, time);
  if (!estimate.IsValid())
    return 1.0;  // No data, assume free flow

  return SpeedToFactor(estimate.m_speedKmph, hwType, isInCity);
}

bool TrafficEstimator::HasData(MwmSet::MwmId const & mwmId) const
{
  // Personal storage has data regardless of MWM (it's global user data)
  if (m_personalStorage && m_personalStorage->GetRecordCount() > 0)
    return true;

  if (m_historicalProvider && m_historicalProvider->HasData(mwmId))
    return true;

  // OSM inference is always available as fallback
  return m_config.m_useOSMInference;
}

std::optional<TrafficEstimate> TrafficEstimator::GetPersonalEstimate(
    uint32_t featureId, uint16_t segmentIdx, bool isForward,
    HighwayType hwType, bool isInCity, std::time_t time) const
{
  if (!m_personalStorage)
    return std::nullopt;

  // Extract dayOfWeek and hour from time_t (same logic as historical_speed_data.cpp)
  std::tm localTime;
#ifdef _WIN32
  if (localtime_s(&localTime, &time) != 0)
    return std::nullopt;
#else
  if (localtime_r(&time, &localTime) == nullptr)
    return std::nullopt;
#endif

  uint8_t const dayOfWeek = static_cast<uint8_t>(localTime.tm_wday);
  uint8_t const hour = static_cast<uint8_t>(localTime.tm_hour);

  float const speedKmph = m_personalStorage->GetSpeed(featureId, segmentIdx, isForward, dayOfWeek, hour);
  if (speedKmph <= 0.0f)
    return std::nullopt;

  TrafficEstimate result;
  result.m_speedKmph = static_cast<double>(speedKmph);
  result.m_source = TrafficSource::kPersonalHistory;
  result.m_confidence = m_config.m_personalHistoryWeight;
  result.m_isValid = true;

  // Calculate percentage relative to free-flow
  double freeFlowSpeed = 0.0;
  if (m_osmInference)
    freeFlowSpeed = m_osmInference->GetFreeFlowSpeedKmph(hwType, isInCity);
  else
    freeFlowSpeed = OSMSpeedInference::GetDefaultSpeedKmph(hwType, isInCity) * 1.15;

  if (freeFlowSpeed > 0.0)
  {
    result.m_percentage = static_cast<SpeedPercentage>(
        std::min(200.0, std::max(1.0, (result.m_speedKmph / freeFlowSpeed) * 100.0)));
  }
  else
  {
    result.m_percentage = 80;  // Default: 80% of theoretical max
  }

  result.m_speedGroup = SpeedPercentageToGroup(result.m_percentage);
  return result;
}

std::optional<TrafficEstimate> TrafficEstimator::GetHistoricalEstimate(
    MwmSet::MwmId const & mwmId, uint32_t featureId, uint16_t segmentIdx, bool isForward,
    HighwayType hwType, bool isInCity, std::time_t time) const
{
  if (!m_historicalProvider)
    return std::nullopt;

  SpeedPercentage const percentage =
      m_historicalProvider->GetSpeed(mwmId, featureId, segmentIdx, isForward, time);

  if (percentage == kNoSpeedData)
    return std::nullopt;

  TrafficEstimate result;
  result.m_percentage = percentage;
  result.m_speedGroup = SpeedPercentageToGroup(percentage);
  result.m_source = TrafficSource::kHistoricalPattern;
  result.m_isValid = true;

  // Convert percentage to absolute speed using OSM inference for free-flow
  if (m_osmInference)
  {
    double const freeFlowSpeed = m_osmInference->GetFreeFlowSpeedKmph(hwType, isInCity);
    result.m_speedKmph = freeFlowSpeed * static_cast<double>(percentage) / 100.0;
  }
  else
  {
    // Fallback: use default speeds
    double const defaultSpeed = OSMSpeedInference::GetDefaultSpeedKmph(hwType, isInCity);
    result.m_speedKmph = defaultSpeed * static_cast<double>(percentage) / 100.0;
  }

  result.m_confidence = m_config.m_historicalPatternWeight;
  return result;
}

std::optional<TrafficEstimate> TrafficEstimator::GetOSMEstimate(HighwayType hwType,
                                                                 bool isInCity) const
{
  if (!m_osmInference)
    return std::nullopt;

  auto inference = m_osmInference->GetInferredSpeed(hwType, isInCity);
  if (!inference)
    return std::nullopt;

  TrafficEstimate result;
  result.m_speedKmph = inference->m_speedKmph;
  result.m_confidence = inference->m_confidence * m_config.m_osmInferenceWeight;
  result.m_source = TrafficSource::kOSMInference;
  result.m_isValid = true;

  // Calculate percentage relative to free-flow
  double const freeFlowSpeed = m_osmInference->GetFreeFlowSpeedKmph(hwType, isInCity);
  if (freeFlowSpeed > 0.0)
  {
    result.m_percentage = static_cast<SpeedPercentage>(
        std::min(200.0, std::max(1.0, (result.m_speedKmph / freeFlowSpeed) * 100.0)));
  }
  else
  {
    result.m_percentage = 80;  // Default: 80% of theoretical max
  }

  result.m_speedGroup = SpeedPercentageToGroup(result.m_percentage);
  return result;
}

TrafficEstimate TrafficEstimator::GetDefaultEstimate(HighwayType hwType, bool isInCity) const
{
  TrafficEstimate result;
  result.m_speedKmph = OSMSpeedInference::GetDefaultSpeedKmph(hwType, isInCity);
  result.m_percentage = 80;  // Assume 80% of free-flow by default
  result.m_speedGroup = SpeedGroup::G4;  // Moderate traffic
  result.m_source = TrafficSource::kRoadClassDefault;
  result.m_confidence = 0.1;
  result.m_isValid = result.m_speedKmph > 0.0;
  return result;
}

double TrafficEstimator::SpeedToFactor(double speedKmph, HighwayType hwType, bool isInCity) const
{
  if (speedKmph <= 0.0)
    return 1.0;

  double freeFlowSpeed = 0.0;
  if (m_osmInference)
    freeFlowSpeed = m_osmInference->GetFreeFlowSpeedKmph(hwType, isInCity);
  else
    freeFlowSpeed = OSMSpeedInference::GetDefaultSpeedKmph(hwType, isInCity) * 1.15;

  if (freeFlowSpeed <= 0.0)
    return 1.0;

  // Factor = free_flow_speed / actual_speed
  // Factor > 1.0 means slower (takes more time)
  // Factor < 1.0 means faster (takes less time)
  double const factor = freeFlowSpeed / speedKmph;

  // Clamp to reasonable range (0.5x to 10x)
  return std::max(0.5, std::min(10.0, factor));
}

double CalcDecayFactor(uint32_t dataAgeMinutes, uint32_t halfLifeMinutes)
{
  if (halfLifeMinutes == 0)
    return 1.0;

  // Exponential decay: factor = 2^(-age/half_life)
  double const exponent = -static_cast<double>(dataAgeMinutes) / static_cast<double>(halfLifeMinutes);
  return std::pow(2.0, exponent);
}

std::string DebugPrint(TrafficSource source)
{
  switch (source)
  {
  case TrafficSource::kPersonalHistory: return "PersonalHistory";
  case TrafficSource::kOnDeviceML: return "OnDeviceML";
  case TrafficSource::kP2PReceived: return "P2PReceived";
  case TrafficSource::kHistoricalPattern: return "HistoricalPattern";
  case TrafficSource::kOSMInference: return "OSMInference";
  case TrafficSource::kRoadClassDefault: return "RoadClassDefault";
  case TrafficSource::kCount: return "Count";
  }
  return "Unknown";
}

std::string DebugPrint(TrafficEstimate const & estimate)
{
  std::ostringstream oss;
  oss << "TrafficEstimate ["
      << "speed=" << estimate.m_speedKmph << " km/h"
      << ", percent=" << static_cast<int>(estimate.m_percentage)
      << ", group=" << DebugPrint(estimate.m_speedGroup)
      << ", source=" << DebugPrint(estimate.m_source)
      << ", confidence=" << estimate.m_confidence
      << ", valid=" << (estimate.m_isValid ? "yes" : "no")
      << "]";
  return oss.str();
}

}  // namespace traffic

#include "traffic/osm_speed_inference.hpp"

#include "routing_common/car_model.hpp"

#include "base/logging.hpp"

#include <algorithm>
#include <sstream>

namespace traffic
{
using namespace routing;

OSMSpeedInference::OSMSpeedInference()
  : m_vehicleModel(std::make_shared<CarModel>())
  , m_countryProfile(GetCountryProfile(""))
{
}

OSMSpeedInference::OSMSpeedInference(std::string const & countryName)
  : m_countryName(countryName)
  , m_countryProfile(GetCountryProfile(countryName))
{
  // Create country-specific car model if possible
  // For now, use default model - country-specific models would require
  // CountryParentNameGetterFn which is complex to set up standalone
  m_vehicleModel = std::make_shared<CarModel>();
}

std::optional<OSMSpeedInference::SpeedEstimate> OSMSpeedInference::GetInferredSpeed(
    feature::TypesHolder const & types, bool isInCity) const
{
  if (!m_vehicleModel)
    return std::nullopt;

  auto const hwType = m_vehicleModel->GetHighwayType(types);
  if (!hwType)
    return std::nullopt;

  return GetInferredSpeed(*hwType, isInCity);
}

std::optional<OSMSpeedInference::SpeedEstimate> OSMSpeedInference::GetInferredSpeed(
    HighwayType hwType, bool isInCity) const
{
  double const baseSpeed = GetDefaultSpeedKmph(hwType, isInCity);
  if (baseSpeed <= 0.0)
    return std::nullopt;

  SpeedEstimate result;
  result.m_speedKmph = ApplyAdjustments(baseSpeed, hwType, isInCity);
  result.m_isUrban = isInCity;

  // Confidence based on road type - higher tier roads have more predictable speeds
  switch (hwType)
  {
  case HighwayType::HighwayMotorway:
  case HighwayType::HighwayMotorwayLink:
  case HighwayType::HighwayTrunk:
  case HighwayType::HighwayTrunkLink:
    result.m_confidence = 0.8;
    break;

  case HighwayType::HighwayPrimary:
  case HighwayType::HighwayPrimaryLink:
  case HighwayType::HighwaySecondary:
  case HighwayType::HighwaySecondaryLink:
    result.m_confidence = 0.7;
    break;

  case HighwayType::HighwayTertiary:
  case HighwayType::HighwayTertiaryLink:
    result.m_confidence = 0.6;
    break;

  case HighwayType::HighwayResidential:
  case HighwayType::HighwayLivingStreet:
  case HighwayType::HighwayService:
    result.m_confidence = 0.5;
    break;

  default:
    result.m_confidence = 0.4;
    break;
  }

  return result;
}

namespace
{
// Table-driven speed lookup: {urban speed, rural speed, link factor}
struct SpeedEntry { double urban; double rural; double linkFactor; };
constexpr double kLinkFactor = 0.85;

SpeedEntry GetSpeedEntry(HighwayType hwType)
{
  using D = DefaultRoadSpeeds;
  switch (hwType)
  {
  case HighwayType::HighwayMotorway:
  case HighwayType::HighwayMotorwayLink:
    return {D::kMotorwayUrban, D::kMotorwayRural, kLinkFactor};
  case HighwayType::HighwayTrunk:
  case HighwayType::HighwayTrunkLink:
    return {D::kTrunkUrban, D::kTrunkRural, kLinkFactor};
  case HighwayType::HighwayPrimary:
  case HighwayType::HighwayPrimaryLink:
    return {D::kPrimaryUrban, D::kPrimaryRural, kLinkFactor};
  case HighwayType::HighwaySecondary:
  case HighwayType::HighwaySecondaryLink:
    return {D::kSecondaryUrban, D::kSecondaryRural, kLinkFactor};
  case HighwayType::HighwayTertiary:
  case HighwayType::HighwayTertiaryLink:
    return {D::kTertiaryUrban, D::kTertiaryRural, kLinkFactor};
  case HighwayType::HighwayResidential:
    return {D::kResidentialUrban, D::kResidentialRural, 1.0};
  case HighwayType::HighwayLivingStreet:
    return {D::kLivingStreet, D::kLivingStreet, 1.0};
  case HighwayType::HighwayService:
    return {D::kService, D::kService, 1.0};
  case HighwayType::HighwayUnclassified:
  case HighwayType::HighwayRoad:
    return {D::kUnclassified, D::kUnclassified, 1.0};
  case HighwayType::HighwayTrack:
    return {D::kTrack, D::kTrack, 1.0};
  case HighwayType::RouteFerry:
  case HighwayType::RouteShuttleTrain:
    return {25.0, 25.0, 1.0};
  case HighwayType::ManMadePier:
    return {15.0, 15.0, 1.0};
  default:
    return {0.0, 0.0, 1.0};
  }
}

bool IsLinkType(HighwayType hwType)
{
  return hwType == HighwayType::HighwayMotorwayLink ||
         hwType == HighwayType::HighwayTrunkLink ||
         hwType == HighwayType::HighwayPrimaryLink ||
         hwType == HighwayType::HighwaySecondaryLink ||
         hwType == HighwayType::HighwayTertiaryLink;
}
}  // namespace

// static
double OSMSpeedInference::GetDefaultSpeedKmph(HighwayType hwType, bool isInCity)
{
  auto const entry = GetSpeedEntry(hwType);
  double speed = isInCity ? entry.urban : entry.rural;
  if (IsLinkType(hwType))
    speed *= entry.linkFactor;
  return speed;
}

uint8_t OSMSpeedInference::SpeedToPercentage(double actualSpeedKmph, HighwayType hwType,
                                              bool isInCity) const
{
  double const freeFlowSpeed = GetFreeFlowSpeedKmph(hwType, isInCity);
  if (freeFlowSpeed <= 0.0)
    return 0;

  double const percentage = (actualSpeedKmph / freeFlowSpeed) * 100.0;

  // Clamp to valid range (1-200, 0 = no data, 255 = blocked)
  if (percentage < 1.0)
    return 1;
  if (percentage > 200.0)
    return 200;

  return static_cast<uint8_t>(percentage);
}

double OSMSpeedInference::GetFreeFlowSpeedKmph(HighwayType hwType, bool isInCity) const
{
  // Free-flow speed is the maximum expected speed on this road type
  // It's higher than typical speed but represents best-case scenario
  double const typicalSpeed = GetDefaultSpeedKmph(hwType, isInCity);
  if (typicalSpeed <= 0.0)
    return 0.0;

  // Free-flow is typically 15-25% higher than typical observed speed
  // depending on road type (higher tier roads have less variance)
  double freeFlowFactor = 1.15;

  switch (hwType)
  {
  case HighwayType::HighwayMotorway:
  case HighwayType::HighwayMotorwayLink:
    freeFlowFactor = 1.10;  // Motorways are more consistent
    break;

  case HighwayType::HighwayResidential:
  case HighwayType::HighwayLivingStreet:
  case HighwayType::HighwayService:
    freeFlowFactor = 1.25;  // Local roads have more variance
    break;

  default:
    freeFlowFactor = 1.15;
    break;
  }

  return typicalSpeed * freeFlowFactor;
}

OSMSpeedInference::CountrySpeedProfile OSMSpeedInference::GetCountryProfile(
    std::string const & countryName) const
{
  CountrySpeedProfile profile;

  // Country-specific adjustments based on typical driving patterns
  // These are rough estimates and could be refined with actual data

  if (countryName == "Germany")
  {
    // No motorway speed limit, faster driving culture
    profile.m_motorwayFactor = 1.15;
    profile.m_ruralFactor = 1.05;
    profile.m_urbanFactor = 1.0;
  }
  else if (countryName == "Italy" || countryName == "France")
  {
    // Higher motorway speeds
    profile.m_motorwayFactor = 1.08;
    profile.m_ruralFactor = 1.0;
    profile.m_urbanFactor = 0.95;
  }
  else if (countryName == "United Kingdom" || countryName == "Ireland")
  {
    // Slower due to narrower roads
    profile.m_motorwayFactor = 0.95;
    profile.m_ruralFactor = 0.90;
    profile.m_urbanFactor = 0.90;
  }
  else if (countryName == "Russian Federation" || countryName == "Ukraine" ||
           countryName == "Belarus")
  {
    // Variable road conditions
    profile.m_motorwayFactor = 0.95;
    profile.m_ruralFactor = 0.85;
    profile.m_urbanFactor = 0.90;
  }
  else if (countryName == "India" || countryName == "Indonesia" ||
           countryName == "Philippines")
  {
    // Dense traffic, varied road conditions
    profile.m_motorwayFactor = 0.80;
    profile.m_ruralFactor = 0.70;
    profile.m_urbanFactor = 0.60;
  }
  else if (countryName == "Japan" || countryName == "South Korea")
  {
    // Well-maintained roads but strict limits
    profile.m_motorwayFactor = 0.92;
    profile.m_ruralFactor = 0.95;
    profile.m_urbanFactor = 0.85;
  }
  else if (countryName == "United States" || countryName == "Canada")
  {
    // Larger roads, higher speeds in rural
    profile.m_motorwayFactor = 1.05;
    profile.m_ruralFactor = 1.05;
    profile.m_urbanFactor = 0.95;
  }
  else if (countryName == "Australia")
  {
    // Long distances, higher rural speeds
    profile.m_motorwayFactor = 1.0;
    profile.m_ruralFactor = 1.10;
    profile.m_urbanFactor = 0.95;
  }
  // Default profile for unknown countries
  else
  {
    profile.m_motorwayFactor = 1.0;
    profile.m_ruralFactor = 1.0;
    profile.m_urbanFactor = 1.0;
  }

  return profile;
}

double OSMSpeedInference::ApplyAdjustments(double baseSpeed, HighwayType hwType,
                                            bool isInCity) const
{
  double adjusted = baseSpeed;

  // Apply context factor
  if (isInCity)
    adjusted *= kUrbanReductionFactor;
  else
    adjusted *= kRuralReductionFactor;

  // Apply country-specific factor
  switch (hwType)
  {
  case HighwayType::HighwayMotorway:
  case HighwayType::HighwayMotorwayLink:
    adjusted *= m_countryProfile.m_motorwayFactor;
    break;

  default:
    if (isInCity)
      adjusted *= m_countryProfile.m_urbanFactor;
    else
      adjusted *= m_countryProfile.m_ruralFactor;
    break;
  }

  return adjusted;
}

namespace
{
// Table-driven lookup for string-based highway class
struct StringSpeedEntry { char const * name; double urban; double rural; bool isLink; };
constexpr StringSpeedEntry kStringSpeedTable[] = {
    {"motorway", DefaultRoadSpeeds::kMotorwayUrban, DefaultRoadSpeeds::kMotorwayRural, false},
    {"motorway_link", DefaultRoadSpeeds::kMotorwayUrban, DefaultRoadSpeeds::kMotorwayRural, true},
    {"trunk", DefaultRoadSpeeds::kTrunkUrban, DefaultRoadSpeeds::kTrunkRural, false},
    {"trunk_link", DefaultRoadSpeeds::kTrunkUrban, DefaultRoadSpeeds::kTrunkRural, true},
    {"primary", DefaultRoadSpeeds::kPrimaryUrban, DefaultRoadSpeeds::kPrimaryRural, false},
    {"primary_link", DefaultRoadSpeeds::kPrimaryUrban, DefaultRoadSpeeds::kPrimaryRural, true},
    {"secondary", DefaultRoadSpeeds::kSecondaryUrban, DefaultRoadSpeeds::kSecondaryRural, false},
    {"secondary_link", DefaultRoadSpeeds::kSecondaryUrban, DefaultRoadSpeeds::kSecondaryRural, true},
    {"tertiary", DefaultRoadSpeeds::kTertiaryUrban, DefaultRoadSpeeds::kTertiaryRural, false},
    {"tertiary_link", DefaultRoadSpeeds::kTertiaryUrban, DefaultRoadSpeeds::kTertiaryRural, true},
    {"residential", DefaultRoadSpeeds::kResidentialUrban, DefaultRoadSpeeds::kResidentialRural, false},
    {"living_street", DefaultRoadSpeeds::kLivingStreet, DefaultRoadSpeeds::kLivingStreet, false},
    {"service", DefaultRoadSpeeds::kService, DefaultRoadSpeeds::kService, false},
    {"unclassified", DefaultRoadSpeeds::kUnclassified, DefaultRoadSpeeds::kUnclassified, false},
    {"road", DefaultRoadSpeeds::kUnclassified, DefaultRoadSpeeds::kUnclassified, false},
    {"track", DefaultRoadSpeeds::kTrack, DefaultRoadSpeeds::kTrack, false},
};
}  // namespace

// static
double DefaultRoadSpeeds::GetTypicalSpeed(std::string const & highwayClass, bool isInCity)
{
  for (auto const & entry : kStringSpeedTable)
  {
    if (highwayClass == entry.name)
    {
      double speed = isInCity ? entry.urban : entry.rural;
      return entry.isLink ? speed * kLinkFactor : speed;
    }
  }
  // Unknown type - use conservative estimate
  return isInCity ? 25.0 : 35.0;
}

std::string DebugPrint(OSMSpeedInference::SpeedEstimate const & estimate)
{
  std::ostringstream oss;
  oss << "SpeedEstimate [speed=" << estimate.m_speedKmph << " km/h"
      << ", confidence=" << estimate.m_confidence
      << ", urban=" << (estimate.m_isUrban ? "yes" : "no") << "]";
  return oss.str();
}

}  // namespace traffic

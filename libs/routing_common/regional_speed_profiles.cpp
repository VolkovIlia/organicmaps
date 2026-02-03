#include "routing_common/regional_speed_profiles.hpp"

#include <utility>

namespace routing
{
namespace
{
/// \brief Create Russia speed profile based on ПДД РФ (Russian Traffic Rules).
///
/// Legal limits from ПДД 10.2-10.3:
/// - В населённых пунктах: 60 км/ч (жилые зоны: 20 км/ч)
/// - Вне населённых пунктов: автомагистрали 110 км/ч, остальные 90 км/ч
///
/// Values below are REALISTIC AVERAGE speeds (not legal limits) accounting for:
/// - Traffic flow and congestion
/// - Frequent passing through settlements on non-motorway roads
/// - Road surface quality variations
/// - Intersections and traffic lights
RegionalSpeedConfig CreateRussiaProfile()
{
  RegionalSpeedConfig config;

  // Motorways (автомагистрали): legal 110 km/h, can be 130 km/h on some sections.
  // Realistic average ~100-105 km/h accounting for traffic.
  config.roadSpeeds[HighwayType::HighwayMotorway] =
      InOutCitySpeedKMpH(SpeedKMpH(80, 80), SpeedKMpH(105, 100));
  config.roadSpeeds[HighwayType::HighwayMotorwayLink] =
      InOutCitySpeedKMpH(SpeedKMpH(50, 50), SpeedKMpH(60, 55));

  // Trunk (магистральные дороги): legal 90 km/h outside cities.
  // Often 2-lane with trucks, realistic ~80-85 km/h.
  config.roadSpeeds[HighwayType::HighwayTrunk] =
      InOutCitySpeedKMpH(SpeedKMpH(55, 55), SpeedKMpH(85, 80));
  config.roadSpeeds[HighwayType::HighwayTrunkLink] =
      InOutCitySpeedKMpH(SpeedKMpH(45, 45), SpeedKMpH(55, 50));

  // Primary (основные дороги): legal 90 km/h outside cities, 60 km/h in cities.
  // Often passes through villages with reduced limits, realistic ~70-75 km/h.
  config.roadSpeeds[HighwayType::HighwayPrimary] =
      InOutCitySpeedKMpH(SpeedKMpH(50, 50), SpeedKMpH(75, 70));
  config.roadSpeeds[HighwayType::HighwayPrimaryLink] =
      InOutCitySpeedKMpH(SpeedKMpH(40, 40), SpeedKMpH(50, 45));

  // Secondary (второстепенные дороги): legal 90 km/h, but variable quality.
  // Realistic ~60-65 km/h accounting for conditions.
  config.roadSpeeds[HighwayType::HighwaySecondary] =
      InOutCitySpeedKMpH(SpeedKMpH(45, 45), SpeedKMpH(65, 60));
  config.roadSpeeds[HighwayType::HighwaySecondaryLink] =
      InOutCitySpeedKMpH(SpeedKMpH(35, 35), SpeedKMpH(45, 40));

  // Tertiary (местные дороги): legal 90 km/h, often poor condition.
  // Realistic ~50-55 km/h.
  config.roadSpeeds[HighwayType::HighwayTertiary] =
      InOutCitySpeedKMpH(SpeedKMpH(40, 40), SpeedKMpH(55, 50));
  config.roadSpeeds[HighwayType::HighwayTertiaryLink] =
      InOutCitySpeedKMpH(SpeedKMpH(30, 30), SpeedKMpH(40, 35));

  // Unclassified: often unpaved or very poor quality.
  // Realistic ~40 km/h outside, 30 km/h in settlements.
  config.roadSpeeds[HighwayType::HighwayUnclassified] =
      InOutCitySpeedKMpH(SpeedKMpH(30, 30), SpeedKMpH(40, 35));

  // Residential: legal 60 km/h in cities, but traffic/parking reduce speed.
  // Realistic ~35-40 km/h.
  config.roadSpeeds[HighwayType::HighwayResidential] =
      InOutCitySpeedKMpH(SpeedKMpH(35, 35), SpeedKMpH(40, 35));

  // Service roads: parking lots, driveways, etc.
  config.roadSpeeds[HighwayType::HighwayService] =
      InOutCitySpeedKMpH(SpeedKMpH(20, 20), SpeedKMpH(25, 20));

  // Living street (жилая зона): legal 20 km/h per ПДД 10.2.
  config.roadSpeeds[HighwayType::HighwayLivingStreet] =
      InOutCitySpeedKMpH(SpeedKMpH(20, 20), SpeedKMpH(20, 20));

  // Track: unpaved roads, very variable condition.
  config.roadSpeeds[HighwayType::HighwayTrack] =
      InOutCitySpeedKMpH(SpeedKMpH(20, 20), SpeedKMpH(25, 20));

  // Surface factors: more aggressive penalties for Russia
  // due to frequent unpaved roads in poor condition.
  config.surfaceFactors = {
    {"paved", 1.0},
    {"asphalt", 1.0},
    {"concrete", 0.95},
    {"compacted", 0.85},     // More penalty than default
    {"gravel", 0.65},        // More penalty than default
    {"fine_gravel", 0.70},
    {"unpaved", 0.50},       // Significant penalty
    {"ground", 0.35},        // Very slow
    {"dirt", 0.35},
    {"mud", 0.20},           // Near impassable
    {"sand", 0.25},
  };

  return config;
}

/// \brief Create Ukraine speed profile (similar to Russia with local adjustments).
RegionalSpeedConfig CreateUkraineProfile()
{
  // Start with Russia profile as base.
  RegionalSpeedConfig config = CreateRussiaProfile();

  // Ukraine-specific adjustments can be added here.
  // Currently using same profile as road conditions are similar.

  return config;
}

/// \brief Create Belarus speed profile.
/// Belarus ПДД: similar to Russia but roads are generally better maintained.
RegionalSpeedConfig CreateBelarusProfile()
{
  RegionalSpeedConfig config = CreateRussiaProfile();

  // Belarus has generally better maintained roads than Russia.
  // Higher realistic speeds on major routes.
  config.roadSpeeds[HighwayType::HighwayMotorway] =
      InOutCitySpeedKMpH(SpeedKMpH(80, 80), SpeedKMpH(110, 105));
  config.roadSpeeds[HighwayType::HighwayTrunk] =
      InOutCitySpeedKMpH(SpeedKMpH(55, 55), SpeedKMpH(90, 85));
  config.roadSpeeds[HighwayType::HighwayPrimary] =
      InOutCitySpeedKMpH(SpeedKMpH(50, 50), SpeedKMpH(80, 75));
  config.roadSpeeds[HighwayType::HighwaySecondary] =
      InOutCitySpeedKMpH(SpeedKMpH(45, 45), SpeedKMpH(70, 65));

  return config;
}

/// \brief Create Kazakhstan speed profile.
RegionalSpeedConfig CreateKazakhstanProfile()
{
  RegionalSpeedConfig config = CreateRussiaProfile();

  // Kazakhstan has long distances with variable road quality.
  // Keep similar to Russia but adjust for local conditions.

  return config;
}
}  // namespace

RegionalSpeedProfiles const & RegionalSpeedProfiles::Instance()
{
  static RegionalSpeedProfiles instance;
  return instance;
}

RegionalSpeedProfiles::RegionalSpeedProfiles()
{
  LoadBuiltinProfiles();
}

void RegionalSpeedProfiles::LoadBuiltinProfiles()
{
  // Russia and CIS countries.
  RegisterProfile("RU", CreateRussiaProfile());
  RegisterProfile("UA", CreateUkraineProfile());
  RegisterProfile("BY", CreateBelarusProfile());
  RegisterProfile("KZ", CreateKazakhstanProfile());
}

void RegionalSpeedProfiles::RegisterProfile(
    std::string const & countryCode,
    RegionalSpeedConfig config)
{
  m_profiles[countryCode] = std::move(config);
}

RegionalSpeedConfig const * RegionalSpeedProfiles::GetProfile(
    std::string const & countryCode) const
{
  auto const it = m_profiles.find(countryCode);
  return it != m_profiles.end() ? &it->second : nullptr;
}

std::optional<SpeedKMpH> RegionalSpeedProfiles::GetSpeed(
    std::string const & countryCode,
    HighwayType roadClass,
    bool inCity) const
{
  auto const * profile = GetProfile(countryCode);
  if (!profile)
    return std::nullopt;

  auto const it = profile->roadSpeeds.find(roadClass);
  if (it == profile->roadSpeeds.end())
    return std::nullopt;

  return inCity ? it->second.m_inCity : it->second.m_outCity;
}

double RegionalSpeedProfiles::GetSurfaceFactor(
    std::string const & countryCode,
    std::string const & surfaceType) const
{
  auto const * profile = GetProfile(countryCode);
  if (!profile)
    return 1.0;

  auto const it = profile->surfaceFactors.find(surfaceType);
  return it != profile->surfaceFactors.end() ? it->second : 1.0;
}

bool RegionalSpeedProfiles::HasProfile(std::string const & countryCode) const
{
  return m_profiles.find(countryCode) != m_profiles.end();
}

}  // namespace routing

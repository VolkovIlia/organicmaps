#pragma once

#include "routing_common/vehicle_model.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace routing
{

/// \brief Speed profile for a specific country/region.
/// Contains road type speeds and surface factors.
struct RegionalSpeedConfig
{
  // Road class -> {inCity speed, outCity speed} in km/h.
  std::unordered_map<HighwayType, InOutCitySpeedKMpH> roadSpeeds;

  // Surface type -> speed factor (0.0 - 1.0).
  std::unordered_map<std::string, double> surfaceFactors;
};

/// \brief Registry of regional speed profiles.
/// Singleton that provides speed and surface factor overrides for specific countries.
class RegionalSpeedProfiles
{
public:
  /// \brief Get singleton instance.
  static RegionalSpeedProfiles const & Instance();

  /// \brief Get speed profile for country.
  /// \param countryCode ISO 3166-1 alpha-2 country code (e.g., "RU", "UA").
  /// \return Pointer to config if found, nullptr otherwise.
  RegionalSpeedConfig const * GetProfile(std::string const & countryCode) const;

  /// \brief Get speed for specific road class in country.
  /// \param countryCode ISO country code.
  /// \param roadClass Highway type.
  /// \param inCity Whether the road is in a city.
  /// \return Speed if profile exists and road class is defined, nullopt otherwise.
  std::optional<SpeedKMpH> GetSpeed(
      std::string const & countryCode,
      HighwayType roadClass,
      bool inCity) const;

  /// \brief Get surface factor for country.
  /// \param countryCode ISO country code.
  /// \param surfaceType Surface type string (e.g., "paved", "gravel", "unpaved").
  /// \return Factor between 0.0-1.0, or 1.0 if not found.
  double GetSurfaceFactor(
      std::string const & countryCode,
      std::string const & surfaceType) const;

  /// \brief Check if country has a regional profile.
  bool HasProfile(std::string const & countryCode) const;

private:
  RegionalSpeedProfiles();

  void LoadBuiltinProfiles();
  void RegisterProfile(std::string const & countryCode, RegionalSpeedConfig config);

  std::unordered_map<std::string, RegionalSpeedConfig> m_profiles;
};

}  // namespace routing

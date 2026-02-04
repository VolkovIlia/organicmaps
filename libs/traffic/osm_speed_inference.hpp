#pragma once

#include "routing_common/vehicle_model.hpp"

#include "indexer/feature_data.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace traffic
{
/// \brief Provides default/typical speed inference based on OSM road classification.
/// This serves as a fallback when no historical traffic data is available.
///
/// Speed inference is based on:
/// 1. Road classification (highway type)
/// 2. Country-specific speed limits and driving patterns
/// 3. Urban vs rural context
///
/// The speeds are conservative estimates of typical travel speeds,
/// not legal speed limits.
class OSMSpeedInference
{
public:
  /// \brief Speed estimate with metadata.
  struct SpeedEstimate
  {
    double m_speedKmph = 0.0;      // Estimated typical speed in km/h
    double m_confidence = 0.0;     // Confidence level (0.0 - 1.0)
    bool m_isUrban = false;        // Whether in urban context

    bool IsValid() const { return m_speedKmph > 0.0; }
  };

  OSMSpeedInference();

  /// \brief Initialize with country-specific vehicle model.
  /// \param countryName Country name for country-specific rules.
  explicit OSMSpeedInference(std::string const & countryName);

  /// \brief Get inferred speed for a road segment.
  /// \param types Feature types from the road.
  /// \param isInCity Whether the road is in an urban area.
  /// \return Estimated speed, or nullopt if road type not recognized.
  std::optional<SpeedEstimate> GetInferredSpeed(feature::TypesHolder const & types,
                                                 bool isInCity) const;

  /// \brief Get inferred speed using highway type directly.
  /// \param hwType Highway type from vehicle model.
  /// \param isInCity Whether in urban area.
  /// \return Estimated speed, or nullopt if type not supported.
  std::optional<SpeedEstimate> GetInferredSpeed(routing::HighwayType hwType, bool isInCity) const;

  /// \brief Get default speed for highway type (no country-specific adjustments).
  /// This is used as ultimate fallback.
  static double GetDefaultSpeedKmph(routing::HighwayType hwType, bool isInCity);

  /// \brief Convert speed to percentage of free-flow speed for the road type.
  /// \param actualSpeedKmph Actual or estimated speed.
  /// \param hwType Highway type to get max speed.
  /// \param isInCity Urban context.
  /// \return Percentage (1-200), or 0 if cannot calculate.
  uint8_t SpeedToPercentage(double actualSpeedKmph, routing::HighwayType hwType,
                            bool isInCity) const;

  /// \brief Get free-flow speed for road type (maximum expected speed).
  double GetFreeFlowSpeedKmph(routing::HighwayType hwType, bool isInCity) const;

private:
  /// Country-specific speed adjustments (multiplier).
  struct CountrySpeedProfile
  {
    double m_urbanFactor = 1.0;    // Multiplier for urban speeds
    double m_ruralFactor = 1.0;    // Multiplier for rural speeds
    double m_motorwayFactor = 1.0; // Special factor for motorways
  };

  /// \brief Get country-specific speed profile.
  CountrySpeedProfile GetCountryProfile(std::string const & countryName) const;

  /// \brief Apply country-specific and context adjustments.
  double ApplyAdjustments(double baseSpeed, routing::HighwayType hwType, bool isInCity) const;

  std::shared_ptr<routing::VehicleModelInterface> m_vehicleModel;
  std::string m_countryName;
  CountrySpeedProfile m_countryProfile;

  /// Typical speed reduction factors for different road contexts.
  /// These represent typical congestion-free speeds as percentage of model speed.
  static constexpr double kUrbanReductionFactor = 0.75;  // 75% of model speed in city
  static constexpr double kRuralReductionFactor = 0.85;  // 85% of model speed outside city
  static constexpr double kPeakHourFactor = 0.60;        // 60% during peak hours (not used in base inference)
};

/// \brief Get typical speed for road classification without vehicle model.
/// Simple fallback based on OSM highway classification.
/// These are conservative estimates of typical travel speeds.
struct DefaultRoadSpeeds
{
  /// \brief Get typical speed for OSM highway classification.
  /// \param highwayClass OSM highway value (e.g., "motorway", "primary").
  /// \param isInCity Urban context.
  /// \return Typical speed in km/h.
  static double GetTypicalSpeed(std::string const & highwayClass, bool isInCity);

  /// Default speeds by highway class (km/h)
  /// Based on typical observed speeds, not speed limits
  static constexpr double kMotorwayUrban = 100.0;
  static constexpr double kMotorwayRural = 120.0;
  static constexpr double kTrunkUrban = 70.0;
  static constexpr double kTrunkRural = 90.0;
  static constexpr double kPrimaryUrban = 45.0;
  static constexpr double kPrimaryRural = 65.0;
  static constexpr double kSecondaryUrban = 40.0;
  static constexpr double kSecondaryRural = 55.0;
  static constexpr double kTertiaryUrban = 35.0;
  static constexpr double kTertiaryRural = 45.0;
  static constexpr double kResidentialUrban = 25.0;
  static constexpr double kResidentialRural = 30.0;
  static constexpr double kLivingStreet = 15.0;
  static constexpr double kService = 20.0;
  static constexpr double kUnclassified = 30.0;
  static constexpr double kTrack = 15.0;
};

std::string DebugPrint(OSMSpeedInference::SpeedEstimate const & estimate);

}  // namespace traffic

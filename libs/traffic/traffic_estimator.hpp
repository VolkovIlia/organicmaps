#pragma once

#include "traffic/historical_speed_data.hpp"
#include "traffic/historical_speed_provider.hpp"
#include "traffic/osm_speed_inference.hpp"
#include "traffic/speed_groups.hpp"

#include "indexer/mwm_set.hpp"

#include "routing_common/vehicle_model.hpp"

#include <ctime>
#include <memory>
#include <optional>

namespace traffic
{
/// \brief Source of traffic speed data, ordered by priority.
enum class TrafficSource : uint8_t
{
  kPersonalHistory = 0,   // Highest priority: user's own driving data
  kOnDeviceML = 1,        // ML model predictions
  kP2PReceived = 2,       // Data from nearby users (opt-in)
  kHistoricalPattern = 3, // Pre-computed historical patterns
  kOSMInference = 4,      // Inference from OSM road classification
  kRoadClassDefault = 5,  // Lowest priority: default for road class
  kCount
};

/// \brief Result of traffic estimation with metadata.
struct TrafficEstimate
{
  double m_speedKmph = 0.0;          // Estimated speed in km/h
  SpeedPercentage m_percentage = 0;  // Speed as percentage of free-flow (0-200)
  SpeedGroup m_speedGroup = SpeedGroup::Unknown;  // For rendering
  TrafficSource m_source = TrafficSource::kCount; // Which source provided this estimate
  double m_confidence = 0.0;         // Confidence level (0.0 - 1.0)
  bool m_isValid = false;            // Whether estimate is valid

  bool IsValid() const { return m_isValid && m_speedKmph > 0.0; }

  /// \brief Combine with another estimate using weighted average.
  void CombineWith(TrafficEstimate const & other, double otherWeight);
};

/// \brief Provides time-aware traffic speed estimates by combining multiple data sources.
///
/// Data source priority (highest to lowest):
/// 1. Personal history (user's own driving patterns)
/// 2. On-device ML predictions
/// 3. P2P received data (from nearby users, opt-in)
/// 4. Historical patterns (pre-computed, embedded in maps)
/// 5. OSM inference (based on road classification)
/// 6. Road class defaults (ultimate fallback)
///
/// All data processing is 100% local - no network requests.
class TrafficEstimator
{
public:
  /// \brief Configuration for the estimator.
  struct Config
  {
    // Whether to use historical patterns
    bool m_useHistoricalPatterns = true;

    // Whether to use OSM inference as fallback
    bool m_useOSMInference = true;

    // Maximum age of personal data to consider (in minutes)
    uint32_t m_maxPersonalDataAgeMinutes = 60;

    // Source weights for combining estimates
    float m_personalHistoryWeight = 0.9f;
    float m_onDeviceMLWeight = 0.7f;
    float m_p2pReceivedWeight = 0.5f;
    float m_historicalPatternWeight = 0.4f;
    float m_osmInferenceWeight = 0.2f;
  };

  TrafficEstimator();

  /// \brief Create estimator with custom configuration.
  explicit TrafficEstimator(Config const & config);

  /// \brief Set historical speed provider.
  void SetHistoricalProvider(std::shared_ptr<IHistoricalSpeedProvider> provider);

  /// \brief Set OSM speed inference (for country-specific rules).
  void SetOSMInference(std::shared_ptr<OSMSpeedInference> inference);

  /// \brief Get traffic estimate for a road segment at given time.
  /// \param mwmId MWM identifier.
  /// \param featureId Feature ID within MWM.
  /// \param segmentIdx Segment index within feature.
  /// \param isForward Direction flag.
  /// \param hwType Highway type for the segment.
  /// \param isInCity Whether segment is in urban area.
  /// \param time Time for which to get estimate (0 = current time).
  /// \return Traffic estimate with speed and metadata.
  [[nodiscard]] TrafficEstimate GetEstimate(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                             uint16_t segmentIdx, bool isForward,
                                             routing::HighwayType hwType, bool isInCity,
                                             std::time_t time = 0) const;

  /// \brief Get speed group for a segment (for rendering).
  /// This is a convenience method that returns just the SpeedGroup.
  [[nodiscard]] SpeedGroup GetSpeedGroup(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                          uint16_t segmentIdx, bool isForward,
                                          routing::HighwayType hwType, bool isInCity,
                                          std::time_t time = 0) const;

  /// \brief Get estimated speed in km/h for a segment.
  /// Returns 0 if no estimate available.
  [[nodiscard]] double GetSpeedKmph(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                     uint16_t segmentIdx, bool isForward,
                                     routing::HighwayType hwType, bool isInCity,
                                     std::time_t time = 0) const;

  /// \brief Calculate traffic factor for routing (multiplier for travel time).
  /// Factor > 1.0 means slower than free-flow, < 1.0 means faster.
  /// Returns 1.0 if no traffic data available.
  [[nodiscard]] double GetTrafficFactor(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                         uint16_t segmentIdx, bool isForward,
                                         routing::HighwayType hwType, bool isInCity,
                                         std::time_t time = 0) const;

  /// \brief Check if any traffic data is available for an MWM.
  [[nodiscard]] bool HasData(MwmSet::MwmId const & mwmId) const;

  /// \brief Get current configuration.
  Config const & GetConfig() const { return m_config; }

private:
  /// \brief Get estimate from historical patterns.
  std::optional<TrafficEstimate> GetHistoricalEstimate(MwmSet::MwmId const & mwmId,
                                                        uint32_t featureId, uint16_t segmentIdx,
                                                        bool isForward, routing::HighwayType hwType,
                                                        bool isInCity, std::time_t time) const;

  /// \brief Get estimate from OSM inference.
  std::optional<TrafficEstimate> GetOSMEstimate(routing::HighwayType hwType,
                                                 bool isInCity) const;

  /// \brief Get default estimate based on road class.
  TrafficEstimate GetDefaultEstimate(routing::HighwayType hwType, bool isInCity) const;

  /// \brief Convert speed to traffic factor.
  double SpeedToFactor(double speedKmph, routing::HighwayType hwType, bool isInCity) const;

  Config m_config;
  std::shared_ptr<IHistoricalSpeedProvider> m_historicalProvider;
  std::shared_ptr<OSMSpeedInference> m_osmInference;
};

/// \brief Calculate decay factor for data age.
/// Returns 1.0 for fresh data, decreasing as data ages.
/// \param dataAgeMinutes Age of data in minutes.
/// \param halfLifeMinutes Time at which factor = 0.5.
double CalcDecayFactor(uint32_t dataAgeMinutes, uint32_t halfLifeMinutes = 30);

std::string DebugPrint(TrafficSource source);
std::string DebugPrint(TrafficEstimate const & estimate);

}  // namespace traffic

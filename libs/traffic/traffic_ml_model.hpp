#pragma once

#include "routing_common/vehicle_model.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace traffic
{
/// \brief Input features for traffic ML model.
struct TrafficMLFeatures
{
  // Road classification (one-hot encoded internally)
  routing::HighwayType m_highwayType = routing::HighwayType::HighwayPrimary;
  bool m_isInCity = false;

  // Time components (will be cyclic encoded)
  uint8_t m_hour = 0;       // 0-23
  uint8_t m_dayOfWeek = 0;  // 0=Sunday, 6=Saturday

  // Historical context (normalized 0-1)
  float m_historicalSpeed = 0.0f;  // From embedded patterns, 0 if unavailable
  float m_personalBaseline = 0.0f; // From personal history, 0 if unavailable
  float m_freeFlowSpeed = 0.0f;    // Reference speed for normalization
};

/// \brief Output from traffic ML model.
struct TrafficMLPrediction
{
  float m_speedMultiplier = 1.0f;  // 0.0-2.0, multiply by free-flow speed
  float m_confidence = 0.0f;       // 0.0-1.0
  bool m_isValid = false;
};

/// \brief Abstract interface for traffic ML model inference.
/// Implementations: StubTrafficMLModel (testing), TFLiteTrafficMLModel, CoreMLTrafficMLModel
class ITrafficMLModel
{
public:
  virtual ~ITrafficMLModel() = default;

  /// \brief Run inference on input features.
  [[nodiscard]] virtual TrafficMLPrediction Predict(TrafficMLFeatures const & features) const = 0;

  /// \brief Check if model is loaded and ready.
  [[nodiscard]] virtual bool IsReady() const = 0;

  /// \brief Get model name/version for debugging.
  [[nodiscard]] virtual std::string GetModelInfo() const = 0;
};

/// \brief Build feature vector from raw inputs.
class TrafficMLFeatureBuilder
{
public:
  /// \brief Build features for ML model.
  /// \param hwType Highway type of the segment.
  /// \param isInCity Whether segment is in urban area.
  /// \param hour Hour of day (0-23).
  /// \param dayOfWeek Day of week (0=Sunday).
  /// \param historicalSpeedKmph Historical pattern speed, 0 if unavailable.
  /// \param personalSpeedKmph Personal history speed, 0 if unavailable.
  /// \param freeFlowSpeedKmph Free-flow reference speed.
  [[nodiscard]] static TrafficMLFeatures Build(routing::HighwayType hwType, bool isInCity,
                                               uint8_t hour, uint8_t dayOfWeek,
                                               float historicalSpeedKmph, float personalSpeedKmph,
                                               float freeFlowSpeedKmph);

  /// \brief Convert features to normalized float vector for model input.
  /// Layout: [road_class_onehot(8), hour_sin, hour_cos, day_sin, day_cos,
  ///          historical_norm, personal_norm, is_city]
  /// Total: 8 + 2 + 2 + 1 + 1 + 1 = 15 floats
  [[nodiscard]] static std::vector<float> ToVector(TrafficMLFeatures const & features);

  /// \brief Get expected input size for ML model.
  static constexpr size_t kInputSize = 15;

  /// \brief Number of road type categories for one-hot encoding.
  static constexpr size_t kRoadTypeCategories = 8;

private:
  /// \brief Cyclic encoding for hour (0-23) -> (sin, cos)
  [[nodiscard]] static std::pair<float, float> EncodeHour(uint8_t hour);

  /// \brief Cyclic encoding for day (0-6) -> (sin, cos)
  [[nodiscard]] static std::pair<float, float> EncodeDay(uint8_t dayOfWeek);

  /// \brief One-hot encode highway type (8 categories)
  [[nodiscard]] static std::array<float, kRoadTypeCategories> EncodeHighwayType(
      routing::HighwayType hwType);

  /// \brief Map highway type to category index (0-7)
  [[nodiscard]] static size_t GetRoadTypeCategory(routing::HighwayType hwType);
};

std::string DebugPrint(TrafficMLFeatures const & features);
std::string DebugPrint(TrafficMLPrediction const & prediction);

}  // namespace traffic

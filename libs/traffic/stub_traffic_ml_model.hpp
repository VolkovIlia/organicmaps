#pragma once

#include "traffic/traffic_ml_model.hpp"

namespace traffic
{
/// \brief Stub implementation of ITrafficMLModel for testing.
/// Uses simple heuristics based on time of day and road type.
/// Returns deterministic predictions (same input -> same output).
class StubTrafficMLModel : public ITrafficMLModel
{
public:
  StubTrafficMLModel() = default;

  /// \brief Predict speed multiplier using time-based heuristics.
  /// Rush hours (7-9 AM, 5-7 PM weekdays): 0.6-0.8 multiplier
  /// Night (10 PM - 6 AM): 1.1-1.2 multiplier (faster)
  /// Weekend: 0.9-1.0 multiplier
  [[nodiscard]] TrafficMLPrediction Predict(TrafficMLFeatures const & features) const override;

  [[nodiscard]] bool IsReady() const override { return true; }

  [[nodiscard]] std::string GetModelInfo() const override { return "StubTrafficMLModel v1.0"; }

private:
  /// \brief Check if time is during rush hour (weekdays 7-9 AM or 5-7 PM).
  [[nodiscard]] bool IsRushHour(uint8_t hour, uint8_t dayOfWeek) const;

  /// \brief Check if time is during night hours (10 PM - 6 AM).
  [[nodiscard]] bool IsNightTime(uint8_t hour) const;

  /// \brief Check if day is weekend (Saturday=6 or Sunday=0).
  [[nodiscard]] bool IsWeekend(uint8_t dayOfWeek) const;

  /// \brief Get base multiplier for road type (city roads are slower).
  [[nodiscard]] float GetRoadTypeMultiplier(routing::HighwayType hwType, bool isInCity) const;
};

}  // namespace traffic

// Library Documentation Verified: 2026-02-03
// Source: Organic Maps codebase analysis (routing_session.hpp, route.hpp)
// API Versions: Internal routing APIs

#pragma once

#include "routing/route.hpp"

#include "routing_common/vehicle_model.hpp"

#include "geometry/latlon.hpp"
#include "geometry/point2d.hpp"

#include <chrono>
#include <map>
#include <optional>

namespace routing
{
/// @brief Corridor state for off-route handling state machine
enum class CorridorState
{
  OnRoute,           ///< User is within corridor
  Monitoring,        ///< User outside corridor, timer running
  Deciding,          ///< Timer expired, evaluating options
  SuggestingReturn,  ///< Suggesting user return to route
  Rerouting          ///< Calculating new route
};

/// @brief Configuration for corridor behavior
struct CorridorConfig
{
  /// Base corridor widths by highway type (meters)
  /// HighwayMotorway: 50m, HighwayTrunk: 40m, HighwayPrimary: 30m
  /// HighwaySecondary: 25m, HighwayTertiary: 20m, HighwayResidential: 15m
  std::map<HighwayType, double> baseWidths;

  double minGpsCompensation = 10.0;       ///< Minimum GPS accuracy compensation (meters)
  double offRouteTimeThreshold = 10.0;    ///< Seconds outside corridor before Deciding
  double returnDistanceThreshold = 200.0; ///< Max distance (m) for suggesting return
  int returnTurnCountMax = 2;             ///< Max turns for return route suggestion
  double forwardRatioThreshold = 1.2;     ///< Accept deviation if forward <= return * ratio

  /// @brief Get default configuration with all base widths
  static CorridorConfig Default();
};

/// @brief Result of corridor position check
struct CorridorCheckResult
{
  CorridorState state = CorridorState::OnRoute;
  bool isInsideCorridor = true;
  double lateralDistanceMeters = 0.0;   ///< Distance from route centerline
  double corridorWidthMeters = 0.0;     ///< Current corridor width
  double timeOutsideSeconds = 0.0;      ///< Time spent outside corridor (if applicable)

  /// @brief Recommendation for UI/session
  enum class Recommendation
  {
    Continue,        ///< Stay on current route
    SuggestReturn,   ///< Show "return when safe" prompt
    Reroute          ///< Calculate new route
  };
  Recommendation recommendation = Recommendation::Continue;
};

/// @brief Interface for corridor-based route following
class ICorridorTracker
{
public:
  virtual ~ICorridorTracker() = default;

  /// @brief Check position against route corridor
  /// @param position Current GPS position
  /// @param gpsAccuracy GPS horizontal accuracy in meters
  /// @param route Current route being followed
  /// @return Corridor check result with state and recommendation
  virtual CorridorCheckResult CheckPosition(
      ms::LatLon const & position,
      double gpsAccuracy,
      Route const & route) = 0;

  /// @brief Get corridor width at specific route segment
  /// @param route Route to query
  /// @param segmentIndex Segment index along route
  /// @return Corridor width in meters
  virtual double GetCorridorWidth(Route const & route, size_t segmentIndex) const = 0;

  /// @brief Reset state machine to OnRoute state
  virtual void Reset() = 0;

  /// @brief Get current state
  virtual CorridorState GetState() const = 0;
};

/// @brief Production implementation of corridor tracking
class CorridorTracker : public ICorridorTracker
{
public:
  explicit CorridorTracker(CorridorConfig config = CorridorConfig::Default());

  CorridorCheckResult CheckPosition(
      ms::LatLon const & position,
      double gpsAccuracy,
      Route const & route) override;

  double GetCorridorWidth(Route const & route, size_t segmentIndex) const override;

  void Reset() override;
  CorridorState GetState() const override { return m_state; }

private:
  /// @brief Calculate lateral distance from position to route
  double CalcLateralDistance(ms::LatLon const & position, Route const & route) const;

  /// @brief Get base corridor width for a highway type
  double GetBaseWidthForHighwayType(HighwayType type) const;

  /// @brief Transition state machine
  void TransitionTo(CorridorState newState);

private:
  CorridorConfig m_config;
  CorridorState m_state = CorridorState::OnRoute;

  std::chrono::steady_clock::time_point m_outsideStartTime;
  double m_lastLateralDistance = 0.0;
  double m_lastGpsAccuracy = 10.0;
};

std::string DebugPrint(CorridorState state);

}  // namespace routing

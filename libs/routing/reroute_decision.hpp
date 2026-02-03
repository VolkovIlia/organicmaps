// Library Documentation Verified: 2026-02-03
// Source: Organic Maps codebase analysis (routing_session.hpp, route.hpp)
// API Versions: Internal routing APIs

#pragma once

#include "routing/route.hpp"

#include "geometry/latlon.hpp"

#include <string>

namespace routing
{
/// @brief Decision types for off-route handling
enum class RerouteDecisionType
{
  StayOnRoute,          ///< Continue on current route
  SuggestReturn,        ///< Suggest returning to route
  CalculateNewRoute     ///< Calculate completely new route
};

/// @brief Result of reroute decision analysis
struct RerouteDecisionResult
{
  RerouteDecisionType decision = RerouteDecisionType::CalculateNewRoute;
  double returnDistanceMeters = 0.0;    ///< Distance to return to route
  double forwardDistanceMeters = 0.0;   ///< Distance to destination via current position
  int returnTurnCount = 0;              ///< Number of turns to return
  double timeDifferenceSeconds = 0.0;   ///< Time saved by returning

  /// @brief Human-readable reason for decision
  std::string reason;
};

/// @brief Configuration for reroute decisions
struct RerouteDecisionConfig
{
  double maxReturnDistance = 200.0;      ///< Max meters for suggesting return
  int maxReturnTurns = 2;                ///< Max turns for suggesting return
  double forwardAcceptanceRatio = 1.2;   ///< Accept forward if <= return * ratio
  double minTimeSavingSeconds = 60.0;    ///< Min time saving to suggest return

  static RerouteDecisionConfig Default() { return RerouteDecisionConfig{}; }
};

/// @brief Interface for making reroute decisions
class IRerouteDecision
{
public:
  virtual ~IRerouteDecision() = default;

  /// @brief Decide how to handle off-route situation
  /// @param currentPosition User's current position
  /// @param currentRoute Route user deviated from
  /// @return Decision result with analysis
  virtual RerouteDecisionResult Decide(
      ms::LatLon const & currentPosition,
      Route const & currentRoute) = 0;
};

/// @brief Production implementation of reroute decision making
class RerouteDecision : public IRerouteDecision
{
public:
  explicit RerouteDecision(RerouteDecisionConfig config = RerouteDecisionConfig::Default());

  RerouteDecisionResult Decide(
      ms::LatLon const & currentPosition,
      Route const & currentRoute) override;

private:
  /// @brief Calculate distance to closest point on route
  double CalcReturnDistance(ms::LatLon const & position, Route const & route) const;

  /// @brief Calculate distance to destination from current position (simplified)
  double CalcForwardDistance(ms::LatLon const & position, Route const & route) const;

private:
  RerouteDecisionConfig m_config;
};

std::string DebugPrint(RerouteDecisionType type);

}  // namespace routing

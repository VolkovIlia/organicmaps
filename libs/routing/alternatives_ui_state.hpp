#pragma once

#include "routing/alternative_route.hpp"

#include <vector>

namespace routing
{

/// @brief UI state for alternative route display.
enum class AlternativeUIState
{
  Hidden,           ///< No alternatives shown (route too short or dismissed)
  Preview,          ///< Showing alternatives on map during route selection
  ApproachingPoint, ///< Approaching a decision point during navigation
  Dismissed         ///< User dismissed this alternative at decision point
};

/// @brief Callback interface for alternative route UI events.
class IAlternativeRouteListener
{
public:
  virtual ~IAlternativeRouteListener() = default;

  /// @brief Called when alternatives are computed and ready.
  /// @param alternatives Vector of alternative routes (excludes primary).
  virtual void OnAlternativesReady(
      std::vector<AlternativeRoute> const & alternatives) = 0;

  /// @brief Called when approaching a decision point.
  /// @param point The decision point being approached.
  /// @param alternative The alternative route at this decision point.
  virtual void OnDecisionPointApproaching(
      DecisionPoint const & point,
      AlternativeRoute const & alternative) = 0;

  /// @brief Called when passed a decision point without switching.
  /// @param decisionPointId ID of the passed decision point (alternativeIndex).
  virtual void OnDecisionPointPassed(size_t decisionPointId) = 0;
};

}  // namespace routing

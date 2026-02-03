#pragma once

#include "routing/segment.hpp"

#include "geometry/latlon.hpp"

#include <cstdint>
#include <vector>

namespace routing
{

/// @brief Point where user can switch to alternative route.
struct DecisionPoint
{
  ms::LatLon position;              ///< Geographic position
  double distanceFromStartMeters;   ///< Distance along primary route
  int alternativeIndex;             ///< Which alternative diverges here (1-based)
  double timeSavingSeconds;         ///< Time saved by taking alternative (positive = faster)

  /// @brief Check if this is a good time to show alternative notification.
  /// @param currentDistanceFromStart Current position along route in meters.
  /// @return True if within 500m-2km before decision point.
  bool IsGoodTimeToShow(double currentDistanceFromStart) const
  {
    double const distanceToDecision = distanceFromStartMeters - currentDistanceFromStart;
    return distanceToDecision >= 500.0 && distanceToDecision <= 2000.0;
  }

  bool operator==(DecisionPoint const & rhs) const
  {
    return alternativeIndex == rhs.alternativeIndex;
  }
};

/// @brief Alternative route with metadata for k-SPwLO algorithm.
struct AlternativeRoute
{
  int routeIndex = 0;               ///< 0 = primary, 1+ = alternatives
  double distanceMeters = 0.0;      ///< Total distance
  double durationSeconds = 0.0;     ///< Total duration
  double overlapWithPrimary = 0.0;  ///< Overlap ratio with primary (0.0-1.0)
  double diversityScore = 0.0;      ///< How different from primary (higher = better)

  std::vector<DecisionPoint> decisionPoints;
  std::vector<Segment> path;        ///< Full path segments

  /// @brief Get stretch ratio compared to primary route.
  /// @param primaryDistance Primary route distance in meters.
  /// @return Ratio (1.0 = same length, >1.0 = longer).
  double GetStretchRatio(double primaryDistance) const
  {
    if (primaryDistance <= 0.0)
      return 0.0;
    return distanceMeters / primaryDistance;
  }

  /// @brief Check if alternative meets acceptance criteria.
  /// @param maxOverlap Maximum allowed overlap (e.g., 0.6 = 60%).
  /// @param maxStretch Maximum allowed stretch (e.g., 1.3 = 130%).
  /// @param primaryDistance Primary route distance in meters.
  /// @return True if acceptable.
  bool IsAcceptable(double maxOverlap, double maxStretch, double primaryDistance) const
  {
    return overlapWithPrimary <= maxOverlap &&
           GetStretchRatio(primaryDistance) <= maxStretch;
  }

  bool IsValid() const
  {
    return routeIndex > 0 && distanceMeters > 0.0 && !path.empty();
  }
};

/// @brief Parameters for k-SPwLO alternative route computation.
struct AlternativeParams
{
  int k = 3;                        ///< Total routes (1 primary + k-1 alternatives)
  double overlapThreshold = 0.6;    ///< Max overlap with primary (Jaccard similarity)
  double maxLengthRatio = 1.3;      ///< Max stretch factor vs primary
  double minLengthMeters = 50000;   ///< Minimum route length to compute alternatives (50km)
  int maxViaNodeCandidates = 100;   ///< Max via-nodes to evaluate
  double minTimeSavingSeconds = 60; ///< Min time saving to show decision point (1 min)

  static AlternativeParams Default() { return AlternativeParams{}; }

  /// @brief Parameters for shorter routes (20-50km).
  static AlternativeParams ForMediumRoutes()
  {
    AlternativeParams params;
    params.minLengthMeters = 20000;
    params.maxViaNodeCandidates = 50;
    params.k = 2;  // Only 1 alternative for medium routes
    return params;
  }
};

}  // namespace routing

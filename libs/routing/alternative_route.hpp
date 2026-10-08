#pragma once

#include "routing/segment.hpp"

#include "geometry/latlon.hpp"
#include "geometry/point2d.hpp"

#include <cstdint>
#include <vector>

namespace routing
{

/// @brief Distance thresholds for showing alternative route notifications.
/// Show notification when user is between these distances before decision point.
constexpr double kMinDecisionPointShowDistanceMeters = 500.0;   ///< Don't show too close
constexpr double kMaxDecisionPointShowDistanceMeters = 2000.0;  ///< Don't show too early

/// @brief Point where user can switch to alternative route.
struct DecisionPoint
{
  ms::LatLon position;              ///< Geographic position
  double distanceFromStartMeters;   ///< Distance along primary route
  int alternativeIndex;             ///< Which alternative diverges here (1-based)
  double timeSavingSeconds;         ///< Time saved by taking alternative (positive = faster)

  /// @brief Check if this is a good time to show alternative notification.
  /// @param currentDistanceFromStart Current position along route in meters.
  /// @return True if within kMinDecisionPointShowDistanceMeters-kMaxDecisionPointShowDistanceMeters before decision point.
  bool IsGoodTimeToShow(double currentDistanceFromStart) const
  {
    double const distanceToDecision = distanceFromStartMeters - currentDistanceFromStart;
    return distanceToDecision >= kMinDecisionPointShowDistanceMeters &&
           distanceToDecision <= kMaxDecisionPointShowDistanceMeters;
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
  std::vector<m2::PointD> polylinePoints;  ///< Geographic points (Mercator) for map rendering

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
  double overlapThreshold = 0.95;   ///< Max overlap with primary (Jaccard similarity); high for highways
  double maxLengthRatio = 2.0;      ///< Max stretch factor vs primary
  double minLengthMeters = 10000;   ///< Minimum route length to compute alternatives (10km)
  int maxViaNodeCandidates = 10;    ///< Max via-nodes to evaluate
  double minTimeSavingSeconds = 60; ///< Min time saving to show decision point (1 min)
  double maxComputeTimeSec = 60.0;  ///< Abort search after this many wall-clock seconds

  static AlternativeParams Default() { return AlternativeParams{}; }

  /// @brief Parameters for shorter routes (5-10km).
  static AlternativeParams ForMediumRoutes()
  {
    AlternativeParams params;
    params.minLengthMeters = 5000;
    params.maxViaNodeCandidates = 7;
    params.k = 2;
    params.overlapThreshold = 0.93;
    params.maxLengthRatio = 2.0;
    params.maxComputeTimeSec = 30.0;
    return params;
  }
};

}  // namespace routing

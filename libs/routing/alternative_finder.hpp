#pragma once

#include "routing/alternative_route.hpp"
#include "routing/checkpoints.hpp"
#include "routing/route.hpp"
#include "routing/router.hpp"
#include "routing/segment.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace routing
{

/// @brief Callback for synchronous route calculation used by alternative finder.
using RouteCalculationFn = std::function<RouterResultCode(Checkpoints const &, Route &)>;

/// @brief Interface for finding alternative routes via waypoint approach.
class IAlternativeFinder
{
public:
  virtual ~IAlternativeFinder() = default;

  /// @brief Find alternative routes for a given primary route.
  /// @param primaryRoute Primary (shortest) route.
  /// @param params Algorithm parameters (k, overlap threshold, etc.).
  /// @param calculateRoute Callback for computing a route through checkpoints.
  /// @return Vector of alternatives (excluding primary). Empty if route too short.
  virtual std::vector<AlternativeRoute> Find(
      Route const & primaryRoute,
      AlternativeParams const & params,
      RouteCalculationFn const & calculateRoute) = 0;

  /// @brief Calculate overlap ratio between two segment paths (Jaccard similarity).
  virtual double CalcOverlap(
      std::vector<Segment> const & path1,
      std::vector<Segment> const & path2) const = 0;

  /// @brief Find decision points where alternatives diverge from primary.
  virtual std::vector<DecisionPoint> FindDecisionPoints(
      Route const & primary,
      std::vector<AlternativeRoute> const & alternatives) const = 0;
};

/// @brief Factory function to create alternative route finder.
std::unique_ptr<IAlternativeFinder> CreateAlternativeFinder();

}  // namespace routing

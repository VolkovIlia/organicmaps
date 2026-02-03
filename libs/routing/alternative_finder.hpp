#pragma once

#include "routing/alternative_route.hpp"
#include "routing/route.hpp"
#include "routing/segment.hpp"

#include <memory>
#include <vector>

namespace routing
{

/// @brief Interface for finding alternative routes using k-SPwLO algorithm.
/// k-SPwLO = k-Shortest Paths with Limited Overlap
class IAlternativeFinder
{
public:
  virtual ~IAlternativeFinder() = default;

  /// @brief Find alternative routes for a given primary route.
  /// @param primaryRoute Primary (shortest) route.
  /// @param params Algorithm parameters (k, overlap threshold, etc.).
  /// @return Vector of alternatives (excluding primary). Empty if route too short.
  virtual std::vector<AlternativeRoute> Find(
      Route const & primaryRoute,
      AlternativeParams const & params) = 0;

  /// @brief Calculate overlap ratio between two segment paths (Jaccard similarity).
  /// @param path1 First path segments.
  /// @param path2 Second path segments.
  /// @return Overlap ratio (0.0 = no overlap, 1.0 = identical).
  virtual double CalcOverlap(
      std::vector<Segment> const & path1,
      std::vector<Segment> const & path2) const = 0;

  /// @brief Find decision points where alternatives diverge from primary.
  /// @param primary Primary route.
  /// @param alternatives Alternative routes.
  /// @return Decision points sorted by distance from start.
  virtual std::vector<DecisionPoint> FindDecisionPoints(
      Route const & primary,
      std::vector<AlternativeRoute> const & alternatives) const = 0;
};

/// @brief Factory function to create alternative route finder.
std::unique_ptr<IAlternativeFinder> CreateAlternativeFinder();

}  // namespace routing

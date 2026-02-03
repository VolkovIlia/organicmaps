#include "routing/alternative_finder.hpp"

#include "geometry/mercator.hpp"

#include "base/logging.hpp"
#include "base/timer.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <unordered_set>

namespace routing
{
namespace
{
/// @brief Convert route segments to a set for overlap calculation.
std::set<Segment> CollectEdgeSet(std::vector<Segment> const & path)
{
  return std::set<Segment>(path.begin(), path.end());
}

/// @brief Collect segments from route.
std::vector<Segment> CollectRouteSegments(Route const & route)
{
  std::vector<Segment> segments;
  segments.reserve(route.GetRouteSegments().size());

  for (auto const & routeSegment : route.GetRouteSegments())
    segments.push_back(routeSegment.GetSegment());

  return segments;
}

}  // namespace

/// @brief Implementation of k-SPwLO alternative route finder.
/// Uses via-node approach: for each alternative, find a "via" node
/// that forces the route to diverge from primary.
class AlternativeFinder : public IAlternativeFinder
{
public:
  std::vector<AlternativeRoute> Find(
      Route const & primaryRoute,
      AlternativeParams const & params) override
  {
    base::Timer timer;

    if (!ValidateRouteLength(primaryRoute, params))
      return {};

    std::vector<Segment> const primaryPath = CollectRouteSegments(primaryRoute);
    if (primaryPath.empty())
    {
      LOG(LDEBUG, ("Primary route has no segments"));
      return {};
    }

    std::vector<AlternativeRoute> alternatives = EvaluateViaCandidates(
        primaryRoute, primaryPath, params);

    PopulateDecisionPoints(primaryRoute, alternatives);

    LOG(LINFO, ("Found", alternatives.size(), "alternatives in",
        timer.ElapsedSeconds() * 1000, "ms"));

    return alternatives;
  }

private:
  /// @brief Validate that route is long enough for alternatives.
  bool ValidateRouteLength(Route const & route, AlternativeParams const & params) const
  {
    double const length = route.GetTotalDistanceMeters();
    if (length < params.minLengthMeters)
    {
      LOG(LDEBUG, ("Route too short for alternatives:",
          length, "m <", params.minLengthMeters, "m"));
      return false;
    }
    return true;
  }

  /// @brief Evaluate via-node candidates and build valid alternatives.
  std::vector<AlternativeRoute> EvaluateViaCandidates(
      Route const & primaryRoute,
      std::vector<Segment> const & primaryPath,
      AlternativeParams const & params) const
  {
    std::vector<AlternativeRoute> alternatives;
    double const primaryLength = primaryRoute.GetTotalDistanceMeters();
    double const primaryDuration = primaryRoute.GetTotalTimeSec();

    std::unordered_set<Segment, std::hash<Segment>> primarySegmentSet(
        primaryPath.begin(), primaryPath.end());

    std::vector<uint32_t> viaCandidates = FindViaCandidates(
        primaryRoute, primarySegmentSet, params.maxViaNodeCandidates);

    for (uint32_t viaNode : viaCandidates)
    {
      if (static_cast<int>(alternatives.size()) >= params.k - 1)
        break;

      auto candidate = TryAcceptCandidate(
          primaryRoute, viaNode, primaryPath, primaryLength, primaryDuration, params, alternatives);

      if (candidate.has_value())
        alternatives.push_back(std::move(candidate.value()));
    }

    return alternatives;
  }

  /// @brief Try to build and accept a candidate alternative through via-node.
  std::optional<AlternativeRoute> TryAcceptCandidate(
      Route const & primaryRoute,
      uint32_t viaNode,
      std::vector<Segment> const & primaryPath,
      double primaryLength,
      double primaryDuration,
      AlternativeParams const & params,
      std::vector<AlternativeRoute> const & existingAlternatives) const
  {
    auto candidateOpt = TryBuildAlternative(primaryRoute, viaNode, primaryPath, params);
    if (!candidateOpt.has_value())
      return std::nullopt;

    AlternativeRoute & candidate = candidateOpt.value();
    candidate.routeIndex = static_cast<int>(existingAlternatives.size()) + 1;

    if (!candidate.IsAcceptable(params.overlapThreshold, params.maxLengthRatio, primaryLength))
      return std::nullopt;

    if (OverlapsExistingAlternative(candidate, existingAlternatives, params.overlapThreshold))
      return std::nullopt;

    candidate.diversityScore = 1.0 - candidate.overlapWithPrimary;

    LOG(LDEBUG, ("Found alternative", candidate.routeIndex,
        "overlap:", candidate.overlapWithPrimary,
        "stretch:", candidate.GetStretchRatio(primaryLength),
        "time saving:", primaryDuration - candidate.durationSeconds, "s"));

    return candidate;
  }

  /// @brief Check if candidate overlaps with any existing alternative.
  bool OverlapsExistingAlternative(
      AlternativeRoute const & candidate,
      std::vector<AlternativeRoute> const & existingAlternatives,
      double overlapThreshold) const
  {
    return std::any_of(existingAlternatives.begin(), existingAlternatives.end(),
        [&](AlternativeRoute const & existing) {
          return CalcOverlap(candidate.path, existing.path) > overlapThreshold;
        });
  }

  /// @brief Populate decision points for all alternatives.
  void PopulateDecisionPoints(
      Route const & primaryRoute,
      std::vector<AlternativeRoute> & alternatives) const
  {
    if (alternatives.empty())
      return;

    std::vector<DecisionPoint> decisionPoints = FindDecisionPoints(primaryRoute, alternatives);

    for (auto & point : decisionPoints)
    {
      int const idx = point.alternativeIndex - 1;
      if (idx >= 0 && idx < static_cast<int>(alternatives.size()))
        alternatives[idx].decisionPoints.push_back(point);
    }
  }

public:

  double CalcOverlap(
      std::vector<Segment> const & path1,
      std::vector<Segment> const & path2) const override
  {
    if (path1.empty() || path2.empty())
      return 0.0;

    std::set<Segment> const edges1 = CollectEdgeSet(path1);
    std::set<Segment> const edges2 = CollectEdgeSet(path2);

    // Calculate Jaccard similarity
    std::set<Segment> intersection;
    std::set_intersection(
        edges1.begin(), edges1.end(),
        edges2.begin(), edges2.end(),
        std::inserter(intersection, intersection.begin()));

    std::set<Segment> unionSet;
    std::set_union(
        edges1.begin(), edges1.end(),
        edges2.begin(), edges2.end(),
        std::inserter(unionSet, unionSet.begin()));

    if (unionSet.empty())
      return 0.0;

    return static_cast<double>(intersection.size()) /
           static_cast<double>(unionSet.size());
  }

  std::vector<DecisionPoint> FindDecisionPoints(
      Route const & primary,
      std::vector<AlternativeRoute> const & alternatives) const override
  {
    std::vector<DecisionPoint> points;
    std::vector<Segment> const primaryPath = CollectRouteSegments(primary);

    for (auto const & alt : alternatives)
    {
      // Find first segment where alternative diverges from primary
      size_t const divergeIdx = FindDivergenceIndex(primaryPath, alt.path);

      if (divergeIdx > 0 && divergeIdx < primaryPath.size())
      {
        DecisionPoint point;
        point.position = GetPositionAtIndex(primary, divergeIdx - 1);
        point.distanceFromStartMeters = GetDistanceAtIndex(primary, divergeIdx - 1);
        point.alternativeIndex = alt.routeIndex;
        point.timeSavingSeconds = primary.GetTotalTimeSec() - alt.durationSeconds;

        points.push_back(point);
      }
    }

    // Sort by distance from start
    std::sort(points.begin(), points.end(),
        [](DecisionPoint const & a, DecisionPoint const & b) {
          return a.distanceFromStartMeters < b.distanceFromStartMeters;
        });

    return points;
  }

private:
  /// @brief Find via-node candidates for alternative route generation.
  /// In full implementation, this queries CCH high-level nodes.
  std::vector<uint32_t> FindViaCandidates(
      Route const & /* primaryRoute */,
      std::unordered_set<Segment, std::hash<Segment>> const & /* primarySegments */,
      int /* maxCandidates */) const
  {
    // Placeholder for CCH integration.
    // Full implementation would:
    // 1. Get high-level nodes from CCH topology
    // 2. Filter out nodes on primary route
    // 3. Score by distance from primary corridor and CCH level
    // 4. Return top candidates
    return {};
  }

  /// @brief Try to build alternative route through via-node.
  std::optional<AlternativeRoute> TryBuildAlternative(
      Route const & primaryRoute,
      uint32_t /* viaNode */,
      std::vector<Segment> const & primaryPath,
      AlternativeParams const & /* params */) const
  {
    // Placeholder for CCH query integration.
    // Full implementation would:
    // 1. Query CCH: origin -> viaNode
    // 2. Query CCH: viaNode -> destination
    // 3. Concatenate paths
    // 4. Calculate overlap with primary
    // 5. Return candidate if valid

    // For now, return empty to allow structure to compile and test
    (void)primaryRoute;
    (void)primaryPath;
    return std::nullopt;
  }

  /// @brief Find index where alternative path diverges from primary.
  size_t FindDivergenceIndex(
      std::vector<Segment> const & primaryPath,
      std::vector<Segment> const & altPath) const
  {
    size_t i = 0;
    size_t const minSize = std::min(primaryPath.size(), altPath.size());

    while (i < minSize && primaryPath[i] == altPath[i])
      ++i;

    return i;
  }

  /// @brief Get geographic position at route segment index.
  ms::LatLon GetPositionAtIndex(Route const & route, size_t idx) const
  {
    auto const & segments = route.GetRouteSegments();
    if (idx < segments.size())
    {
      auto const & junction = segments[idx].GetJunction();
      return mercator::ToLatLon(junction.GetPoint());
    }
    return ms::LatLon();
  }

  /// @brief Get distance from start at route segment index.
  double GetDistanceAtIndex(Route const & route, size_t idx) const
  {
    auto const & segments = route.GetRouteSegments();
    if (idx < segments.size())
      return segments[idx].GetDistFromBeginningMeters();
    return 0.0;
  }
};

// Factory function
std::unique_ptr<IAlternativeFinder> CreateAlternativeFinder()
{
  return std::make_unique<AlternativeFinder>();
}

}  // namespace routing

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
    std::vector<AlternativeRoute> alternatives;

    // Step 0: Check minimum route length
    double const primaryLength = primaryRoute.GetTotalDistanceMeters();
    if (primaryLength < params.minLengthMeters)
    {
      LOG(LDEBUG, ("Route too short for alternatives:",
          primaryLength, "m <", params.minLengthMeters, "m"));
      return alternatives;
    }

    // Step 1: Collect primary route segments
    std::vector<Segment> const primaryPath = CollectRouteSegments(primaryRoute);
    if (primaryPath.empty())
    {
      LOG(LDEBUG, ("Primary route has no segments"));
      return alternatives;
    }

    std::unordered_set<Segment, std::hash<Segment>> primarySegmentSet(
        primaryPath.begin(), primaryPath.end());

    // Step 2: Find via-node candidates
    // In full implementation, this would query CCH high-level nodes.
    // For now, we implement the structure for future CCH integration.
    std::vector<uint32_t> viaCandidates = FindViaCandidates(
        primaryRoute, primarySegmentSet, params.maxViaNodeCandidates);

    // Step 3: For each via-node, try to build alternative
    double const primaryDuration = primaryRoute.GetTotalTimeSec();

    for (uint32_t viaNode : viaCandidates)
    {
      if (static_cast<int>(alternatives.size()) >= params.k - 1)
        break;  // Found enough alternatives

      // Try to construct alternative through via-node
      // In full implementation: query CCH for origin->via and via->dest
      auto candidateOpt = TryBuildAlternative(
          primaryRoute, viaNode, primaryPath, params);

      if (!candidateOpt.has_value())
        continue;

      AlternativeRoute & candidate = candidateOpt.value();
      candidate.routeIndex = static_cast<int>(alternatives.size()) + 1;

      // Check acceptance criteria
      if (!candidate.IsAcceptable(params.overlapThreshold,
                                   params.maxLengthRatio,
                                   primaryLength))
      {
        continue;
      }

      // Check overlap with existing alternatives
      bool const overlapsExisting = std::any_of(
          alternatives.begin(), alternatives.end(),
          [&](AlternativeRoute const & existing) {
            return CalcOverlap(candidate.path, existing.path) > params.overlapThreshold;
          });

      if (overlapsExisting)
        continue;

      // Calculate diversity score
      candidate.diversityScore = 1.0 - candidate.overlapWithPrimary;

      LOG(LDEBUG, ("Found alternative", candidate.routeIndex,
          "overlap:", candidate.overlapWithPrimary,
          "stretch:", candidate.GetStretchRatio(primaryLength),
          "time saving:", primaryDuration - candidate.durationSeconds, "s"));

      alternatives.push_back(std::move(candidate));
    }

    // Step 4: Find decision points for accepted alternatives
    if (!alternatives.empty())
    {
      std::vector<DecisionPoint> decisionPoints = FindDecisionPoints(
          primaryRoute, alternatives);

      // Assign decision points to alternatives
      for (auto & point : decisionPoints)
      {
        int const idx = point.alternativeIndex - 1;
        if (idx >= 0 && idx < static_cast<int>(alternatives.size()))
          alternatives[idx].decisionPoints.push_back(point);
      }
    }

    LOG(LINFO, ("Found", alternatives.size(), "alternatives in",
        timer.ElapsedSeconds() * 1000, "ms"));

    return alternatives;
  }

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

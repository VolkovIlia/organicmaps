#include "routing/alternative_finder.hpp"

#include "geometry/distance_on_sphere.hpp"
#include "geometry/mercator.hpp"
#include "geometry/point2d.hpp"

#include "base/logging.hpp"
#include "base/timer.hpp"

#include <algorithm>
#include <cmath>
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

/// @brief Perpendicular offset distances in meters.
double const kOffsetDistancesM[] = {2000.0, 5000.0, 10000.0};
size_t const kNumOffsets = std::size(kOffsetDistancesM);

/// @brief Route sample points (fraction of total length).
double const kSampleFractions[] = {0.25, 0.50, 0.75};
size_t const kNumSamples = std::size(kSampleFractions);

/// @brief Get a point on the route at a given fraction of total length.
m2::PointD GetRoutePointAtFraction(Route const & route, double fraction)
{
  auto const & poly = route.GetPoly();
  double const totalLen = poly.GetLength();
  return poly.GetPointByDistance(totalLen * fraction);
}

/// @brief Get direction vector at a route point (normalized).
m2::PointD GetRouteDirectionAtFraction(Route const & route, double fraction)
{
  auto const & poly = route.GetPoly();
  double const totalLen = poly.GetLength();
  double const targetLen = totalLen * fraction;

  double accumulated = 0.0;
  auto const & points = poly.GetPoints();

  for (size_t i = 1; i < points.size(); ++i)
  {
    double const segLen = points[i - 1].Length(points[i]);
    if (accumulated + segLen >= targetLen && segLen > 0.0)
    {
      auto dir = points[i] - points[i - 1];
      double const len = dir.Length();
      if (len > 0.0)
        return dir / len;
      break;
    }
    accumulated += segLen;
  }

  // Fallback: use last segment direction
  if (points.size() >= 2)
  {
    auto dir = points.back() - points[points.size() - 2];
    double const len = dir.Length();
    if (len > 0.0)
      return dir / len;
  }
  return {1.0, 0.0};
}

/// @brief Offset a point perpendicular to a direction in mercator.
m2::PointD OffsetPerpendicular(m2::PointD const & point, m2::PointD const & direction,
                                double offsetMeters, bool toRight)
{
  // Perpendicular direction (rotate 90 degrees)
  m2::PointD perp = toRight ? m2::PointD(direction.y, -direction.x)
                             : m2::PointD(-direction.y, direction.x);

  // Convert offset from meters to mercator units using separate X/Y scale factors
  double constexpr kEps = 1e-5;
  double const metersPerMercatorX = mercator::DistanceOnEarth(point, point + m2::PointD(kEps, 0.0)) / kEps;
  double const metersPerMercatorY = mercator::DistanceOnEarth(point, point + m2::PointD(0.0, kEps)) / kEps;

  if (metersPerMercatorX <= 0.0 || metersPerMercatorY <= 0.0)
    return point;

  return point + m2::PointD(perp.x * offsetMeters / metersPerMercatorX,
                             perp.y * offsetMeters / metersPerMercatorY);
}

}  // namespace

/// @brief Implementation of alternative route finder using via-waypoint approach.
class AlternativeFinder : public IAlternativeFinder
{
public:
  std::vector<AlternativeRoute> Find(
      Route const & primaryRoute,
      AlternativeParams const & params,
      RouteCalculationFn const & calculateRoute) override
  {
    base::Timer timer;

    if (!calculateRoute)
    {
      LOG(LDEBUG, ("No route calculation function provided"));
      return {};
    }

    if (!ValidateRouteLength(primaryRoute, params))
      return {};

    std::vector<Segment> const primaryPath = CollectRouteSegments(primaryRoute);
    if (primaryPath.empty())
    {
      LOG(LDEBUG, ("Primary route has no segments"));
      return {};
    }

    auto const start = primaryRoute.GetPoly().Front();
    auto const finish = primaryRoute.GetPoly().Back();

    std::vector<m2::PointD> viaCandidates = FindViaCandidates(primaryRoute, params.maxViaNodeCandidates);

    std::vector<AlternativeRoute> alternatives;
    double const primaryLength = primaryRoute.GetTotalDistanceMeters();
    double const primaryDuration = primaryRoute.GetTotalTimeSec();

    for (auto const & viaPoint : viaCandidates)
    {
      if (static_cast<int>(alternatives.size()) >= params.k - 1)
        break;

      auto candidate = TryBuildAlternative(
          primaryRoute, start, finish, viaPoint, primaryPath, primaryLength,
          primaryDuration, params, alternatives, calculateRoute);

      if (candidate.has_value())
      {
        candidate->routeIndex = static_cast<int>(alternatives.size()) + 1;
        candidate->diversityScore = 1.0 - candidate->overlapWithPrimary;

        LOG(LDEBUG, ("Found alternative", candidate->routeIndex,
            "overlap:", candidate->overlapWithPrimary,
            "stretch:", candidate->GetStretchRatio(primaryLength),
            "time diff:", primaryDuration - candidate->durationSeconds, "s"));

        alternatives.push_back(std::move(*candidate));
      }
    }

    PopulateDecisionPoints(primaryRoute, alternatives);

    LOG(LINFO, ("Found", alternatives.size(), "alternatives in",
        timer.ElapsedSeconds() * 1000, "ms"));

    return alternatives;
  }

private:
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

  /// @brief Generate via-waypoints perpendicular to primary route.
  std::vector<m2::PointD> FindViaCandidates(
      Route const & primaryRoute,
      int maxCandidates) const
  {
    std::vector<m2::PointD> candidates;
    candidates.reserve(kNumSamples * kNumOffsets * 2);

    for (size_t s = 0; s < kNumSamples; ++s)
    {
      double const fraction = kSampleFractions[s];
      auto const point = GetRoutePointAtFraction(primaryRoute, fraction);
      auto const direction = GetRouteDirectionAtFraction(primaryRoute, fraction);

      for (size_t o = 0; o < kNumOffsets; ++o)
      {
        if (static_cast<int>(candidates.size()) >= maxCandidates)
          break;

        // Offset to the right and left
        candidates.push_back(OffsetPerpendicular(point, direction, kOffsetDistancesM[o], true));
        candidates.push_back(OffsetPerpendicular(point, direction, kOffsetDistancesM[o], false));
      }
    }

    return candidates;
  }

  /// @brief Try to build alternative route through via-waypoint.
  std::optional<AlternativeRoute> TryBuildAlternative(
      Route const & primaryRoute,
      m2::PointD const & start,
      m2::PointD const & finish,
      m2::PointD const & viaPoint,
      std::vector<Segment> const & primaryPath,
      double primaryLength,
      double primaryDuration,
      AlternativeParams const & params,
      std::vector<AlternativeRoute> const & existingAlternatives,
      RouteCalculationFn const & calculateRoute) const
  {
    Route altRoute("alternative", 0 /* routeId */);

    // Build route: start → via → finish
    std::vector<m2::PointD> points = {start, viaPoint, finish};
    Checkpoints viaCheckpoints(std::move(points));

    auto const code = calculateRoute(viaCheckpoints, altRoute);
    if (code != RouterResultCode::NoError || !altRoute.IsValid())
      return std::nullopt;

    // Extract segments and compute overlap
    std::vector<Segment> altPath = CollectRouteSegments(altRoute);
    if (altPath.empty())
      return std::nullopt;

    double const overlap = CalcOverlap(primaryPath, altPath);
    double const altLength = altRoute.GetTotalDistanceMeters();
    double const altDuration = altRoute.GetTotalTimeSec();

    AlternativeRoute candidate;
    candidate.distanceMeters = altLength;
    candidate.durationSeconds = altDuration;
    candidate.overlapWithPrimary = overlap;
    candidate.path = std::move(altPath);

    // Copy polyline for rendering
    candidate.polyline = altRoute.GetPoly();

    // Copy route segments for traffic rendering
    auto const & routeSegs = altRoute.GetRouteSegments();
    candidate.routeSegments.assign(routeSegs.begin(), routeSegs.end());

    // Validate acceptance
    if (!candidate.IsAcceptable(params.overlapThreshold, params.maxLengthRatio, primaryLength))
      return std::nullopt;

    // Check overlap with existing alternatives
    if (OverlapsExistingAlternative(candidate, existingAlternatives, params.overlapThreshold))
      return std::nullopt;

    return candidate;
  }

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

    std::sort(points.begin(), points.end(),
        [](DecisionPoint const & a, DecisionPoint const & b) {
          return a.distanceFromStartMeters < b.distanceFromStartMeters;
        });

    return points;
  }

private:
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

  ms::LatLon GetPositionAtIndex(Route const & route, size_t idx) const
  {
    auto const & segments = route.GetRouteSegments();
    if (idx < segments.size())
      return mercator::ToLatLon(segments[idx].GetJunction().GetPoint());
    return ms::LatLon();
  }

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

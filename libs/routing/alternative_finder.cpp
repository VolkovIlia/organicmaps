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

/// @brief A corridor candidate — two via-points on the same side forming a parallel corridor.
struct CorridorCandidate
{
  std::vector<m2::PointD> viaPoints;  // 2 via-points defining the corridor
  double offsetMeters = 0;
  bool isRight = false;
};

/// @brief Implementation of alternative route finder using corridor-based via-waypoints.
/// Uses TWO via-points on the same side to force routes onto a parallel corridor
/// instead of creating loops from single via-point detours.
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
      return {};

    if (!ValidateRouteLength(primaryRoute, params))
      return {};

    std::vector<Segment> const primaryPath = CollectRouteSegments(primaryRoute);
    if (primaryPath.empty())
      return {};

    auto const start = primaryRoute.GetPoly().Front();
    auto const finish = primaryRoute.GetPoly().Back();

    auto corridors = GenerateCorridors(primaryRoute);

    std::vector<AlternativeRoute> alternatives;
    double const primaryLength = primaryRoute.GetTotalDistanceMeters();
    double const primaryDuration = primaryRoute.GetTotalTimeSec();

    for (size_t ci = 0; ci < corridors.size(); ++ci)
    {
      if (static_cast<int>(alternatives.size()) >= params.k - 1)
        break;

      auto const & corridor = corridors[ci];
      LOG(LINFO, ("Trying corridor", ci, ": offset", corridor.offsetMeters, "m",
          corridor.isRight ? "right" : "left",
          "via", mercator::ToLatLon(corridor.viaPoints[0]),
          "and", mercator::ToLatLon(corridor.viaPoints[1])));

      auto candidate = TryBuildCorridor(
          start, finish, corridor, primaryPath, primaryLength,
          primaryDuration, params, alternatives, calculateRoute);

      if (candidate.has_value())
      {
        candidate->routeIndex = static_cast<int>(alternatives.size()) + 1;
        candidate->diversityScore = 1.0 - candidate->overlapWithPrimary;

        LOG(LINFO, ("Found alternative", candidate->routeIndex,
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

  /// @brief Generate corridor candidates — pairs of via-points on the same side.
  /// Two via-points at fractions 0.30 and 0.70 force the route onto a parallel corridor
  /// for the middle 40% of the route, avoiding single-point loop artifacts.
  std::vector<CorridorCandidate> GenerateCorridors(Route const & primaryRoute) const
  {
    // Offset distances to try: medium (nearby parallel road), larger (ring road level).
    double constexpr kOffsets[] = {3000.0, 5000.0, 1500.0};
    // Via-point fractions: early third and late third of the route.
    double constexpr kFrac1 = 0.30;
    double constexpr kFrac2 = 0.70;

    auto const pt1 = GetRoutePointAtFraction(primaryRoute, kFrac1);
    auto const dir1 = GetRouteDirectionAtFraction(primaryRoute, kFrac1);
    auto const pt2 = GetRoutePointAtFraction(primaryRoute, kFrac2);
    auto const dir2 = GetRouteDirectionAtFraction(primaryRoute, kFrac2);

    std::vector<CorridorCandidate> corridors;
    for (auto const offset : kOffsets)
    {
      for (bool toRight : {true, false})
      {
        CorridorCandidate c;
        c.offsetMeters = offset;
        c.isRight = toRight;
        c.viaPoints = {
          OffsetPerpendicular(pt1, dir1, offset, toRight),
          OffsetPerpendicular(pt2, dir2, offset, toRight)
        };
        corridors.push_back(std::move(c));
      }
    }
    return corridors;
  }

  /// @brief Try to build alternative route through a corridor (2 via-points).
  std::optional<AlternativeRoute> TryBuildCorridor(
      m2::PointD const & start,
      m2::PointD const & finish,
      CorridorCandidate const & corridor,
      std::vector<Segment> const & primaryPath,
      double primaryLength,
      double primaryDuration,
      AlternativeParams const & params,
      std::vector<AlternativeRoute> const & existingAlternatives,
      RouteCalculationFn const & calculateRoute) const
  {
    Route altRoute("alternative", 0 /* routeId */);

    // Build route: start → via1 → via2 → finish (4-point corridor)
    std::vector<m2::PointD> points = {start, corridor.viaPoints[0], corridor.viaPoints[1], finish};
    Checkpoints checkpoints(std::move(points));

    base::Timer candidateTimer;
    auto const code = calculateRoute(checkpoints, altRoute);
    double const routeTimeMs = candidateTimer.ElapsedSeconds() * 1000;

    if (code != RouterResultCode::NoError || !altRoute.IsValid())
    {
      LOG(LINFO, ("  Corridor routing failed:", code, "in", routeTimeMs, "ms"));
      return std::nullopt;
    }

    std::vector<Segment> altPath = CollectRouteSegments(altRoute);
    if (altPath.empty())
      return std::nullopt;

    double const overlap = CalcOverlap(primaryPath, altPath);
    double const altLength = altRoute.GetTotalDistanceMeters();
    double const altDuration = altRoute.GetTotalTimeSec();
    double const stretch = primaryLength > 0 ? altLength / primaryLength : 0;

    LOG(LINFO, ("  Corridor result:", altLength, "m, overlap:", overlap,
        "stretch:", stretch, "in", routeTimeMs, "ms"));

    AlternativeRoute candidate;
    candidate.distanceMeters = altLength;
    candidate.durationSeconds = altDuration;
    candidate.overlapWithPrimary = overlap;
    candidate.path = std::move(altPath);
    candidate.polyline = altRoute.GetPoly();

    auto const & routeSegs = altRoute.GetRouteSegments();
    candidate.routeSegments.assign(routeSegs.begin(), routeSegs.end());

    if (!candidate.IsAcceptable(params.overlapThreshold, params.maxLengthRatio, primaryLength))
    {
      LOG(LINFO, ("  Rejected: overlap", overlap, ">", params.overlapThreshold,
          "or stretch", stretch, ">", params.maxLengthRatio));
      return std::nullopt;
    }

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

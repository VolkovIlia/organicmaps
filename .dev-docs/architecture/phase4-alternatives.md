# Phase 4: Alternative Routes - Detailed Design

**Version**: 1.0
**Date**: 2026-02-03
**Branch**: `feature/routing-alternatives`

---

## 1. Overview

Phase 4 implements alternative route computation using the k-SPwLO (k-Shortest Paths with Limited Overlap) algorithm. This provides users with meaningful route alternatives that are:
- Sufficiently different (low overlap)
- Not significantly longer (bounded stretch)
- Computed efficiently using Phase 3 CCH

### 1.1 Goals

| Goal | Target |
|------|--------|
| Alternative count | Up to 3 (1 primary + 2 alternatives) |
| Overlap threshold | < 60% with primary |
| Length stretch | < 130% of primary |
| Computation time | < 100ms (with CCH) |
| Minimum route length | > 50km (for alternatives to be meaningful) |

### 1.2 Algorithm: k-SPwLO

The k-Shortest Paths with Limited Overlap algorithm:
1. Find primary shortest path P1
2. For each alternative k:
   a. Select via-node from high-importance nodes NOT on P1
   b. Compute path: origin → via_node → destination
   c. Check overlap with P1 and existing alternatives
   d. Accept if overlap < threshold AND length < stretch * P1

```
Primary: A ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ B

Alt 1:   A ━━━━━━━┓                                    ┏━━━ B
                 ┃ (via V1 - diverges significantly)  ┃
                 ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛

Alt 2:   A ━━━━━━━━━━━━━━━┓                   ┏━━━━━━━━━━━ B
                         ┃ (via V2)          ┃
                         ┗━━━━━━━━━━━━━━━━━━━┛
```

---

## 2. Data Structures

### 2.1 Alternative Route Representation

```cpp
// alternative_route.hpp
#pragma once

#include "routing/route.hpp"
#include "geometry/latlon.hpp"

#include <vector>

namespace routing
{

/// @brief Point where user can switch to alternative
struct DecisionPoint
{
  ms::LatLon position;              ///< Geographic position
  double distanceFromStartMeters;   ///< Distance along primary route
  int alternativeIndex;             ///< Which alternative diverges here
  double timeSavingSeconds;         ///< Time saved by taking alternative

  /// @brief Check if this is a good time to show alternative
  bool IsGoodTimeToShow(double currentDistanceFromStart) const
  {
    double const distanceToDecision = distanceFromStartMeters - currentDistanceFromStart;
    return distanceToDecision >= 500.0 && distanceToDecision <= 2000.0;
  }
};

/// @brief Alternative route with metadata
struct AlternativeRoute
{
  int routeIndex;                   ///< 0 = primary, 1+ = alternatives
  double distanceMeters;            ///< Total distance
  double durationSeconds;           ///< Total duration
  double overlapWithPrimary;        ///< Overlap ratio (0.0-1.0)
  double diversityScore;            ///< How different (higher = better)

  std::vector<DecisionPoint> decisionPoints;
  std::vector<Segment> path;        ///< Full path segments

  /// @brief Stretch ratio vs primary route
  double GetStretchRatio(double primaryDistance) const
  {
    return distanceMeters / primaryDistance;
  }

  /// @brief Check if alternative is acceptable
  bool IsAcceptable(double maxOverlap, double maxStretch, double primaryDistance) const
  {
    return overlapWithPrimary <= maxOverlap &&
           GetStretchRatio(primaryDistance) <= maxStretch;
  }
};

/// @brief Parameters for alternative route computation
struct AlternativeParams
{
  int k = 3;                        ///< Number of routes (including primary)
  double overlapThreshold = 0.6;    ///< Max overlap with primary
  double maxLengthRatio = 1.3;      ///< Max stretch factor
  double minLengthMeters = 50000;   ///< Minimum route length for alternatives
  int maxViaNodeCandidates = 100;   ///< Max via-nodes to consider

  static AlternativeParams Default() { return AlternativeParams{}; }
};

}  // namespace routing
```

### 2.2 Alternative Finder Interface

```cpp
// alternative_finder.hpp
#pragma once

#include "routing/alternative_route.hpp"
#include "routing/route.hpp"
#include "routing/cch_query_engine.hpp"

namespace routing
{

/// @brief Interface for finding alternative routes
class IAlternativeFinder
{
public:
  virtual ~IAlternativeFinder() = default;

  /// @brief Find alternative routes using k-SPwLO algorithm
  /// @param primaryRoute Primary (shortest) route
  /// @param params Algorithm parameters
  /// @param cchEngine CCH engine for fast queries
  /// @return Vector of alternatives (excluding primary)
  virtual std::vector<AlternativeRoute> Find(
    Route const & primaryRoute,
    AlternativeParams const & params,
    ICCHQueryEngine & cchEngine) = 0;

  /// @brief Calculate overlap ratio between two routes
  /// @param route1 First route
  /// @param route2 Second route
  /// @return Overlap ratio (0.0 = no overlap, 1.0 = identical)
  virtual double CalcOverlap(
    Route const & route1,
    Route const & route2) const = 0;

  /// @brief Find decision points where alternatives diverge
  /// @param primary Primary route
  /// @param alternatives Alternative routes
  /// @return Decision points for UI display
  virtual std::vector<DecisionPoint> FindDecisionPoints(
    Route const & primary,
    std::vector<AlternativeRoute> const & alternatives) const = 0;
};

}  // namespace routing
```

### 2.3 Alternative Finder Implementation

```cpp
// alternative_finder.cpp
#include "routing/alternative_finder.hpp"

#include "base/logging.hpp"
#include "base/timer.hpp"

#include <algorithm>
#include <set>
#include <unordered_set>

namespace routing
{

namespace
{

/// @brief Score function for via-node selection
/// Higher score = better candidate for diverse alternative
double ScoreViaNode(
    uint32_t nodeId,
    Route const & primaryRoute,
    CCHTopology const & topology)
{
  // Factors:
  // 1. Distance from primary route (further = better)
  // 2. Node importance (higher level = faster query)
  // 3. Connectivity (more edges = more route options)

  double const level = topology.GetLevel(nodeId);
  double const maxLevel = topology.GetLevelCount();

  // Prefer high-level nodes (contracted late = important)
  return level / maxLevel;
}

}  // namespace

class AlternativeFinder : public IAlternativeFinder
{
public:
  explicit AlternativeFinder(CCHTopology const & topology)
    : m_topology(topology)
  {
  }

  std::vector<AlternativeRoute> Find(
      Route const & primaryRoute,
      AlternativeParams const & params,
      ICCHQueryEngine & cchEngine) override
  {
    base::Timer timer;
    std::vector<AlternativeRoute> alternatives;

    // Step 0: Check if route is long enough for alternatives
    double const primaryLength = primaryRoute.GetTotalDistanceMeters();
    if (primaryLength < params.minLengthMeters)
    {
      LOG(LDEBUG, ("Route too short for alternatives:",
        primaryLength, "m <", params.minLengthMeters, "m"));
      return alternatives;
    }

    // Step 1: Collect nodes on primary route (to exclude from via-nodes)
    std::unordered_set<uint32_t> primaryNodes = CollectRouteNodes(primaryRoute);

    // Step 2: Find via-node candidates (high-level nodes not on primary)
    std::vector<std::pair<double, uint32_t>> candidates =
      FindViaCandidates(primaryRoute, primaryNodes, params.maxViaNodeCandidates);

    // Step 3: Try each via-node and compute alternative
    ms::LatLon const origin = primaryRoute.GetStartPoint();
    ms::LatLon const destination = primaryRoute.GetFinishPoint();

    for (auto const & [score, viaNodeId] : candidates)
    {
      if (static_cast<int>(alternatives.size()) >= params.k - 1)
        break;  // Found enough alternatives

      // Query: origin → via_node
      CCHQueryRequest req1;
      req1.origin = origin;
      req1.destination = NodeToLatLon(viaNodeId);
      auto const result1 = cchEngine.Query(req1);

      if (!result1.success)
        continue;

      // Query: via_node → destination
      CCHQueryRequest req2;
      req2.origin = NodeToLatLon(viaNodeId);
      req2.destination = destination;
      auto const result2 = cchEngine.Query(req2);

      if (!result2.success)
        continue;

      // Construct candidate alternative
      AlternativeRoute candidate;
      candidate.routeIndex = alternatives.size() + 1;
      candidate.distanceMeters = result1.distanceMeters + result2.distanceMeters;
      candidate.durationSeconds = result1.durationSeconds + result2.durationSeconds;
      candidate.path = ConcatenatePaths(result1.path, result2.path);

      // Check overlap with primary
      candidate.overlapWithPrimary = CalcOverlapWithPrimary(
        candidate.path, primaryRoute);

      // Check if acceptable
      if (candidate.IsAcceptable(
            params.overlapThreshold,
            params.maxLengthRatio,
            primaryLength))
      {
        // Check overlap with existing alternatives
        bool const overlapsExisting = std::any_of(
          alternatives.begin(), alternatives.end(),
          [&](AlternativeRoute const & existing) {
            return CalcOverlapBetween(candidate.path, existing.path) > params.overlapThreshold;
          });

        if (!overlapsExisting)
        {
          candidate.diversityScore = 1.0 - candidate.overlapWithPrimary;
          alternatives.push_back(std::move(candidate));
          LOG(LDEBUG, ("Found alternative", candidate.routeIndex,
            "overlap:", candidate.overlapWithPrimary,
            "stretch:", candidate.GetStretchRatio(primaryLength)));
        }
      }
    }

    LOG(LINFO, ("Found", alternatives.size(), "alternatives in",
      timer.ElapsedSeconds() * 1000, "ms"));

    return alternatives;
  }

  double CalcOverlap(Route const & route1, Route const & route2) const override
  {
    // Use edge-based overlap (Jaccard similarity)
    std::set<Segment> edges1 = CollectEdges(route1);
    std::set<Segment> edges2 = CollectEdges(route2);

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

    for (auto const & alt : alternatives)
    {
      // Find first segment where alternative diverges from primary
      size_t divergeIdx = FindDivergenceIndex(primary, alt.path);

      if (divergeIdx > 0)
      {
        DecisionPoint point;
        point.position = GetPositionAtSegment(primary, divergeIdx - 1);
        point.distanceFromStartMeters = GetDistanceToSegment(primary, divergeIdx - 1);
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
  std::unordered_set<uint32_t> CollectRouteNodes(Route const & route) const
  {
    std::unordered_set<uint32_t> nodes;
    for (auto const & segment : route.GetRouteSegments())
    {
      // Map segment to CCH nodes
      // ... implementation ...
    }
    return nodes;
  }

  std::vector<std::pair<double, uint32_t>> FindViaCandidates(
      Route const & primaryRoute,
      std::unordered_set<uint32_t> const & excludeNodes,
      int maxCandidates) const
  {
    std::vector<std::pair<double, uint32_t>> candidates;

    // Scan high-level nodes
    for (uint32_t nodeId = 0; nodeId < m_topology.GetNodeCount(); ++nodeId)
    {
      if (excludeNodes.count(nodeId) > 0)
        continue;

      double const score = ScoreViaNode(nodeId, primaryRoute, m_topology);

      // Only consider high-scoring nodes
      if (score > 0.7)  // Top 30% by level
      {
        candidates.emplace_back(score, nodeId);
      }
    }

    // Sort by score (descending) and take top candidates
    std::sort(candidates.begin(), candidates.end(),
      [](auto const & a, auto const & b) { return a.first > b.first; });

    if (static_cast<int>(candidates.size()) > maxCandidates)
      candidates.resize(maxCandidates);

    return candidates;
  }

  ms::LatLon NodeToLatLon(uint32_t nodeId) const
  {
    // Map CCH node to geographic position
    // ... implementation using spatial index ...
    return ms::LatLon();
  }

  std::vector<Segment> ConcatenatePaths(
      std::vector<Segment> const & path1,
      std::vector<Segment> const & path2) const
  {
    std::vector<Segment> result = path1;
    result.insert(result.end(), path2.begin(), path2.end());
    return result;
  }

  double CalcOverlapWithPrimary(
      std::vector<Segment> const & altPath,
      Route const & primary) const
  {
    std::set<Segment> altEdges(altPath.begin(), altPath.end());
    std::set<Segment> primaryEdges = CollectEdges(primary);

    std::set<Segment> intersection;
    std::set_intersection(
      altEdges.begin(), altEdges.end(),
      primaryEdges.begin(), primaryEdges.end(),
      std::inserter(intersection, intersection.begin()));

    std::set<Segment> unionSet;
    std::set_union(
      altEdges.begin(), altEdges.end(),
      primaryEdges.begin(), primaryEdges.end(),
      std::inserter(unionSet, unionSet.begin()));

    return static_cast<double>(intersection.size()) /
           static_cast<double>(unionSet.size());
  }

  double CalcOverlapBetween(
      std::vector<Segment> const & path1,
      std::vector<Segment> const & path2) const
  {
    std::set<Segment> edges1(path1.begin(), path1.end());
    std::set<Segment> edges2(path2.begin(), path2.end());

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

    return static_cast<double>(intersection.size()) /
           static_cast<double>(unionSet.size());
  }

  std::set<Segment> CollectEdges(Route const & route) const
  {
    std::set<Segment> edges;
    for (auto const & segment : route.GetRouteSegments())
    {
      edges.insert(segment.GetSegment());
    }
    return edges;
  }

  size_t FindDivergenceIndex(
      Route const & primary,
      std::vector<Segment> const & altPath) const
  {
    auto const & primarySegments = primary.GetRouteSegments();
    size_t i = 0;

    while (i < primarySegments.size() && i < altPath.size())
    {
      if (primarySegments[i].GetSegment() != altPath[i])
        break;
      ++i;
    }

    return i;
  }

  ms::LatLon GetPositionAtSegment(Route const & route, size_t idx) const
  {
    auto const & segments = route.GetRouteSegments();
    if (idx < segments.size())
    {
      return segments[idx].GetJunction().GetLatLon();
    }
    return route.GetFinishPoint();
  }

  double GetDistanceToSegment(Route const & route, size_t idx) const
  {
    auto const & segments = route.GetRouteSegments();
    double distance = 0.0;
    for (size_t i = 0; i <= idx && i < segments.size(); ++i)
    {
      distance += segments[i].GetDistanceFromBeginningMeters();
    }
    return distance;
  }

private:
  CCHTopology const & m_topology;
};

}  // namespace routing
```

---

## 3. Integration with RoutingSession

### 3.1 Storage of Alternatives

```cpp
// routing_session.hpp additions

#include "routing/alternative_route.hpp"

class RoutingSession
{
  // ... existing members ...

private:
  // Phase 4: Alternative routes
  std::vector<AlternativeRoute> m_alternatives;
  std::set<size_t> m_dismissedDecisionPoints;

public:
  /// @brief Get available alternatives
  std::vector<AlternativeRoute> const & GetAlternatives() const;

  /// @brief Switch to alternative route
  /// @param index Alternative index (1-based)
  /// @return True if switch succeeded
  bool SwitchToAlternative(int index);

  /// @brief Dismiss alternative at decision point
  /// @param decisionPointId ID of decision point
  void DismissAlternative(size_t decisionPointId);

  /// @brief Check if should show alternative (based on position)
  std::optional<DecisionPoint> GetVisibleDecisionPoint() const;
};
```

### 3.2 Alternative Display Logic

```cpp
// routing_session.cpp additions

void RoutingSession::OnLocationPositionChanged(GpsInfo const & info)
{
  // ... existing code ...

  // Phase 4: Check if approaching decision point
  if (!m_alternatives.empty())
  {
    auto const visiblePoint = GetVisibleDecisionPoint();

    if (visiblePoint && !m_dismissedDecisionPoints.count(visiblePoint->alternativeIndex))
    {
      // Check if alternative saves significant time
      if (visiblePoint->timeSavingSeconds > 60.0)  // > 1 minute savings
      {
        m_routingStateListener->OnAlternativeAvailable(
          visiblePoint->alternativeIndex,
          visiblePoint->timeSavingSeconds);
      }
    }
  }
}

std::optional<DecisionPoint> RoutingSession::GetVisibleDecisionPoint() const
{
  if (!m_route || m_alternatives.empty())
    return std::nullopt;

  double const currentDistance = m_route->GetCurrentDistanceFromBeginMeters();

  for (auto const & alt : m_alternatives)
  {
    for (auto const & point : alt.decisionPoints)
    {
      if (point.IsGoodTimeToShow(currentDistance))
      {
        return point;
      }
    }
  }

  return std::nullopt;
}

bool RoutingSession::SwitchToAlternative(int index)
{
  if (index < 1 || index > static_cast<int>(m_alternatives.size()))
    return false;

  auto const & alt = m_alternatives[index - 1];

  // Create new route from alternative path
  Route newRoute;
  // ... populate route from alt.path ...

  m_route = std::make_unique<Route>(std::move(newRoute));
  m_alternatives.clear();  // Clear alternatives after switch

  return true;
}

void RoutingSession::DismissAlternative(size_t decisionPointId)
{
  m_dismissedDecisionPoints.insert(decisionPointId);
}
```

### 3.3 Integration with IndexRouter

```cpp
// index_router.cpp modifications

RouterResultCode IndexRouter::DoCalculateRoute(
    Checkpoints const & checkpoints,
    Route & route)
{
  // Calculate primary route (existing logic)
  RouterResultCode const result = DoCalculateRoutePrimary(checkpoints, route);

  if (result != RouterResultCode::NoError)
    return result;

  // Phase 4: Calculate alternatives if route is long enough
  if (m_computeAlternatives && route.GetTotalDistanceMeters() > 50000)
  {
    CalculateAlternatives(checkpoints, route);
  }

  return result;
}

void IndexRouter::CalculateAlternatives(
    Checkpoints const & checkpoints,
    Route & primaryRoute)
{
  if (!m_cchQueryEngine)
  {
    LOG(LWARNING, ("CCH not available, skipping alternatives"));
    return;
  }

  AlternativeParams params;
  params.k = 3;  // Primary + 2 alternatives
  params.overlapThreshold = 0.6;
  params.maxLengthRatio = 1.3;

  AlternativeFinder finder(m_cchTopology);
  std::vector<AlternativeRoute> alternatives = finder.Find(
    primaryRoute, params, *m_cchQueryEngine);

  // Store alternatives in route for session to access
  primaryRoute.SetAlternatives(std::move(alternatives));
}
```

---

## 4. UI Integration

### 4.1 Alternative Route Display States

```cpp
// routing/alternatives_ui_state.hpp
#pragma once

namespace routing
{

/// @brief States for alternative route UI
enum class AlternativeUIState
{
  Hidden,           ///< No alternatives shown
  Preview,          ///< Showing alternatives on map (route selection)
  ApproachingPoint, ///< Approaching decision point during navigation
  Dismissed         ///< User dismissed this decision point
};

/// @brief UI callback for alternative route events
class IAlternativeRouteListener
{
public:
  virtual ~IAlternativeRouteListener() = default;

  /// @brief Called when alternatives are computed
  virtual void OnAlternativesReady(
    std::vector<AlternativeRoute> const & alternatives) = 0;

  /// @brief Called when approaching a decision point
  virtual void OnDecisionPointApproaching(
    DecisionPoint const & point,
    AlternativeRoute const & alternative) = 0;

  /// @brief Called when passed a decision point
  virtual void OnDecisionPointPassed(size_t decisionPointId) = 0;
};

}  // namespace routing
```

### 4.2 Alternative Visualization

```cpp
// map/alternative_route_mark.hpp

/// @brief Visual representation of alternative route on map
class AlternativeRouteMark
{
public:
  void SetRoute(AlternativeRoute const & route);
  void SetHighlighted(bool highlighted);
  void SetSelected(bool selected);

  /// @brief Render route polyline
  void Render(dp::GraphicsContext & context) const;

private:
  std::vector<m2::PointD> m_polyline;
  dp::Color m_color = dp::Color(128, 128, 128, 180);  // Gray, semi-transparent
  bool m_highlighted = false;
  bool m_selected = false;
};

/// @brief Visual representation of decision point
class DecisionPointMark
{
public:
  void SetPosition(ms::LatLon const & position);
  void SetTimeSaving(double seconds);

  void Render(dp::GraphicsContext & context) const;

private:
  m2::PointD m_position;
  double m_timeSavingSeconds = 0.0;
};
```

---

## 5. Unit Tests

### 5.1 Alternative Finder Tests

```cpp
// alternative_finder_tests.cpp
#include "testing/testing.hpp"
#include "routing/alternative_finder.hpp"

UNIT_TEST(AlternativeFinder_FindsAlternatives)
{
  auto const topology = LoadTestTopology();
  AlternativeFinder finder(topology);
  MockCCHQueryEngine cchEngine;

  // Create primary route (100km)
  Route primary = CreateMockRoute(100000);

  AlternativeParams params;
  params.k = 3;
  params.overlapThreshold = 0.6;

  auto const alternatives = finder.Find(primary, params, cchEngine);

  TEST_GREATER_OR_EQUAL(alternatives.size(), 1, ("Should find at least one alternative"));
  TEST_LESS_OR_EQUAL(alternatives.size(), 2, ("Should find at most 2 alternatives"));

  for (auto const & alt : alternatives)
  {
    TEST_LESS_OR_EQUAL(alt.overlapWithPrimary, 0.6, ("Overlap should be <= 60%"));
    TEST_LESS_OR_EQUAL(alt.GetStretchRatio(100000), 1.3, ("Stretch should be <= 130%"));
  }
}

UNIT_TEST(AlternativeFinder_ShortRouteNoAlternatives)
{
  auto const topology = LoadTestTopology();
  AlternativeFinder finder(topology);
  MockCCHQueryEngine cchEngine;

  // Create short route (10km)
  Route primary = CreateMockRoute(10000);

  AlternativeParams params;
  params.minLengthMeters = 50000;

  auto const alternatives = finder.Find(primary, params, cchEngine);

  TEST(alternatives.empty(), ("Short routes should have no alternatives"));
}

UNIT_TEST(AlternativeFinder_OverlapCalculation)
{
  auto const topology = LoadTestTopology();
  AlternativeFinder finder(topology);

  // Two identical routes
  Route route1 = CreateMockRoute({{0,0}, {1,0}, {2,0}});
  Route route2 = CreateMockRoute({{0,0}, {1,0}, {2,0}});

  double const overlap = finder.CalcOverlap(route1, route2);
  TEST_ALMOST_EQUAL_ABS(overlap, 1.0, 0.01, ("Identical routes should have 100% overlap"));

  // Two completely different routes
  Route route3 = CreateMockRoute({{0,0}, {0,1}, {0,2}});
  double const overlap2 = finder.CalcOverlap(route1, route3);
  TEST_ALMOST_EQUAL_ABS(overlap2, 0.0, 0.01, ("Different routes should have 0% overlap"));
}

UNIT_TEST(AlternativeFinder_DecisionPoints)
{
  auto const topology = LoadTestTopology();
  AlternativeFinder finder(topology);

  Route primary = CreateMockRoute(100000);
  std::vector<AlternativeRoute> alternatives;

  AlternativeRoute alt1;
  alt1.routeIndex = 1;
  alt1.path = CreateMockPath(/* diverges at 30km */);
  alt1.durationSeconds = primary.GetTotalTimeSec() - 120;  // Saves 2 minutes
  alternatives.push_back(alt1);

  auto const points = finder.FindDecisionPoints(primary, alternatives);

  TEST_EQUAL(points.size(), 1, ("Should find one decision point"));
  TEST_GREATER(points[0].distanceFromStartMeters, 0, ());
  TEST_EQUAL(points[0].alternativeIndex, 1, ());
}

UNIT_TEST(DecisionPoint_IsGoodTimeToShow)
{
  DecisionPoint point;
  point.distanceFromStartMeters = 30000;  // 30km from start

  // 500m - 2km before = good time
  TEST(point.IsGoodTimeToShow(28500), ("Should show at 1.5km before"));
  TEST(point.IsGoodTimeToShow(29000), ("Should show at 1km before"));
  TEST(point.IsGoodTimeToShow(29500), ("Should show at 500m before"));

  // Too early or too late
  TEST(!point.IsGoodTimeToShow(25000), ("Should not show at 5km before"));
  TEST(!point.IsGoodTimeToShow(29800), ("Should not show at 200m before"));
  TEST(!point.IsGoodTimeToShow(31000), ("Should not show after passing"));
}
```

### 5.2 Integration Tests

```cpp
UNIT_TEST(Alternatives_EndToEnd)
{
  // Full integration test with real MWM data
  IndexRouter router(/* ... */);
  router.SetComputeAlternatives(true);

  Checkpoints checkpoints;
  checkpoints.SetStart(ms::LatLon(55.7558, 37.6173));  // Moscow
  checkpoints.SetFinish(ms::LatLon(59.9343, 30.3351));  // St. Petersburg

  Route route;
  auto const result = router.CalculateRoute(checkpoints, route);

  TEST_EQUAL(result, RouterResultCode::NoError, ());

  auto const & alternatives = route.GetAlternatives();
  TEST_GREATER_OR_EQUAL(alternatives.size(), 1,
    ("Moscow-SPB should have alternatives"));

  for (auto const & alt : alternatives)
  {
    // Verify all alternatives are valid
    TEST_GREATER(alt.distanceMeters, 0, ());
    TEST_GREATER(alt.durationSeconds, 0, ());
    TEST_LESS(alt.overlapWithPrimary, 1.0, ());
    TEST(!alt.path.empty(), ());
  }
}
```

---

## 6. Feature Flag & Rollout

### 6.1 Feature Flag

```cpp
// platform/settings.hpp
namespace settings
{
  extern char const * kAlternativeRoutesEnabled;
}

// Default: false until Phase 3 CCH is deployed
```

### 6.2 Rollout Strategy

1. **Prerequisite**: Phase 3 CCH must be deployed first
2. **Alpha**: Enable for test routes only
3. **Beta**: Enable for routes > 100km, 10% of users
4. **RC**: Enable for routes > 50km, 50% of users
5. **Release**: Full rollout for all qualifying routes

---

## 7. Performance Optimization

### 7.1 Via-Node Selection Optimization

```cpp
/// @brief Precomputed via-node candidates per region
class ViaNodeCache
{
public:
  /// @brief Precompute high-quality via-nodes for region
  void PrecomputeForRegion(NumMwmId mwmId, CCHTopology const & topology);

  /// @brief Get cached via-nodes near a corridor
  std::vector<uint32_t> GetCandidatesNear(
    Route const & route,
    double corridorWidth) const;

private:
  // Spatial index of via-node candidates
  std::map<NumMwmId, std::vector<uint32_t>> m_cache;
};
```

### 7.2 Parallel Query Optimization

```cpp
/// @brief Compute alternatives in parallel
std::vector<AlternativeRoute> ParallelFind(
    Route const & primaryRoute,
    AlternativeParams const & params,
    ICCHQueryEngine & cchEngine)
{
  // Split via-node candidates into batches
  auto const candidates = FindViaCandidates(primaryRoute, params);

  // Process batches in parallel
  std::vector<std::future<std::optional<AlternativeRoute>>> futures;

  for (size_t i = 0; i < candidates.size(); i += kBatchSize)
  {
    futures.push_back(std::async(std::launch::async, [&]() {
      return TryFindAlternative(candidates[i], primaryRoute, cchEngine);
    }));
  }

  // Collect results
  std::vector<AlternativeRoute> alternatives;
  for (auto & future : futures)
  {
    if (auto alt = future.get())
    {
      if (IsAcceptable(*alt, params, primaryRoute, alternatives))
        alternatives.push_back(std::move(*alt));
    }
  }

  return alternatives;
}
```

---

## 8. Acceptance Criteria Verification

| Criterion | Status | Verification |
|-----------|--------|--------------|
| Up to 3 routes (primary + 2 alt) | DESIGNED | AlternativeParams::k = 3 |
| Overlap < 60% | DESIGNED | CalcOverlap + threshold check |
| Length stretch < 130% | DESIGNED | IsAcceptable check |
| Min route length 50km | DESIGNED | minLengthMeters param |
| Computation < 100ms | DESIGNED | CCH-based queries |
| Decision point detection | DESIGNED | FindDecisionPoints |
| UI integration | DESIGNED | IAlternativeRouteListener |
| Unit tests | DESIGNED | Test cases above |

---

## 9. Dependencies

- **Phase 3**: CCH preprocessing (REQUIRED - alternatives need fast queries)
- **Phase 1**: TurnCostModel (for accurate duration estimates)
- **Existing**: Route, RoutingSession, IndexRouter

## 10. Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `routing/alternative_route.hpp` | CREATE | Data structures |
| `routing/alternative_finder.hpp` | CREATE | Interface |
| `routing/alternative_finder.cpp` | CREATE | k-SPwLO implementation |
| `routing/alternatives_ui_state.hpp` | CREATE | UI states and callbacks |
| `routing/routing_session.hpp` | MODIFY | Add alternatives storage |
| `routing/routing_session.cpp` | MODIFY | Decision point logic |
| `routing/index_router.hpp` | MODIFY | Add alternative computation |
| `routing/index_router.cpp` | MODIFY | Integration |
| `map/alternative_route_mark.hpp` | CREATE | Visualization |
| `map/alternative_route_mark.cpp` | CREATE | Rendering |
| `platform/settings.hpp` | MODIFY | Feature flag |
| `routing_tests/alternative_finder_tests.cpp` | CREATE | Unit tests |

---

## 11. References

- [k-Shortest Paths with Limited Overlap](https://link.springer.com/article/10.1007/s00778-020-00604-x) - VLDB Journal (2020)
- ADR-003: k-SPwLO for alternatives
- PRD Phase 4: Alternative Routes
- Phase 3: CCH Preprocessing (dependency)

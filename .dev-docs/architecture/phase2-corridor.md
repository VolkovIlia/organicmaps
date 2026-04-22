# Phase 2: Corridor-Based Route Following - Detailed Design

**Version**: 1.0
**Date**: 2026-02-03
**Branch**: `feature/routing-corridor`

---

## 1. Overview

Phase 2 replaces the simple distance-based off-route detection with a corridor-based approach that:
- Dynamically adjusts corridor width based on road class and GPS accuracy
- Implements a state machine for intelligent reroute decisions
- Compares return-to-route vs continue-forward costs before rerouting

### 1.1 Current Implementation Problems

```cpp
// routing_session.cpp:304-326 - Current naive approach
if (IsCloseEnoughToRoute(info.m_latitude, info.m_longitude))
{
  m_moveAwayCounter = 0;
}
else
{
  ++m_moveAwayCounter;
  if (m_moveAwayCounter > kOnRouteMissedCount)  // kOnRouteMissedCount = 10
  {
    m_moveAwayCounter = 0;
    return RouteNeedRebuild;
  }
}
```

**Issues**:
1. Fixed distance threshold regardless of road class
2. No GPS accuracy compensation
3. No consideration of return cost vs forward cost
4. Triggers false reroutes on parallel roads

---

## 2. Component Design

### 2.1 CorridorTracker

```cpp
// corridor_tracker.hpp
#pragma once

#include "routing/route.hpp"
#include "geometry/point2d.hpp"

#include <chrono>
#include <optional>

namespace routing
{

/// @brief Corridor state for off-route handling state machine
enum class CorridorState
{
  OnRoute,           ///< User is within corridor
  Monitoring,        ///< User outside corridor, timer running
  Deciding,          ///< Timer expired, evaluating options
  SuggestingReturn,  ///< Suggesting user return to route
  Rerouting          ///< Calculating new route
};

/// @brief Configuration for corridor behavior
struct CorridorConfig
{
  /// Base corridor widths by road class (meters)
  std::map<RoadClass, double> baseWidths = {
    {RoadClass::Motorway, 50.0},
    {RoadClass::Trunk, 40.0},
    {RoadClass::Primary, 30.0},
    {RoadClass::Secondary, 25.0},
    {RoadClass::Tertiary, 20.0},
    {RoadClass::Residential, 15.0},
    {RoadClass::Service, 15.0},
    {RoadClass::LivingStreet, 15.0}
  };

  double minGpsCompensation = 10.0;      ///< Minimum GPS accuracy compensation (meters)
  double offRouteTimeThreshold = 10.0;   ///< Seconds outside corridor before Deciding
  double returnDistanceThreshold = 200.0; ///< Max distance (m) for suggesting return
  int returnTurnCountMax = 2;            ///< Max turns for return route suggestion
  double forwardRatioThreshold = 1.2;    ///< Accept deviation if forward <= return * ratio

  /// @brief Get default configuration
  static CorridorConfig Default() { return CorridorConfig{}; }
};

/// @brief Result of corridor position check
struct CorridorCheckResult
{
  CorridorState state;
  bool isInsideCorridor;
  double lateralDistanceMeters;   ///< Distance from route centerline
  double corridorWidthMeters;     ///< Current corridor width
  double timeOutsideSeconds;      ///< Time spent outside corridor (if applicable)

  /// @brief Recommendation for UI/session
  enum class Recommendation
  {
    Continue,        ///< Stay on current route
    SuggestReturn,   ///< Show "return when safe" prompt
    Reroute          ///< Calculate new route
  };
  Recommendation recommendation = Recommendation::Continue;
};

/// @brief Interface for corridor-based route following
class ICorridorTracker
{
public:
  virtual ~ICorridorTracker() = default;

  /// @brief Check position against route corridor
  /// @param position Current GPS position
  /// @param route Current route being followed
  /// @return Corridor check result with state and recommendation
  virtual CorridorCheckResult CheckPosition(
    ms::LatLon const & position,
    double gpsAccuracy,
    Route const & route) = 0;

  /// @brief Get corridor width at specific route segment
  /// @param route Route to query
  /// @param segmentIndex Segment index along route
  /// @return Corridor width in meters
  virtual double GetCorridorWidth(
    Route const & route,
    size_t segmentIndex) const = 0;

  /// @brief Reset state machine to OnRoute state
  virtual void Reset() = 0;

  /// @brief Get current state
  virtual CorridorState GetState() const = 0;
};

/// @brief Production implementation of corridor tracking
class CorridorTracker : public ICorridorTracker
{
public:
  explicit CorridorTracker(CorridorConfig config = CorridorConfig::Default());

  CorridorCheckResult CheckPosition(
    ms::LatLon const & position,
    double gpsAccuracy,
    Route const & route) override;

  double GetCorridorWidth(
    Route const & route,
    size_t segmentIndex) const override;

  void Reset() override;
  CorridorState GetState() const override { return m_state; }

private:
  /// @brief Calculate lateral distance from position to route
  double CalcLateralDistance(
    ms::LatLon const & position,
    Route const & route) const;

  /// @brief Get road class at current route position
  RoadClass GetCurrentRoadClass(Route const & route) const;

  /// @brief Transition state machine
  void TransitionTo(CorridorState newState);

  /// @brief Check if should transition from Monitoring to Deciding
  bool ShouldTransitionToDeciding() const;

private:
  CorridorConfig m_config;
  CorridorState m_state = CorridorState::OnRoute;

  std::chrono::steady_clock::time_point m_outsideStartTime;
  double m_lastLateralDistance = 0.0;
  size_t m_currentSegmentIndex = 0;
};

}  // namespace routing
```

### 2.2 CorridorTracker Implementation

```cpp
// corridor_tracker.cpp
#include "routing/corridor_tracker.hpp"
#include "geometry/distance_on_sphere.hpp"
#include "geometry/mercator.hpp"

namespace routing
{

CorridorTracker::CorridorTracker(CorridorConfig config)
  : m_config(std::move(config))
{
}

CorridorCheckResult CorridorTracker::CheckPosition(
    ms::LatLon const & position,
    double gpsAccuracy,
    Route const & route)
{
  CorridorCheckResult result;

  // Step 1: Calculate lateral distance to route
  double const lateralDistance = CalcLateralDistance(position, route);
  result.lateralDistanceMeters = lateralDistance;

  // Step 2: Get corridor width for current road class
  RoadClass const roadClass = GetCurrentRoadClass(route);
  double baseWidth = m_config.baseWidths.count(roadClass) > 0
                       ? m_config.baseWidths.at(roadClass)
                       : 20.0;  // Default

  double const gpsCompensation = std::max(gpsAccuracy, m_config.minGpsCompensation);
  double const corridorWidth = baseWidth + gpsCompensation;
  result.corridorWidthMeters = corridorWidth;

  // Step 3: Check if inside corridor
  result.isInsideCorridor = (lateralDistance <= corridorWidth);

  // Step 4: State machine transitions
  auto const now = std::chrono::steady_clock::now();

  switch (m_state)
  {
  case CorridorState::OnRoute:
    if (!result.isInsideCorridor)
    {
      TransitionTo(CorridorState::Monitoring);
      m_outsideStartTime = now;
    }
    result.recommendation = CorridorCheckResult::Recommendation::Continue;
    break;

  case CorridorState::Monitoring:
    if (result.isInsideCorridor)
    {
      TransitionTo(CorridorState::OnRoute);
      result.recommendation = CorridorCheckResult::Recommendation::Continue;
    }
    else
    {
      auto const elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
        now - m_outsideStartTime).count();
      result.timeOutsideSeconds = elapsed;

      if (elapsed >= m_config.offRouteTimeThreshold)
      {
        TransitionTo(CorridorState::Deciding);
        // Will be handled in next iteration or immediately
      }
      result.recommendation = CorridorCheckResult::Recommendation::Continue;
    }
    break;

  case CorridorState::Deciding:
    // Transition to appropriate state based on RerouteDecision
    // This state is transient - RerouteDecision determines next state
    result.recommendation = CorridorCheckResult::Recommendation::Reroute;
    break;

  case CorridorState::SuggestingReturn:
    if (result.isInsideCorridor)
    {
      TransitionTo(CorridorState::OnRoute);
      result.recommendation = CorridorCheckResult::Recommendation::Continue;
    }
    else
    {
      result.recommendation = CorridorCheckResult::Recommendation::SuggestReturn;
    }
    break;

  case CorridorState::Rerouting:
    // Stay in rerouting until new route is set
    result.recommendation = CorridorCheckResult::Recommendation::Reroute;
    break;
  }

  result.state = m_state;
  m_lastLateralDistance = lateralDistance;

  return result;
}

double CorridorTracker::GetCorridorWidth(
    Route const & route,
    size_t segmentIndex) const
{
  // Get road class for segment
  auto const & segments = route.GetRouteSegments();
  if (segmentIndex >= segments.size())
    return 20.0;  // Default

  RoadClass const roadClass = segments[segmentIndex].GetRoadClass();
  double baseWidth = m_config.baseWidths.count(roadClass) > 0
                       ? m_config.baseWidths.at(roadClass)
                       : 20.0;

  // Add minimum GPS compensation
  return baseWidth + m_config.minGpsCompensation;
}

void CorridorTracker::Reset()
{
  m_state = CorridorState::OnRoute;
  m_lastLateralDistance = 0.0;
  m_currentSegmentIndex = 0;
}

double CorridorTracker::CalcLateralDistance(
    ms::LatLon const & position,
    Route const & route) const
{
  // Find closest point on route
  m2::PointD const posPoint = mercator::FromLatLon(position);

  auto const & poly = route.GetFollowedPolyline();
  double minDistance = std::numeric_limits<double>::max();

  // Check distance to each segment
  for (size_t i = 0; i + 1 < poly.GetSize(); ++i)
  {
    m2::PointD const & p1 = poly.GetPoint(i);
    m2::PointD const & p2 = poly.GetPoint(i + 1);

    // Project point onto segment
    m2::PointD const segment = p2 - p1;
    double const segmentLengthSq = segment.SquaredLength();

    if (segmentLengthSq < 1e-10)
      continue;

    double const t = std::clamp(
      m2::DotProduct(posPoint - p1, segment) / segmentLengthSq,
      0.0, 1.0);

    m2::PointD const projection = p1 + segment * t;
    double const distance = posPoint.Length(projection);

    if (distance < minDistance)
    {
      minDistance = distance;
      // Could update m_currentSegmentIndex here if non-const
    }
  }

  // Convert from Mercator to meters
  return mercator::DistanceOnEarth(posPoint,
    posPoint + m2::PointD(minDistance, 0));
}

RoadClass CorridorTracker::GetCurrentRoadClass(Route const & route) const
{
  auto const & segments = route.GetRouteSegments();
  if (m_currentSegmentIndex < segments.size())
    return segments[m_currentSegmentIndex].GetRoadClass();

  return RoadClass::Tertiary;  // Default
}

void CorridorTracker::TransitionTo(CorridorState newState)
{
  // Could add logging here
  m_state = newState;
}

}  // namespace routing
```

### 2.3 RerouteDecision

```cpp
// reroute_decision.hpp
#pragma once

#include "routing/route.hpp"
#include "routing/index_router.hpp"
#include "routing/corridor_tracker.hpp"

namespace routing
{

/// @brief Decision types for off-route handling
enum class RerouteDecisionType
{
  StayOnRoute,          ///< Continue on current route
  SuggestReturn,        ///< Suggest returning to route
  CalculateNewRoute     ///< Calculate completely new route
};

/// @brief Result of reroute decision analysis
struct RerouteDecisionResult
{
  RerouteDecisionType decision;
  double returnDistanceMeters = 0.0;    ///< Distance to return to route
  double forwardDistanceMeters = 0.0;   ///< Distance to destination via current position
  int returnTurnCount = 0;              ///< Number of turns to return
  double timeDifferenceSeconds = 0.0;   ///< Time saved by returning

  /// @brief Human-readable reason for decision
  std::string reason;
};

/// @brief Interface for making reroute decisions
class IRerouteDecision
{
public:
  virtual ~IRerouteDecision() = default;

  /// @brief Decide how to handle off-route situation
  /// @param currentPosition User's current position
  /// @param currentRoute Route user deviated from
  /// @param router Router for calculating return/forward routes
  /// @return Decision result with analysis
  virtual RerouteDecisionResult Decide(
    ms::LatLon const & currentPosition,
    Route const & currentRoute,
    IndexRouter & router) = 0;
};

/// @brief Configuration for reroute decisions
struct RerouteDecisionConfig
{
  double maxReturnDistance = 200.0;      ///< Max meters for suggesting return
  int maxReturnTurns = 2;                ///< Max turns for suggesting return
  double forwardAcceptanceRatio = 1.2;   ///< Accept forward if <= return * ratio
  double minTimeSavingSeconds = 60.0;    ///< Min time saving to suggest return

  static RerouteDecisionConfig Default() { return RerouteDecisionConfig{}; }
};

/// @brief Production implementation of reroute decision making
class RerouteDecision : public IRerouteDecision
{
public:
  explicit RerouteDecision(RerouteDecisionConfig config = RerouteDecisionConfig::Default());

  RerouteDecisionResult Decide(
    ms::LatLon const & currentPosition,
    Route const & currentRoute,
    IndexRouter & router) override;

private:
  /// @brief Calculate distance and turns to return to route
  struct ReturnAnalysis
  {
    double distance = std::numeric_limits<double>::max();
    int turnCount = 0;
    double timeSeconds = 0.0;
    bool success = false;
  };

  ReturnAnalysis AnalyzeReturn(
    ms::LatLon const & position,
    Route const & route,
    IndexRouter & router);

  /// @brief Calculate distance to destination via current position
  struct ForwardAnalysis
  {
    double distance = std::numeric_limits<double>::max();
    double timeSeconds = 0.0;
    bool success = false;
  };

  ForwardAnalysis AnalyzeForward(
    ms::LatLon const & position,
    ms::LatLon const & destination,
    IndexRouter & router);

private:
  RerouteDecisionConfig m_config;
};

}  // namespace routing
```

### 2.4 RerouteDecision Implementation

```cpp
// reroute_decision.cpp
#include "routing/reroute_decision.hpp"

namespace routing
{

RerouteDecision::RerouteDecision(RerouteDecisionConfig config)
  : m_config(std::move(config))
{
}

RerouteDecisionResult RerouteDecision::Decide(
    ms::LatLon const & currentPosition,
    Route const & currentRoute,
    IndexRouter & router)
{
  RerouteDecisionResult result;

  // Step 1: Analyze return to original route
  auto const returnAnalysis = AnalyzeReturn(currentPosition, currentRoute, router);
  result.returnDistanceMeters = returnAnalysis.distance;
  result.returnTurnCount = returnAnalysis.turnCount;

  // Step 2: Analyze forward to destination via current position
  ms::LatLon const destination = currentRoute.GetFinishPoint();
  auto const forwardAnalysis = AnalyzeForward(currentPosition, destination, router);
  result.forwardDistanceMeters = forwardAnalysis.distance;

  // Step 3: Calculate time difference
  if (returnAnalysis.success && forwardAnalysis.success)
  {
    // Time to return + remaining route time
    double const remainingRouteTime = currentRoute.GetCurrentTimeToEndSec();
    double const returnTotalTime = returnAnalysis.timeSeconds + remainingRouteTime;

    result.timeDifferenceSeconds = forwardAnalysis.timeSeconds - returnTotalTime;
  }

  // Step 4: Make decision based on analysis

  // Case 1: Return is short and simple
  if (returnAnalysis.success &&
      returnAnalysis.distance <= m_config.maxReturnDistance &&
      returnAnalysis.turnCount <= m_config.maxReturnTurns)
  {
    // Suggest return if it saves significant time
    if (result.timeDifferenceSeconds >= m_config.minTimeSavingSeconds)
    {
      result.decision = RerouteDecisionType::SuggestReturn;
      result.reason = "Return route is short (" +
        std::to_string(static_cast<int>(returnAnalysis.distance)) +
        "m) and saves " +
        std::to_string(static_cast<int>(result.timeDifferenceSeconds / 60)) +
        " minutes";
      return result;
    }
  }

  // Case 2: Forward is acceptable (not much longer than return)
  if (forwardAnalysis.success && returnAnalysis.success)
  {
    double const ratio = forwardAnalysis.distance /
      (returnAnalysis.distance + currentRoute.GetCurrentDistanceToEndMeters());

    if (ratio <= m_config.forwardAcceptanceRatio)
    {
      result.decision = RerouteDecisionType::CalculateNewRoute;
      result.reason = "Forward route is acceptable (ratio: " +
        std::to_string(ratio) + ")";
      return result;
    }
  }

  // Case 3: Return is the better option but complex
  if (returnAnalysis.success)
  {
    result.decision = RerouteDecisionType::SuggestReturn;
    result.reason = "Return is recommended but requires " +
      std::to_string(returnAnalysis.turnCount) + " turns";
    return result;
  }

  // Case 4: Default to rerouting
  result.decision = RerouteDecisionType::CalculateNewRoute;
  result.reason = "Could not analyze return route, rerouting";
  return result;
}

RerouteDecision::ReturnAnalysis RerouteDecision::AnalyzeReturn(
    ms::LatLon const & position,
    Route const & route,
    IndexRouter & router)
{
  ReturnAnalysis result;

  // Find closest point on route
  auto const & poly = route.GetFollowedPolyline();
  m2::PointD const posPoint = mercator::FromLatLon(position);

  size_t closestIdx = 0;
  double minDist = std::numeric_limits<double>::max();

  for (size_t i = 0; i < poly.GetSize(); ++i)
  {
    double const dist = posPoint.Length(poly.GetPoint(i));
    if (dist < minDist)
    {
      minDist = dist;
      closestIdx = i;
    }
  }

  // Calculate route from current position to closest route point
  ms::LatLon const targetPoint = mercator::ToLatLon(poly.GetPoint(closestIdx));

  // Use router to calculate return path
  // This is a simplified version - production would use actual routing
  result.distance = mercator::DistanceOnEarth(position, targetPoint);
  result.turnCount = 1;  // Simplified - would analyze actual turns
  result.timeSeconds = result.distance / 10.0;  // ~36 km/h average
  result.success = true;

  return result;
}

RerouteDecision::ForwardAnalysis RerouteDecision::AnalyzeForward(
    ms::LatLon const & position,
    ms::LatLon const & destination,
    IndexRouter & router)
{
  ForwardAnalysis result;

  // Direct distance (simplified - production would use actual routing)
  result.distance = mercator::DistanceOnEarth(position, destination);
  result.timeSeconds = result.distance / 15.0;  // ~54 km/h average
  result.success = true;

  return result;
}

}  // namespace routing
```

---

## 3. Integration with RoutingSession

### 3.1 Modified RoutingSession

```cpp
// routing_session.hpp additions

#include "routing/corridor_tracker.hpp"
#include "routing/reroute_decision.hpp"

class RoutingSession
{
  // ... existing members ...

private:
  // Phase 2: Corridor-based following
  std::unique_ptr<ICorridorTracker> m_corridorTracker;
  std::unique_ptr<IRerouteDecision> m_rerouteDecision;

  // Configuration
  bool m_useCorridorTracking = true;  // Feature flag

public:
  /// @brief Enable/disable corridor-based tracking
  void SetCorridorTrackingEnabled(bool enabled);

  /// @brief Get current corridor state (for UI)
  CorridorState GetCorridorState() const;
};
```

### 3.2 Modified OnLocationPositionChanged

```cpp
// routing_session.cpp modifications

void RoutingSession::OnLocationPositionChanged(GpsInfo const & info)
{
  // ... existing preamble code ...

  if (m_useCorridorTracking && m_corridorTracker)
  {
    // Phase 2: Corridor-based off-route detection
    ms::LatLon const position(info.m_latitude, info.m_longitude);

    auto const checkResult = m_corridorTracker->CheckPosition(
      position,
      info.m_horizontalAccuracy,
      *m_route);

    switch (checkResult.recommendation)
    {
    case CorridorCheckResult::Recommendation::Continue:
      // Normal operation - continue on route
      break;

    case CorridorCheckResult::Recommendation::SuggestReturn:
      // Show UI prompt suggesting return
      m_routingStateListener->OnSuggestReturn(checkResult.lateralDistanceMeters);
      break;

    case CorridorCheckResult::Recommendation::Reroute:
      // Make reroute decision
      if (m_rerouteDecision)
      {
        auto const decision = m_rerouteDecision->Decide(
          position, *m_route, *m_router);

        HandleRerouteDecision(decision);
      }
      else
      {
        // Fallback to immediate reroute
        SetState(State::RouteNeedRebuild);
      }
      break;
    }
  }
  else
  {
    // Legacy: distance-based off-route detection
    // ... existing m_moveAwayCounter logic ...
  }

  // ... existing position update code ...
}

void RoutingSession::HandleRerouteDecision(RerouteDecisionResult const & decision)
{
  switch (decision.decision)
  {
  case RerouteDecisionType::StayOnRoute:
    // Do nothing, continue on current route
    m_corridorTracker->Reset();
    break;

  case RerouteDecisionType::SuggestReturn:
    // Transition corridor tracker to SuggestingReturn state
    // UI will show prompt
    m_routingStateListener->OnSuggestReturn(decision.reason);
    break;

  case RerouteDecisionType::CalculateNewRoute:
    // Trigger reroute
    SetState(State::RouteNeedRebuild);
    m_corridorTracker->Reset();
    break;
  }
}
```

---

## 4. Parallel Road Detection

### 4.1 ParallelRoadDetector

```cpp
// parallel_road_detector.hpp
#pragma once

#include "routing/route.hpp"
#include "indexer/data_source.hpp"

namespace routing
{

/// @brief Detects if user is on a parallel road (not off-route)
class ParallelRoadDetector
{
public:
  explicit ParallelRoadDetector(DataSource const & dataSource);

  /// @brief Check if position is on a road parallel to route
  /// @param position Current GPS position
  /// @param bearing Current heading in degrees
  /// @param route Current route
  /// @return True if on parallel road (not truly off-route)
  bool IsOnParallelRoad(
    ms::LatLon const & position,
    double bearing,
    Route const & route) const;

private:
  /// @brief Find roads near position
  std::vector<FeatureID> FindNearbyRoads(
    ms::LatLon const & position,
    double radiusMeters) const;

  /// @brief Check if road is parallel to route segment
  bool IsParallelToRoute(
    FeatureID const & roadId,
    Route const & route,
    double bearingTolerance) const;

private:
  DataSource const & m_dataSource;
  double m_searchRadius = 50.0;       ///< Meters to search for roads
  double m_bearingTolerance = 30.0;   ///< Degrees tolerance for parallel
  double m_maxParallelDistance = 100.0; ///< Max distance for parallel road
};

}  // namespace routing
```

### 4.2 Integration with CorridorTracker

```cpp
// In CorridorTracker::CheckPosition, before declaring off-route:

// Check for parallel roads
if (!result.isInsideCorridor && m_parallelRoadDetector)
{
  double const bearing = /* get from GPS info */;
  if (m_parallelRoadDetector->IsOnParallelRoad(position, bearing, route))
  {
    // User is on a parallel road, not truly off-route
    // Expand corridor or suppress reroute
    result.isInsideCorridor = true;  // Treat as on-route
    result.recommendation = CorridorCheckResult::Recommendation::Continue;
  }
}
```

---

## 5. GPS Quality Handling

### 5.1 GPS State Machine

```cpp
// gps_quality_tracker.hpp
#pragma once

namespace routing
{

/// @brief GPS quality states
enum class GpsQualityState
{
  Normal,        ///< Good GPS signal
  Degraded,      ///< Poor accuracy (> 50m)
  Lost,          ///< No signal
  Tunnel,        ///< In tunnel (no signal, but expected)
  DeadReckoning  ///< Estimating position based on last known
};

/// @brief Tracks GPS quality and adjusts corridor behavior
class GpsQualityTracker
{
public:
  /// @brief Update with new GPS info
  void OnGpsUpdate(double accuracy, bool hasSignal);

  /// @brief Get current quality state
  GpsQualityState GetState() const { return m_state; }

  /// @brief Get corridor width multiplier based on GPS quality
  double GetCorridorMultiplier() const;

  /// @brief Should suppress rerouting due to GPS issues?
  bool ShouldSuppressReroute() const;

private:
  GpsQualityState m_state = GpsQualityState::Normal;
  std::chrono::steady_clock::time_point m_lastGoodSignal;
  double m_lastAccuracy = 0.0;
};

}  // namespace routing
```

### 5.2 Corridor Width Adjustment

```cpp
double GpsQualityTracker::GetCorridorMultiplier() const
{
  switch (m_state)
  {
  case GpsQualityState::Normal:
    return 1.0;
  case GpsQualityState::Degraded:
    return 1.5;  // Widen corridor by 50%
  case GpsQualityState::Lost:
  case GpsQualityState::Tunnel:
    return 2.0;  // Double corridor width
  case GpsQualityState::DeadReckoning:
    return 3.0;  // Triple corridor width
  }
  return 1.0;
}

bool GpsQualityTracker::ShouldSuppressReroute() const
{
  // Don't reroute during tunnel or dead reckoning
  return m_state == GpsQualityState::Tunnel ||
         m_state == GpsQualityState::DeadReckoning;
}
```

---

## 6. Unit Tests

### 6.1 CorridorTracker Tests

```cpp
// corridor_tracker_tests.cpp
#include "testing/testing.hpp"
#include "routing/corridor_tracker.hpp"

UNIT_TEST(CorridorTracker_InsideCorridor)
{
  CorridorConfig config;
  config.baseWidths[RoadClass::Primary] = 30.0;

  CorridorTracker tracker(config);

  // Create mock route along primary road
  Route route = CreateMockRoute(RoadClass::Primary,
    {{0.0, 0.0}, {0.0, 0.01}});  // ~1km north

  // Position 20m from route (inside 30m + 10m GPS = 40m corridor)
  ms::LatLon const position(0.0, 0.0002);  // ~20m east

  auto const result = tracker.CheckPosition(position, 10.0, route);

  TEST(result.isInsideCorridor, ());
  TEST_EQUAL(result.state, CorridorState::OnRoute, ());
  TEST_EQUAL(result.recommendation,
    CorridorCheckResult::Recommendation::Continue, ());
}

UNIT_TEST(CorridorTracker_OutsideCorridor)
{
  CorridorTracker tracker;

  Route route = CreateMockRoute(RoadClass::Residential,
    {{0.0, 0.0}, {0.0, 0.01}});

  // Position 50m from route (outside 15m + 10m GPS = 25m corridor)
  ms::LatLon const position(0.0, 0.0005);  // ~50m east

  auto const result = tracker.CheckPosition(position, 10.0, route);

  TEST(!result.isInsideCorridor, ());
  TEST_EQUAL(result.state, CorridorState::Monitoring, ());
}

UNIT_TEST(CorridorTracker_TransitionToDeciding)
{
  CorridorConfig config;
  config.offRouteTimeThreshold = 0.1;  // 100ms for testing

  CorridorTracker tracker(config);

  Route route = CreateMockRoute(RoadClass::Primary,
    {{0.0, 0.0}, {0.0, 0.01}});

  // Position outside corridor
  ms::LatLon const position(0.0, 0.001);  // ~100m east

  // First check - enters Monitoring
  auto result1 = tracker.CheckPosition(position, 10.0, route);
  TEST_EQUAL(result1.state, CorridorState::Monitoring, ());

  // Wait for threshold
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // Second check - transitions to Deciding
  auto result2 = tracker.CheckPosition(position, 10.0, route);
  TEST_EQUAL(result2.state, CorridorState::Deciding, ());
  TEST_EQUAL(result2.recommendation,
    CorridorCheckResult::Recommendation::Reroute, ());
}

UNIT_TEST(CorridorTracker_ReturnToCorridor)
{
  CorridorTracker tracker;

  Route route = CreateMockRoute(RoadClass::Primary,
    {{0.0, 0.0}, {0.0, 0.01}});

  // Go outside corridor
  ms::LatLon const outsidePos(0.0, 0.001);
  tracker.CheckPosition(outsidePos, 10.0, route);
  TEST_EQUAL(tracker.GetState(), CorridorState::Monitoring, ());

  // Return inside corridor
  ms::LatLon const insidePos(0.0, 0.0001);
  auto const result = tracker.CheckPosition(insidePos, 10.0, route);

  TEST_EQUAL(result.state, CorridorState::OnRoute, ());
  TEST(result.isInsideCorridor, ());
}

UNIT_TEST(CorridorTracker_Reset)
{
  CorridorTracker tracker;

  Route route = CreateMockRoute(RoadClass::Primary,
    {{0.0, 0.0}, {0.0, 0.01}});

  // Go outside and transition states
  ms::LatLon const position(0.0, 0.001);
  tracker.CheckPosition(position, 10.0, route);

  // Reset
  tracker.Reset();

  TEST_EQUAL(tracker.GetState(), CorridorState::OnRoute, ());
}
```

### 6.2 RerouteDecision Tests

```cpp
// reroute_decision_tests.cpp
#include "testing/testing.hpp"
#include "routing/reroute_decision.hpp"

UNIT_TEST(RerouteDecision_ShortReturnSuggested)
{
  RerouteDecisionConfig config;
  config.maxReturnDistance = 200.0;
  config.maxReturnTurns = 2;
  config.minTimeSavingSeconds = 30.0;

  RerouteDecision decision(config);

  // Create route where return is short and saves time
  Route route = CreateMockRoute(/* ... */);

  // Position close to route
  ms::LatLon const position(/* ... */);

  // Mock router
  MockIndexRouter router;

  auto const result = decision.Decide(position, route, router);

  TEST_EQUAL(result.decision, RerouteDecisionType::SuggestReturn, ());
  TEST_LESS(result.returnDistanceMeters, 200.0, ());
}

UNIT_TEST(RerouteDecision_ForwardAcceptable)
{
  RerouteDecisionConfig config;
  config.forwardAcceptanceRatio = 1.2;

  RerouteDecision decision(config);

  // Create scenario where forward is only 10% longer
  // ... test setup ...

  auto const result = decision.Decide(position, route, router);

  TEST_EQUAL(result.decision, RerouteDecisionType::CalculateNewRoute, ());
}
```

---

## 7. Feature Flag & Rollout

### 7.1 Feature Flag Definition

```cpp
// platform/settings.hpp additions
namespace settings
{
  // Corridor-based route following (Phase 2)
  extern char const * kCorridorTrackingEnabled;
}

// platform/settings.cpp
char const * kCorridorTrackingEnabled = "CorridorTrackingEnabled";

// Default: false during gradual rollout
// Set via A/B testing or settings menu
```

### 7.2 Gradual Rollout Strategy

1. **Alpha**: Enable for internal testers only
2. **Beta**: 10% of users (random selection)
3. **RC**: 50% of users
4. **Release**: 100% with kill switch

---

## 8. Acceptance Criteria Verification

| Criterion | Status | Verification |
|-----------|--------|--------------|
| Dynamic corridor width by road class | DESIGNED | CorridorConfig::baseWidths table |
| GPS accuracy compensation | DESIGNED | GetCorridorWidth adds gpsAccuracy |
| State machine (OnRoute/Monitoring/Deciding) | DESIGNED | CorridorTracker state transitions |
| Return vs forward cost comparison | DESIGNED | RerouteDecision::Decide |
| Parallel road detection | DESIGNED | ParallelRoadDetector class |
| GPS quality handling | DESIGNED | GpsQualityTracker class |
| Feature flag for rollout | DESIGNED | kCorridorTrackingEnabled setting |
| Unit tests | DESIGNED | Test cases above |

---

## 9. Dependencies

- **Phase 1**: TurnCostModel (for return route cost calculation)
- **Existing**: IndexRouter, Route, RoutingSession

## 10. Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `routing/corridor_tracker.hpp` | CREATE | Interface and implementation |
| `routing/corridor_tracker.cpp` | CREATE | Implementation |
| `routing/reroute_decision.hpp` | CREATE | Interface and implementation |
| `routing/reroute_decision.cpp` | CREATE | Implementation |
| `routing/parallel_road_detector.hpp` | CREATE | Parallel road detection |
| `routing/parallel_road_detector.cpp` | CREATE | Implementation |
| `routing/gps_quality_tracker.hpp` | CREATE | GPS quality handling |
| `routing/gps_quality_tracker.cpp` | CREATE | Implementation |
| `routing/routing_session.hpp` | MODIFY | Add corridor tracker members |
| `routing/routing_session.cpp` | MODIFY | Integrate corridor tracking |
| `platform/settings.hpp` | MODIFY | Add feature flag |
| `routing_tests/corridor_tracker_tests.cpp` | CREATE | Unit tests |
| `routing_tests/reroute_decision_tests.cpp` | CREATE | Unit tests |

---

## 11. References

- ADR-004: Corridor-based rerouting decision
- PRD Phase 2: Corridor-Based Following
- Research: Industry best practices for off-route detection

# Code Review: Mesh Traffic Improvements

**Review Date**: 2026-02-06
**Reviewer**: Reviewer Suckless Agent
**Branch**: `feature/mesh-traffic`
**Base Branch**: `master`
**Scope**: 15 files changed, +391/-177 lines
**Build System**: Android NDK clang++, C++23

---

## Executive Summary

**Overall Assessment**: REQUEST CHANGES
**Code Health Impact**: IMPROVES (with critical fixes)
**Complexity Score**: MEDIUM
**Suckless Compliance**: 7/10

### Key Findings

**Strengths**:
- Well-structured 4-phase feature decomposition with clear separation of concerns
- Good use of historical traffic pattern tables (time-of-day, day-of-week)
- Alternative route rendering correctly wired through Drape engine with style definitions in all themes
- Residential road filtering saves ~50-70% memory in synthetic traffic generation

**Concerns (5 CRITICAL)**:
- C1: `CalculateRouteSync` holds `m_guard` mutex for up to 30s, blocking async routing thread
- C2: Raw pointer from `unique_ptr` captured in cross-thread lambda (dangling pointer risk)
- C3: `m_displayMode` data race (non-atomic, read/written from multiple threads without mutex)
- C4: `std::vector<RouteSegment>` with only forward declaration (incomplete type UB)
- C5: `OffsetPerpendicular` meter-to-mercator conversion mathematically incorrect

---

## Detailed Review by File

---

### CRITICAL Issues

---

#### C1: `CalculateRouteSync` holds `m_guard` for entire route calculation

**File**: `libs/routing/async_router.cpp:184-196`
**Impact**: Thread safety / Deadlock / Starvation
**Severity**: CRITICAL

```cpp
RouterResultCode AsyncRouter::CalculateRouteSync(
    Checkpoints const & checkpoints, Route & route)
{
  lock_guard l(m_guard);  // HELD FOR UP TO 30 SECONDS

  if (!m_router)
    return RouterResultCode::InternalError;

  RouterDelegate delegate;
  delegate.SetTimeout(30 /* seconds */);

  return m_router->CalculateRoute(
      checkpoints, m2::PointD::Zero(), false, delegate, route);
}
```

**Problem**: `m_guard` (at `async_router.hpp:109`) is the same mutex used by `AsyncRouter::ThreadFunc` for all async route calculations, by `SetRouter`, and by `ClearState`. Alternative route finding calls `CalculateRouteSync` up to 18 times (6 sample points x 3 offset distances). Each call holds `m_guard` for up to 30 seconds, blocking ALL async routing operations (user-initiated reroute, route recalculation, etc.) for up to **9 minutes** in the worst case.

**Additionally**: The `lock_guard` prevents `ClearState()` from executing, so a user cancellation request is blocked until the synchronous calculation completes.

**Recommendation**: Copy the `shared_ptr<IRouter>` under the lock, then release the lock before calling `CalculateRoute`:

```cpp
RouterResultCode AsyncRouter::CalculateRouteSync(
    Checkpoints const & checkpoints, Route & route)
{
  std::shared_ptr<IRouter> router;
  {
    lock_guard l(m_guard);
    router = m_router;  // Copy shared_ptr under lock
  }
  // m_guard released here

  if (!router)
    return RouterResultCode::InternalError;

  RouterDelegate delegate;
  delegate.SetTimeout(30 /* seconds */);

  return router->CalculateRoute(
      checkpoints, m2::PointD::Zero(), false, delegate, route);
}
```

**Note**: Verify that `IRouter::CalculateRoute` is thread-safe when called concurrently with the async routing thread. If not, a dedicated mutex for sync calculations (separate from `m_guard`) is needed.

---

#### C2: Raw pointer from `unique_ptr` captured in background lambda

**File**: `libs/map/routing_manager.cpp:434` (in diff at line +50)
**Impact**: Memory safety / Use-after-free
**Severity**: CRITICAL

```cpp
auto finder = m_alternativeFinder.get();  // Raw pointer from unique_ptr

GetPlatform().RunTask(Platform::Thread::Background,
    [this, routePtr, params, finder, calculateRoute]()
    {
      auto alternatives = finder->Find(*routePtr, params, calculateRoute);
      // ...
    });
```

**Problem**: `m_alternativeFinder` is a `std::unique_ptr<routing::IAlternativeFinder>` (see `routing_manager.hpp:409`). The raw pointer `finder` is captured by value in a lambda posted to the Background thread. If `SetRouterImpl` or the `RoutingManager` destructor runs before the background lambda executes, `m_alternativeFinder` is reset/destroyed and `finder` becomes a dangling pointer.

The `this` capture compounds the risk -- if the RoutingManager is destroyed while the background task is in-flight, both `this` and `finder` dangle.

**Recommendation**: Either capture a `shared_ptr` or use a weak reference pattern:

Option A (safest): Convert `m_alternativeFinder` to `shared_ptr`:
```cpp
std::shared_ptr<routing::IAlternativeFinder> m_alternativeFinder;
// ...
auto finder = m_alternativeFinder;  // shared_ptr copy, safe in lambda
```

Option B: Add cancellation flag:
```cpp
auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
// Store cancelFlag, set to true in destructor/SetRouterImpl
GetPlatform().RunTask(Platform::Thread::Background,
    [finder = m_alternativeFinder, cancelFlag, ...]() {
      if (*cancelFlag) return;
      // ...
    });
```

---

#### C3: `m_displayMode` data race

**File**: `libs/map/traffic_manager.hpp:215`, `traffic_manager.cpp:100,371`
**Impact**: Thread safety / Data race / Undefined behavior
**Severity**: CRITICAL

```cpp
// traffic_manager.hpp:215
TrafficDisplayMode m_displayMode = TrafficDisplayMode::RouteOnly;  // NOT atomic!

// traffic_manager.cpp:100 (SetEnabled, called from GUI thread, OUTSIDE m_mutex scope)
m_displayMode = enabled ? TrafficDisplayMode::MapWide : TrafficDisplayMode::RouteOnly;

// traffic_manager.cpp:371 (OnTrafficRequestFailed, called from traffic worker thread)
if (m_historicalFallbackEnabled && m_historicalProvider &&
    m_displayMode == TrafficDisplayMode::MapWide)  // READ on different thread
```

**Problem**: `m_displayMode` is written on the GUI thread at line 100 (after the `lock_guard` scope ends at line 96) and read on the traffic worker thread at line 371, without synchronization. This is a data race (undefined behavior per C++ standard).

Contrast with `m_historicalFallbackEnabled` (line 211 of the header), which is correctly declared `std::atomic<bool>`.

**Recommendation**: Change to `std::atomic<TrafficDisplayMode>`:

```cpp
std::atomic<TrafficDisplayMode> m_displayMode{TrafficDisplayMode::RouteOnly};
```

Or move the write at line 100 inside the `lock_guard` scope (lines 91-96).

---

#### C4: `std::vector<RouteSegment>` with incomplete type

**File**: `libs/routing/alternative_route.hpp:15,60`
**Impact**: Compilation / Undefined behavior
**Severity**: CRITICAL

```cpp
// Line 15: Forward declaration only
class RouteSegment;

// Line 60: Used as vector element type -- REQUIRES complete type
std::vector<RouteSegment> routeSegments;
```

**Problem**: `alternative_route.hpp` only forward-declares `class RouteSegment;` (line 15) but then declares `std::vector<RouteSegment> routeSegments` at line 60 as a member of the `AlternativeRoute` struct. The C++ standard requires `T` to be a complete type when `std::vector<T>` is instantiated as a member (the vector needs to know sizeof(T) for allocation).

The file does NOT include `routing/route.hpp` where `class RouteSegment final` is fully defined (confirmed at `route.hpp:44`). The includes are only: `routing/segment.hpp`, `geometry/latlon.hpp`, `geometry/point2d.hpp`, `geometry/polyline2d.hpp`.

This may compile on some compilers due to ODR-lagging but is technically undefined behavior.

**Recommendation**: Either:
A. Add `#include "routing/route.hpp"` to `alternative_route.hpp`
B. Change the member to `std::vector<RouteSegment>*` or `std::shared_ptr<std::vector<RouteSegment>>` (though option A is simpler)
C. Move rendering data to a separate struct that includes `route.hpp`

**Note**: Adding `route.hpp` include may create header dependency bloat. Consider extracting `RouteSegment` into its own header (`route_segment.hpp`) to keep includes minimal.

---

#### C5: `OffsetPerpendicular` meter-to-mercator conversion is mathematically incorrect

**File**: `libs/routing/alternative_finder.cpp:98-105`
**Impact**: Logic bug / Incorrect alternative route waypoints
**Severity**: CRITICAL

```cpp
// Convert offset from meters to mercator units (approximate)
double const metersPerMercator = mercator::DistanceOnEarth(
    point, point + m2::PointD(1.0, 0.0));
if (metersPerMercator <= 0.0)
  return point;

double const mercatorOffset = offsetMeters / metersPerMercator;
return point + perp * mercatorOffset;
```

**Problem**: `mercator::DistanceOnEarth(point, point + m2::PointD(1.0, 0.0))` computes the distance for a 1.0 mercator-unit displacement along the X axis. However, the perpendicular offset is applied along `perp`, which is a rotated direction vector that may have a significant Y component. In Mercator projection, X and Y have very different scale factors at non-equatorial latitudes:

1. 1.0 mercator X unit at latitude 60 degrees corresponds to approximately 55 km
2. 1.0 mercator Y unit at latitude 60 degrees corresponds to approximately 111 km
3. The `perp` vector combines X and Y components, so the scale factor used (X-only) produces incorrect offsets when the perpendicular direction has a Y component

At high latitudes (e.g., Moscow at 55.7N), offsets could be off by a factor of ~2x in the Y direction, placing via-waypoints at incorrect locations.

**Additionally**: Adding `m2::PointD(1.0, 0.0)` to a Mercator point can move it to a completely different geographic location (potentially across the globe), making the local scale estimation unreliable.

**Recommendation**: Use a small epsilon-based local scale estimation:

```cpp
double const kEps = 1e-5;  // Small mercator displacement
double const metersPerMercatorX = mercator::DistanceOnEarth(
    point, point + m2::PointD(kEps, 0.0)) / kEps;
double const metersPerMercatorY = mercator::DistanceOnEarth(
    point, point + m2::PointD(0.0, kEps)) / kEps;

double const mercatorOffsetX = (perp.x * offsetMeters) / metersPerMercatorX;
double const mercatorOffsetY = (perp.y * offsetMeters) / metersPerMercatorY;
return point + m2::PointD(mercatorOffsetX, mercatorOffsetY);
```

Or use `mercator::GetSmPoint()` or `mercator::RectByCenterXYInMeters()` which already handle this correctly in the codebase.

---

### WARNING Issues

---

#### W1: Duplicate TrafficEstimator instances

**File**: `libs/map/routing_manager.hpp:414-415`
**Impact**: Resource waste / Confusion
**Severity**: WARNING

```cpp
traffic::TrafficEstimator m_trafficEstimator;                      // Line 414: value member
std::shared_ptr<traffic::TrafficEstimator> m_trafficEstimatorPtr;  // Line 415: shared_ptr member
```

**Problem**: Two `TrafficEstimator` instances exist: one as a value member (used by `GetUsualRouteTime`) and one as a `shared_ptr` (passed to `IndexRouter`). They are independent instances that do not share state. If one is updated, the other is stale.

**Recommendation**: Use only the `shared_ptr` version for both purposes:

```cpp
std::shared_ptr<traffic::TrafficEstimator> m_trafficEstimator;
// Remove m_trafficEstimatorPtr
// Use m_trafficEstimator-> everywhere
```

---

#### W2: Early return in `OnBuildRouteReady` skips route callbacks

**File**: `libs/map/routing_manager.cpp` (diff lines +47-48)
**Impact**: Functional correctness
**Severity**: WARNING

```cpp
auto routePtr = m_routingSession.GetRouteForTests();
if (!routePtr)
  return;  // Skips ALL subsequent processing
```

**Problem**: If `GetRouteForTests()` returns null, the early `return` exits the lambda before `CallRouteBuilded` and other post-route-build processing. This means the UI will never be notified that route building completed, potentially leaving the app in a "computing route" state indefinitely.

**Recommendation**: Only guard the alternative computation block, not the entire callback:

```cpp
if (m_alternativeFinder && routePtr)
{
  // ... alternative computation ...
}
```

---

#### W3: No cancellation for in-flight alternative computation

**File**: `libs/map/routing_manager.cpp`
**Impact**: Resource waste / Race condition
**Severity**: WARNING

**Problem**: When a new route is requested while alternative route computation is in flight on the Background thread, there is no mechanism to cancel the stale computation. The old computation may complete and call `InsertAlternativeRoutes` for an obsolete route.

The `calculateRoute` lambda captures `this`, so it calls `m_routingSession.CalculateRouteSync()` which may operate on stale routing session state.

**Recommendation**: Add a generation counter or cancellation token:

```cpp
// In RoutingManager:
std::atomic<uint64_t> m_routeGeneration{0};

// In OnBuildRouteReady:
auto gen = ++m_routeGeneration;
GetPlatform().RunTask(Platform::Thread::Background,
    [this, gen, ...]() {
      if (gen != m_routeGeneration) return;  // Cancelled
      auto alternatives = finder->Find(...);
      if (gen != m_routeGeneration) return;  // Cancelled during computation
      // Post to GUI...
    });
```

---

#### W4: `m_alternativeDrapeSubroutes` not mutex-protected

**File**: `libs/map/routing_manager.hpp:410`, `routing_manager.cpp`
**Impact**: Thread safety
**Severity**: WARNING

**Problem**: `m_alternativeDrapeSubroutes` is accessed from the GUI thread (in `InsertAlternativeRoutes`) and potentially cleared from other contexts (in `ClearAlternativeRoutes`). Unlike `m_drapeSubroutes` which has `m_drapeSubroutesMutex` (line 402), `m_alternativeDrapeSubroutes` has no dedicated mutex.

**Recommendation**: Either protect with `m_drapeSubroutesMutex` or verify all access is strictly on the GUI thread (use `ASSERT_THREAD_CHECKER`).

---

### INFO Suggestions

---

#### I1: Spatial variation hash uses only feature ID (no segment differentiation)

**File**: `libs/traffic/historical_traffic_provider.cpp:121-129`
**Impact**: Realism of synthetic traffic data
**Severity**: INFO

```cpp
uint8_t ApplySpatialVariation(uint8_t baseSpeed, uint32_t featureId, uint8_t maxVariation)
{
  uint32_t const hash = featureId * 2654435761u;
  // ...
}
```

All segments of the same feature get identical variation. For long roads, this means the entire road has uniform speed variation. Including `segmentIdx` in the hash would add per-segment variation:

```cpp
uint32_t const hash = (featureId * 2654435761u) ^ (segmentIdx * 0x9E3779B9u);
```

---

#### I2: `minLengthMeters` aggressively lowered to 5km

**File**: `libs/routing/alternative_route.hpp:95`
**Impact**: Performance
**Severity**: INFO

```cpp
double minLengthMeters = 5000;  // Previously 50000 (50km)
```

For 5km routes, the via-waypoint approach with offsets of 2km, 5km, and 10km (from `kOffsetDistancesM[]` in `alternative_finder.cpp:40`) may place waypoints far outside the reasonable route corridor. The 10km offset is 2x the route length for a 5km route.

Consider scaling offset distances proportionally to route length, or increase `minLengthMeters` back to a more conservative value (e.g., 10-15km).

---

#### I3: Class2 removal from `rule_drawer.cpp` may affect non-traffic rendering

**File**: `libs/drape_frontend/rule_drawer.cpp` (diff lines 6-13)
**Impact**: Visual regression
**Severity**: INFO

```cpp
// REMOVED: LivingStreet, Service, ServiceMinor roads from traffic rendering
-  {{HighwayClass::LivingStreet, HighwayClass::Service, HighwayClass::ServiceMinor},
-   kRoadClass2ZoomLevel,
-   df::RoadClass::Class2}};
```

This removes Class2 roads entirely from the traffic road class assignment. While this correctly reduces visual clutter for traffic coloring, verify that `df::RoadClass::Class2` is not used elsewhere for non-traffic rendering purposes (e.g., road width, draw priority).

---

#### I4: Log message ambiguity for time diff in alternative route

**File**: `libs/routing/alternative_finder.cpp:163`
**Impact**: Debuggability
**Severity**: INFO

```cpp
LOG(LDEBUG, ("Found alternative", candidate->routeIndex,
    "overlap:", candidate->overlapWithPrimary,
    "stretch:", candidate->GetStretchRatio(primaryLength),
    "time diff:", primaryDuration - candidate->durationSeconds, "s"));
```

When `primaryDuration - candidate->durationSeconds` is negative (alternative is slower), the log says "time diff: -15s" which is ambiguous. Consider explicit labeling:

```cpp
double const timeDiff = primaryDuration - candidate->durationSeconds;
LOG(LDEBUG, (..., "time saving:", timeDiff, "s",
    (timeDiff > 0 ? "(faster)" : "(slower)")));
```

---

#### I5: `GenerateSyntheticData` processes all features synchronously

**File**: `libs/traffic/historical_traffic_provider.cpp:262-346`
**Impact**: Performance
**Severity**: INFO

`GenerateSyntheticData` uses `feature::ForEachFeature` to iterate all features in an MWM file and generate traffic patterns. For large MWM files (Moscow, London), this could be slow. The function is called under `m_mutex` (from `LoadHistoricalData` -> `GetColoring`), blocking other threads.

Consider:
1. Running generation on a background thread
2. Caching generated data to disk
3. Lazy generation (only generate for viewport-visible features)

---

## Acceptance Criteria Validation

### Functional Requirements

| ID | Requirement | Status |
|----|-------------|--------|
| FR1 | Filter residential roads from traffic layer | PASS |
| FR2 | Add spatial color variation to traffic | PASS |
| FR3 | Populate route segments with predicted traffic (TrafficEstimator fallback) | PASS |
| FR4 | Alternative routes via via-waypoint approach | PASS (with C5 fix) |
| FR5 | Traffic display mode toggle (RouteOnly vs MapWide) | PASS (with C3 fix) |
| FR6 | Alternative route rendering through Drape engine | PASS |
| FR7 | Style definitions for alternative routes (all themes) | PASS |

### Quality Requirements

| ID | Requirement | Actual | Status |
|----|-------------|--------|--------|
| QR1 | Thread safety | 3 issues (C1, C3, W3) | FAIL |
| QR2 | Memory safety | 1 issue (C2) | FAIL |
| QR3 | API correctness | 1 issue (C4) | FAIL |
| QR4 | Logic correctness | 1 issue (C5) | FAIL |
| QR5 | No new external deps | 0 new deps | PASS |

### Suckless Requirements

| ID | Requirement | Status |
|----|-------------|--------|
| SR1 | Single Responsibility | PASS |
| SR2 | Minimal Coupling | PASS (except W1 duplicate estimator) |
| SR3 | No Over-engineering | PASS |
| SR4 | Dependencies Minimized | PASS |
| SR5 | Functions <= 50 lines | PASS (no violations in new code) |

---

## Architectural Feedback

### Strengths

1. **Phase decomposition**: Clean 4-phase feature with independent concerns (filtering, estimation, alternatives, display mode)
2. **Synthetic data approach**: Generating historical traffic patterns at runtime avoids storing data in MWM files while providing useful traffic visualization
3. **Table-driven patterns**: Time-of-day patterns use clean `TimePattern` structs with lookup functions
4. **Style integration**: Alternative route colors defined in all 6 theme files (default/outdoors/vehicle x light/dark)

### Concerns

1. **Threading model**: Alternative route computation crosses 3 threads (GUI -> Background -> GUI) with insufficient synchronization. The `CalculateRouteSync` approach of holding `m_guard` is a fundamental design issue.

2. **Ownership model**: The raw pointer extraction from `unique_ptr` is a fragile pattern. The codebase uses `shared_ptr` elsewhere (e.g., `m_router` in `AsyncRouter`) which is safer for cross-thread scenarios.

3. **Scale of changes**: 15 files, 4 phases, touching routing core (`AsyncRouter`, `IndexRouter`), traffic layer (`TrafficManager`), and rendering (`rule_drawer`, `route_renderer`). The blast radius is large.

### Recommendations

1. **Separate CalculateRouteSync from AsyncRouter**: Create a lightweight wrapper that does NOT acquire `m_guard`:
   ```cpp
   class SyncRouteCalculator {
     std::shared_ptr<IRouter> m_router;  // Copied from AsyncRouter
   public:
     RouterResultCode Calculate(Checkpoints const &, Route &);
   };
   ```

2. **Add generation-based cancellation**: Every time a new primary route is computed, increment a generation counter. All background alternative computation should check this counter before posting results.

3. **Consolidate TrafficEstimator ownership**: Use a single `shared_ptr<TrafficEstimator>` shared between `RoutingManager` and `IndexRouter`.

---

## Action Items

### Must Fix Before Merge (BLOCKING)

1. [ ] **C1**: Remove `m_guard` lock from `CalculateRouteSync` -- copy `m_router` under lock, then calculate without lock
2. [ ] **C2**: Replace raw pointer capture with `shared_ptr` for `m_alternativeFinder` in background lambda
3. [ ] **C3**: Make `m_displayMode` `std::atomic<TrafficDisplayMode>` or move write inside mutex scope
4. [ ] **C4**: Add `#include "routing/route.hpp"` to `alternative_route.hpp` (or extract `RouteSegment` to own header)
5. [ ] **C5**: Fix meter-to-mercator conversion in `OffsetPerpendicular` to handle X/Y scale difference

### Should Address (IMPORTANT)

1. [ ] **W1**: Consolidate duplicate `TrafficEstimator` instances to single `shared_ptr`
2. [ ] **W2**: Fix early return in `OnBuildRouteReady` to not skip route callbacks
3. [ ] **W3**: Add cancellation mechanism for in-flight alternative computation
4. [ ] **W4**: Protect `m_alternativeDrapeSubroutes` with mutex or assert thread affinity

### Optional Improvements (NITS)

1. [ ] **I1**: Include `segmentIdx` in spatial variation hash for per-segment variation
2. [ ] **I2**: Scale via-waypoint offset distances relative to route length
3. [ ] **I3**: Verify Class2 removal does not affect non-traffic road rendering
4. [ ] **I4**: Improve log message clarity for time diff (positive = faster, negative = slower)
5. [ ] **I5**: Consider lazy/async generation of synthetic traffic data

---

## Verdict

**Decision**: REQUEST CHANGES

**Rationale**: The 4-phase feature set is well-designed and adds meaningful traffic visualization and alternative route capabilities. However, 5 critical issues must be resolved before merging:

- 2 thread safety issues (mutex starvation in C1, data race in C3) that could cause deadlocks or undefined behavior in production
- 1 memory safety issue (dangling pointer in C2) that could cause use-after-free crashes
- 1 compilation/UB issue (incomplete type in C4) that may cause build failures on strict compilers
- 1 logic bug (incorrect coordinate math in C5) that places alternative route waypoints at wrong locations

**Path to Approval**:
1. Fix all 5 CRITICAL items
2. Address at least W1, W2, and W3 from IMPORTANT items
3. Re-submit for review

**Estimated Fix Effort**: 4-8 hours

---

## Handoff

**Status**: Review Completed
**Verdict**: CHANGES_REQUESTED
**Files Reviewed**: 15
**Issues Found**: 5 blocking / 4 important / 5 nit

**Next Steps**: Author should address all CRITICAL items (C1-C5) and re-submit.

---

*Review completed by Reviewer Suckless Agent*
*Methodology: Evidence-based practices from Google, Microsoft, and Meta engineering guidelines*
*Focus areas: Thread safety, Memory safety, API correctness, Logic bugs, Performance, Code style, Edge cases*

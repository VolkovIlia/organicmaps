# PRD: Routing Improvements for Organic Maps

**Version**: 1.0
**Date**: 2026-02-03
**Status**: Draft
**Owner**: Product Strategist

---

## Table of Contents

1. [Problem Statement](#1-problem-statement)
2. [Goals and Non-Goals](#2-goals-and-non-goals)
3. [Success Metrics](#3-success-metrics)
4. [Phased Implementation](#4-phased-implementation)
5. [Acceptance Criteria](#5-acceptance-criteria)
6. [Risk Assessment](#6-risk-assessment)
7. [Dependencies](#7-dependencies)
8. [Appendix](#8-appendix)

---

## 1. Problem Statement

### 1.1 User Pain Points

Organic Maps users, particularly in Russia and CIS regions, report three critical routing issues:

| Issue | Evidence | Impact |
|-------|----------|--------|
| **Suboptimal Routes** | GitHub issues (e.g., #1071 Moscow-Desnogorsk), community feedback | Users choose competing apps |
| **Late Alternative Routes** | User reports: alternatives shown after decision points | Missed opportunities, frustration |
| **Aggressive U-Turn Suggestions** | Repeated U-turn prompts instead of accepting user deviation | Distraction, safety concerns |

### 1.2 Root Cause Analysis

**Technical Root Causes** (Evidence Level A - Source Code Analysis):

1. **No preprocessing** - Pure bidirectional A* at query time results in:
   - Query times 50-500ms for long routes
   - Limited time budget for alternative computation
   - Heuristic-dependent quality

2. **Simplistic turn penalties** - Fixed 2-minute U-turn penalty ignores:
   - Junction types (traffic lights, roundabouts)
   - Road class transitions
   - Turn angle severity

3. **Distance-based rerouting** - `routing_session.hpp` uses simple distance threshold:
   - No corridor concept
   - No intelligent U-turn vs. reroute decision
   - `m_moveAwayCounter` triggers premature rerouting

4. **Missing speed profile customization** - Default speeds inappropriate for CIS:
   - Primary roads assumed 80 km/h (actual ~60 km/h in Russia)
   - Surface quality not factored
   - No regional profiles

### 1.3 Hypothesis

**For** offline map users navigating in Russia and CIS regions,
**there is a need** for improved route calculation with regional speed profiles, intelligent rerouting, and meaningful alternatives
**because** current routing quality causes users to abandon navigation or switch to competitor apps;
**this will lead to** increased navigation session completion rate and reduced user complaints.

**Evidence Level**: B (user feedback + code analysis)

**Success Criteria**:
- 25% reduction in routing-related GitHub issues within 6 months of Phase 4 completion
- Navigation session completion rate increase from baseline (measure needed)
- Route quality score improvement (user surveys)

---

## 2. Goals and Non-Goals

### 2.1 Goals

| ID | Goal | Phase |
|----|------|-------|
| G1 | Improve route optimality through better turn costs and speed profiles | 1 |
| G2 | Eliminate excessive U-turn suggestions through corridor-based following | 2 |
| G3 | Reduce query times 10-50x through CCH preprocessing | 3 |
| G4 | Provide 2-3 meaningful alternative routes at navigation start | 4 |
| G5 | Support Russia/CIS-specific road characteristics | 1 |

### 2.2 Non-Goals (Out of Scope)

| Item | Reason |
|------|--------|
| Real-time traffic integration | Requires online connectivity; conflicts with offline-first mission |
| Voice guidance improvements | Separate concern; not routing algorithm |
| UI/UX redesign of route display | Separate product initiative |
| Public transit routing | Different algorithm family; separate project |
| Truck/HGV routing | Requires additional data (weight limits, height restrictions) |
| Map data quality improvements | Community effort; not algorithmic |

---

## 3. Success Metrics

### 3.1 North Star Metric

**Navigation Session Completion Rate**: % of started navigation sessions that reach destination without user abandonment.

### 3.2 Supporting Metrics

| Metric | Baseline | Phase 1 Target | Phase 4 Target | Measurement |
|--------|----------|----------------|----------------|-------------|
| Route query time (500km route) | 200-500ms | 200-500ms | <10ms | Instrumentation |
| U-turn suggestions per session | TBD | -50% | -80% | Session logs |
| Reroute triggers per session | TBD | -30% | -50% | Session logs |
| Routing issue reports/month | TBD | -15% | -25% | GitHub issues |
| Alternative route availability | 0% | 0% | 100% | Feature flag |

### 3.3 Quality Metrics

| Metric | Definition | Target |
|--------|------------|--------|
| Route stretch ratio | Actual route length / Euclidean distance | <1.5 for urban |
| Route time accuracy | Estimated time vs actual time | +/- 15% |
| Alternative diversity | % non-overlapping segments between alternatives | >40% |

---

## 4. Phased Implementation

### Overview

```
Phase 1 (Quick Wins)     Phase 2 (Corridor)      Phase 3 (CCH)           Phase 4 (Alternatives)
[2-3 weeks]              [4-6 weeks]             [8-12 weeks]            [4-6 weeks]
     |                        |                       |                       |
     v                        v                       v                       v
feature/routing-quick-wins   feature/routing-corridor  feature/routing-cch   feature/routing-alternatives
     |                        |                       |                       |
     +---- Can parallel ------+                       |                       |
                                                      |                       |
                                    Depends on Phase 3 -----------------------+
```

### Phase 1: Quick Wins

**Branch**: `feature/routing-quick-wins`
**Duration**: 2-3 weeks
**Independently Shippable**: Yes

#### 1.1 Scope

| Feature | File(s) | Description |
|---------|---------|-------------|
| Improved turn costs | `edge_estimator.cpp` | Junction-aware, angle-based turn penalties |
| Russia speed profiles | `edge_estimator.cpp`, new config | Regional speed defaults |
| Surface penalties | `edge_estimator.cpp` | Penalty for unpaved surfaces |
| U-turn context | `edge_estimator.cpp` | Road-class-aware U-turn penalties |

#### 1.2 Technical Approach

**Turn Cost Model**:
```
Turn Cost = Base Angle Cost + Junction Delay + Road Class Penalty

Where:
- Base Angle Cost: cos(angle) mapped to 0-45s
- Junction Delay: traffic_signals=30s, stop=10s, give_way=5s
- Road Class Penalty: downgrade penalty (motorway->trunk=5s, trunk->primary=10s)
```

**Russia Speed Profile**:
| Road Type | Current | New RU Default |
|-----------|---------|----------------|
| motorway | 110 km/h | 110 km/h |
| trunk | 90 km/h | 80 km/h |
| primary | 80 km/h | 60 km/h |
| secondary | 60 km/h | 50 km/h |
| tertiary | 50 km/h | 40 km/h |
| unclassified | 40 km/h | 30 km/h |

**Surface Penalty**:
- `surface=paved`: 0%
- `surface=compacted`: +10%
- `surface=gravel`: +30%
- `surface=unpaved`: +50%
- `surface=ground`: +100%

#### 1.3 Deliverables

- [ ] Modified `edge_estimator.cpp` with new turn cost model
- [ ] Speed profile configuration (JSON/YAML) for regional overrides
- [ ] Unit tests for all new penalty calculations
- [ ] Integration tests comparing route quality before/after

---

### Phase 2: Corridor-Based Route Following

**Branch**: `feature/routing-corridor`
**Duration**: 4-6 weeks
**Independently Shippable**: Yes
**Can Parallel with Phase 1**: Yes (different code areas)

#### 2.1 Scope

| Feature | File(s) | Description |
|---------|---------|-------------|
| Corridor definition | `routing_session.hpp/cpp` | Road-class-aware corridor width |
| Off-route detection | `routing_session.hpp/cpp` | Replace distance-based with corridor-based |
| U-turn vs reroute logic | `routing_session.hpp/cpp` | Intelligent decision tree |
| GPS accuracy compensation | `routing_session.hpp/cpp` | Dynamic corridor adjustment |

#### 2.2 Technical Approach

**Corridor Width Calculation**:
```cpp
CorridorWidth(road_class, gps_accuracy) {
  base_width = {
    motorway: 50m,
    trunk: 40m,
    primary: 30m,
    secondary: 25m,
    tertiary: 20m,
    residential: 15m
  }[road_class]

  return base_width + max(gps_accuracy, 10m)
}
```

**Off-Route Decision Tree**:
```
IF position outside corridor for >10 seconds THEN
  distance_back = distance_to_return_to_route()
  distance_forward = distance_via_alternative()

  IF distance_back < 200m AND turn_count(return_route) <= 2 THEN
    action = SUGGEST_RETURN  // "Turn around when safe"
  ELSE IF distance_forward <= distance_back * 1.2 THEN
    action = CALCULATE_NEW_ROUTE  // Accept deviation
  ELSE
    action = SUGGEST_RETURN_WITH_REASON  // Explain time difference
  END IF
END IF
```

**State Machine**:
```
ON_ROUTE --> [deviation detected] --> MONITORING
MONITORING --> [back in corridor] --> ON_ROUTE
MONITORING --> [10s outside] --> DECIDING
DECIDING --> [return cheaper] --> SUGGESTING_RETURN
DECIDING --> [forward cheaper] --> REROUTING
SUGGESTING_RETURN --> [user ignores 30s] --> REROUTING
```

#### 2.3 Deliverables

- [ ] Corridor class with width calculation
- [ ] Modified `routing_session` with state machine
- [ ] Off-route decision logic
- [ ] Unit tests for state transitions
- [ ] Integration tests for common deviation scenarios

---

### Phase 3: CCH Preprocessing

**Branch**: `feature/routing-cch`
**Duration**: 8-12 weeks
**Independently Shippable**: Yes (but enables Phase 4)
**Dependencies**: None (but Phase 4 depends on this)

#### 3.1 Scope

| Feature | File(s) | Description |
|---------|---------|-------------|
| CCH topology builder | `generator/routing_cch.cpp` (new) | Metric-independent preprocessing |
| CCH customizer | `routing/cch_customizer.cpp` (new) | Weight-dependent phase |
| CCH query engine | `routing/cch_router.cpp` (new) | Fast CCH queries |
| Profile customization | `routing/cch_profiles.cpp` (new) | Car/bicycle/pedestrian weights |
| MWM format extension | `generator/routing_index_generator.cpp` | Store CCH in map files |

#### 3.2 Technical Approach

**Three-Phase CCH**:

1. **Topology Preprocessing** (generator, ~30 min for Europe)
   - Node ordering based on hierarchy
   - Shortcut computation (metric-independent)
   - Output: topology overlay stored in MWM

2. **Customization** (app startup or profile change, <1 min)
   - Load topology from MWM
   - Compute edge weights for selected profile
   - Output: in-memory customized graph

3. **Query** (per-request, 1-10ms)
   - Bidirectional Dijkstra on customized CCH
   - Path unpacking to original edges

**Memory Budget**:
- Target: <500MB for continental routing (Europe)
- CCH overlay: ~2x original graph
- Memory-mapped topology file
- In-memory customization for active profile only

**File Format Extension**:
```
MWM v2.x
├── existing sections...
├── CCH_TOPOLOGY_SECTION
│   ├── node_order[]
│   ├── shortcuts[]
│   └── level_offsets[]
└── CCH_WEIGHTS_SECTION (optional, for pre-computed default profile)
```

#### 3.3 Deliverables

- [ ] CCH topology builder in `generator/`
- [ ] CCH customizer for runtime profile switching
- [ ] CCH query engine integrated with `index_router`
- [ ] MWM format extension with backward compatibility
- [ ] Build system updates for CCH generation
- [ ] Performance benchmarks (query time, memory usage)
- [ ] Fallback to A* when CCH unavailable
- [ ] Unit tests for CCH correctness
- [ ] Integration tests comparing CCH vs A* routes

---

### Phase 4: Alternative Routes

**Branch**: `feature/routing-alternatives`
**Duration**: 4-6 weeks
**Independently Shippable**: Yes
**Dependencies**: Phase 3 (CCH) required for performance

#### 4.1 Scope

| Feature | File(s) | Description |
|---------|---------|-------------|
| k-SPwLO algorithm | `routing/alternative_routes.cpp` (new) | Limited overlap alternatives |
| Alternative display | `routing_session.hpp/cpp` | Store and expose alternatives |
| Decision point detection | `routing/alternative_routes.cpp` | Identify divergence points |
| Alternative timing | `routing_session.hpp/cpp` | Show alternatives at right time |

#### 4.2 Technical Approach

**k-SPwLO Parameters**:
- k = 3 (primary + 2 alternatives)
- Overlap threshold: 60%
- Max length ratio: 1.3 (alternatives max 30% longer)

**Algorithm**:
```
FindAlternatives(origin, destination, k=3, overlap_threshold=0.6):
  paths = []
  P1 = CCH_Query(origin, destination)
  paths.append(P1)

  FOR i = 2 TO k:
    best_candidate = null
    best_score = infinity

    FOR each via_node in high_importance_nodes:
      candidate = CCH_Query(origin, via_node) + CCH_Query(via_node, destination)
      overlap = compute_overlap(candidate, paths)

      IF overlap < overlap_threshold AND length(candidate) < 1.3 * length(P1):
        score = length(candidate) + penalty * overlap
        IF score < best_score:
          best_candidate = candidate
          best_score = score

    IF best_candidate != null:
      paths.append(best_candidate)

  RETURN paths
```

**Display Timing**:
- Pre-compute alternatives with initial route request
- Show alternatives at decision points (500m-2km before divergence)
- Suggest alternative if it saves >10% travel time
- Remember dismissed alternatives (don't re-show for same segment)

#### 4.3 Deliverables

- [ ] k-SPwLO implementation
- [ ] Alternative route storage in `routing_session`
- [ ] API for platform layers to retrieve alternatives
- [ ] Decision point calculation
- [ ] Unit tests for overlap calculation
- [ ] Integration tests for alternative quality
- [ ] Performance tests (must complete within 100ms)

---

## 5. Acceptance Criteria

### Phase 1: Quick Wins

| ID | Criterion | Test Method |
|----|-----------|-------------|
| AC1.1 | Turn cost at traffic_signals junction is 25-35s higher than uncontrolled | Unit test |
| AC1.2 | U-turn on motorway penalized 180s, on residential 60s | Unit test |
| AC1.3 | Route Moscow-Tver via M10 (not through small towns) | Integration test |
| AC1.4 | Route on unpaved road is 50%+ slower than equivalent paved | Unit test |
| AC1.5 | All existing routing tests pass | CI |
| AC1.6 | No performance regression (query time within 10% of baseline) | Benchmark |

**Gherkin Example (AC1.3)**:
```gherkin
Given user requests route from Moscow (55.7558, 37.6173) to Tver (56.8584, 35.9006)
When route is calculated with Russia speed profile
Then route should prefer M10 highway
And route should NOT pass through Klin city center
And estimated time should be 2h-2h30m
```

### Phase 2: Corridor-Based Following

| ID | Criterion | Test Method |
|----|-----------|-------------|
| AC2.1 | Lane change on highway (within 50m) does not trigger reroute | Integration test |
| AC2.2 | Deliberate deviation (>50m for >10s) triggers evaluation | Integration test |
| AC2.3 | If return is <200m with 1 turn, suggest return | Unit test |
| AC2.4 | If alternative is within 20% of return distance, accept deviation | Unit test |
| AC2.5 | U-turn suggestions reduced by 50% vs baseline | A/B test |
| AC2.6 | GPS accuracy of 30m expands corridor by 30m | Unit test |

**Gherkin Example (AC2.2)**:
```gherkin
Given user is navigating on a route
And user deviates 60m from route corridor
When 10 seconds pass without returning to corridor
Then system should calculate distance to return vs distance via new route
And system should make reroute decision based on comparison
```

### Phase 3: CCH Preprocessing

| ID | Criterion | Test Method |
|----|-----------|-------------|
| AC3.1 | CCH query for 500km route completes in <10ms | Benchmark |
| AC3.2 | CCH route is identical to A* route for same weights | Integration test |
| AC3.3 | CCH memory usage <500MB for Europe graph | Memory profiling |
| AC3.4 | MWM files with CCH load successfully on existing app versions (fallback) | Compatibility test |
| AC3.5 | Profile customization completes in <60s on mobile | Benchmark |
| AC3.6 | CCH generator completes Europe in <2 hours | CI benchmark |

**Gherkin Example (AC3.1)**:
```gherkin
Given CCH index is loaded for Germany
When user requests route Berlin to Munich (585km)
Then route query completes in <10ms
And route is valid (continuous path from origin to destination)
```

### Phase 4: Alternative Routes

| ID | Criterion | Test Method |
|----|-----------|-------------|
| AC4.1 | 3 alternatives returned for routes >50km | Integration test |
| AC4.2 | Alternatives have <60% overlap with primary route | Unit test |
| AC4.3 | Alternatives are max 30% longer than primary | Unit test |
| AC4.4 | Alternative calculation adds <100ms to query time | Benchmark |
| AC4.5 | Decision points correctly identified at route divergence | Integration test |
| AC4.6 | Dismissed alternative not shown again for same segment | Integration test |

**Gherkin Example (AC4.2)**:
```gherkin
Given user requests route from A to B with alternatives
When alternatives are calculated
Then each alternative should share <60% of edges with primary route
And each alternative should share <60% of edges with other alternatives
```

---

## 6. Risk Assessment

### Technical Risks

| ID | Risk | Likelihood | Impact | Mitigation |
|----|------|------------|--------|------------|
| R1 | CCH memory exceeds mobile limits | Medium | High | Memory-mapped files, hierarchical loading |
| R2 | CCH preprocessing time unacceptable | Low | Medium | Parallel processing, incremental updates |
| R3 | Route quality regression in edge cases | Medium | Medium | Comprehensive test suite, A/B rollout |
| R4 | Backward compatibility breaks with MWM format | Medium | High | Version negotiation, fallback to A* |
| R5 | Performance on low-end devices | Medium | Medium | Configurable CCH levels, graceful degradation |
| R6 | Corridor width tuning across road types | High | Low | Configurable parameters, user feedback loop |

### Mitigation Details

**R1 - Memory Limits**:
- Use memory-mapped files for CCH topology
- Load only active MWM region's CCH
- Implement LRU cache for CCH data

**R3 - Quality Regression**:
- Golden route test suite (100+ routes with expected results)
- Shadow mode: run new algorithm alongside old, compare
- Feature flag for gradual rollout

**R4 - Backward Compatibility**:
- MWM version field indicates CCH presence
- Old app versions ignore CCH sections
- New app versions fallback to A* if CCH unavailable

### Assumptions Requiring Validation

| ID | Assumption | Validation Method | Phase |
|----|------------|-------------------|-------|
| A1 | Russia speed profile values are accurate | Community feedback, trip logs | 1 |
| A2 | 60% overlap threshold produces diverse routes | User testing | 4 |
| A3 | 10-second off-route threshold is appropriate | Telemetry analysis | 2 |
| A4 | Users want 3 alternatives (not 2 or 5) | User survey | 4 |

---

## 7. Dependencies

### Phase Dependencies

```
Phase 1 ─────────────────────────────────────────────────────────────────────>
         [no dependencies, can start immediately]

Phase 2 ─────────────────────────────────────────────────────────────────────>
         [no dependencies, can parallel with Phase 1]

Phase 3        [start after Phase 1 merged, for stable base] ───────────────>
                           |
                           v
Phase 4                    [requires Phase 3 CCH for performance] ──────────>
```

### External Dependencies

| Dependency | Phase | Description | Risk |
|------------|-------|-------------|------|
| OSM tag coverage | 1 | `surface`, `junction` tags for penalties | Low (graceful fallback) |
| Generator rebuild | 3 | Map rebuild required for CCH | Medium (timing) |
| Platform UI changes | 4 | iOS/Android for alternative display | Medium (coordination) |

### Team Dependencies

| Phase | Expertise Required |
|-------|-------------------|
| 1 | C++ routing engineer |
| 2 | C++ routing engineer |
| 3 | C++ routing engineer + generator expert |
| 4 | C++ routing engineer + platform engineers (iOS, Android) |

---

## 8. Appendix

### A. Key Files Reference

| File | Purpose | Modified In |
|------|---------|-------------|
| `libs/routing/astar_algorithm.hpp` | A* implementation | Phase 3 (refactor) |
| `libs/routing/edge_estimator.cpp` | Weight calculation | Phase 1 |
| `libs/routing/index_router.cpp` | Main router entry | Phase 3, 4 |
| `libs/routing/routing_session.hpp` | Session management | Phase 2, 4 |
| `generator/routing_index_generator.cpp` | Index generation | Phase 3 |

### B. Research References

- Dibbelt, Strasser, Wagner (2016): "Customizable Contraction Hierarchies" - ACM JEA
- k-SPwLO: VLDB Journal (2020)
- Turn costs: Springer (2011)
- Full research: `.dev-docs/reports/RESEARCH_FINDINGS.md`

### C. Glossary

| Term | Definition |
|------|------------|
| CCH | Customizable Contraction Hierarchies - preprocessing technique for fast routing |
| k-SPwLO | k-Shortest Paths with Limited Overlap - algorithm for diverse alternatives |
| MWM | Mobile World Map - Organic Maps file format |
| Corridor | Buffer zone around route where deviations are tolerated |
| Stretch ratio | Actual route length divided by straight-line distance |

---

*PRD generated by Product Strategist Agent based on research findings from Researcher Agent.*

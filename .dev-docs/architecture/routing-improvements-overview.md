# Routing Improvements Architecture Overview

**Version**: 1.0
**Date**: 2026-02-03
**Author**: AI Solutions Architect

---

## 1. Executive Summary

This document describes the technical architecture for implementing routing improvements in Organic Maps across four phases:

1. **Phase 1: Quick Wins** - Regional speed profiles, improved turn costs, surface penalties
2. **Phase 2: Corridor-Based Following** - Intelligent off-route detection and U-turn decisions
3. **Phase 3: CCH Preprocessing** - Customizable Contraction Hierarchies for 10-50x query speedup
4. **Phase 4: Alternative Routes** - k-SPwLO algorithm for meaningful route alternatives

---

## 2. Current Architecture Analysis

### 2.1 Core Components

```
libs/routing/
├── base/
│   └── astar_algorithm.hpp      # Bidirectional A* implementation
├── edge_estimator.cpp           # Weight calculation, turn/ferry penalties
├── index_router.cpp             # Main routing entry point
├── index_graph.hpp              # Road graph representation
├── routing_session.cpp          # Navigation session, rerouting logic
└── vehicle_model.cpp            # Speed profiles per road class

generator/
└── routing_index_generator.cpp  # Index generation for MWM files
```

### 2.2 Current Routing Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            RoutingSession                                    │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │ OnLocationPositionChanged() → MoveIterator() → State Management      │  │
│  │ m_moveAwayCounter → kOnRouteMissedCount(10) → RouteNeedRebuild       │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              IndexRouter                                     │
│  ┌───────────────────────────────────────────────────────────────────────┐ │
│  │ DoCalculateRoute() → CalculateSubroute() → AStarAlgorithm             │ │
│  │                                                                        │ │
│  │ WorldGraphMode:                                                        │ │
│  │   - Joints (pedestrian/bicycle, near routes)                           │ │
│  │   - LeapsOnly (car, distant routes - cross-MWM)                        │ │
│  │   - NoLeaps (guides mode)                                              │ │
│  └───────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            EdgeEstimator                                     │
│  ┌───────────────────────────────────────────────────────────────────────┐ │
│  │ CalcSegmentWeight() → GetSpeedMpS() * distance                         │ │
│  │                                                                        │ │
│  │ Current penalties:                                                     │ │
│  │   - U-turn: 2 minutes (fixed)                                          │ │
│  │   - Ferry landing: 20 minutes (fixed)                                  │ │
│  │   - Traffic factor: SpeedGroup-based multiplier                        │ │
│  │   - Altitude: pedestrian/bicycle climb penalties                       │ │
│  └───────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.3 Key Findings from Code Analysis

| Component | Current State | Issue |
|-----------|---------------|-------|
| `edge_estimator.cpp:362-368` | Fixed 2-minute U-turn penalty | No context (road class, junction type) |
| `routing_session.cpp:304-326` | `m_moveAwayCounter` triggers reroute | Simple distance threshold, no corridor |
| `vehicle_model.cpp` | Global speed profiles | No regional customization |
| `astar_algorithm.hpp` | Bidirectional A* only | No preprocessing speedup |
| `index_router.cpp` | No alternative route computation | Single route returned |

---

## 3. Target Architecture

### 3.1 Component Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PHASE 1: Quick Wins                                │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌───────────────────┐   │
│  │  TurnCostModel      │  │  RegionalSpeedProf  │  │  SurfacePenalty   │   │
│  │  - angle_cost       │  │  - country_profiles │  │  - surface_factor │   │
│  │  - junction_delay   │  │  - road_class_speed │  │  - quality_factor │   │
│  │  - road_class_pen   │  │  - override_config  │  │                   │   │
│  └─────────────────────┘  └─────────────────────┘  └───────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                        PHASE 2: Corridor Following                           │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌───────────────────┐   │
│  │  CorridorTracker    │  │  OffRouteDetector   │  │  RerouteDecision  │   │
│  │  - width_by_class   │  │  - lateral_distance │  │  - return_cost    │   │
│  │  - gps_compensation │  │  - heading_delta    │  │  - forward_cost   │   │
│  │  - state_machine    │  │  - time_threshold   │  │  - decision_tree  │   │
│  └─────────────────────┘  └─────────────────────┘  └───────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                         PHASE 3: CCH Preprocessing                           │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  GENERATOR (offline)                    RUNTIME (mobile)                    │
│  ┌─────────────────────┐               ┌─────────────────────┐              │
│  │  CCHTopologyBuilder │               │  CCHCustomizer      │              │
│  │  - node_ordering    │ ──stores──▶  │  - load_topology    │              │
│  │  - shortcut_compute │               │  - apply_profile    │              │
│  │  - level_offsets    │               │  - compute_weights  │              │
│  └─────────────────────┘               └─────────────────────┘              │
│           │                                     │                            │
│           ▼                                     ▼                            │
│  ┌─────────────────────┐               ┌─────────────────────┐              │
│  │  MWM CCH Section    │               │  CCHQueryEngine     │              │
│  │  - CCH_TOPOLOGY     │               │  - bidirectional    │              │
│  │  - optional weights │               │  - path_unpacking   │              │
│  └─────────────────────┘               └─────────────────────┘              │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                        PHASE 4: Alternative Routes                           │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌───────────────────┐   │
│  │  AlternativeFinder  │  │  OverlapCalculator  │  │  RouteComparator  │   │
│  │  - k_spw_lo_algo    │  │  - edge_overlap     │  │  - length_ratio   │   │
│  │  - via_node_select  │  │  - threshold_check  │  │  - diversity_score│   │
│  │  - cch_integration  │  │  - path_similarity  │  │  - time_estimate  │   │
│  └─────────────────────┘  └─────────────────────┘  └───────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Data Flow Architecture

```
                                    User Request
                                         │
                                         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              IndexRouter                                     │
│                                                                              │
│  1. Check CCH availability for MWM region                                   │
│  2. IF CCH available AND route > threshold:                                 │
│        → CCHQueryEngine.Query(origin, destination, profile)                 │
│     ELSE:                                                                   │
│        → AStarAlgorithm.FindPath() (fallback)                              │
│  3. IF alternatives requested:                                              │
│        → AlternativeFinder.Find(k=3, overlap=0.6)                          │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
                                         │
                                         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            RoutingSession                                    │
│                                                                              │
│  1. Store primary route + alternatives                                      │
│  2. OnLocationPositionChanged():                                            │
│        → CorridorTracker.CheckPosition(gps_info)                           │
│        → IF outside_corridor > 10s:                                        │
│              OffRouteDetector.Evaluate() → RerouteDecision.Decide()        │
│  3. IF approaching decision point:                                         │
│        → Show alternative routes if beneficial                             │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Integration Points

### 4.1 Phase Dependencies

```
Phase 1 ─────────────────────────────────────────────────────────────▶
         [Independent - can start immediately]

Phase 2 ─────────────────────────────────────────────────────────────▶
         [Independent - can parallel with Phase 1]

Phase 3        [After Phase 1 merged for stable base] ───────────────▶
                          │
                          ▼
Phase 4                   [Requires Phase 3 CCH] ────────────────────▶
```

### 4.2 File Modification Summary

| Phase | Files Modified | Files Added |
|-------|---------------|-------------|
| 1 | `edge_estimator.cpp/hpp`, `vehicle_model.cpp/hpp` | `turn_cost_model.cpp/hpp`, `regional_profiles.hpp` |
| 2 | `routing_session.cpp/hpp` | `corridor_tracker.cpp/hpp`, `reroute_decision.cpp/hpp` |
| 3 | `routing_index_generator.cpp` | `cch_topology_builder.cpp/hpp`, `cch_customizer.cpp/hpp`, `cch_query_engine.cpp/hpp` |
| 4 | `index_router.cpp/hpp`, `routing_session.cpp/hpp` | `alternative_finder.cpp/hpp`, `overlap_calculator.cpp/hpp` |

### 4.3 MWM Format Changes

**New sections (Phase 3)**:
```
MWM v2.x (current)          MWM v2.x+1 (with CCH)
├── existing sections       ├── existing sections
│   ├── ROUTING             │   ├── ROUTING
│   ├── CROSS_MWM           │   ├── CROSS_MWM
│   └── ...                 │   └── ...
                            ├── CCH_TOPOLOGY_SECTION (new)
                            │   ├── node_order[]
                            │   ├── shortcuts[]
                            │   └── level_offsets[]
                            └── CCH_WEIGHTS_SECTION (optional)
```

**Backward Compatibility**:
- New app reads old MWM: Falls back to A* (no CCH section)
- Old app reads new MWM: Ignores CCH sections (unknown tags)

---

## 5. Memory and Performance Budget

### 5.1 Memory Constraints

| Component | Target | Notes |
|-----------|--------|-------|
| CCH Topology | < 2x graph size | Memory-mapped file |
| CCH Customization | < 100MB | In-memory for active profile |
| Alternative Routes | < 10MB | 3 routes max |
| Corridor State | < 1MB | Single route tracking |

### 5.2 Performance Targets

| Operation | Current | Phase 1 | Phase 3 | Phase 4 |
|-----------|---------|---------|---------|---------|
| 500km route query | 200-500ms | 200-500ms | < 10ms | < 100ms (with alternatives) |
| Reroute trigger | Distance-based | Distance-based | Corridor-based | Corridor-based |
| Profile customization | N/A | N/A | < 60s | < 60s |

---

## 6. Testing Strategy

### 6.1 Unit Tests

| Phase | Test Focus | Location |
|-------|------------|----------|
| 1 | Turn cost calculations, speed profile lookup | `routing_tests/turn_cost_tests.cpp` |
| 2 | Corridor width, state transitions, decision logic | `routing_tests/corridor_tests.cpp` |
| 3 | CCH correctness (vs A*), serialization | `routing_tests/cch_tests.cpp` |
| 4 | Overlap calculation, diversity metrics | `routing_tests/alternative_tests.cpp` |

### 6.2 Integration Tests

- **Golden Route Suite**: 100+ predefined routes with expected results
- **CCH vs A* Comparison**: Automated verification that CCH produces identical routes
- **Regression Tests**: Ensure existing behavior preserved where not explicitly changed

### 6.3 Performance Benchmarks

- **Query Time**: Automated benchmarks for various route lengths
- **Memory Profile**: Track memory usage during CCH customization
- **Device Testing**: Verify on low-end Android devices (2GB RAM)

---

## 7. Risk Mitigation

### 7.1 Technical Risks

| Risk | Mitigation |
|------|------------|
| CCH memory exceeds mobile limits | Memory-mapped files, hierarchical loading |
| Route quality regression | Golden test suite, A/B rollout with feature flags |
| MWM format breaking changes | Version negotiation, fallback to A* |
| Performance on low-end devices | Configurable CCH levels, graceful degradation |

### 7.2 Rollout Strategy

1. **Phase 1**: Direct merge (low risk, independent changes)
2. **Phase 2**: Feature flag for corridor behavior
3. **Phase 3**: Feature flag for CCH usage, server-side MWM generation first
4. **Phase 4**: Feature flag, gradual rollout by region

---

## 8. ADR Summary

| ADR | Decision | Rationale |
|-----|----------|-----------|
| ADR-001 | Use CCH over CH | CCH supports profile customization without rebuild; critical for car/bike/pedestrian modes |
| ADR-002 | Store CCH topology in MWM | Maintains offline-first architecture; no separate index files |
| ADR-003 | k-SPwLO for alternatives | Guarantees diversity; works with CCH preprocessing |
| ADR-004 | Corridor-based rerouting | Reduces false reroutes; aligns with industry best practices |
| ADR-005 | Regional speed profiles via config | Allows community contribution without code changes |

---

## 9. References

- [Customizable Contraction Hierarchies](https://arxiv.org/pdf/1402.0402) - Dibbelt et al. (2016)
- [k-Shortest Paths with Limited Overlap](https://link.springer.com/article/10.1007/s00778-020-00604-x) - VLDB Journal (2020)
- [Turn Costs in Road Networks](https://link.springer.com/chapter/10.1007/978-3-642-20662-7_9) - Springer (2011)
- PRD: `.dev-docs/prd/routing-improvements.md`
- Research: `.dev-docs/reports/RESEARCH_FINDINGS.md`

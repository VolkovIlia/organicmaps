# Architecture Design Report: Routing Improvements

**Date**: 2026-02-03
**Author**: AI Solutions Architect
**Status**: Complete

---

## Executive Summary

This report documents the complete technical architecture for implementing routing improvements in Organic Maps across four phases. The architecture follows contract-first design, message-passing topology, and suckless principles.

---

## Deliverables Created

### 1. Overview Document
**File**: `.dev-docs/architecture/routing-improvements-overview.md`
- High-level architecture overview
- Component diagrams (Phase 1-4)
- Data flow architecture
- Integration points and dependencies
- Memory and performance budgets
- Testing strategy
- Risk mitigation plan
- ADR summary

### 2. API Contracts
**File**: `.dev-docs/architecture/contracts/routing-api.yaml`
- OpenAPI 3.0 format for C++ interface documentation
- Phase 1: TurnCostModel, RegionalSpeedProfile schemas
- Phase 2: CorridorTracker, RerouteDecision schemas
- Phase 3: CCHTopology, CCHCustomizer, CCHQueryEngine schemas
- Phase 4: AlternativeFinder, DecisionPoint schemas
- C++ interface signatures documented in OpenAPI paths

### 3. Message Flow Diagrams
**File**: `.dev-docs/architecture/diagrams/routing-message-flows.md`
- Mermaid sequence diagrams for all 4 phases
- State machine diagrams (corridor state, GPS quality)
- Class diagrams (CCH data structures, alternatives)
- Data flow diagrams
- Component dependency graph

### 4. Phase 1: Quick Wins
**File**: `.dev-docs/architecture/phase1-quick-wins.md`
- TurnCostModel interface and implementation (C++ code)
- RegionalSpeedProfiles for Russia (C++ code)
- EdgeEstimator integration modifications
- Unit tests and integration tests
- Acceptance criteria verification

### 5. Phase 2: Corridor-Based Following
**File**: `.dev-docs/architecture/phase2-corridor.md`
- CorridorTracker interface and implementation
- RerouteDecision logic (return vs forward cost)
- ParallelRoadDetector for false reroute prevention
- GpsQualityTracker for degraded GPS handling
- RoutingSession integration
- State machine design
- Unit tests

### 6. Phase 3: CCH Preprocessing
**File**: `.dev-docs/architecture/phase3-cch.md`
- CCHTopology data structures
- CCHCustomizer for profile switching
- CCHQueryEngine with bidirectional Dijkstra
- Generator integration (offline preprocessing)
- MWM section format (CCH_TOPOLOGY)
- Memory-mapped loading
- Runtime integration with IndexRouter
- Unit tests (correctness vs A*)

### 7. Phase 4: Alternative Routes
**File**: `.dev-docs/architecture/phase4-alternatives.md`
- k-SPwLO algorithm implementation
- AlternativeFinder with via-node selection
- Overlap calculation (Jaccard similarity)
- DecisionPoint detection and UI integration
- RoutingSession integration
- Unit tests

---

## Architecture Decisions

| ADR | Decision | Rationale |
|-----|----------|-----------|
| ADR-001 | Use CCH over CH | CCH supports profile customization without rebuild |
| ADR-002 | Store CCH topology in MWM | Maintains offline-first architecture |
| ADR-003 | k-SPwLO for alternatives | Guarantees diversity; works with CCH |
| ADR-004 | Corridor-based rerouting | Reduces false reroutes |
| ADR-005 | Regional speed profiles via config | Community contribution without code changes |

---

## Phase Dependencies

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

---

## File Summary

| Phase | Files to Create | Files to Modify |
|-------|-----------------|-----------------|
| 1 | `turn_cost_model.cpp/hpp`, `regional_profiles.hpp` | `edge_estimator.cpp/hpp`, `vehicle_model.cpp/hpp` |
| 2 | `corridor_tracker.cpp/hpp`, `reroute_decision.cpp/hpp`, `parallel_road_detector.cpp/hpp`, `gps_quality_tracker.cpp/hpp` | `routing_session.cpp/hpp` |
| 3 | `cch_topology.cpp/hpp`, `cch_customizer.cpp/hpp`, `cch_query_engine.cpp/hpp`, `cch_topology_builder.cpp/hpp`, `cch_section_writer.cpp/hpp` | `routing_index_generator.cpp`, `index_router.cpp/hpp` |
| 4 | `alternative_route.hpp`, `alternative_finder.cpp/hpp`, `alternatives_ui_state.hpp`, `alternative_route_mark.cpp/hpp` | `routing_session.cpp/hpp`, `index_router.cpp/hpp` |

---

## Performance Targets

| Operation | Current | Phase 1 | Phase 3 | Phase 4 |
|-----------|---------|---------|---------|---------|
| 500km route query | 200-500ms | 200-500ms | < 10ms | < 100ms (with alternatives) |
| Reroute trigger | Distance-based | Distance-based | Corridor-based | Corridor-based |
| Profile customization | N/A | N/A | < 60s | < 60s |

---

## Memory Budget

| Component | Target |
|-----------|--------|
| CCH Topology | < 2x graph size (memory-mapped) |
| CCH Customization | < 100MB (in-memory for active profile) |
| Alternative Routes | < 10MB (3 routes max) |
| Corridor State | < 1MB (single route tracking) |

---

## Backward Compatibility

- **New app reads old MWM**: Falls back to A* (no CCH section)
- **Old app reads new MWM**: Ignores CCH sections (unknown tags)
- **Feature flags**: All phases gated behind settings

---

## Next Steps

1. **Engineer Implementation**: Hand off to Agentic Engineer with:
   - API contracts from `.dev-docs/architecture/contracts/routing-api.yaml`
   - Detailed designs from phase documents
   - C++ code templates provided in each phase document

2. **Parallel Development**:
   - Phase 1 and Phase 2 can proceed in parallel
   - Phase 3 should wait for Phase 1 to stabilize
   - Phase 4 requires Phase 3 CCH

3. **Testing Strategy**:
   - Unit tests for each component (templates provided)
   - CCH vs A* correctness verification
   - Golden route suite for regression testing

---

## Verification Checklist

- [x] API contracts created (OpenAPI format)
- [x] Message flow diagrams (Mermaid)
- [x] Phase 1 detailed design with C++ code
- [x] Phase 2 detailed design with C++ code
- [x] Phase 3 detailed design with C++ code
- [x] Phase 4 detailed design with C++ code
- [x] Memory/performance budgets defined
- [x] Backward compatibility strategy
- [x] Feature flag rollout plan
- [x] Unit test templates

---

## References

- [Customizable Contraction Hierarchies](https://arxiv.org/pdf/1402.0402) - Dibbelt et al. (2016)
- [k-Shortest Paths with Limited Overlap](https://link.springer.com/article/10.1007/s00778-020-00604-x) - VLDB Journal (2020)
- PRD: `.dev-docs/prd/routing-improvements.md`
- Research: `.dev-docs/reports/RESEARCH_FINDINGS.md`

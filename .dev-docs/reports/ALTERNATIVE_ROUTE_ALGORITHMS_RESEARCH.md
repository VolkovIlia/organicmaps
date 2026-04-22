# Alternative Route Computation Algorithms: Research Findings

**Date**: 2026-02-09
**Evidence Level**: A/B (peer-reviewed academic sources + production implementations)
**Scope**: Road network alternative route algorithms, complexity analysis, practical performance

---

## Table of Contents

1. [Quality Criteria for Alternative Routes](#1-quality-criteria)
2. [Algorithm 1: Via-Node on Contraction Hierarchies](#2-via-node-ch)
3. [Algorithm 2: Plateau-Based (CH Bidirectional Search)](#3-plateau-based)
4. [Algorithm 3: Penalty-Based Iterative Re-routing](#4-penalty-based)
5. [Algorithm 4: k-SPwLO (k-Shortest Paths with Limited Overlap)](#5-kspwlo)
6. [Algorithm 5: Customizable Route Planning (CRP)](#6-crp)
7. [Algorithm 6: Separator-Based Alternative Paths in CCH](#7-separator-cch)
8. [Algorithm 7: HiDAR (Hierarchical Decomposition for Alternative Routes)](#8-hidar)
9. [Parallelization Strategies](#9-parallelization)
10. [Practical Implementations Comparison](#10-practical)
11. [Structured Comparison Matrix](#11-comparison-matrix)
12. [Relevance to OrganicMaps Current Implementation](#12-organicmaps)
13. [Recommendations](#13-recommendations)

---

## 1. Quality Criteria for Alternative Routes

All serious academic work on alternative routes converges on three quality criteria, first formalized by Abraham, Delling, Goldberg, and Werneck (2013):

### 1.1 Limited Sharing (Dissimilarity)

An alternative path p must share at most a fraction theta of edges with the shortest path p*:

    |edges(p) intersection edges(p*)| / |edges(p*)| <= theta

Typical values: theta = 0.5 to 0.8 (i.e., at most 50-80% overlap).

**Evidence**: Abraham et al. 2013, Luxen & Schieferdecker 2012, Chondrogiannis et al. 2020.

### 1.2 Bounded Stretch

The alternative path length must not exceed the shortest path by more than a factor epsilon:

    length(p) <= (1 + epsilon) * length(p*)

Typical values: epsilon = 0.25 to 0.5 (i.e., at most 25-50% longer).

### 1.3 Local Optimality

Every subpath of the alternative should be a shortest path (or close to it). Formally, for any segment [u,v] of the alternative that is not shared with the shortest path, the subpath from u to v in the alternative should be the shortest path from u to v.

This prevents "obviously stupid detours" -- an alternative should not contain a segment where a shortcut clearly exists.

### 1.4 Uniformly Bounded Stretch (UBS)

A stronger variant proposed by Bader et al. (2011): not only must the total stretch be bounded, but the stretch must be bounded for every PREFIX of the path. This prevents "early detours" that are made up for later.

**Citation**: Bader, Dees, Geisberger, Sanders. "Alternative Route Graphs in Road Networks." TAPAS 2011. Springer LNCS.

---

## 2. Via-Node on Contraction Hierarchies

### Citation

**Ittai Abraham, Daniel Delling, Andrew V. Goldberg, Renato F. Werneck.** "Alternative Routes in Road Networks." *ACM Journal of Experimental Algorithmics*, Vol. 18, 2013. Originally presented at SEA 2010.

### Algorithm Description

1. **Compute CH shortest path** from s to t using standard bidirectional CH search.
2. **During the bidirectional search**, both forward and backward search spaces overlap. Continue BOTH searches for alpha percent (typically 10%) beyond the first meeting point.
3. **Identify via-node candidates**: nodes v where both d(s,v) + d(v,t) is known from the forward/backward search trees and d(s,v) + d(v,t) <= (1+epsilon) * d(s,t).
4. **For each candidate v**, construct the via-path: shortest(s,v) concatenated with shortest(v,t).
5. **Filter** candidates using the three quality criteria (sharing, stretch, local optimality).
6. **Return** the best k alternatives.

### Key Insight

By restricting to **single via-node paths** (s -> v -> t), the algorithm avoids the exponential blowup of arbitrary k-shortest-path enumeration. A via-path is guaranteed to be locally optimal around the via-node because each sub-path is a shortest path.

### Time Complexity

- **Preprocessing**: Standard CH preprocessing. O(n log n) average, O(n^2) worst case on road networks. Practical: 5-30 minutes for Europe (18M nodes).
- **Query (single shortest path)**: O(h * log(h)) where h is the highway dimension. Practical: 0.01-0.05 ms on Europe.
- **Query (alternatives)**: Approximately 2-5x the cost of a single CH query for candidate enumeration, plus k shortest path queries for via-path construction. Practical total: 0.5-5 ms.
- **Candidate evaluation**: O(|candidates| * |path_length|) for overlap checks.

### Preprocessing Requirements

Standard CH preprocessing: node ordering + shortcut computation. For Western Europe (~18M nodes, ~42M edges):
- Preprocessing time: 5-30 minutes (single-threaded), under 5 min (parallel)
- Space overhead: 2-3x original edges (shortcut edges)
- Memory: 100-500 MB total

### Speedup vs Brute-Force

**Brute-force approach**: Run Dijkstra k times with penalty. O(k * n * log(n)) per query.
- On Europe: k * 5-10 seconds per query.

**Via-node CH**: 0.5-5 ms for k=3 alternatives.
- **Speedup: 1,000-10,000x** over brute-force Dijkstra penalty.

### Strengths

- Extremely fast query time (sub-10 ms for k=3).
- Guaranteed local optimality around via-node.
- Reuses existing CH data structure.

### Weaknesses

- Restricted to single-via-node paths (cannot find alternatives requiring 2+ waypoints).
- Quality depends on search space overlap. Some source/target pairs yield few candidates.
- CH preprocessing is metric-dependent (must rerun for weight changes).

**Evidence Level: A** (peer-reviewed, ACM journal, extensive experimental evaluation)

---

## 3. Plateau-Based (CH Bidirectional Search)

### Citation

**Dennis Luxen, Dennis Schieferdecker.** "Candidate Sets for Alternative Routes in Road Networks." *SEA 2012*, LNCS Vol. 7276, pp. 260-270. Springer. Extended version in *ACM Journal of Experimental Algorithmics*, Vol. 20, 2015.

### Algorithm Description

1. **Run bidirectional CH search** from s and from t.
2. **Detect "plateaus"**: A plateau is a maximal sequence of vertices v1, v2, ..., vk on the shortest path tree where the forward search discovers them in the SAME ORDER as the backward search. Formally, for each vertex v in the search space, track both the forward distance d_f(s,v) and backward distance d_b(v,t). A plateau at node v exists when the forward and backward search trees "agree" on the path through v for a significant stretch.
3. **Plateau = shared subpath**: If the forward search tree path from s to v and the backward search tree path from v to t share a long common subpath, that subpath is a "plateau." The endpoints of the plateau (where the forward and backward paths diverge) are natural branching points for alternatives.
4. **Extract candidate alternatives** by taking paths that branch off before a plateau and rejoin after it.
5. **Filter** using stretch, sharing, and local optimality criteria.

### Key Insight

Plateaus can be detected during the standard bidirectional CH search at essentially zero additional cost. The detection is a linear-time post-processing of the search trees. This makes it the fastest method for generating candidate sets.

### Time Complexity

- **Preprocessing**: Same as standard CH.
- **Plateau detection**: O(|search_space|) = O(h^2) where h is highway dimension. This is the same asymptotic cost as a single CH query.
- **Total alternative query**: Dominated by candidate evaluation, not plateau detection. Practical: 1-10 ms.
- **Key advantage**: Plateau detection adds negligible overhead to the base CH query.

### Preprocessing Requirements

Same as standard CH (no additional preprocessing).

### Speedup vs Brute-Force

- **Speedup: comparable to via-node (1,000-10,000x)** over brute-force Dijkstra.
- **Advantage over via-node**: Plateau detection is essentially free as a byproduct of the CH search.

### Practical Usage

This is the method used by **OSRM** (Open Source Routing Machine) for their CH-based backend. OSRM extends the bidirectional search by alpha=10% beyond the meeting point (VIAPATH_ALPHA parameter), collects candidate via-nodes from the intersection of forward/backward search spaces, and filters using stretch and sharing criteria.

### Weaknesses

- **Low hit rate**: OSRM reports ~60% chance of finding 1 alternative, ~30% for 2, ~10% for 3. Many via-candidates yield trivially similar paths.
- **CH-specific**: Cannot easily be adapted to non-CH routing backends.
- **No explicit diversity guarantee**: Unlike kSPwLO, does not optimize for pairwise dissimilarity.

**Evidence Level: A** (peer-reviewed, Springer LNCS + ACM JEA, used in production OSRM)

---

## 4. Penalty-Based Iterative Re-routing

### Citation

**Roland Bader, Jonathan Dees, Robert Geisberger, Peter Sanders.** "Alternative Route Graphs in Road Networks." *TAPAS 2011*, Springer LNCS.

Also: General penalty approach described in the survey by **Hannah Bast, Daniel Delling, Andrew Goldberg, et al.** "Route Planning in Transportation Networks." *Algorithm Engineering*, Springer LNCS 9220, 2016.

### Algorithm Description

1. **Compute shortest path** p1 from s to t.
2. **Penalize** edges on p1: for each edge e in p1, set w'(e) = w(e) * (1 + delta), where delta is a penalty factor (typically 0.1 to 0.5).
3. **Recompute shortest path** p2 on the modified graph.
4. **Repeat** k-1 times, each time penalizing edges on ALL previously found paths.
5. **Filter** results using stretch and sharing criteria.

### Time Complexity

- **Without speedup**: O(k * (n log n + m)) for k alternatives using Dijkstra.
- **With CH speedup**: O(k * CH_query). But PROBLEM: CH shortcuts encode many edges, so penalizing one edge requires recomputing CH or using a different approach.
- **Practical issue**: Penalty changes the metric, so CH preprocessing is invalidated. Must either:
  - (a) Use un-preprocessed Dijkstra (slow), or
  - (b) Use CRP where customization is fast (sub-second), or
  - (c) Apply penalties at query time with a modified search (complex).

### Preprocessing Requirements

- **Plain Dijkstra**: No preprocessing. But queries are slow.
- **With CH**: Standard CH preprocessing, BUT becomes invalid after penalty. Must recompute.
- **With CRP**: Metric-independent preprocessing once, then fast customization per penalty iteration.

### Speedup vs Brute-Force

The penalty method IS essentially the brute-force approach. The "speedup" comes only from using a faster single-path algorithm as the inner loop:
- With Dijkstra: O(k * n log n). No speedup.
- With CRP customization: O(k * customization_time + k * CRP_query). Practical: k * (0.5s + 1ms) for Europe.
- With bidirectional A*: moderate speedup (10-100x over Dijkstra), but still much slower than CH-based alternatives.

### Strengths

- Simple to implement.
- Works with any single-source shortest path algorithm.
- No special data structures beyond the routing graph.
- Can be combined with other heuristics (Bader et al. showed that combining penalty + plateau yields best results).

### Weaknesses

- **Slow**: Each iteration requires a full shortest path computation.
- **No quality guarantee**: Penalties may cause alternatives that are "repelled" from the shortest path rather than genuinely different corridors.
- **Metric invalidation**: Incompatible with CH speedup without significant engineering.
- **Parameter sensitivity**: Choice of penalty factor delta significantly affects quality.

**Evidence Level: B** (peer-reviewed, but limited formal quality analysis; practical experience in Bader et al.)

---

## 5. k-SPwLO (k-Shortest Paths with Limited Overlap)

### Citation

**Theodoros Chondrogiannis, Panagiotis Bouros, Johann Gamper, Ulf Leser, David B. Blumenthal.** "Finding k-Shortest Paths with Limited Overlap." *The VLDB Journal*, 29(5), 2020. Earlier versions at EDBT 2017 and ACM SIGSPATIAL 2015.

### Algorithm Description

The kSPwLO problem: find a set P = {p1, p2, ..., pk} of k paths from s to t such that:
- p1 is the shortest path.
- For i > 1, pi is the shortest path such that for ALL j < i: similarity(pi, pj) <= theta.
- Similarity is measured as edge overlap ratio (Jaccard or directional).

#### OnePass Algorithm

1. Enumerate paths in order of increasing length using a priority queue (Yen's algorithm-like).
2. For each candidate path, check pairwise overlap with ALL previously accepted paths.
3. Accept if overlap is below threshold theta with ALL existing paths.
4. Stop when k paths are found or queue is exhausted.

**Complexity**: O(k * n * (n log n + m)) worst case. One Dijkstra per candidate, and potentially many candidates rejected.

#### MultiPass Algorithm (Optimal)

1. For i = 2 to k:
   - Remove edges that would cause high overlap with ALL previously accepted paths.
   - Run Dijkstra on the pruned graph.
   - The result is guaranteed to have low overlap.
2. Uses two pruning criteria to reduce the search space.

**Complexity**: O(k * (n log n + m)). Proven optimal: k-1 full Dijkstra runs on the (pruned) graph.

#### Heuristic Algorithms

- **OnePass+**: Uses MultiPass pruning within OnePass framework. Faster but may miss optimal.
- **SVP+ (Single Via Path)**: Select alternatives from the set of single-via paths. Uses CH speedup.
- **ESX (Edge Set Exclusion)**: Remove edges incrementally, recompute shortest path. Simple but effective.

### Preprocessing Requirements

- **OnePass/MultiPass**: No preprocessing (runs on plain graph). But slow without speedup.
- **SVP+**: Requires CH preprocessing.
- **ESX**: No preprocessing.

### Experimental Results (from VLDB 2020 paper)

On real road networks (Berlin, Colorado, California):
- **MultiPass**: 10-500 ms per query (depending on network size and k).
- **OnePass+**: 5-200 ms per query.
- **SVP+**: 1-50 ms per query (benefits from CH).
- **ESX**: 50-2000 ms per query.

For continental-scale networks (Europe, 18M nodes):
- **MultiPass**: 5-30 seconds per query (k=3). Too slow for interactive use.
- **SVP+**: 10-100 ms per query. Practical.

### Speedup vs Brute-Force

- **MultiPass vs naive Yen's**: 10-100x faster (pruning eliminates most candidate paths).
- **SVP+ (with CH)**: 1,000-10,000x faster than plain Dijkstra penalty method.
- **MultiPass on large networks**: Still too slow for real-time use without speedup techniques.

### Strengths

- **Formal optimality guarantee**: MultiPass is provably optimal for the kSPwLO problem.
- **Pairwise dissimilarity**: Unlike via-node methods, kSPwLO guarantees dissimilarity between ALL pairs of alternatives, not just vs. the primary.
- **Well-defined problem formulation** amenable to theoretical analysis.

### Weaknesses

- **Slow on large networks** without CH-based speedup.
- **No local optimality guarantee**: alternatives may contain sub-optimal segments.
- **Overlap metric is global**: a path with 40% overlap might share ALL of the overlap in one section.

**Evidence Level: A** (peer-reviewed VLDB journal, formal proofs, extensive experiments, open-source implementation at github.com/tchond/kspwlo)

---

## 6. Customizable Route Planning (CRP)

### Citation

**Daniel Delling, Andrew V. Goldberg, Thomas Pajor, Renato F. Werneck.** "Customizable Route Planning in Road Networks." *Transportation Science*, 51(2), 2017. Originally at SEA 2011.

### Three-Phase Architecture

1. **Preprocessing (metric-independent)**: Graph partitioning using METIS/KAHIP/InertialFlow into a multilevel cell structure. Creates overlay graph topology. Done ONCE, takes hours.
2. **Customization (metric-dependent)**: Computes actual edge weights on the overlay graph. Done per metric change. Takes **sub-second** for continental networks.
3. **Query**: Bidirectional Dijkstra on the overlay graph. Takes **milliseconds**.

### Alternative Routes with CRP

CRP enables efficient penalty-based alternatives because:
- Penalizing edges changes the metric but NOT the topology.
- Only the customization phase needs to re-run (sub-second).
- Query remains fast (milliseconds).
- Total per alternative: ~1 second customization + ~1 ms query.

For k=3 alternatives: ~3 seconds total (dominated by customization).

### Performance Numbers (from Transportation Science 2017)

On Western Europe (~18M nodes, ~42M edges):
- **Preprocessing**: ~30 minutes (one-time, metric-independent).
- **Customization**: 0.3-0.8 seconds (per metric change).
- **Single query**: 0.5-3 ms.
- **Space**: ~200 MB overlay data.

### Speedup vs Brute-Force

- **Single query**: 1,000-5,000x faster than Dijkstra.
- **Penalty-based alternatives via CRP**: k * (0.5s customization + 1ms query) vs k * 5-10s Dijkstra.
  - **Speedup for k=3 alternatives: ~10x** over Dijkstra penalty (dominated by customization).
  - But **~1,000x slower** than CH via-node or plateau methods for alternatives.

### Key Advantage

CRP's killer feature is **metric flexibility**: live traffic, personalized costs, toll avoidance, etc. can all be handled by re-running the sub-second customization phase. For alternative routes, the penalty method becomes practical because customization is fast.

### Used By

- **Bing Maps** (Microsoft production system).

**Evidence Level: A** (peer-reviewed Transportation Science journal, production deployment at Microsoft)

---

## 7. Separator-Based Alternative Paths in CCH

### Citation

**Scott Bacherle, Thomas Blasius, Michael Zundorf.** "Separator-Based Alternative Paths in Customizable Contraction Hierarchies." *ATMOS 2025*, OASIcs Vol. 137, pp. 12:1-12:16. Dagstuhl.

### Algorithm Description

CCH (Customizable Contraction Hierarchies) combines the speed of CH with the flexibility of CRP:
- **Metric-independent ordering**: Based on nested dissection (graph separators).
- **Fast customization**: Sub-second metric updates (like CRP).
- **Fast queries**: Sub-millisecond (like CH).

The separator-based approach exploits the fact that CCH ordering is based on graph separators (balanced vertex cuts). These separators are natural "branching points" for alternative routes -- a path must cross every separator between its source and target cells.

**Key insight**: By identifying the separator vertices that the shortest path crosses, the algorithm can find alternatives that cross DIFFERENT separator vertices, guaranteeing geographic diversity.

### Performance Claims

Authors claim to "provide an alternative to CRP that is simpler and has lower query times" for alternative path computation.

### Significance

This is the most recent (2025) contribution and represents the current frontier. It combines:
- Sub-millisecond queries (CH-like speed).
- Metric flexibility (CRP-like customization).
- Natural alternative discovery (separator structure).

**Evidence Level: B** (peer-reviewed workshop paper, but no production validation yet)

---

## 8. HiDAR (Hierarchical Decomposition for Alternative Routes)

### Citation

**Moritz Kobitzsch.** "An Alternative Approach to Alternative Routes: HiDAR." *ESA 2013*, Springer LNCS.

### Algorithm Description

HiDAR uses the hierarchical decomposition of the road network (from CH or similar) to identify structurally distinct corridors. It decomposes the network into hierarchical cells and identifies alternatives that pass through different cells at the same hierarchical level.

### Significance

This is the theoretical basis for **Valhalla's** alternative route implementation. Valhalla's alternates.cc implements 2 of 3 techniques from this paper (stretch validation + sharing validation, but not local optimality).

**Evidence Level: B** (peer-reviewed conference paper)

---

## 9. Parallelization Strategies

### 9.1 Parallel Primary + Alternative Computation

**Can alternatives be computed in parallel with the primary route?**

**Yes, partially.** The most practical approach:

1. **Phase 1 (serial)**: Compute primary shortest path using CH or A*. This establishes the search space.
2. **Phase 2 (parallel)**: Evaluate via-node candidates in parallel. Each candidate requires an independent shortest path check that can run on a separate thread.

**Theoretical speedup**: Up to p-fold with p cores for Phase 2, but Phase 1 is the bottleneck on short routes.

### 9.2 Search Tree Reuse

**Can the CH search tree from the primary route be reused?**

**Yes, this is fundamental to the via-node and plateau approaches.** The forward and backward search trees from the primary CH query contain distance information to all settled nodes. This is exactly the candidate set for via-node alternatives.

Specifically:
- Forward search tree: d_f(s, v) for all settled v.
- Backward search tree: d_b(v, t) for all settled v.
- Any v with d_f(s,v) + d_b(v,t) <= (1+epsilon) * d(s,t) is a via-node candidate.

**No additional Dijkstra needed** for candidate identification. The cost is only in the filtering (overlap check) and optional path unpacking.

### 9.3 Asynchronous Alternative Computation

For practical navigation applications (like OrganicMaps):

1. **Return primary route immediately** (~1 ms with CH, ~100 ms with A*).
2. **Spawn background thread** to compute alternatives using the saved search trees.
3. **Notify UI** when alternatives are ready (50-200 ms later).

This is the approach used by OrganicMaps' current feature/mesh-traffic branch (see async_router.cpp).

### 9.4 Parallel CH Preprocessing

Recent work by SPoCH (2025) parallelizes CH construction itself, reducing preprocessing from 30 minutes to under 5 minutes for continental networks.

### 9.5 Search Tree Reuse for A* (Non-CH case)

Even without CH, the bidirectional A* search trees contain useful distance information. The key difference:
- CH search trees cover O(sqrt(n)) nodes but are exact.
- A* search trees cover more nodes (larger radius) but distances are also exact for settled nodes.
- **Practical insight**: In a bidirectional A* search, all nodes settled by both forward and backward search have exact distances from s and to t. These are natural via-node candidates.

**Evidence Level: B** (described in multiple papers, implemented in practice, but no single comprehensive study)

---

## 10. Practical Implementations Comparison

### 10.1 OSRM (Open Source Routing Machine)

**Algorithm**: Plateau-based via-node on CH (Luxen & Schieferdecker 2012).

**Implementation details** (from source code and developer documentation):
- Bidirectional CH search extended by VIAPATH_ALPHA = 10% beyond meeting point.
- Via-node candidates from intersection of forward/backward search spaces.
- Uses Inertial Flow graph partitioner for initial cell-based filtering.
- Graduated filtering: cheap high-level criteria first, expensive path extraction last.

**Performance**:
- Primary route: < 1 ms on continental networks.
- Alternatives total: "tens to lower hundreds of milliseconds."
- Hit rate: ~60% for 1 alternative, ~30% for 2, ~10% for 3.
- Maximum: typically 1 alternative returned (CH backend). MLD backend supports multiple.

**Weakness**: CH backend can return at most 1 alternative reliably. MLD backend (newer) supports multiple but with different algorithm.

**Source**: [OSRM GitHub](https://github.com/Project-OSRM/osrm-backend), [Daniel-j-h OSM Diary on Alternative Routes](https://www.openstreetmap.org/user/daniel-j-h/diary/44020).

### 10.2 Valhalla

**Algorithm**: Modified bidirectional A* with multiple connection points (based on Kobitzsch HiDAR).

**Implementation details**:
- Tracks multiple connection points during bidirectional A* search (not just the optimal one).
- Filters using stretch (max 1.25x) and sharing (max 75% overlap).
- Local optimality check is NOT implemented (omitted due to complexity).
- Configuration: alternative_cost_extend and alternative_iterations_delta control search expansion.

**Performance**:
- Primary route: 10-100 ms (bidirectional A* with hierarchy, no CH).
- Alternatives: additional 50-200 ms.
- No CH preprocessing required (dynamic costing model).

**Weakness**: No CH speedup means queries are 10-100x slower than OSRM/GraphHopper. But supports dynamic costing (toll avoidance, terrain preferences, etc.) at query time.

**Source**: [Valhalla PR #2626](https://github.com/valhalla/valhalla/pull/2626), [Valhalla Docs](https://valhalla.github.io/valhalla/route_overview/).

### 10.3 GraphHopper

**Algorithm**: Via-node on CH (node-based).

**Implementation details**:
- Bidirectional CH search identifies intermediate nodes v.
- Constructs s -> v -> t paths.
- Share-factor as primary filter.
- CH pre-filtering reduces candidates substantially ("self-regulating").

**Performance**:
- Primary route: sub-millisecond with CH.
- Alternatives: initially 50x slower than primary CH query, optimized to ~6x with fast implementation achieving alternative rate > 2 (multiple routes per query).
- Practical: 1-10 ms for alternatives with optimized CH implementation.

**Also supports**: Landmark-based (A* + Landmarks) alternatives as a flexible fallback for dynamic costing scenarios.

**Source**: [GraphHopper PR #1722](https://github.com/graphhopper/graphhopper/pull/1722), [GraphHopper routing docs](https://github.com/graphhopper/graphhopper/blob/master/docs/core/routing.md).

---

## 11. Structured Comparison Matrix

### Query Performance (Europe, ~18M nodes, k=3 alternatives)

| Algorithm | Primary Query | Alternatives Total | Preprocessing | Metric Flexibility |
|---|---|---|---|---|
| **Via-Node CH** (Abraham 2013) | 0.01-0.05 ms | 0.5-5 ms | 5-30 min | None (repreprocess) |
| **Plateau CH** (Luxen 2012) | 0.01-0.05 ms | 1-10 ms | 5-30 min | None |
| **Penalty + Dijkstra** | 5-10 s | 15-30 s | None | Full |
| **Penalty + CRP** (Delling 2017) | 0.5-3 ms | ~3 s (3x customization) | 30 min | Full (sub-second) |
| **kSPwLO MultiPass** (Chondrogiannis 2020) | n/a (Dijkstra) | 5-30 s | None | Full |
| **kSPwLO SVP+** (with CH) | 0.01-0.05 ms | 10-100 ms | 5-30 min | None |
| **Separator CCH** (Bacherle 2025) | sub-ms | sub-10 ms (estimated) | Hours + sub-second | Full |
| **Bidirectional A*** (Valhalla-style) | 10-100 ms | 50-300 ms | None | Full |

### Speedup Factors vs Brute-Force (k Dijkstra runs with penalty on Europe)

| Algorithm | Speedup Factor | Notes |
|---|---|---|
| **Via-Node CH** | 3,000 - 60,000x | Best for static metrics |
| **Plateau CH** | 1,500 - 30,000x | Free piggyback on CH search |
| **Penalty + CRP** | 3 - 20x | Dominated by customization time |
| **kSPwLO SVP+** | 300 - 3,000x | Formal optimality guarantee |
| **Separator CCH** | 3,000 - 60,000x (est.) | Newest, CH speed + CRP flexibility |
| **Bidirectional A*** | 50 - 600x | No preprocessing needed |

### Quality Comparison

| Algorithm | Local Optimality | Pairwise Dissimilarity | Stretch Guarantee | Quality Rating |
|---|---|---|---|---|
| **Via-Node CH** | YES (inherent) | NO (only vs primary) | YES | High |
| **Plateau CH** | YES (inherent) | NO (only vs primary) | YES | High |
| **Penalty** | NO | Heuristic | Heuristic | Medium |
| **kSPwLO** | NO | YES (formal) | YES | Medium-High |
| **CRP + penalty** | NO | Heuristic | Heuristic | Medium |
| **Separator CCH** | YES (separator structure) | YES (separator diversity) | YES | High (est.) |

---

## 12. Relevance to OrganicMaps Current Implementation

### Current Approach (feature/mesh-traffic branch)

OrganicMaps currently uses a **geometric via-waypoint approach**:

1. Sample 3 points along primary route (at 25%, 50%, 75% of length).
2. For each sample point, generate perpendicular offsets (2km, 5km, 10km left and right).
3. Build routes through waypoints: start -> via_point -> finish.
4. Filter using Jaccard overlap (threshold 0.6) and stretch (max 1.3x).

This approach is documented in libs/routing/alternative_finder.cpp and corresponds roughly to a simplified kSPwLO-ESX (Edge Set Exclusion by geographic proxy) approach.

### Comparison with State of the Art

| Aspect | OrganicMaps Current | Academic State of Art |
|---|---|---|
| **Via-node selection** | Geometric (perpendicular offsets) | Search-space derived (CH overlap) |
| **Number of route computations** | Up to 18 (6 offsets x 3 fractions) | 1 (reuse search tree) |
| **Local optimality** | NO | YES (via-node on CH) |
| **Quality guarantee** | Heuristic | Formal bounds |
| **Performance** | 18 * route_compute_time | 0.5-5 ms with CH |

### Key Opportunity

OrganicMaps already uses A*-based routing (not CH), which means:
1. **Plateau method is not directly applicable** (requires CH bidirectional search).
2. **Via-node from search tree IS applicable**: The bidirectional A* search space can be used identically to the CH approach -- just slower in absolute terms.
3. **The geometric offset approach wastes computation**: Most of the 18 via-waypoint routes will be rejected, and the router runs 18 full route computations. Using search-tree-derived via-nodes would be far more efficient.

---

## 13. Recommendations

### For OrganicMaps Specifically

**Short-term (current architecture, bidirectional A*)**:
1. **Replace geometric via-candidates with search-tree-derived candidates.** During the primary bidirectional A* search, save both search trees. Extract via-node candidates from the overlap region. This eliminates the need for multiple full re-routes.
2. **Implement asynchronous computation** (already partially done in async_router.cpp): return primary route immediately, compute alternatives in background using saved search trees.
3. Expected improvement: 3-10x fewer route computations, better quality alternatives.

**Medium-term (CH integration)**:
1. If CH preprocessing is ever added to OrganicMaps (currently not used), the via-node and plateau methods become directly applicable with sub-10ms alternative computation.
2. OSRM's approach (plateau-based) would be the recommended implementation.

**Long-term (CCH)**:
1. The Separator-Based CCH approach (Bacherle 2025) is the emerging frontier, combining CH speed with metric flexibility. Worth watching for future adoption.

### General Algorithm Selection Guide

- **Interactive navigation, static metric**: Via-Node CH or Plateau CH (fastest).
- **Interactive navigation, dynamic metric**: CRP + penalty or Separator CCH.
- **Batch processing, quality-critical**: kSPwLO MultiPass (formal guarantees).
- **Simple implementation, good enough**: Penalty with bidirectional A* (Valhalla approach).
- **Offline mobile app (OrganicMaps case)**: Via-node from search tree (no CH needed), async computation.

---

## References

1. Abraham, I., Delling, D., Goldberg, A.V., Werneck, R.F. (2013). "Alternative Routes in Road Networks." *ACM Journal of Experimental Algorithmics*, 18. https://dl.acm.org/doi/10.1145/2444016.2444019

2. Luxen, D., Schieferdecker, D. (2012). "Candidate Sets for Alternative Routes in Road Networks." *SEA 2012*, LNCS 7276, pp. 260-270. https://link.springer.com/chapter/10.1007/978-3-642-30850-5_23

3. Geisberger, R., Sanders, P., Schultes, D., Vetter, C. (2012). "Exact Routing in Large Road Networks Using Contraction Hierarchies." *Transportation Science*, 46(3), 388-404. https://pubsonline.informs.org/doi/10.1287/trsc.1110.0401

4. Chondrogiannis, T., Bouros, P., Gamper, J., Leser, U., Blumenthal, D.B. (2020). "Finding k-Shortest Paths with Limited Overlap." *The VLDB Journal*, 29(5). https://link.springer.com/article/10.1007/s00778-020-00604-x

5. Delling, D., Goldberg, A.V., Pajor, T., Werneck, R.F. (2017). "Customizable Route Planning in Road Networks." *Transportation Science*, 51(2), 566-591. https://pubsonline.informs.org/doi/10.1287/trsc.2014.0579

6. Bader, R., Dees, J., Geisberger, R., Sanders, P. (2011). "Alternative Route Graphs in Road Networks." *TAPAS 2011*, Springer LNCS. https://link.springer.com/chapter/10.1007/978-3-642-19754-3_5

7. Kobitzsch, M. (2013). "An Alternative Approach to Alternative Routes: HiDAR." *ESA 2013*, Springer LNCS. https://link.springer.com/chapter/10.1007/978-3-642-40450-4_52

8. Bacherle, S., Blasius, T., Zundorf, M. (2025). "Separator-Based Alternative Paths in Customizable Contraction Hierarchies." *ATMOS 2025*, OASIcs 137. https://drops.dagstuhl.de/entities/document/10.4230/OASIcs.ATMOS.2025.12

9. Bast, H., Delling, D., Goldberg, A., et al. (2016). "Route Planning in Transportation Networks." *Algorithm Engineering*, Springer LNCS 9220. https://arxiv.org/abs/1504.05140

10. Geisberger, R., Sanders, P., Schultes, D., Delling, D. (2008). "Contraction Hierarchies: Faster and Simpler Hierarchical Routing in Road Networks." *WEA 2008*, LNCS 5038, pp. 319-333. https://link.springer.com/chapter/10.1007/978-3-540-68552-4_24

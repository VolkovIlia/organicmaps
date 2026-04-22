# Phase 3: CCH Preprocessing - Detailed Design

**Version**: 1.0
**Date**: 2026-02-03
**Branch**: `feature/routing-cch`

---

## 1. Overview

Phase 3 implements Customizable Contraction Hierarchies (CCH) for 10-50x faster route queries on long-distance routes. Unlike standard Contraction Hierarchies (CH), CCH separates topology preprocessing from metric customization, enabling profile changes without full rebuilds.

### 1.1 Performance Targets

| Metric | Current (A*) | Target (CCH) |
|--------|--------------|--------------|
| 500km route query | 200-500ms | < 10ms |
| Profile switch | N/A | < 60s customization |
| Memory overhead | 0 | < 2x graph size |

### 1.2 Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           GENERATOR (Offline)                                │
│                                                                              │
│  ┌─────────────────┐    ┌─────────────────┐    ┌──────────────────────────┐│
│  │  Road Graph     │───▶│  CCH Topology   │───▶│  MWM CCH_TOPOLOGY       ││
│  │  from MWM       │    │  Builder        │    │  Section                 ││
│  └─────────────────┘    └─────────────────┘    └──────────────────────────┘│
│                                                                              │
│  Preprocessing: Node ordering → Shortcut computation → Level assignment    │
│  One-time cost: ~10 minutes per country                                    │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      │ MWM file with CCH section
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            RUNTIME (Mobile)                                  │
│                                                                              │
│  ┌─────────────────┐    ┌─────────────────┐    ┌──────────────────────────┐│
│  │  CCH Topology   │───▶│  CCH Customizer │───▶│  CCH Query Engine       ││
│  │  (from MWM)     │    │  (apply profile)│    │  (bidirectional Dijkstra)││
│  └─────────────────┘    └─────────────────┘    └──────────────────────────┘│
│                                                                              │
│  First query: Load topology → Customize (30-60s) → Query (~5ms)            │
│  Subsequent: Query (~5ms)                                                   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Data Structures

### 2.1 CCH Topology (Metric-Independent)

```cpp
// cch_topology.hpp
#pragma once

#include "coding/reader.hpp"
#include "coding/writer.hpp"

#include <cstdint>
#include <vector>

namespace routing
{

/// @brief Version for CCH format compatibility
constexpr uint32_t kCCHVersion = 1;

/// @brief Node ordering information
struct CCHNodeOrder
{
  uint32_t originalId;    ///< Original node ID in road graph
  uint32_t contractedId;  ///< Position in contraction order
  uint32_t level;         ///< Contraction level (higher = contracted later)

  void Serialize(Writer & writer) const;
  void Deserialize(Reader & reader);
};

/// @brief Shortcut edge created during contraction
struct CCHShortcut
{
  uint32_t fromNode;      ///< Source node (contracted ID)
  uint32_t toNode;        ///< Target node (contracted ID)
  uint32_t middleNode;    ///< Node contracted to create this shortcut
  bool isForward : 1;     ///< Direction flag
  bool isBackward : 1;    ///< Bidirectional flag

  void Serialize(Writer & writer) const;
  void Deserialize(Reader & reader);
};

/// @brief Original edge in the road graph
struct CCHOriginalEdge
{
  uint32_t fromNode;      ///< Source node (contracted ID)
  uint32_t toNode;        ///< Target node (contracted ID)
  uint32_t featureId;     ///< Feature ID for weight lookup
  uint16_t segmentIdx;    ///< Segment index within feature
  bool isForward : 1;     ///< Direction flag

  void Serialize(Writer & writer) const;
  void Deserialize(Reader & reader);
};

/// @brief CCH topology stored in MWM file
class CCHTopology
{
public:
  /// @brief Check if topology is valid
  bool IsValid() const { return m_version == kCCHVersion && m_nodeCount > 0; }

  /// @brief Get statistics
  uint64_t GetNodeCount() const { return m_nodeCount; }
  uint64_t GetOriginalEdgeCount() const { return m_originalEdges.size(); }
  uint64_t GetShortcutCount() const { return m_shortcuts.size(); }
  uint32_t GetLevelCount() const { return m_levelCount; }

  /// @brief Get node level (for query ordering)
  uint32_t GetLevel(uint32_t contractedId) const;

  /// @brief Get original ID from contracted ID
  uint32_t GetOriginalId(uint32_t contractedId) const;

  /// @brief Get contracted ID from original ID
  uint32_t GetContractedId(uint32_t originalId) const;

  /// @brief Get outgoing edges for a node (both original and shortcuts)
  struct EdgeRange
  {
    uint32_t originalBegin, originalEnd;
    uint32_t shortcutBegin, shortcutEnd;
  };
  EdgeRange GetOutgoingEdges(uint32_t contractedId) const;
  EdgeRange GetIncomingEdges(uint32_t contractedId) const;

  /// @brief Access edges
  CCHOriginalEdge const & GetOriginalEdge(uint32_t idx) const
  {
    return m_originalEdges[idx];
  }
  CCHShortcut const & GetShortcut(uint32_t idx) const
  {
    return m_shortcuts[idx];
  }

  /// @brief Serialization
  void Serialize(Writer & writer) const;
  void Deserialize(Reader & reader);

  /// @brief Memory-mapped loading (preferred for mobile)
  bool LoadFromMappedMemory(void const * data, size_t size);

private:
  uint32_t m_version = kCCHVersion;
  uint64_t m_nodeCount = 0;
  uint32_t m_levelCount = 0;

  std::vector<CCHNodeOrder> m_nodeOrder;
  std::vector<CCHOriginalEdge> m_originalEdges;
  std::vector<CCHShortcut> m_shortcuts;

  // Adjacency list offsets for efficient traversal
  std::vector<uint32_t> m_outgoingOriginalOffsets;
  std::vector<uint32_t> m_outgoingShortcutOffsets;
  std::vector<uint32_t> m_incomingOriginalOffsets;
  std::vector<uint32_t> m_incomingShortcutOffsets;

  // Lookup tables
  std::vector<uint32_t> m_originalToContracted;
  std::vector<uint32_t> m_contractedToOriginal;
};

}  // namespace routing
```

### 2.2 CCH Customizer

```cpp
// cch_customizer.hpp
#pragma once

#include "routing/cch_topology.hpp"
#include "routing/vehicle_model.hpp"
#include "routing/edge_estimator.hpp"

namespace routing
{

/// @brief Routing profile for CCH customization
enum class CCHProfile
{
  Car,
  Bicycle,
  Pedestrian
};

/// @brief Configuration for CCH customization
struct CCHCustomizationConfig
{
  CCHProfile profile = CCHProfile::Car;
  bool avoidTolls = false;
  bool avoidHighways = false;
  bool avoidFerries = false;

  bool operator==(CCHCustomizationConfig const & other) const;
};

/// @brief Customizes CCH topology with profile-specific weights
class CCHCustomizer
{
public:
  explicit CCHCustomizer(CCHTopology const & topology);

  /// @brief Customize CCH for given profile
  /// @param config Profile configuration
  /// @param vehicleModel Vehicle model for speed lookup
  /// @param estimator Edge estimator for weight calculation
  /// @return True if customization succeeded
  bool Customize(
    CCHCustomizationConfig const & config,
    VehicleModelInterface const & vehicleModel,
    EdgeEstimator const & estimator);

  /// @brief Check if CCH is customized for given config
  bool IsCustomized(CCHCustomizationConfig const & config) const;

  /// @brief Get weight for original edge
  double GetOriginalEdgeWeight(uint32_t edgeIdx) const;

  /// @brief Get weight for shortcut
  double GetShortcutWeight(uint32_t shortcutIdx) const;

  /// @brief Get current profile
  CCHCustomizationConfig const & GetCurrentConfig() const { return m_currentConfig; }

private:
  /// @brief Calculate weight for a single original edge
  double CalcOriginalEdgeWeight(
    CCHOriginalEdge const & edge,
    VehicleModelInterface const & vehicleModel,
    EdgeEstimator const & estimator) const;

  /// @brief Bottom-up weight propagation through contraction levels
  void PropagateWeights();

private:
  CCHTopology const & m_topology;
  CCHCustomizationConfig m_currentConfig;
  bool m_isCustomized = false;

  // Weights indexed by edge/shortcut index
  std::vector<double> m_originalEdgeWeights;
  std::vector<double> m_shortcutWeights;
};

}  // namespace routing
```

### 2.3 CCH Customizer Implementation

```cpp
// cch_customizer.cpp
#include "routing/cch_customizer.hpp"

#include "base/logging.hpp"
#include "base/timer.hpp"

namespace routing
{

CCHCustomizer::CCHCustomizer(CCHTopology const & topology)
  : m_topology(topology)
{
  // Pre-allocate weight vectors
  m_originalEdgeWeights.resize(topology.GetOriginalEdgeCount(), 0.0);
  m_shortcutWeights.resize(topology.GetShortcutCount(), 0.0);
}

bool CCHCustomizer::Customize(
    CCHCustomizationConfig const & config,
    VehicleModelInterface const & vehicleModel,
    EdgeEstimator const & estimator)
{
  if (IsCustomized(config))
    return true;  // Already customized for this profile

  base::Timer timer;
  LOG(LINFO, ("CCH customization started for profile:", static_cast<int>(config.profile)));

  // Step 1: Calculate weights for all original edges
  size_t const edgeCount = m_topology.GetOriginalEdgeCount();
  for (size_t i = 0; i < edgeCount; ++i)
  {
    auto const & edge = m_topology.GetOriginalEdge(i);
    m_originalEdgeWeights[i] = CalcOriginalEdgeWeight(edge, vehicleModel, estimator);
  }

  // Step 2: Bottom-up weight propagation for shortcuts
  PropagateWeights();

  m_currentConfig = config;
  m_isCustomized = true;

  LOG(LINFO, ("CCH customization completed in", timer.ElapsedSeconds(), "seconds"));
  return true;
}

bool CCHCustomizer::IsCustomized(CCHCustomizationConfig const & config) const
{
  return m_isCustomized && m_currentConfig == config;
}

double CCHCustomizer::GetOriginalEdgeWeight(uint32_t edgeIdx) const
{
  ASSERT(m_isCustomized, ());
  ASSERT_LESS(edgeIdx, m_originalEdgeWeights.size(), ());
  return m_originalEdgeWeights[edgeIdx];
}

double CCHCustomizer::GetShortcutWeight(uint32_t shortcutIdx) const
{
  ASSERT(m_isCustomized, ());
  ASSERT_LESS(shortcutIdx, m_shortcutWeights.size(), ());
  return m_shortcutWeights[shortcutIdx];
}

double CCHCustomizer::CalcOriginalEdgeWeight(
    CCHOriginalEdge const & edge,
    VehicleModelInterface const & vehicleModel,
    EdgeEstimator const & estimator) const
{
  // Get road segment info
  Segment const segment(/* mwmId */, edge.featureId, edge.segmentIdx, edge.isForward);

  // Use edge estimator to calculate weight (includes turn costs from Phase 1)
  return estimator.CalcSegmentWeight(segment, vehicleModel);
}

void CCHCustomizer::PropagateWeights()
{
  // Process levels from bottom to top
  // Shortcuts at level L depend only on edges/shortcuts at levels < L

  uint32_t const levelCount = m_topology.GetLevelCount();

  for (uint32_t level = 0; level < levelCount; ++level)
  {
    // Find shortcuts at this level and compute their weights
    for (size_t i = 0; i < m_topology.GetShortcutCount(); ++i)
    {
      auto const & shortcut = m_topology.GetShortcut(i);

      // Shortcut weight = lower path through middle node
      // Need to find the two edges: from->middle and middle->to
      // This is simplified - actual implementation needs edge lookup

      // For now, estimate as sum of constituent edges
      // (In production, we'd look up the actual edges used)
      double weight = 0.0;

      // Get original node level to determine processing order
      uint32_t const middleLevel = m_topology.GetLevel(shortcut.middleNode);

      if (middleLevel < level)
      {
        // Already processed - look up constituent edges
        // weight = GetPathWeight(shortcut.fromNode, shortcut.middleNode)
        //        + GetPathWeight(shortcut.middleNode, shortcut.toNode);
      }

      m_shortcutWeights[i] = weight;
    }
  }
}

bool CCHCustomizationConfig::operator==(CCHCustomizationConfig const & other) const
{
  return profile == other.profile &&
         avoidTolls == other.avoidTolls &&
         avoidHighways == other.avoidHighways &&
         avoidFerries == other.avoidFerries;
}

}  // namespace routing
```

### 2.4 CCH Query Engine

```cpp
// cch_query_engine.hpp
#pragma once

#include "routing/cch_topology.hpp"
#include "routing/cch_customizer.hpp"
#include "routing/routing_result.hpp"

#include <vector>
#include <queue>

namespace routing
{

/// @brief Result of CCH query
struct CCHQueryResult
{
  bool success = false;
  std::string errorMessage;
  double queryTimeMs = 0.0;

  double distanceMeters = 0.0;
  double durationSeconds = 0.0;

  // Path as sequence of segments (for UI display)
  std::vector<Segment> path;

  // For debugging: was fallback used?
  bool usedFallback = false;
};

/// @brief Query request parameters
struct CCHQueryRequest
{
  ms::LatLon origin;
  ms::LatLon destination;
  CCHProfile profile = CCHProfile::Car;
  bool computeAlternatives = false;
  int maxAlternatives = 3;
};

/// @brief Interface for CCH query execution
class ICCHQueryEngine
{
public:
  virtual ~ICCHQueryEngine() = default;

  /// @brief Execute CCH query
  virtual CCHQueryResult Query(CCHQueryRequest const & request) = 0;

  /// @brief Check if CCH is available for given MWM
  virtual bool IsCCHAvailable(NumMwmId mwmId) const = 0;

  /// @brief Customize CCH for profile (must call before Query if profile changed)
  virtual bool Customize(CCHCustomizationConfig const & config) = 0;
};

/// @brief Production CCH query engine using bidirectional Dijkstra
class CCHQueryEngine : public ICCHQueryEngine
{
public:
  CCHQueryEngine(
    CCHTopology const & topology,
    CCHCustomizer & customizer);

  CCHQueryResult Query(CCHQueryRequest const & request) override;
  bool IsCCHAvailable(NumMwmId mwmId) const override;
  bool Customize(CCHCustomizationConfig const & config) override;

private:
  /// @brief Bidirectional Dijkstra on contracted graph
  struct QueryState
  {
    double distance;
    uint32_t node;
    uint32_t parent;
    bool isShortcut;
    uint32_t edgeIdx;

    bool operator>(QueryState const & other) const
    {
      return distance > other.distance;
    }
  };

  using PriorityQueue = std::priority_queue<
    QueryState,
    std::vector<QueryState>,
    std::greater<QueryState>>;

  /// @brief Find path using bidirectional Dijkstra
  bool FindPath(
    uint32_t sourceNode,
    uint32_t targetNode,
    std::vector<QueryState> & forwardTree,
    std::vector<QueryState> & backwardTree,
    uint32_t & meetingNode);

  /// @brief Unpack shortcuts to original segments
  std::vector<Segment> UnpackPath(
    std::vector<QueryState> const & forwardTree,
    std::vector<QueryState> const & backwardTree,
    uint32_t meetingNode);

  /// @brief Recursively unpack a shortcut
  void UnpackShortcut(
    uint32_t shortcutIdx,
    bool isForward,
    std::vector<Segment> & result);

  /// @brief Map GPS position to nearest graph node
  uint32_t MapToGraphNode(ms::LatLon const & position) const;

private:
  CCHTopology const & m_topology;
  CCHCustomizer & m_customizer;

  // Reusable data structures to avoid allocations
  std::vector<double> m_forwardDist;
  std::vector<double> m_backwardDist;
  std::vector<bool> m_forwardVisited;
  std::vector<bool> m_backwardVisited;
};

}  // namespace routing
```

### 2.5 CCH Query Engine Implementation

```cpp
// cch_query_engine.cpp
#include "routing/cch_query_engine.hpp"

#include "base/logging.hpp"
#include "base/timer.hpp"

#include <limits>

namespace routing
{

namespace
{
constexpr double kInfinity = std::numeric_limits<double>::max();
}  // namespace

CCHQueryEngine::CCHQueryEngine(
    CCHTopology const & topology,
    CCHCustomizer & customizer)
  : m_topology(topology)
  , m_customizer(customizer)
{
  size_t const nodeCount = topology.GetNodeCount();
  m_forwardDist.resize(nodeCount, kInfinity);
  m_backwardDist.resize(nodeCount, kInfinity);
  m_forwardVisited.resize(nodeCount, false);
  m_backwardVisited.resize(nodeCount, false);
}

CCHQueryResult CCHQueryEngine::Query(CCHQueryRequest const & request)
{
  CCHQueryResult result;
  base::Timer timer;

  // Step 1: Map origin/destination to graph nodes
  uint32_t const sourceNode = MapToGraphNode(request.origin);
  uint32_t const targetNode = MapToGraphNode(request.destination);

  if (sourceNode == UINT32_MAX || targetNode == UINT32_MAX)
  {
    result.success = false;
    result.errorMessage = "Could not map positions to graph nodes";
    return result;
  }

  // Step 2: Run bidirectional Dijkstra
  std::vector<QueryState> forwardTree, backwardTree;
  uint32_t meetingNode = UINT32_MAX;

  bool const found = FindPath(sourceNode, targetNode,
    forwardTree, backwardTree, meetingNode);

  if (!found)
  {
    result.success = false;
    result.errorMessage = "No path found";
    return result;
  }

  // Step 3: Unpack path
  result.path = UnpackPath(forwardTree, backwardTree, meetingNode);

  // Step 4: Calculate total distance and duration
  result.durationSeconds = m_forwardDist[meetingNode] + m_backwardDist[meetingNode];

  // Distance calculation (simplified - would sum segment lengths)
  result.distanceMeters = result.durationSeconds * 15.0;  // ~54 km/h average

  result.success = true;
  result.queryTimeMs = timer.ElapsedSeconds() * 1000.0;

  LOG(LDEBUG, ("CCH query completed in", result.queryTimeMs, "ms"));

  return result;
}

bool CCHQueryEngine::FindPath(
    uint32_t sourceNode,
    uint32_t targetNode,
    std::vector<QueryState> & forwardTree,
    std::vector<QueryState> & backwardTree,
    uint32_t & meetingNode)
{
  // Reset state
  std::fill(m_forwardDist.begin(), m_forwardDist.end(), kInfinity);
  std::fill(m_backwardDist.begin(), m_backwardDist.end(), kInfinity);
  std::fill(m_forwardVisited.begin(), m_forwardVisited.end(), false);
  std::fill(m_backwardVisited.begin(), m_backwardVisited.end(), false);

  forwardTree.clear();
  backwardTree.clear();

  // Initialize queues
  PriorityQueue forwardQueue, backwardQueue;

  m_forwardDist[sourceNode] = 0.0;
  forwardQueue.push({0.0, sourceNode, UINT32_MAX, false, UINT32_MAX});

  m_backwardDist[targetNode] = 0.0;
  backwardQueue.push({0.0, targetNode, UINT32_MAX, false, UINT32_MAX});

  double bestDist = kInfinity;
  meetingNode = UINT32_MAX;

  // Bidirectional search
  while (!forwardQueue.empty() || !backwardQueue.empty())
  {
    // Forward step
    if (!forwardQueue.empty())
    {
      auto const state = forwardQueue.top();
      forwardQueue.pop();

      if (m_forwardVisited[state.node])
        continue;

      m_forwardVisited[state.node] = true;
      forwardTree.push_back(state);

      // Check if met backward search
      if (m_backwardVisited[state.node])
      {
        double const totalDist = state.distance + m_backwardDist[state.node];
        if (totalDist < bestDist)
        {
          bestDist = totalDist;
          meetingNode = state.node;
        }
      }

      // Relaxation: only go UP in hierarchy (to higher levels)
      uint32_t const nodeLevel = m_topology.GetLevel(state.node);
      auto const edges = m_topology.GetOutgoingEdges(state.node);

      // Original edges
      for (uint32_t i = edges.originalBegin; i < edges.originalEnd; ++i)
      {
        auto const & edge = m_topology.GetOriginalEdge(i);
        uint32_t const targetLevel = m_topology.GetLevel(edge.toNode);

        if (targetLevel >= nodeLevel)  // Only go up
        {
          double const weight = m_customizer.GetOriginalEdgeWeight(i);
          double const newDist = state.distance + weight;

          if (newDist < m_forwardDist[edge.toNode])
          {
            m_forwardDist[edge.toNode] = newDist;
            forwardQueue.push({newDist, edge.toNode, state.node, false, i});
          }
        }
      }

      // Shortcuts
      for (uint32_t i = edges.shortcutBegin; i < edges.shortcutEnd; ++i)
      {
        auto const & shortcut = m_topology.GetShortcut(i);
        uint32_t const targetLevel = m_topology.GetLevel(shortcut.toNode);

        if (targetLevel >= nodeLevel && shortcut.isForward)
        {
          double const weight = m_customizer.GetShortcutWeight(i);
          double const newDist = state.distance + weight;

          if (newDist < m_forwardDist[shortcut.toNode])
          {
            m_forwardDist[shortcut.toNode] = newDist;
            forwardQueue.push({newDist, shortcut.toNode, state.node, true, i});
          }
        }
      }
    }

    // Backward step (symmetric, using incoming edges)
    if (!backwardQueue.empty())
    {
      auto const state = backwardQueue.top();
      backwardQueue.pop();

      if (m_backwardVisited[state.node])
        continue;

      m_backwardVisited[state.node] = true;
      backwardTree.push_back(state);

      // Check if met forward search
      if (m_forwardVisited[state.node])
      {
        double const totalDist = m_forwardDist[state.node] + state.distance;
        if (totalDist < bestDist)
        {
          bestDist = totalDist;
          meetingNode = state.node;
        }
      }

      // Relaxation: only go UP in hierarchy
      uint32_t const nodeLevel = m_topology.GetLevel(state.node);
      auto const edges = m_topology.GetIncomingEdges(state.node);

      // Original edges (reversed)
      for (uint32_t i = edges.originalBegin; i < edges.originalEnd; ++i)
      {
        auto const & edge = m_topology.GetOriginalEdge(i);
        uint32_t const sourceLevel = m_topology.GetLevel(edge.fromNode);

        if (sourceLevel >= nodeLevel)
        {
          double const weight = m_customizer.GetOriginalEdgeWeight(i);
          double const newDist = state.distance + weight;

          if (newDist < m_backwardDist[edge.fromNode])
          {
            m_backwardDist[edge.fromNode] = newDist;
            backwardQueue.push({newDist, edge.fromNode, state.node, false, i});
          }
        }
      }

      // Shortcuts (reversed)
      for (uint32_t i = edges.shortcutBegin; i < edges.shortcutEnd; ++i)
      {
        auto const & shortcut = m_topology.GetShortcut(i);
        uint32_t const sourceLevel = m_topology.GetLevel(shortcut.fromNode);

        if (sourceLevel >= nodeLevel && shortcut.isBackward)
        {
          double const weight = m_customizer.GetShortcutWeight(i);
          double const newDist = state.distance + weight;

          if (newDist < m_backwardDist[shortcut.fromNode])
          {
            m_backwardDist[shortcut.fromNode] = newDist;
            backwardQueue.push({newDist, shortcut.fromNode, state.node, true, i});
          }
        }
      }
    }

    // Early termination
    if (!forwardQueue.empty() && !backwardQueue.empty())
    {
      double const lowerBound = forwardQueue.top().distance +
                                backwardQueue.top().distance;
      if (lowerBound >= bestDist)
        break;
    }
  }

  return meetingNode != UINT32_MAX;
}

std::vector<Segment> CCHQueryEngine::UnpackPath(
    std::vector<QueryState> const & forwardTree,
    std::vector<QueryState> const & backwardTree,
    uint32_t meetingNode)
{
  std::vector<Segment> result;

  // Unpack forward path (reversed, from meeting point to source)
  // ... implementation ...

  // Unpack backward path (from meeting point to target)
  // ... implementation ...

  return result;
}

void CCHQueryEngine::UnpackShortcut(
    uint32_t shortcutIdx,
    bool isForward,
    std::vector<Segment> & result)
{
  auto const & shortcut = m_topology.GetShortcut(shortcutIdx);

  // Recursively unpack: shortcut = path through middleNode
  // Find edges from->middle and middle->to

  // ... recursive unpacking ...
}

uint32_t CCHQueryEngine::MapToGraphNode(ms::LatLon const & position) const
{
  // Map GPS position to nearest road graph node
  // This would use spatial index from MWM

  // Simplified placeholder
  return 0;
}

bool CCHQueryEngine::IsCCHAvailable(NumMwmId mwmId) const
{
  // Check if MWM has CCH_TOPOLOGY section
  // This would query DataSource for section existence
  return true;  // Placeholder
}

bool CCHQueryEngine::Customize(CCHCustomizationConfig const & config)
{
  // Delegate to customizer
  // Would need access to VehicleModel and EdgeEstimator
  return true;  // Placeholder
}

}  // namespace routing
```

---

## 3. Generator Integration (Offline Preprocessing)

### 3.1 CCH Topology Builder

```cpp
// generator/cch_topology_builder.hpp
#pragma once

#include "routing/cch_topology.hpp"
#include "generator/routing_helpers.hpp"

namespace generator
{

/// @brief Configuration for CCH topology building
struct CCHBuildConfig
{
  uint32_t maxLevelCount = 20;           ///< Max contraction levels
  double nodeOrderingAlpha = 0.5;        ///< Balance between edge difference and level
  bool useNestedDissection = true;       ///< Use nested dissection for node ordering

  static CCHBuildConfig Default() { return CCHBuildConfig{}; }
};

/// @brief Builds CCH topology for MWM file
class CCHTopologyBuilder
{
public:
  explicit CCHTopologyBuilder(CCHBuildConfig config = CCHBuildConfig::Default());

  /// @brief Build CCH topology from road graph
  /// @param graph Road graph from MWM
  /// @return Built topology
  routing::CCHTopology Build(RoadGraph const & graph);

private:
  /// @brief Calculate node ordering using nested dissection
  std::vector<uint32_t> ComputeNodeOrdering(RoadGraph const & graph);

  /// @brief Compute shortcuts during contraction
  std::vector<routing::CCHShortcut> ComputeShortcuts(
    RoadGraph const & graph,
    std::vector<uint32_t> const & ordering);

  /// @brief Assign levels to nodes based on contraction order
  std::vector<uint32_t> AssignLevels(std::vector<uint32_t> const & ordering);

private:
  CCHBuildConfig m_config;
};

}  // namespace generator
```

### 3.2 MWM Section Writer

```cpp
// generator/cch_section_writer.hpp
#pragma once

#include "routing/cch_topology.hpp"
#include "coding/file_writer.hpp"

namespace generator
{

/// @brief Section tag for CCH topology
constexpr char kCCHTopologySection[] = "CCH_TOPOLOGY";

/// @brief Writes CCH topology section to MWM file
class CCHSectionWriter
{
public:
  /// @brief Write CCH topology to MWM file
  /// @param mwmPath Path to MWM file
  /// @param topology CCH topology to write
  /// @return True if write succeeded
  static bool Write(std::string const & mwmPath,
                    routing::CCHTopology const & topology);
};

/// @brief Reads CCH topology section from MWM file
class CCHSectionReader
{
public:
  /// @brief Read CCH topology from MWM file
  /// @param mwmPath Path to MWM file
  /// @param topology Output topology
  /// @return True if read succeeded
  static bool Read(std::string const & mwmPath,
                   routing::CCHTopology & topology);

  /// @brief Check if MWM has CCH section
  static bool HasCCHSection(std::string const & mwmPath);
};

}  // namespace generator
```

### 3.3 Generator Pipeline Integration

```cpp
// generator/routing_index_generator.cpp modifications

#include "generator/cch_topology_builder.hpp"
#include "generator/cch_section_writer.hpp"

bool BuildRoutingIndexWithCCH(
    std::string const & mwmPath,
    std::string const & countryName,
    bool buildCCH)
{
  // Existing routing index generation
  // ... existing code ...

  // Phase 3: Build CCH topology
  if (buildCCH)
  {
    LOG(LINFO, ("Building CCH topology for", countryName));

    RoadGraph graph = LoadRoadGraph(mwmPath);

    generator::CCHTopologyBuilder builder;
    routing::CCHTopology topology = builder.Build(graph);

    if (!generator::CCHSectionWriter::Write(mwmPath, topology))
    {
      LOG(LERROR, ("Failed to write CCH section for", countryName));
      return false;
    }

    LOG(LINFO, ("CCH topology built:",
      topology.GetNodeCount(), "nodes,",
      topology.GetShortcutCount(), "shortcuts"));
  }

  return true;
}
```

---

## 4. Runtime Integration

### 4.1 Modified IndexRouter

```cpp
// index_router.hpp additions

#include "routing/cch_topology.hpp"
#include "routing/cch_customizer.hpp"
#include "routing/cch_query_engine.hpp"

class IndexRouter
{
  // ... existing members ...

private:
  // Phase 3: CCH support
  std::unique_ptr<CCHTopology> m_cchTopology;
  std::unique_ptr<CCHCustomizer> m_cchCustomizer;
  std::unique_ptr<ICCHQueryEngine> m_cchQueryEngine;

  bool m_useCCH = true;  // Feature flag

public:
  /// @brief Enable/disable CCH routing
  void SetCCHEnabled(bool enabled);

  /// @brief Check if CCH is available for route
  bool IsCCHAvailable(ms::LatLon const & origin,
                      ms::LatLon const & destination) const;

private:
  /// @brief Load CCH topology for MWM
  bool LoadCCHTopology(NumMwmId mwmId);

  /// @brief Route using CCH (fast path)
  RouterResultCode DoCalculateRouteCCH(
    Checkpoints const & checkpoints,
    Route & route);
};
```

### 4.2 CCH Integration in DoCalculateRoute

```cpp
// index_router.cpp modifications

RouterResultCode IndexRouter::DoCalculateRoute(
    Checkpoints const & checkpoints,
    Route & route)
{
  // Check if CCH is available and enabled
  if (m_useCCH && IsCCHAvailable(checkpoints.GetStart(), checkpoints.GetFinish()))
  {
    RouterResultCode const cchResult = DoCalculateRouteCCH(checkpoints, route);

    if (cchResult == RouterResultCode::NoError)
      return cchResult;

    // CCH failed, fall back to A*
    LOG(LWARNING, ("CCH query failed, falling back to A*"));
  }

  // Existing A* routing
  return DoCalculateRouteAStar(checkpoints, route);
}

RouterResultCode IndexRouter::DoCalculateRouteCCH(
    Checkpoints const & checkpoints,
    Route & route)
{
  // Ensure CCH is loaded
  // ... MWM lookup and CCH loading ...

  // Ensure customization matches current profile
  CCHCustomizationConfig config;
  config.profile = GetCurrentCCHProfile();
  config.avoidTolls = m_avoidTolls;
  config.avoidHighways = m_avoidHighways;

  if (!m_cchCustomizer->IsCustomized(config))
  {
    base::Timer timer;
    LOG(LINFO, ("CCH customization required..."));

    if (!m_cchCustomizer->Customize(config, *m_vehicleModel, *m_estimator))
    {
      return RouterResultCode::InternalError;
    }

    LOG(LINFO, ("CCH customized in", timer.ElapsedSeconds(), "seconds"));
  }

  // Execute CCH query
  CCHQueryRequest request;
  request.origin = mercator::ToLatLon(checkpoints.GetStart());
  request.destination = mercator::ToLatLon(checkpoints.GetFinish());
  request.profile = config.profile;

  CCHQueryResult const result = m_cchQueryEngine->Query(request);

  if (!result.success)
  {
    LOG(LWARNING, ("CCH query failed:", result.errorMessage));
    return RouterResultCode::RouteNotFound;
  }

  // Convert CCH result to Route
  // ... path conversion ...

  LOG(LINFO, ("CCH query completed in", result.queryTimeMs, "ms"));
  return RouterResultCode::NoError;
}

bool IndexRouter::IsCCHAvailable(
    ms::LatLon const & origin,
    ms::LatLon const & destination) const
{
  // Check if both points are in MWMs with CCH support
  // For single-MWM routes, check if MWM has CCH section

  // For cross-MWM routes, CCH requires all involved MWMs to have CCH
  // (Fallback to A* for cross-MWM until Phase 3.5)

  return true;  // Simplified
}
```

---

## 5. Memory Management

### 5.1 Memory-Mapped Loading

```cpp
// cch_topology_mmap.cpp

bool CCHTopology::LoadFromMappedMemory(void const * data, size_t size)
{
  // Memory-mapped loading for mobile devices
  // Avoids loading entire topology into heap

  // Parse header
  auto const * header = static_cast<uint32_t const *>(data);
  m_version = header[0];
  m_nodeCount = header[1];
  m_levelCount = header[2];

  // Validate version
  if (m_version != kCCHVersion)
    return false;

  // Set up pointers to data sections without copying
  // ... memory mapping setup ...

  return true;
}
```

### 5.2 Memory Budget

| Component | Budget | Notes |
|-----------|--------|-------|
| CCH Topology | < 2x graph | Memory-mapped |
| Node ordering | N * 12 bytes | 3 uint32_t per node |
| Shortcuts | S * 16 bytes | ~10x edges typically |
| Customization | E * 8 bytes | Double per edge |
| Query state | 2N * 24 bytes | Forward + backward |

---

## 6. Unit Tests

### 6.1 CCH Correctness Tests

```cpp
// cch_tests.cpp
#include "testing/testing.hpp"
#include "routing/cch_topology.hpp"
#include "routing/cch_customizer.hpp"
#include "routing/cch_query_engine.hpp"

UNIT_TEST(CCH_vs_AStar_Correctness)
{
  // Load test MWM with CCH
  auto const [topology, graph] = LoadTestData();

  CCHCustomizer customizer(topology);
  MockVehicleModel vehicleModel;
  MockEdgeEstimator estimator;

  CCHCustomizationConfig config;
  config.profile = CCHProfile::Car;
  customizer.Customize(config, vehicleModel, estimator);

  CCHQueryEngine cchEngine(topology, customizer);

  // Run 100 random queries
  std::mt19937 rng(42);
  for (int i = 0; i < 100; ++i)
  {
    ms::LatLon const origin = RandomPoint(rng);
    ms::LatLon const destination = RandomPoint(rng);

    // CCH query
    CCHQueryRequest request;
    request.origin = origin;
    request.destination = destination;
    auto const cchResult = cchEngine.Query(request);

    // A* query
    auto const astarResult = RunAStar(graph, origin, destination);

    // Compare results (allow small floating point tolerance)
    if (cchResult.success && astarResult.success)
    {
      TEST_LESS(std::abs(cchResult.durationSeconds - astarResult.durationSeconds),
                0.1, ("Duration mismatch"));
    }
    else
    {
      // Both should fail or succeed
      TEST_EQUAL(cchResult.success, astarResult.success, ());
    }
  }
}

UNIT_TEST(CCH_Customization_ProfileSwitch)
{
  auto const topology = LoadTestTopology();
  CCHCustomizer customizer(topology);
  MockVehicleModel vehicleModel;
  MockEdgeEstimator estimator;

  // Customize for Car
  CCHCustomizationConfig carConfig;
  carConfig.profile = CCHProfile::Car;
  TEST(customizer.Customize(carConfig, vehicleModel, estimator), ());
  TEST(customizer.IsCustomized(carConfig), ());

  // Customize for Bicycle (should invalidate Car)
  CCHCustomizationConfig bikeConfig;
  bikeConfig.profile = CCHProfile::Bicycle;
  TEST(customizer.Customize(bikeConfig, vehicleModel, estimator), ());
  TEST(customizer.IsCustomized(bikeConfig), ());
  TEST(!customizer.IsCustomized(carConfig), ());
}

UNIT_TEST(CCH_QueryEngine_Performance)
{
  auto const topology = LoadLargeTopology();  // 1M+ nodes
  CCHCustomizer customizer(topology);
  // ... setup ...

  CCHQueryEngine engine(topology, customizer);

  // Benchmark queries
  std::vector<double> queryTimes;
  for (int i = 0; i < 100; ++i)
  {
    ms::LatLon const origin = RandomPoint();
    ms::LatLon const destination = RandomPoint();

    CCHQueryRequest request;
    request.origin = origin;
    request.destination = destination;

    auto const result = engine.Query(request);
    if (result.success)
      queryTimes.push_back(result.queryTimeMs);
  }

  // Median should be < 10ms
  std::sort(queryTimes.begin(), queryTimes.end());
  double const median = queryTimes[queryTimes.size() / 2];
  TEST_LESS(median, 10.0, ("Median query time too slow"));
}
```

### 6.2 Serialization Tests

```cpp
UNIT_TEST(CCH_Topology_Serialization)
{
  // Create test topology
  CCHTopology original;
  // ... populate ...

  // Serialize
  std::vector<uint8_t> buffer;
  MemWriter writer(buffer);
  original.Serialize(writer);

  // Deserialize
  CCHTopology restored;
  MemReader reader(buffer.data(), buffer.size());
  restored.Deserialize(reader);

  // Verify
  TEST_EQUAL(original.GetNodeCount(), restored.GetNodeCount(), ());
  TEST_EQUAL(original.GetShortcutCount(), restored.GetShortcutCount(), ());
  // ... more verification ...
}
```

---

## 7. Feature Flag & Rollout

### 7.1 Feature Flag

```cpp
// platform/settings.hpp
namespace settings
{
  extern char const * kCCHRoutingEnabled;
}

// Default: false until CCH MWMs are deployed
```

### 7.2 Rollout Strategy

1. **Alpha**: Generate CCH for test regions (Moscow, Germany)
2. **Beta**: Generate CCH for all regions, enable for 10% of users
3. **RC**: Enable for 50% with A/B comparison
4. **Release**: Full rollout with A* fallback

---

## 8. Acceptance Criteria Verification

| Criterion | Status | Verification |
|-----------|--------|--------------|
| CCH topology stored in MWM | DESIGNED | CCH_TOPOLOGY section |
| Memory < 2x graph size | DESIGNED | Memory-mapped loading |
| Query time < 10ms for 500km | DESIGNED | Bidirectional Dijkstra |
| Customization < 60s | DESIGNED | Bottom-up propagation |
| Backward compatible | DESIGNED | Falls back to A* |
| Profile switching | DESIGNED | CCHCustomizer |
| Correctness vs A* | DESIGNED | Unit tests |

---

## 9. Dependencies

- **Phase 1**: TurnCostModel, RegionalSpeedProfiles (for customization)
- **Existing**: EdgeEstimator, VehicleModel, IndexRouter

## 10. Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `routing/cch_topology.hpp` | CREATE | Topology data structures |
| `routing/cch_topology.cpp` | CREATE | Serialization, memory-map |
| `routing/cch_customizer.hpp` | CREATE | Profile customization |
| `routing/cch_customizer.cpp` | CREATE | Weight propagation |
| `routing/cch_query_engine.hpp` | CREATE | Query interface |
| `routing/cch_query_engine.cpp` | CREATE | Bidirectional Dijkstra |
| `generator/cch_topology_builder.hpp` | CREATE | Offline preprocessing |
| `generator/cch_topology_builder.cpp` | CREATE | Node ordering, shortcuts |
| `generator/cch_section_writer.hpp` | CREATE | MWM section writer |
| `generator/cch_section_writer.cpp` | CREATE | Implementation |
| `generator/routing_index_generator.cpp` | MODIFY | CCH integration |
| `routing/index_router.hpp` | MODIFY | CCH members |
| `routing/index_router.cpp` | MODIFY | CCH routing path |
| `routing_tests/cch_tests.cpp` | CREATE | Unit tests |

---

## 11. References

- [Customizable Contraction Hierarchies](https://arxiv.org/pdf/1402.0402) - Dibbelt et al. (2016)
- ADR-001: Use CCH over CH
- ADR-002: Store CCH topology in MWM
- PRD Phase 3: CCH Preprocessing

// Library Documentation Verified: 2026-02-04
// Source: Internal Organic Maps APIs
// API Version: CCH v1 (kCCHVersion = 1)
#pragma once

#include "routing/cch_topology.hpp"

#include "indexer/feature.hpp"

#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace generator
{

/// @brief Configuration for CCH topology building
struct CCHBuildConfig
{
  uint32_t maxLevelCount = 20;
  double nodeOrderingAlpha = 0.5;
  bool useNestedDissection = true;

  static CCHBuildConfig Default() { return CCHBuildConfig{}; }
};

/// @brief Road graph interface for CCH building
struct CCHRoadGraph
{
  uint64_t nodeCount = 0;
  std::vector<std::pair<uint32_t, uint32_t>> edges;
  std::vector<uint32_t> featureIds;
  std::vector<uint16_t> segmentIndices;
  std::vector<bool> forwardFlags;
};

/// @brief Builds CCH topology for MWM file
class CCHTopologyBuilder
{
public:
  explicit CCHTopologyBuilder(CCHBuildConfig config = CCHBuildConfig::Default());

  /// @brief Build CCH topology from road graph.
  routing::CCHTopology Build(CCHRoadGraph const & graph);

  /// @brief Extract road graph from MWM file.
  static CCHRoadGraph ExtractRoadGraph(std::string const & mwmPath);

private:
  // Node ordering
  std::vector<uint32_t> ComputeNodeOrdering(CCHRoadGraph const & graph);
  std::vector<std::vector<uint32_t>> BuildAdjacencyList(CCHRoadGraph const & graph);
  std::vector<uint32_t> ContractNodes(uint64_t nodeCount,
                                      std::vector<std::vector<uint32_t>> & adjacency);

  using NodePriorityQueue = std::priority_queue<
      std::pair<int64_t, uint32_t>,
      std::vector<std::pair<int64_t, uint32_t>>,
      std::greater<std::pair<int64_t, uint32_t>>>;

  void InitializeNodePriorities(uint64_t nodeCount,
                                std::vector<std::vector<uint32_t>> const & adjacency,
                                NodePriorityQueue & pq);
  void UpdateNeighborPriorities(uint32_t node,
                                std::vector<bool> const & contracted,
                                std::vector<std::vector<uint32_t>> & adjacency,
                                NodePriorityQueue & pq);

  // Shortcuts
  std::vector<routing::CCHShortcut> ComputeShortcuts(
      CCHRoadGraph const & graph, std::vector<uint32_t> const & ordering);
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> BuildEdgeIndexedAdjacency(
      CCHRoadGraph const & graph);
  std::vector<uint32_t> BuildOrderPositionMap(std::vector<uint32_t> const & ordering);
  void CreateShortcutsForNode(
      uint32_t node,
      std::vector<std::vector<std::pair<uint32_t, uint32_t>>> const & adjacency,
      std::vector<bool> const & contracted,
      std::vector<uint32_t> const & orderPosition,
      std::vector<routing::CCHShortcut> & shortcuts);

  // Topology building
  void BuildNodeOrderEntries(CCHRoadGraph const & graph,
                             std::vector<uint32_t> const & ordering,
                             std::vector<uint32_t> const & levels,
                             routing::CCHTopology & topology);
  void BuildOriginalEdges(CCHRoadGraph const & graph,
                          std::vector<uint32_t> const & ordering,
                          routing::CCHTopology & topology);
  uint32_t FindContractedId(uint32_t originalId,
                            std::vector<uint32_t> const & ordering,
                            uint64_t nodeCount) const;

  // Levels
  std::vector<uint32_t> AssignLevels(std::vector<uint32_t> const & ordering);

  // Utilities
  int64_t ComputeEdgeDifference(uint32_t node,
                                std::vector<std::vector<uint32_t>> const & adjacency) const;

  // Road graph extraction helpers
  static void ProcessFeatureForGraph(FeatureType & ft,
                                     uint32_t featureId,
                                     std::unordered_map<uint64_t, uint32_t> & nodeMap,
                                     uint32_t & nextNodeId,
                                     CCHRoadGraph & graph);
  static bool IsRoutableFeature(FeatureType const & ft);
  static void ProcessFeatureSegments(FeatureType & ft,
                                     uint32_t featureId,
                                     std::unordered_map<uint64_t, uint32_t> & nodeMap,
                                     uint32_t & nextNodeId,
                                     CCHRoadGraph & graph);
  static uint32_t GetOrCreateNode(m2::PointD const & point,
                                  std::unordered_map<uint64_t, uint32_t> & nodeMap,
                                  uint32_t & nextNodeId);
  static uint64_t HashPoint(m2::PointD const & p);
  static void AddEdgePair(uint32_t node1, uint32_t node2,
                          uint32_t featureId, uint16_t segmentIdx,
                          CCHRoadGraph & graph);

private:
  CCHBuildConfig m_config;
};

}  // namespace generator

#pragma once

#include "routing/cch_topology.hpp"

#include "indexer/feature.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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

/// @brief Road graph interface for CCH building
struct CCHRoadGraph
{
  uint64_t nodeCount = 0;
  std::vector<std::pair<uint32_t, uint32_t>> edges;  ///< (from, to) pairs
  std::vector<uint32_t> featureIds;                   ///< Feature ID for each edge
  std::vector<uint16_t> segmentIndices;               ///< Segment index for each edge
  std::vector<bool> forwardFlags;                     ///< Direction for each edge
};

/// @brief Builds CCH topology for MWM file
class CCHTopologyBuilder
{
public:
  explicit CCHTopologyBuilder(CCHBuildConfig config = CCHBuildConfig::Default());

  /// @brief Build CCH topology from road graph
  /// @param graph Road graph extracted from MWM
  /// @return Built topology
  routing::CCHTopology Build(CCHRoadGraph const & graph);

  /// @brief Extract road graph from MWM file
  /// @param mwmPath Path to MWM file
  /// @return Extracted road graph
  static CCHRoadGraph ExtractRoadGraph(std::string const & mwmPath);

private:
  /// @brief Calculate node ordering using nested dissection
  std::vector<uint32_t> ComputeNodeOrdering(CCHRoadGraph const & graph);

  /// @brief Compute shortcuts during contraction
  std::vector<routing::CCHShortcut> ComputeShortcuts(
      CCHRoadGraph const & graph,
      std::vector<uint32_t> const & ordering);

  /// @brief Assign levels to nodes based on contraction order
  std::vector<uint32_t> AssignLevels(std::vector<uint32_t> const & ordering);

  /// @brief Contract a single node and create shortcuts
  void ContractNode(uint32_t node,
                    std::vector<std::vector<uint32_t>> & adjacency,
                    std::vector<routing::CCHShortcut> & shortcuts);

  /// @brief Compute edge difference for node ordering heuristic
  int64_t ComputeEdgeDifference(uint32_t node,
                                std::vector<std::vector<uint32_t>> const & adjacency) const;

private:
  CCHBuildConfig m_config;
};

}  // namespace generator

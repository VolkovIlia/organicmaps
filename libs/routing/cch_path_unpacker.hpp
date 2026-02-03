#pragma once

#include "routing/cch_topology.hpp"
#include "routing/segment.hpp"

#include <cstdint>
#include <vector>

namespace routing
{

/// @brief Query state for bidirectional Dijkstra in CCH
struct CCHSearchState
{
  double distance = 0.0;
  uint32_t node = UINT32_MAX;
  uint32_t parent = UINT32_MAX;
  bool isShortcut = false;
  uint32_t edgeIdx = UINT32_MAX;
};

/// @brief Unpacks CCH shortcuts to original road segments
class CCHPathUnpacker
{
public:
  explicit CCHPathUnpacker(CCHTopology const & topology);

  /// @brief Unpack path from search trees
  std::vector<Segment> UnpackPath(
      std::vector<CCHSearchState> const & forwardTree,
      std::vector<CCHSearchState> const & backwardTree,
      uint32_t meetingNode);

private:
  /// @brief Recursively unpack a shortcut to original segments
  void UnpackShortcut(uint32_t shortcutIdx, bool isForward,
                      std::vector<Segment> & result);

  /// @brief Reconstruct path from search tree
  void ReconstructPath(std::vector<CCHSearchState> const & tree,
                       uint32_t startNode, uint32_t endNode,
                       bool reverse,
                       std::vector<Segment> & result);

  /// @brief Find edge between two nodes
  bool FindEdgeToNode(uint32_t fromNode, uint32_t toNode,
                      bool isForward, std::vector<Segment> & result);

private:
  CCHTopology const & m_topology;
};

}  // namespace routing

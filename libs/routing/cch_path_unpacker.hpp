// Library Documentation Verified: 2026-02-04
// Source: Internal Organic Maps APIs
// API Version: CCH v1 (kCCHVersion = 1)
#pragma once

#include "routing/cch_topology.hpp"
#include "routing/segment.hpp"

#include <cstdint>
#include <stack>
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

  /// @brief Unpack path from search trees.
  std::vector<Segment> UnpackPath(
      std::vector<CCHSearchState> const & forwardTree,
      std::vector<CCHSearchState> const & backwardTree,
      uint32_t meetingNode);

private:
  /// @brief Iteratively unpack a shortcut to original segments.
  /// Uses explicit stack to avoid recursion depth issues.
  void UnpackShortcutIterative(uint32_t shortcutIdx, bool isForward,
                               std::vector<Segment> & result);

  /// @brief Try to add original edge directly to result.
  bool TryAddOriginalEdge(uint32_t fromNode, uint32_t toNode,
                          std::vector<Segment> & result);

  /// @brief Internal stack item for shortcut unpacking.
  struct UnpackTask
  {
    uint32_t fromNode;
    uint32_t toNode;
    bool isForward;
  };

  /// @brief Try to find and expand a shortcut.
  bool TryExpandShortcut(uint32_t fromNode, uint32_t toNode,
                         std::stack<UnpackTask> & toProcess);

  /// @brief Reconstruct path from search tree.
  void ReconstructPath(std::vector<CCHSearchState> const & tree,
                       uint32_t startNode, uint32_t endNode,
                       bool reverse, std::vector<Segment> & result);

  /// @brief Find edge between two nodes.
  bool FindEdgeToNode(uint32_t fromNode, uint32_t toNode,
                      bool isForward, std::vector<Segment> & result);

  /// @brief Find state in search tree by node ID.
  CCHSearchState const * FindStateInTree(
      std::vector<CCHSearchState> const & tree, uint32_t node) const;

  /// @brief Convert path states to segments.
  void ConvertStatesToSegments(
      std::vector<CCHSearchState const *> const & pathStates,
      bool reverse, std::vector<Segment> & result);

private:
  CCHTopology const & m_topology;
};

}  // namespace routing

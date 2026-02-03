#include "routing/cch_path_unpacker.hpp"

#include "routing_common/num_mwm_id.hpp"

#include <algorithm>

namespace routing
{

namespace
{
constexpr uint32_t kInvalidNode = UINT32_MAX;
}  // namespace

CCHPathUnpacker::CCHPathUnpacker(CCHTopology const & topology)
  : m_topology(topology)
{
}

std::vector<Segment> CCHPathUnpacker::UnpackPath(
    std::vector<CCHSearchState> const & forwardTree,
    std::vector<CCHSearchState> const & backwardTree,
    uint32_t meetingNode)
{
  std::vector<Segment> result;

  if (forwardTree.empty() || backwardTree.empty())
    return result;

  // Unpack forward path (from source to meeting point)
  std::vector<Segment> forwardPath;
  ReconstructPath(forwardTree, forwardTree.front().node, meetingNode,
                  false, forwardPath);

  // Unpack backward path (from meeting point to target)
  std::vector<Segment> backwardPath;
  ReconstructPath(backwardTree, backwardTree.front().node, meetingNode,
                  true, backwardPath);

  // Combine paths
  result.insert(result.end(), forwardPath.begin(), forwardPath.end());
  result.insert(result.end(), backwardPath.begin(), backwardPath.end());

  return result;
}

void CCHPathUnpacker::UnpackShortcut(uint32_t shortcutIdx, bool isForward,
                                     std::vector<Segment> & result)
{
  auto const & shortcut = m_topology.GetShortcut(shortcutIdx);

  // Recursively unpack: shortcut = path through middleNode
  // from -> middle -> to

  // Find and unpack edge from->middle
  FindEdgeToNode(shortcut.fromNode, shortcut.middleNode, isForward, result);

  // Find and unpack edge middle->to
  FindEdgeToNode(shortcut.middleNode, shortcut.toNode, isForward, result);
}

bool CCHPathUnpacker::FindEdgeToNode(uint32_t fromNode, uint32_t toNode,
                                     bool isForward,
                                     std::vector<Segment> & result)
{
  auto const edges = m_topology.GetOutgoingEdges(fromNode);

  // Search in original edges
  for (uint32_t i = edges.originalBegin; i < edges.originalEnd; ++i)
  {
    auto const & edge = m_topology.GetOriginalEdge(i);
    if (edge.toNode == toNode)
    {
      Segment seg(kFakeNumMwmId, edge.featureId, edge.segmentIdx,
                  edge.IsForward());
      if (isForward)
        result.push_back(seg);
      else
        result.insert(result.begin(), seg);
      return true;
    }
  }

  // Search in shortcuts (recursive)
  for (uint32_t i = edges.shortcutBegin; i < edges.shortcutEnd; ++i)
  {
    auto const & sc = m_topology.GetShortcut(i);
    if (sc.toNode == toNode && sc.IsForward())
    {
      UnpackShortcut(i, isForward, result);
      return true;
    }
  }

  return false;
}

void CCHPathUnpacker::ReconstructPath(
    std::vector<CCHSearchState> const & tree,
    uint32_t startNode, uint32_t endNode,
    bool reverse,
    std::vector<Segment> & result)
{
  // Build path by following parent pointers
  std::vector<CCHSearchState const *> pathStates;

  // Find the endNode state in tree
  CCHSearchState const * current = nullptr;
  for (auto const & state : tree)
  {
    if (state.node == endNode)
    {
      current = &state;
      break;
    }
  }

  if (current == nullptr)
    return;

  // Walk back to start
  while (current != nullptr && current->node != startNode)
  {
    pathStates.push_back(current);

    // Find parent state
    if (current->parent == kInvalidNode)
      break;

    CCHSearchState const * parent = nullptr;
    for (auto const & state : tree)
    {
      if (state.node == current->parent)
      {
        parent = &state;
        break;
      }
    }
    current = parent;
  }

  // Convert states to segments
  for (auto it = pathStates.rbegin(); it != pathStates.rend(); ++it)
  {
    auto const & state = **it;

    if (state.edgeIdx == kInvalidNode)
      continue;

    if (state.isShortcut)
      UnpackShortcut(state.edgeIdx, !reverse, result);
    else
    {
      auto const & edge = m_topology.GetOriginalEdge(state.edgeIdx);
      Segment seg(kFakeNumMwmId, edge.featureId, edge.segmentIdx,
                  edge.IsForward());
      result.push_back(seg);
    }
  }

  if (reverse)
    std::reverse(result.begin(), result.end());
}

}  // namespace routing

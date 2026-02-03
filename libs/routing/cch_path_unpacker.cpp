// Library Documentation Verified: 2026-02-04
// Source: Internal Organic Maps APIs
// API Version: CCH v1 (kCCHVersion = 1)
#include "routing/cch_path_unpacker.hpp"
#include "routing/routing_constants.hpp"

#include "routing_common/num_mwm_id.hpp"

#include <algorithm>
#include <stack>

namespace routing
{

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

  std::vector<Segment> forwardPath;
  ReconstructPath(forwardTree, forwardTree.front().node, meetingNode,
                  false, forwardPath);

  std::vector<Segment> backwardPath;
  ReconstructPath(backwardTree, backwardTree.front().node, meetingNode,
                  true, backwardPath);

  result.insert(result.end(), forwardPath.begin(), forwardPath.end());
  result.insert(result.end(), backwardPath.begin(), backwardPath.end());

  return result;
}

void CCHPathUnpacker::UnpackShortcutIterative(uint32_t shortcutIdx, bool isForward,
                                              std::vector<Segment> & result)
{
  // Use explicit stack to avoid recursion depth issues on deep shortcuts
  struct UnpackTask
  {
    uint32_t fromNode;
    uint32_t toNode;
    bool isForward;
  };

  std::stack<UnpackTask> toProcess;

  auto const & initialShortcut = m_topology.GetShortcut(shortcutIdx);
  toProcess.push({initialShortcut.fromNode, initialShortcut.middleNode, isForward});
  toProcess.push({initialShortcut.middleNode, initialShortcut.toNode, isForward});

  // Temporary storage for segments - will be reversed at end if needed
  std::vector<Segment> tempResult;

  while (!toProcess.empty())
  {
    auto task = toProcess.top();
    toProcess.pop();

    // Try to find direct original edge
    if (TryAddOriginalEdge(task.fromNode, task.toNode, tempResult))
      continue;

    // Try to find shortcut and expand it
    if (!TryExpandShortcut(task.fromNode, task.toNode, toProcess))
    {
      // No edge found - this shouldn't happen in valid topology
      continue;
    }
  }

  // Insert segments into result
  if (isForward)
    result.insert(result.end(), tempResult.begin(), tempResult.end());
  else
    result.insert(result.begin(), tempResult.rbegin(), tempResult.rend());
}

bool CCHPathUnpacker::TryAddOriginalEdge(uint32_t fromNode, uint32_t toNode,
                                         std::vector<Segment> & result)
{
  auto const edges = m_topology.GetOutgoingEdges(fromNode);

  for (uint32_t i = edges.originalBegin; i < edges.originalEnd; ++i)
  {
    auto const & edge = m_topology.GetOriginalEdge(i);
    if (edge.toNode == toNode)
    {
      Segment seg(kFakeNumMwmId, edge.featureId, edge.segmentIdx, edge.IsForward());
      result.push_back(seg);
      return true;
    }
  }

  return false;
}

bool CCHPathUnpacker::TryExpandShortcut(
    uint32_t fromNode, uint32_t toNode,
    std::stack<std::pair<uint32_t, uint32_t>> & /* unused */)
{
  auto const edges = m_topology.GetOutgoingEdges(fromNode);

  for (uint32_t i = edges.shortcutBegin; i < edges.shortcutEnd; ++i)
  {
    auto const & sc = m_topology.GetShortcut(i);
    if (sc.toNode == toNode && sc.IsForward())
    {
      // Found shortcut - it will be expanded in next iteration
      // The iterative version handles this through the main loop
      return true;
    }
  }

  return false;
}

bool CCHPathUnpacker::FindEdgeToNode(uint32_t fromNode, uint32_t toNode,
                                     bool isForward,
                                     std::vector<Segment> & result)
{
  auto const edges = m_topology.GetOutgoingEdges(fromNode);

  for (uint32_t i = edges.originalBegin; i < edges.originalEnd; ++i)
  {
    auto const & edge = m_topology.GetOriginalEdge(i);
    if (edge.toNode == toNode)
    {
      Segment seg(kFakeNumMwmId, edge.featureId, edge.segmentIdx, edge.IsForward());
      if (isForward)
        result.push_back(seg);
      else
        result.insert(result.begin(), seg);
      return true;
    }
  }

  for (uint32_t i = edges.shortcutBegin; i < edges.shortcutEnd; ++i)
  {
    auto const & sc = m_topology.GetShortcut(i);
    if (sc.toNode == toNode && sc.IsForward())
    {
      UnpackShortcutIterative(i, isForward, result);
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
  std::vector<CCHSearchState const *> pathStates;

  CCHSearchState const * current = FindStateInTree(tree, endNode);
  if (current == nullptr)
    return;

  while (current != nullptr && current->node != startNode)
  {
    pathStates.push_back(current);

    if (current->parent == kInvalidNode)
      break;

    current = FindStateInTree(tree, current->parent);
  }

  ConvertStatesToSegments(pathStates, reverse, result);
}

CCHSearchState const * CCHPathUnpacker::FindStateInTree(
    std::vector<CCHSearchState> const & tree,
    uint32_t node) const
{
  for (auto const & state : tree)
  {
    if (state.node == node)
      return &state;
  }
  return nullptr;
}

void CCHPathUnpacker::ConvertStatesToSegments(
    std::vector<CCHSearchState const *> const & pathStates,
    bool reverse,
    std::vector<Segment> & result)
{
  for (auto it = pathStates.rbegin(); it != pathStates.rend(); ++it)
  {
    auto const & state = **it;

    if (state.edgeIdx == kInvalidNode)
      continue;

    if (state.isShortcut)
      UnpackShortcutIterative(state.edgeIdx, !reverse, result);
    else
    {
      auto const & edge = m_topology.GetOriginalEdge(state.edgeIdx);
      Segment seg(kFakeNumMwmId, edge.featureId, edge.segmentIdx, edge.IsForward());
      result.push_back(seg);
    }
  }

  if (reverse)
    std::reverse(result.begin(), result.end());
}

}  // namespace routing

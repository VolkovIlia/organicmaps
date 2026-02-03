// Library Documentation Verified: 2026-02-04
// Source: Internal Organic Maps APIs
// API Version: CCH v1 (kCCHVersion = 1)
#include "routing/cch_query_engine.hpp"
#include "routing/routing_constants.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"
#include "base/timer.hpp"

#include <algorithm>

namespace routing
{

/// @brief Search direction for unified search step.
enum class SearchDirection
{
  Forward,
  Backward
};

CCHQueryEngine::CCHQueryEngine(CCHTopology const & topology,
                               CCHCustomizer & customizer)
  : m_topology(topology)
  , m_customizer(customizer)
  , m_pathUnpacker(topology)
{
  size_t const nodeCount = topology.GetNodeCount();
  m_forwardDist.resize(nodeCount, kInfinity);
  m_backwardDist.resize(nodeCount, kInfinity);
  m_forwardVisited.resize(nodeCount, false);
  m_backwardVisited.resize(nodeCount, false);
  m_forwardParent.resize(nodeCount);
  m_backwardParent.resize(nodeCount);
}

void CCHQueryEngine::ResetQueryState()
{
  std::fill(m_forwardDist.begin(), m_forwardDist.end(), kInfinity);
  std::fill(m_backwardDist.begin(), m_backwardDist.end(), kInfinity);
  std::fill(m_forwardVisited.begin(), m_forwardVisited.end(), false);
  std::fill(m_backwardVisited.begin(), m_backwardVisited.end(), false);
}

bool CCHQueryEngine::ValidateQueryRequest(CCHQueryRequest const & request,
                                          CCHQueryResult & result) const
{
  if (request.sourceNode == kInvalidNode || request.targetNode == kInvalidNode)
  {
    result.success = false;
    result.errorMessage = "Invalid source or target node";
    return false;
  }

  if (request.sourceNode >= m_topology.GetNodeCount() ||
      request.targetNode >= m_topology.GetNodeCount())
  {
    result.success = false;
    result.errorMessage = "Node ID out of range";
    return false;
  }

  if (!m_customizer.IsCustomized())
  {
    result.success = false;
    result.errorMessage = "CCH not customized for current profile";
    return false;
  }

  return true;
}

CCHQueryResult CCHQueryEngine::Query(CCHQueryRequest const & request)
{
  CCHQueryResult result;
  base::Timer timer;

  if (!ValidateQueryRequest(request, result))
    return result;

  std::vector<CCHSearchState> forwardTree, backwardTree;
  uint32_t meetingNode = kInvalidNode;

  bool const found = FindPath(request.sourceNode, request.targetNode,
                              forwardTree, backwardTree, meetingNode);

  if (!found)
  {
    result.success = false;
    result.errorMessage = "No path found";
    result.queryTimeMs = timer.ElapsedSeconds() * 1000.0;
    return result;
  }

  result.path = m_pathUnpacker.UnpackPath(forwardTree, backwardTree, meetingNode);
  result.durationSeconds = m_forwardDist[meetingNode] + m_backwardDist[meetingNode];

  constexpr double kAverageSpeedMpS = 15.0;  // ~54 km/h
  result.distanceMeters = result.durationSeconds * kAverageSpeedMpS;
  result.success = true;
  result.queryTimeMs = timer.ElapsedSeconds() * 1000.0;

  LOG(LDEBUG, ("CCH query completed in", result.queryTimeMs, "ms,",
               "path length:", result.path.size(), "segments"));

  return result;
}

void CCHQueryEngine::InitializeSearch(uint32_t sourceNode, uint32_t targetNode,
                                      PriorityQueue & forwardQueue,
                                      PriorityQueue & backwardQueue)
{
  ResetQueryState();

  m_forwardDist[sourceNode] = 0.0;
  forwardQueue.push({0.0, sourceNode, kInvalidNode, false, kInvalidNode});

  m_backwardDist[targetNode] = 0.0;
  backwardQueue.push({0.0, targetNode, kInvalidNode, false, kInvalidNode});
}

bool CCHQueryEngine::CheckTermination(PriorityQueue const & forwardQueue,
                                      PriorityQueue const & backwardQueue,
                                      double bestDist) const
{
  if (forwardQueue.empty() || backwardQueue.empty())
    return false;

  double const lowerBound = forwardQueue.top().distance + backwardQueue.top().distance;
  return lowerBound >= bestDist;
}

bool CCHQueryEngine::FindPath(uint32_t sourceNode, uint32_t targetNode,
                              std::vector<CCHSearchState> & forwardTree,
                              std::vector<CCHSearchState> & backwardTree,
                              uint32_t & meetingNode)
{
  forwardTree.clear();
  backwardTree.clear();

  PriorityQueue forwardQueue, backwardQueue;
  InitializeSearch(sourceNode, targetNode, forwardQueue, backwardQueue);

  double bestDist = kInfinity;
  meetingNode = kInvalidNode;

  while (!forwardQueue.empty() || !backwardQueue.empty())
  {
    ProcessSearchStep(SearchDirection::Forward, forwardQueue, forwardTree,
                      bestDist, meetingNode);
    ProcessSearchStep(SearchDirection::Backward, backwardQueue, backwardTree,
                      bestDist, meetingNode);

    if (CheckTermination(forwardQueue, backwardQueue, bestDist))
      break;
  }

  return meetingNode != kInvalidNode;
}

void CCHQueryEngine::ProcessSearchStep(SearchDirection direction,
                                       PriorityQueue & queue,
                                       std::vector<CCHSearchState> & tree,
                                       double & bestDist,
                                       uint32_t & meetingNode)
{
  if (queue.empty())
    return;

  auto state = queue.top();
  queue.pop();

  auto & myDist = (direction == SearchDirection::Forward) ? m_forwardDist : m_backwardDist;
  auto & myVisited = (direction == SearchDirection::Forward) ? m_forwardVisited : m_backwardVisited;
  auto & myParent = (direction == SearchDirection::Forward) ? m_forwardParent : m_backwardParent;
  auto const & otherDist = (direction == SearchDirection::Forward) ? m_backwardDist : m_forwardDist;
  auto const & otherVisited = (direction == SearchDirection::Forward) ? m_backwardVisited : m_forwardVisited;

  if (myVisited[state.node])
    return;

  myVisited[state.node] = true;
  myParent[state.node] = state;
  tree.push_back(state);

  TryUpdateMeetingPoint(state.node, state.distance, otherVisited, otherDist,
                        bestDist, meetingNode, direction);

  RelaxEdges(direction, state, queue, myDist, otherVisited, otherDist,
             bestDist, meetingNode);
}

void CCHQueryEngine::TryUpdateMeetingPoint(uint32_t node, double myNodeDist,
                                           std::vector<bool> const & otherVisited,
                                           std::vector<double> const & otherDist,
                                           double & bestDist,
                                           uint32_t & meetingNode,
                                           SearchDirection direction) const
{
  if (!otherVisited[node])
    return;

  double const totalDist = (direction == SearchDirection::Forward)
      ? myNodeDist + otherDist[node]
      : otherDist[node] + myNodeDist;

  if (totalDist < bestDist)
  {
    bestDist = totalDist;
    meetingNode = node;
  }
}

void CCHQueryEngine::RelaxEdges(SearchDirection direction,
                                CCHSearchState const & state,
                                PriorityQueue & queue,
                                std::vector<double> & myDist,
                                std::vector<bool> const & otherVisited,
                                std::vector<double> const & otherDist,
                                double & bestDist,
                                uint32_t & meetingNode)
{
  uint32_t const nodeLevel = m_topology.GetLevel(state.node);
  auto const edges = (direction == SearchDirection::Forward)
      ? m_topology.GetOutgoingEdges(state.node)
      : m_topology.GetIncomingEdges(state.node);

  RelaxOriginalEdges(direction, state, edges, nodeLevel, queue, myDist,
                     otherVisited, otherDist, bestDist, meetingNode);
  RelaxShortcuts(direction, state, edges, nodeLevel, queue, myDist,
                 otherVisited, otherDist, bestDist, meetingNode);
}

void CCHQueryEngine::RelaxOriginalEdges(SearchDirection direction,
                                        CCHSearchState const & state,
                                        CCHEdgeRange const & edges,
                                        uint32_t nodeLevel,
                                        PriorityQueue & queue,
                                        std::vector<double> & myDist,
                                        std::vector<bool> const & otherVisited,
                                        std::vector<double> const & otherDist,
                                        double & bestDist,
                                        uint32_t & meetingNode)
{
  for (uint32_t i = edges.originalBegin; i < edges.originalEnd; ++i)
  {
    auto const & edge = m_topology.GetOriginalEdge(i);
    uint32_t const targetNode = (direction == SearchDirection::Forward)
        ? edge.toNode : edge.fromNode;
    uint32_t const targetLevel = m_topology.GetLevel(targetNode);

    if (targetLevel < nodeLevel)
      continue;

    double const weight = m_customizer.GetOriginalEdgeWeight(i);
    if (weight >= kInfinity)
      continue;

    double const newDist = state.distance + weight;
    if (newDist < myDist[targetNode])
    {
      myDist[targetNode] = newDist;
      queue.push({newDist, targetNode, state.node, false, i});
      TryUpdateMeetingPoint(targetNode, newDist, otherVisited, otherDist,
                            bestDist, meetingNode, direction);
    }
  }
}

void CCHQueryEngine::RelaxShortcuts(SearchDirection direction,
                                    CCHSearchState const & state,
                                    CCHEdgeRange const & edges,
                                    uint32_t nodeLevel,
                                    PriorityQueue & queue,
                                    std::vector<double> & myDist,
                                    std::vector<bool> const & otherVisited,
                                    std::vector<double> const & otherDist,
                                    double & bestDist,
                                    uint32_t & meetingNode)
{
  for (uint32_t i = edges.shortcutBegin; i < edges.shortcutEnd; ++i)
  {
    auto const & shortcut = m_topology.GetShortcut(i);
    bool const validDirection = (direction == SearchDirection::Forward)
        ? shortcut.IsForward() : shortcut.IsBackward();

    if (!validDirection)
      continue;

    uint32_t const targetNode = (direction == SearchDirection::Forward)
        ? shortcut.toNode : shortcut.fromNode;
    uint32_t const targetLevel = m_topology.GetLevel(targetNode);

    if (targetLevel < nodeLevel)
      continue;

    double const weight = m_customizer.GetShortcutWeight(i);
    if (weight >= kInfinity)
      continue;

    double const newDist = state.distance + weight;
    if (newDist < myDist[targetNode])
    {
      myDist[targetNode] = newDist;
      queue.push({newDist, targetNode, state.node, true, i});
      TryUpdateMeetingPoint(targetNode, newDist, otherVisited, otherDist,
                            bestDist, meetingNode, direction);
    }
  }
}

bool CCHQueryEngine::IsCCHAvailable(NumMwmId /* mwmId */) const
{
  return m_topology.IsValid();
}

bool CCHQueryEngine::Customize(CCHCustomizationConfig const & /* config */)
{
  return m_customizer.IsCustomized();
}

bool CCHQueryEngine::IsReady() const
{
  return m_topology.IsValid() && m_customizer.IsCustomized();
}

}  // namespace routing

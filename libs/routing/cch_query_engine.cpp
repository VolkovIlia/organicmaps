#include "routing/cch_query_engine.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"
#include "base/timer.hpp"

#include <algorithm>
#include <limits>

namespace routing
{

namespace
{
constexpr double kInfinity = std::numeric_limits<double>::max();
constexpr uint32_t kInvalidNode = UINT32_MAX;
}  // namespace

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

CCHQueryResult CCHQueryEngine::Query(CCHQueryRequest const & request)
{
  CCHQueryResult result;
  base::Timer timer;

  // Validate input
  if (request.sourceNode == kInvalidNode ||
      request.targetNode == kInvalidNode)
  {
    result.success = false;
    result.errorMessage = "Invalid source or target node";
    return result;
  }

  if (request.sourceNode >= m_topology.GetNodeCount() ||
      request.targetNode >= m_topology.GetNodeCount())
  {
    result.success = false;
    result.errorMessage = "Node ID out of range";
    return result;
  }

  // Check customization
  if (!m_customizer.IsCustomized())
  {
    result.success = false;
    result.errorMessage = "CCH not customized for current profile";
    return result;
  }

  // Run bidirectional Dijkstra
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

  // Unpack path using dedicated unpacker
  result.path = m_pathUnpacker.UnpackPath(forwardTree, backwardTree, meetingNode);

  // Calculate total distance and duration
  result.durationSeconds = m_forwardDist[meetingNode] + m_backwardDist[meetingNode];

  // Distance calculation (simplified - assumes average speed)
  // In production, would sum actual segment lengths
  constexpr double kAverageSpeedMpS = 15.0;  // ~54 km/h
  result.distanceMeters = result.durationSeconds * kAverageSpeedMpS;

  result.success = true;
  result.queryTimeMs = timer.ElapsedSeconds() * 1000.0;

  LOG(LDEBUG, ("CCH query completed in", result.queryTimeMs, "ms,",
               "path length:", result.path.size(), "segments"));

  return result;
}

bool CCHQueryEngine::FindPath(uint32_t sourceNode, uint32_t targetNode,
                              std::vector<CCHSearchState> & forwardTree,
                              std::vector<CCHSearchState> & backwardTree,
                              uint32_t & meetingNode)
{
  // Reset state
  ResetQueryState();

  forwardTree.clear();
  backwardTree.clear();

  // Initialize queues
  PriorityQueue forwardQueue, backwardQueue;

  m_forwardDist[sourceNode] = 0.0;
  forwardQueue.push({0.0, sourceNode, kInvalidNode, false, kInvalidNode});

  m_backwardDist[targetNode] = 0.0;
  backwardQueue.push({0.0, targetNode, kInvalidNode, false, kInvalidNode});

  double bestDist = kInfinity;
  meetingNode = kInvalidNode;

  // Bidirectional search - alternate forward and backward
  while (!forwardQueue.empty() || !backwardQueue.empty())
  {
    // Forward step
    if (!forwardQueue.empty())
    {
      auto state = forwardQueue.top();
      forwardQueue.pop();

      if (m_forwardVisited[state.node])
        continue;

      m_forwardVisited[state.node] = true;
      m_forwardParent[state.node] = state;
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

      ProcessForwardStep(forwardQueue, bestDist, meetingNode, forwardTree);
    }

    // Backward step
    if (!backwardQueue.empty())
    {
      auto state = backwardQueue.top();
      backwardQueue.pop();

      if (m_backwardVisited[state.node])
        continue;

      m_backwardVisited[state.node] = true;
      m_backwardParent[state.node] = state;
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

      ProcessBackwardStep(backwardQueue, bestDist, meetingNode, backwardTree);
    }

    // Early termination: if both queues' min distances sum >= best found
    if (!forwardQueue.empty() && !backwardQueue.empty())
    {
      double const lowerBound = forwardQueue.top().distance +
                                backwardQueue.top().distance;
      if (lowerBound >= bestDist)
        break;
    }
  }

  return meetingNode != kInvalidNode;
}

void CCHQueryEngine::ProcessForwardStep(PriorityQueue & forwardQueue,
                                        double & bestDist,
                                        uint32_t & meetingNode,
                                        std::vector<CCHSearchState> & forwardTree)
{
  if (forwardTree.empty())
    return;

  auto const & state = forwardTree.back();
  uint32_t const nodeLevel = m_topology.GetLevel(state.node);
  auto const edges = m_topology.GetOutgoingEdges(state.node);

  // Process original edges - only go UP in hierarchy
  for (uint32_t i = edges.originalBegin; i < edges.originalEnd; ++i)
  {
    auto const & edge = m_topology.GetOriginalEdge(i);
    uint32_t const targetLevel = m_topology.GetLevel(edge.toNode);

    // Only traverse to higher or equal levels
    if (targetLevel < nodeLevel)
      continue;

    double const weight = m_customizer.GetOriginalEdgeWeight(i);
    if (weight >= kInfinity)
      continue;

    double const newDist = state.distance + weight;

    if (newDist < m_forwardDist[edge.toNode])
    {
      m_forwardDist[edge.toNode] = newDist;
      forwardQueue.push({newDist, edge.toNode, state.node, false, i});

      // Check if we found a better path
      if (m_backwardVisited[edge.toNode])
      {
        double const totalDist = newDist + m_backwardDist[edge.toNode];
        if (totalDist < bestDist)
        {
          bestDist = totalDist;
          meetingNode = edge.toNode;
        }
      }
    }
  }

  // Process shortcuts - only go UP in hierarchy
  for (uint32_t i = edges.shortcutBegin; i < edges.shortcutEnd; ++i)
  {
    auto const & shortcut = m_topology.GetShortcut(i);

    if (!shortcut.IsForward())
      continue;

    uint32_t const targetLevel = m_topology.GetLevel(shortcut.toNode);

    if (targetLevel < nodeLevel)
      continue;

    double const weight = m_customizer.GetShortcutWeight(i);
    if (weight >= kInfinity)
      continue;

    double const newDist = state.distance + weight;

    if (newDist < m_forwardDist[shortcut.toNode])
    {
      m_forwardDist[shortcut.toNode] = newDist;
      forwardQueue.push({newDist, shortcut.toNode, state.node, true, i});

      if (m_backwardVisited[shortcut.toNode])
      {
        double const totalDist = newDist + m_backwardDist[shortcut.toNode];
        if (totalDist < bestDist)
        {
          bestDist = totalDist;
          meetingNode = shortcut.toNode;
        }
      }
    }
  }
}

void CCHQueryEngine::ProcessBackwardStep(PriorityQueue & backwardQueue,
                                         double & bestDist,
                                         uint32_t & meetingNode,
                                         std::vector<CCHSearchState> & backwardTree)
{
  if (backwardTree.empty())
    return;

  auto const & state = backwardTree.back();
  uint32_t const nodeLevel = m_topology.GetLevel(state.node);
  auto const edges = m_topology.GetIncomingEdges(state.node);

  // Process incoming original edges - only go UP in hierarchy
  for (uint32_t i = edges.originalBegin; i < edges.originalEnd; ++i)
  {
    auto const & edge = m_topology.GetOriginalEdge(i);
    uint32_t const sourceLevel = m_topology.GetLevel(edge.fromNode);

    if (sourceLevel < nodeLevel)
      continue;

    double const weight = m_customizer.GetOriginalEdgeWeight(i);
    if (weight >= kInfinity)
      continue;

    double const newDist = state.distance + weight;

    if (newDist < m_backwardDist[edge.fromNode])
    {
      m_backwardDist[edge.fromNode] = newDist;
      backwardQueue.push({newDist, edge.fromNode, state.node, false, i});

      if (m_forwardVisited[edge.fromNode])
      {
        double const totalDist = m_forwardDist[edge.fromNode] + newDist;
        if (totalDist < bestDist)
        {
          bestDist = totalDist;
          meetingNode = edge.fromNode;
        }
      }
    }
  }

  // Process incoming shortcuts - only go UP in hierarchy
  for (uint32_t i = edges.shortcutBegin; i < edges.shortcutEnd; ++i)
  {
    auto const & shortcut = m_topology.GetShortcut(i);

    if (!shortcut.IsBackward())
      continue;

    uint32_t const sourceLevel = m_topology.GetLevel(shortcut.fromNode);

    if (sourceLevel < nodeLevel)
      continue;

    double const weight = m_customizer.GetShortcutWeight(i);
    if (weight >= kInfinity)
      continue;

    double const newDist = state.distance + weight;

    if (newDist < m_backwardDist[shortcut.fromNode])
    {
      m_backwardDist[shortcut.fromNode] = newDist;
      backwardQueue.push({newDist, shortcut.fromNode, state.node, true, i});

      if (m_forwardVisited[shortcut.fromNode])
      {
        double const totalDist = m_forwardDist[shortcut.fromNode] + newDist;
        if (totalDist < bestDist)
        {
          bestDist = totalDist;
          meetingNode = shortcut.fromNode;
        }
      }
    }
  }
}

bool CCHQueryEngine::IsCCHAvailable(NumMwmId /* mwmId */) const
{
  // Check if topology is valid
  return m_topology.IsValid();
}

bool CCHQueryEngine::Customize(CCHCustomizationConfig const & /* config */)
{
  // Customization is delegated to CCHCustomizer
  // This method is for interface compliance
  return m_customizer.IsCustomized();
}

bool CCHQueryEngine::IsReady() const
{
  return m_topology.IsValid() && m_customizer.IsCustomized();
}

}  // namespace routing

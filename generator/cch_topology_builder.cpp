// Library Documentation Verified: 2026-02-04
// Source: Internal Organic Maps APIs
// API Version: CCH v1 (kCCHVersion = 1)
#include "generator/cch_topology_builder.hpp"

#include "coding/files_container.hpp"
#include "coding/reader.hpp"

#include "indexer/feature.hpp"
#include "indexer/feature_processor.hpp"
#include "indexer/classificator.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"
#include "base/timer.hpp"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace generator
{

using namespace routing;

CCHTopologyBuilder::CCHTopologyBuilder(CCHBuildConfig config)
  : m_config(config)
{
}

CCHTopology CCHTopologyBuilder::Build(CCHRoadGraph const & graph)
{
  base::Timer timer;
  LOG(LINFO, ("Building CCH topology for", graph.nodeCount, "nodes,",
              graph.edges.size(), "edges"));

  CCHTopology topology;
  topology.SetNodeCount(graph.nodeCount);

  LOG(LINFO, ("Computing node ordering..."));
  std::vector<uint32_t> ordering = ComputeNodeOrdering(graph);

  LOG(LINFO, ("Assigning levels..."));
  std::vector<uint32_t> levels = AssignLevels(ordering);

  uint32_t maxLevel = 0;
  for (auto level : levels)
    maxLevel = std::max(maxLevel, level);

  topology.SetLevelCount(maxLevel + 1);

  BuildNodeOrderEntries(graph, ordering, levels, topology);

  LOG(LINFO, ("Computing shortcuts..."));
  std::vector<CCHShortcut> shortcuts = ComputeShortcuts(graph, ordering);
  auto & topologyShortcuts = topology.GetShortcutsForBuilder();
  topologyShortcuts = std::move(shortcuts);

  BuildOriginalEdges(graph, ordering, topology);

  LOG(LINFO, ("Building adjacency offsets..."));
  topology.BuildAdjacencyOffsets();

  LOG(LINFO, ("CCH topology built in", timer.ElapsedSeconds(), "seconds"));
  LOG(LINFO, ("Nodes:", graph.nodeCount, "Original edges:",
              topology.GetOriginalEdgeCount(), "Shortcuts:",
              topologyShortcuts.size(), "Levels:", topology.GetLevelCount()));

  return topology;
}

void CCHTopologyBuilder::BuildNodeOrderEntries(
    CCHRoadGraph const & graph,
    std::vector<uint32_t> const & ordering,
    std::vector<uint32_t> const & levels,
    CCHTopology & topology)
{
  auto & nodeOrder = topology.GetNodeOrderForBuilder();
  nodeOrder.resize(graph.nodeCount);

  for (uint32_t i = 0; i < graph.nodeCount; ++i)
  {
    nodeOrder[ordering[i]].originalId = ordering[i];
    nodeOrder[ordering[i]].contractedId = i;
    nodeOrder[ordering[i]].level = levels[ordering[i]];
  }
}

void CCHTopologyBuilder::BuildOriginalEdges(
    CCHRoadGraph const & graph,
    std::vector<uint32_t> const & ordering,
    CCHTopology & topology)
{
  auto & originalEdges = topology.GetOriginalEdgesForBuilder();
  originalEdges.resize(graph.edges.size());

  for (size_t i = 0; i < graph.edges.size(); ++i)
  {
    uint32_t fromOriginal = graph.edges[i].first;
    uint32_t toOriginal = graph.edges[i].second;

    uint32_t fromContracted = FindContractedId(fromOriginal, ordering, graph.nodeCount);
    uint32_t toContracted = FindContractedId(toOriginal, ordering, graph.nodeCount);

    originalEdges[i].fromNode = fromContracted;
    originalEdges[i].toNode = toContracted;
    originalEdges[i].featureId = graph.featureIds[i];
    originalEdges[i].segmentIdx = graph.segmentIndices[i];
    originalEdges[i].SetForward(graph.forwardFlags[i]);
  }
}

uint32_t CCHTopologyBuilder::FindContractedId(
    uint32_t originalId,
    std::vector<uint32_t> const & ordering,
    uint64_t nodeCount) const
{
  for (uint32_t j = 0; j < nodeCount; ++j)
  {
    if (ordering[j] == originalId)
      return j;
  }
  return 0;
}

std::vector<uint32_t> CCHTopologyBuilder::ComputeNodeOrdering(CCHRoadGraph const & graph)
{
  auto adjacency = BuildAdjacencyList(graph);
  return ContractNodes(graph.nodeCount, adjacency);
}

std::vector<std::vector<uint32_t>> CCHTopologyBuilder::BuildAdjacencyList(
    CCHRoadGraph const & graph)
{
  std::vector<std::vector<uint32_t>> adjacency(graph.nodeCount);

  for (auto const & edge : graph.edges)
  {
    if (edge.first < graph.nodeCount && edge.second < graph.nodeCount)
    {
      adjacency[edge.first].push_back(edge.second);
      adjacency[edge.second].push_back(edge.first);
    }
  }

  for (auto & neighbors : adjacency)
  {
    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
  }

  return adjacency;
}

std::vector<uint32_t> CCHTopologyBuilder::ContractNodes(
    uint64_t nodeCount,
    std::vector<std::vector<uint32_t>> & adjacency)
{
  using NodePriority = std::pair<int64_t, uint32_t>;
  std::priority_queue<NodePriority, std::vector<NodePriority>,
                      std::greater<NodePriority>> pq;

  std::vector<bool> contracted(nodeCount, false);
  std::vector<uint32_t> ordering;
  ordering.reserve(nodeCount);

  InitializeNodePriorities(nodeCount, adjacency, pq);

  while (!pq.empty() && ordering.size() < nodeCount)
  {
    auto [priority, node] = pq.top();
    pq.pop();

    if (contracted[node])
      continue;

    int64_t currentPriority = ComputeEdgeDifference(node, adjacency);
    if (currentPriority > priority + 1)
    {
      pq.push({currentPriority, node});
      continue;
    }

    contracted[node] = true;
    ordering.push_back(node);
    UpdateNeighborPriorities(node, contracted, adjacency, pq);
  }

  return ordering;
}

void CCHTopologyBuilder::InitializeNodePriorities(
    uint64_t nodeCount,
    std::vector<std::vector<uint32_t>> const & adjacency,
    std::priority_queue<std::pair<int64_t, uint32_t>,
                        std::vector<std::pair<int64_t, uint32_t>>,
                        std::greater<std::pair<int64_t, uint32_t>>> & pq)
{
  for (uint32_t i = 0; i < nodeCount; ++i)
  {
    int64_t priority = ComputeEdgeDifference(i, adjacency);
    pq.push({priority, i});
  }
}

void CCHTopologyBuilder::UpdateNeighborPriorities(
    uint32_t node,
    std::vector<bool> const & contracted,
    std::vector<std::vector<uint32_t>> & adjacency,
    std::priority_queue<std::pair<int64_t, uint32_t>,
                        std::vector<std::pair<int64_t, uint32_t>>,
                        std::greater<std::pair<int64_t, uint32_t>>> & pq)
{
  for (uint32_t neighbor : adjacency[node])
  {
    if (!contracted[neighbor])
    {
      auto & adj = adjacency[neighbor];
      adj.erase(std::remove(adj.begin(), adj.end(), node), adj.end());

      int64_t newPriority = ComputeEdgeDifference(neighbor, adjacency);
      pq.push({newPriority, neighbor});
    }
  }
}

std::vector<CCHShortcut> CCHTopologyBuilder::ComputeShortcuts(
    CCHRoadGraph const & graph,
    std::vector<uint32_t> const & ordering)
{
  std::vector<CCHShortcut> shortcuts;
  auto adjacency = BuildEdgeIndexedAdjacency(graph);
  std::vector<uint32_t> orderPosition = BuildOrderPositionMap(ordering);
  std::vector<bool> contracted(graph.nodeCount, false);

  for (uint32_t node : ordering)
  {
    CreateShortcutsForNode(node, adjacency, contracted, orderPosition, shortcuts);
    contracted[node] = true;
  }

  LOG(LINFO, ("Created", shortcuts.size(), "shortcuts during contraction"));
  return shortcuts;
}

std::vector<std::vector<std::pair<uint32_t, uint32_t>>>
CCHTopologyBuilder::BuildEdgeIndexedAdjacency(CCHRoadGraph const & graph)
{
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> adjacency(graph.nodeCount);

  for (uint32_t i = 0; i < graph.edges.size(); ++i)
  {
    auto const & edge = graph.edges[i];
    if (edge.first < graph.nodeCount && edge.second < graph.nodeCount)
      adjacency[edge.first].push_back({edge.second, i});
  }

  return adjacency;
}

std::vector<uint32_t> CCHTopologyBuilder::BuildOrderPositionMap(
    std::vector<uint32_t> const & ordering)
{
  std::vector<uint32_t> orderPosition(ordering.size());
  for (uint32_t i = 0; i < ordering.size(); ++i)
    orderPosition[ordering[i]] = i;
  return orderPosition;
}

void CCHTopologyBuilder::CreateShortcutsForNode(
    uint32_t node,
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> const & adjacency,
    std::vector<bool> const & contracted,
    std::vector<uint32_t> const & orderPosition,
    std::vector<CCHShortcut> & shortcuts)
{
  std::vector<uint32_t> neighbors;
  for (auto const & [neighbor, edgeIdx] : adjacency[node])
  {
    if (!contracted[neighbor])
      neighbors.push_back(neighbor);
  }

  for (size_t i = 0; i < neighbors.size(); ++i)
  {
    for (size_t j = i + 1; j < neighbors.size(); ++j)
    {
      CCHShortcut shortcut;
      shortcut.fromNode = orderPosition[neighbors[i]];
      shortcut.toNode = orderPosition[neighbors[j]];
      shortcut.middleNode = orderPosition[node];
      shortcut.SetForward(true);
      shortcut.SetBackward(true);
      shortcuts.push_back(shortcut);
    }
  }
}

std::vector<uint32_t> CCHTopologyBuilder::AssignLevels(std::vector<uint32_t> const & ordering)
{
  std::vector<uint32_t> levels(ordering.size(), 0);

  uint32_t const bucketSize = std::max(1u, static_cast<uint32_t>(ordering.size()) /
                                           m_config.maxLevelCount);

  for (uint32_t i = 0; i < ordering.size(); ++i)
  {
    uint32_t level = i / bucketSize;
    levels[ordering[i]] = std::min(level, m_config.maxLevelCount - 1);
  }

  return levels;
}

int64_t CCHTopologyBuilder::ComputeEdgeDifference(
    uint32_t node,
    std::vector<std::vector<uint32_t>> const & adjacency) const
{
  if (node >= adjacency.size())
    return 0;

  auto const & neighbors = adjacency[node];
  int64_t edgesRemoved = static_cast<int64_t>(neighbors.size());

  int64_t shortcutsNeeded = static_cast<int64_t>(neighbors.size()) *
                            static_cast<int64_t>(neighbors.size() - 1) / 2;

  int64_t priority = static_cast<int64_t>(
      m_config.nodeOrderingAlpha * (shortcutsNeeded - edgesRemoved) +
      (1.0 - m_config.nodeOrderingAlpha) * neighbors.size());

  return priority;
}

CCHRoadGraph CCHTopologyBuilder::ExtractRoadGraph(std::string const & mwmPath)
{
  CCHRoadGraph graph;
  LOG(LINFO, ("Extracting road graph from", mwmPath));

  std::unordered_map<uint64_t, uint32_t> nodeMap;
  uint32_t nextNodeId = 0;

  try
  {
    FilesContainerR container(mwmPath);
    feature::ForEachFeature(container, [&](FeatureType & ft, uint32_t featureId) {
      ProcessFeatureForGraph(ft, featureId, nodeMap, nextNodeId, graph);
    });
  }
  catch (std::exception const & e)
  {
    LOG(LERROR, ("Failed to extract road graph:", e.what()));
    return graph;
  }

  graph.nodeCount = nextNodeId;
  LOG(LINFO, ("Extracted road graph:", graph.nodeCount, "nodes,",
              graph.edges.size(), "edges"));

  return graph;
}

void CCHTopologyBuilder::ProcessFeatureForGraph(
    FeatureType & ft,
    uint32_t featureId,
    std::unordered_map<uint64_t, uint32_t> & nodeMap,
    uint32_t & nextNodeId,
    CCHRoadGraph & graph)
{
  if (ft.GetGeomType() != feature::GeomType::Line)
    return;

  if (!IsRoutableFeature(ft))
    return;

  ft.ParseGeometry(FeatureType::BEST_GEOMETRY);
  ProcessFeatureSegments(ft, featureId, nodeMap, nextNodeId, graph);
}

bool CCHTopologyBuilder::IsRoutableFeature(FeatureType const & ft)
{
  feature::TypesHolder types(ft);
  bool isRoad = false;

  types.ForEach([&](uint32_t type) {
    auto const & c = classif();
    if (c.IsTypeValid(type))
    {
      std::string const typeName = c.GetReadableObjectName(type);
      if (typeName.find("highway") != std::string::npos)
        isRoad = true;
    }
  });

  return isRoad;
}

void CCHTopologyBuilder::ProcessFeatureSegments(
    FeatureType & ft,
    uint32_t featureId,
    std::unordered_map<uint64_t, uint32_t> & nodeMap,
    uint32_t & nextNodeId,
    CCHRoadGraph & graph)
{
  size_t const pointCount = ft.GetPointsCount();

  for (size_t i = 0; i + 1 < pointCount; ++i)
  {
    m2::PointD const & p1 = ft.GetPoint(i);
    m2::PointD const & p2 = ft.GetPoint(i + 1);

    uint32_t node1 = GetOrCreateNode(p1, nodeMap, nextNodeId);
    uint32_t node2 = GetOrCreateNode(p2, nodeMap, nextNodeId);

    if (node1 != node2)
      AddEdgePair(node1, node2, featureId, static_cast<uint16_t>(i), graph);
  }
}

uint32_t CCHTopologyBuilder::GetOrCreateNode(
    m2::PointD const & point,
    std::unordered_map<uint64_t, uint32_t> & nodeMap,
    uint32_t & nextNodeId)
{
  uint64_t const hash = HashPoint(point);
  auto it = nodeMap.find(hash);

  if (it != nodeMap.end())
    return it->second;

  uint32_t id = nextNodeId++;
  nodeMap[hash] = id;
  return id;
}

uint64_t CCHTopologyBuilder::HashPoint(m2::PointD const & p)
{
  // Quantize to ~1m precision
  int64_t x = static_cast<int64_t>(p.x * 100000);
  int64_t y = static_cast<int64_t>(p.y * 100000);
  return (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(y & 0xFFFFFFFF);
}

void CCHTopologyBuilder::AddEdgePair(
    uint32_t node1,
    uint32_t node2,
    uint32_t featureId,
    uint16_t segmentIdx,
    CCHRoadGraph & graph)
{
  // Forward edge
  graph.edges.push_back({node1, node2});
  graph.featureIds.push_back(featureId);
  graph.segmentIndices.push_back(segmentIdx);
  graph.forwardFlags.push_back(true);

  // Backward edge (for undirected graph)
  graph.edges.push_back({node2, node1});
  graph.featureIds.push_back(featureId);
  graph.segmentIndices.push_back(segmentIdx);
  graph.forwardFlags.push_back(false);
}

}  // namespace generator

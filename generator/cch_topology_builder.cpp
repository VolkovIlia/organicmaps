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

  // Step 1: Compute node ordering
  LOG(LINFO, ("Computing node ordering..."));
  std::vector<uint32_t> ordering = ComputeNodeOrdering(graph);

  // Step 2: Assign levels based on ordering
  LOG(LINFO, ("Assigning levels..."));
  std::vector<uint32_t> levels = AssignLevels(ordering);

  uint32_t maxLevel = 0;
  for (auto level : levels)
    maxLevel = std::max(maxLevel, level);

  topology.SetLevelCount(maxLevel + 1);

  // Step 3: Build node order entries
  auto & nodeOrder = topology.GetNodeOrderForBuilder();
  nodeOrder.resize(graph.nodeCount);

  for (uint32_t i = 0; i < graph.nodeCount; ++i)
  {
    nodeOrder[ordering[i]].originalId = ordering[i];
    nodeOrder[ordering[i]].contractedId = i;
    nodeOrder[ordering[i]].level = levels[ordering[i]];
  }

  // Step 4: Compute shortcuts during contraction
  LOG(LINFO, ("Computing shortcuts..."));
  std::vector<CCHShortcut> shortcuts = ComputeShortcuts(graph, ordering);

  auto & topologyShortcuts = topology.GetShortcutsForBuilder();
  topologyShortcuts = std::move(shortcuts);

  // Step 5: Build original edges
  auto & originalEdges = topology.GetOriginalEdgesForBuilder();
  originalEdges.resize(graph.edges.size());

  for (size_t i = 0; i < graph.edges.size(); ++i)
  {
    // Convert original node IDs to contracted IDs
    uint32_t fromOriginal = graph.edges[i].first;
    uint32_t toOriginal = graph.edges[i].second;

    // Find contracted IDs
    uint32_t fromContracted = 0, toContracted = 0;
    for (uint32_t j = 0; j < graph.nodeCount; ++j)
    {
      if (ordering[j] == fromOriginal)
        fromContracted = j;
      if (ordering[j] == toOriginal)
        toContracted = j;
    }

    originalEdges[i].fromNode = fromContracted;
    originalEdges[i].toNode = toContracted;
    originalEdges[i].featureId = graph.featureIds[i];
    originalEdges[i].segmentIdx = graph.segmentIndices[i];
    originalEdges[i].SetForward(graph.forwardFlags[i]);
  }

  // Step 6: Build adjacency offsets
  LOG(LINFO, ("Building adjacency offsets..."));
  topology.BuildAdjacencyOffsets();

  LOG(LINFO, ("CCH topology built in", timer.ElapsedSeconds(), "seconds"));
  LOG(LINFO, ("Nodes:", graph.nodeCount, "Original edges:", originalEdges.size(),
              "Shortcuts:", topologyShortcuts.size(), "Levels:", topology.GetLevelCount()));

  return topology;
}

std::vector<uint32_t> CCHTopologyBuilder::ComputeNodeOrdering(CCHRoadGraph const & graph)
{
  // Build adjacency list
  std::vector<std::vector<uint32_t>> adjacency(graph.nodeCount);
  for (auto const & edge : graph.edges)
  {
    if (edge.first < graph.nodeCount && edge.second < graph.nodeCount)
    {
      adjacency[edge.first].push_back(edge.second);
      adjacency[edge.second].push_back(edge.first);  // Undirected for ordering
    }
  }

  // Remove duplicates
  for (auto & neighbors : adjacency)
  {
    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
  }

  // Priority queue for node ordering (min-heap by edge difference)
  using NodePriority = std::pair<int64_t, uint32_t>;  // (priority, node)
  std::priority_queue<NodePriority, std::vector<NodePriority>,
                      std::greater<NodePriority>> pq;

  std::vector<bool> contracted(graph.nodeCount, false);
  std::vector<uint32_t> ordering;
  ordering.reserve(graph.nodeCount);

  // Initialize priorities
  for (uint32_t i = 0; i < graph.nodeCount; ++i)
  {
    int64_t priority = ComputeEdgeDifference(i, adjacency);
    pq.push({priority, i});
  }

  // Contract nodes in priority order
  while (!pq.empty() && ordering.size() < graph.nodeCount)
  {
    auto [priority, node] = pq.top();
    pq.pop();

    if (contracted[node])
      continue;

    // Lazy update: recompute priority if it might have changed
    int64_t currentPriority = ComputeEdgeDifference(node, adjacency);
    if (currentPriority > priority + 1)  // Allow small tolerance
    {
      pq.push({currentPriority, node});
      continue;
    }

    // Contract this node
    contracted[node] = true;
    ordering.push_back(node);

    // Update adjacency (simplified - full implementation would add shortcuts)
    for (uint32_t neighbor : adjacency[node])
    {
      if (!contracted[neighbor])
      {
        // Remove this node from neighbor's adjacency
        auto & adj = adjacency[neighbor];
        adj.erase(std::remove(adj.begin(), adj.end(), node), adj.end());

        // Recompute priority
        int64_t newPriority = ComputeEdgeDifference(neighbor, adjacency);
        pq.push({newPriority, neighbor});
      }
    }
  }

  return ordering;
}

std::vector<CCHShortcut> CCHTopologyBuilder::ComputeShortcuts(
    CCHRoadGraph const & graph,
    std::vector<uint32_t> const & ordering)
{
  std::vector<CCHShortcut> shortcuts;

  // Build adjacency list with edge indices
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> adjacency(graph.nodeCount);
  for (uint32_t i = 0; i < graph.edges.size(); ++i)
  {
    auto const & edge = graph.edges[i];
    if (edge.first < graph.nodeCount && edge.second < graph.nodeCount)
    {
      adjacency[edge.first].push_back({edge.second, i});
    }
  }

  // Create reverse mapping: original ID -> position in ordering
  std::vector<uint32_t> orderPosition(graph.nodeCount);
  for (uint32_t i = 0; i < ordering.size(); ++i)
    orderPosition[ordering[i]] = i;

  // Contract nodes in order, creating shortcuts
  std::vector<bool> contracted(graph.nodeCount, false);

  for (uint32_t node : ordering)
  {
    // Find all pairs of uncontracted neighbors
    std::vector<uint32_t> neighbors;
    for (auto const & [neighbor, edgeIdx] : adjacency[node])
    {
      if (!contracted[neighbor])
        neighbors.push_back(neighbor);
    }

    // Create shortcuts between all pairs of neighbors
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

    contracted[node] = true;
  }

  LOG(LINFO, ("Created", shortcuts.size(), "shortcuts during contraction"));

  return shortcuts;
}

std::vector<uint32_t> CCHTopologyBuilder::AssignLevels(std::vector<uint32_t> const & ordering)
{
  std::vector<uint32_t> levels(ordering.size(), 0);

  // Simple level assignment: level = position in ordering / bucket_size
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
  // Edge difference = shortcuts_needed - edges_removed
  // Lower is better for contraction order

  if (node >= adjacency.size())
    return 0;

  auto const & neighbors = adjacency[node];
  int64_t edgesRemoved = static_cast<int64_t>(neighbors.size());

  // Shortcuts needed = all pairs of neighbors
  int64_t shortcutsNeeded = static_cast<int64_t>(neighbors.size()) *
                            static_cast<int64_t>(neighbors.size() - 1) / 2;

  // Factor in node level (alpha balances between edge diff and level)
  int64_t priority = static_cast<int64_t>(
      m_config.nodeOrderingAlpha * (shortcutsNeeded - edgesRemoved) +
      (1.0 - m_config.nodeOrderingAlpha) * neighbors.size());

  return priority;
}

CCHRoadGraph CCHTopologyBuilder::ExtractRoadGraph(std::string const & mwmPath)
{
  CCHRoadGraph graph;

  LOG(LINFO, ("Extracting road graph from", mwmPath));

  // Use node hash map for deduplication
  std::unordered_map<uint64_t, uint32_t> nodeMap;
  uint32_t nextNodeId = 0;

  auto getNodeId = [&](uint64_t pointHash) -> uint32_t {
    auto it = nodeMap.find(pointHash);
    if (it != nodeMap.end())
      return it->second;

    uint32_t id = nextNodeId++;
    nodeMap[pointHash] = id;
    return id;
  };

  // Hash function for points
  auto hashPoint = [](m2::PointD const & p) -> uint64_t {
    // Quantize to ~1m precision
    int64_t x = static_cast<int64_t>(p.x * 100000);
    int64_t y = static_cast<int64_t>(p.y * 100000);
    return (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(y & 0xFFFFFFFF);
  };

  // Process features
  try
  {
    FilesContainerR container(mwmPath);

    feature::ForEachFeature(container, [&](FeatureType & ft, uint32_t featureId) {
      if (ft.GetGeomType() != feature::GeomType::Line)
        return;

      // Check if it's a road
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

      if (!isRoad)
        return;

      // Extract segments
      ft.ParseGeometry(FeatureType::BEST_GEOMETRY);
      size_t const pointCount = ft.GetPointsCount();

      for (size_t i = 0; i + 1 < pointCount; ++i)
      {
        m2::PointD const & p1 = ft.GetPoint(i);
        m2::PointD const & p2 = ft.GetPoint(i + 1);

        uint32_t node1 = getNodeId(hashPoint(p1));
        uint32_t node2 = getNodeId(hashPoint(p2));

        if (node1 != node2)
        {
          // Forward edge
          graph.edges.push_back({node1, node2});
          graph.featureIds.push_back(featureId);
          graph.segmentIndices.push_back(static_cast<uint16_t>(i));
          graph.forwardFlags.push_back(true);

          // Backward edge (for undirected graph)
          graph.edges.push_back({node2, node1});
          graph.featureIds.push_back(featureId);
          graph.segmentIndices.push_back(static_cast<uint16_t>(i));
          graph.forwardFlags.push_back(false);
        }
      }
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

}  // namespace generator

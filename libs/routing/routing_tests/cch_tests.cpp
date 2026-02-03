#include "testing/testing.hpp"

#include "routing/cch_topology.hpp"
#include "routing/cch_customizer.hpp"
#include "routing/cch_query_engine.hpp"

#include "coding/reader.hpp"
#include "coding/writer.hpp"

#include <cstdint>
#include <vector>

namespace cch_tests
{
using namespace routing;

namespace
{

// Helper to create a simple test topology
CCHTopology CreateSimpleTopology()
{
  CCHTopology topology;
  topology.SetNodeCount(5);
  topology.SetLevelCount(3);

  // Create node ordering
  auto & nodeOrder = topology.GetNodeOrderForBuilder();
  nodeOrder.resize(5);

  // Nodes: 0, 1, 2, 3, 4 with levels 0, 0, 1, 1, 2
  for (uint32_t i = 0; i < 5; ++i)
  {
    nodeOrder[i].originalId = i;
    nodeOrder[i].contractedId = i;
    nodeOrder[i].level = i / 2;  // Levels: 0, 0, 1, 1, 2
  }

  // Create original edges: 0-1, 1-2, 2-3, 3-4
  auto & edges = topology.GetOriginalEdgesForBuilder();
  edges.resize(8);  // 4 edges * 2 directions

  // Edge 0-1 forward
  edges[0].fromNode = 0;
  edges[0].toNode = 1;
  edges[0].featureId = 100;
  edges[0].segmentIdx = 0;
  edges[0].SetForward(true);

  // Edge 0-1 backward
  edges[1].fromNode = 1;
  edges[1].toNode = 0;
  edges[1].featureId = 100;
  edges[1].segmentIdx = 0;
  edges[1].SetForward(false);

  // Edge 1-2 forward
  edges[2].fromNode = 1;
  edges[2].toNode = 2;
  edges[2].featureId = 101;
  edges[2].segmentIdx = 0;
  edges[2].SetForward(true);

  // Edge 1-2 backward
  edges[3].fromNode = 2;
  edges[3].toNode = 1;
  edges[3].featureId = 101;
  edges[3].segmentIdx = 0;
  edges[3].SetForward(false);

  // Edge 2-3 forward
  edges[4].fromNode = 2;
  edges[4].toNode = 3;
  edges[4].featureId = 102;
  edges[4].segmentIdx = 0;
  edges[4].SetForward(true);

  // Edge 2-3 backward
  edges[5].fromNode = 3;
  edges[5].toNode = 2;
  edges[5].featureId = 102;
  edges[5].segmentIdx = 0;
  edges[5].SetForward(false);

  // Edge 3-4 forward
  edges[6].fromNode = 3;
  edges[6].toNode = 4;
  edges[6].featureId = 103;
  edges[6].segmentIdx = 0;
  edges[6].SetForward(true);

  // Edge 3-4 backward
  edges[7].fromNode = 4;
  edges[7].toNode = 3;
  edges[7].featureId = 103;
  edges[7].segmentIdx = 0;
  edges[7].SetForward(false);

  // Create shortcuts: 0-2, 1-3
  auto & shortcuts = topology.GetShortcutsForBuilder();
  shortcuts.resize(4);

  // Shortcut 0-2 forward (through node 1)
  shortcuts[0].fromNode = 0;
  shortcuts[0].toNode = 2;
  shortcuts[0].middleNode = 1;
  shortcuts[0].SetForward(true);
  shortcuts[0].SetBackward(false);

  // Shortcut 0-2 backward
  shortcuts[1].fromNode = 2;
  shortcuts[1].toNode = 0;
  shortcuts[1].middleNode = 1;
  shortcuts[1].SetForward(false);
  shortcuts[1].SetBackward(true);

  // Shortcut 2-4 forward (through node 3)
  shortcuts[2].fromNode = 2;
  shortcuts[2].toNode = 4;
  shortcuts[2].middleNode = 3;
  shortcuts[2].SetForward(true);
  shortcuts[2].SetBackward(false);

  // Shortcut 2-4 backward
  shortcuts[3].fromNode = 4;
  shortcuts[3].toNode = 2;
  shortcuts[3].middleNode = 3;
  shortcuts[3].SetForward(false);
  shortcuts[3].SetBackward(true);

  // Build adjacency offsets
  topology.BuildAdjacencyOffsets();

  return topology;
}

}  // namespace

UNIT_TEST(CCH_Topology_IsValid)
{
  CCHTopology topology = CreateSimpleTopology();

  TEST(topology.IsValid(), ());
  TEST_EQUAL(topology.GetNodeCount(), 5, ());
  TEST_EQUAL(topology.GetOriginalEdgeCount(), 8, ());
  TEST_EQUAL(topology.GetShortcutCount(), 4, ());
  TEST_EQUAL(topology.GetLevelCount(), 3, ());
}

UNIT_TEST(CCH_Topology_NodeLookup)
{
  CCHTopology topology = CreateSimpleTopology();

  // Test level lookup
  TEST_EQUAL(topology.GetLevel(0), 0, ());
  TEST_EQUAL(topology.GetLevel(1), 0, ());
  TEST_EQUAL(topology.GetLevel(2), 1, ());
  TEST_EQUAL(topology.GetLevel(3), 1, ());
  TEST_EQUAL(topology.GetLevel(4), 2, ());

  // Test ID mapping
  TEST_EQUAL(topology.GetOriginalId(0), 0, ());
  TEST_EQUAL(topology.GetContractedId(0), 0, ());
}

UNIT_TEST(CCH_Topology_Serialization)
{
  CCHTopology original = CreateSimpleTopology();

  // Serialize
  std::vector<uint8_t> buffer;
  MemWriter<std::vector<uint8_t>> writer(buffer);
  original.Serialize(writer);

  TEST_GREATER(buffer.size(), 0, ());

  // Deserialize
  CCHTopology restored;
  MemReader reader(buffer.data(), buffer.size());
  ReaderSource<MemReader> source(reader);
  restored.Deserialize(source);

  // Verify
  TEST(restored.IsValid(), ());
  TEST_EQUAL(original.GetNodeCount(), restored.GetNodeCount(), ());
  TEST_EQUAL(original.GetOriginalEdgeCount(), restored.GetOriginalEdgeCount(), ());
  TEST_EQUAL(original.GetShortcutCount(), restored.GetShortcutCount(), ());
  TEST_EQUAL(original.GetLevelCount(), restored.GetLevelCount(), ());

  // Verify node levels
  for (uint32_t i = 0; i < original.GetNodeCount(); ++i)
  {
    TEST_EQUAL(original.GetLevel(i), restored.GetLevel(i), ("Node level mismatch at", i));
  }

  // Verify edge data
  for (uint32_t i = 0; i < original.GetOriginalEdgeCount(); ++i)
  {
    auto const & origEdge = original.GetOriginalEdge(i);
    auto const & restoredEdge = restored.GetOriginalEdge(i);

    TEST_EQUAL(origEdge.fromNode, restoredEdge.fromNode, ());
    TEST_EQUAL(origEdge.toNode, restoredEdge.toNode, ());
    TEST_EQUAL(origEdge.featureId, restoredEdge.featureId, ());
    TEST_EQUAL(origEdge.segmentIdx, restoredEdge.segmentIdx, ());
    TEST_EQUAL(origEdge.IsForward(), restoredEdge.IsForward(), ());
  }

  // Verify shortcut data
  for (uint32_t i = 0; i < original.GetShortcutCount(); ++i)
  {
    auto const & origShortcut = original.GetShortcut(i);
    auto const & restoredShortcut = restored.GetShortcut(i);

    TEST_EQUAL(origShortcut.fromNode, restoredShortcut.fromNode, ());
    TEST_EQUAL(origShortcut.toNode, restoredShortcut.toNode, ());
    TEST_EQUAL(origShortcut.middleNode, restoredShortcut.middleNode, ());
    TEST_EQUAL(origShortcut.IsForward(), restoredShortcut.IsForward(), ());
    TEST_EQUAL(origShortcut.IsBackward(), restoredShortcut.IsBackward(), ());
  }
}

UNIT_TEST(CCH_Topology_EdgeRange)
{
  CCHTopology topology = CreateSimpleTopology();

  // Check outgoing edges for node 1 (should have edges to 0 and 2)
  CCHEdgeRange range = topology.GetOutgoingEdges(1);

  // Should have original edges from node 1
  TEST_GREATER_OR_EQUAL(range.originalEnd, range.originalBegin, ());
}

UNIT_TEST(CCH_Customizer_Creation)
{
  CCHTopology topology = CreateSimpleTopology();
  CCHCustomizer customizer(topology);

  TEST(!customizer.IsCustomized(), ());
}

UNIT_TEST(CCH_CustomizationConfig_Equality)
{
  CCHCustomizationConfig config1;
  config1.profile = CCHProfile::Car;
  config1.avoidTolls = false;

  CCHCustomizationConfig config2;
  config2.profile = CCHProfile::Car;
  config2.avoidTolls = false;

  CCHCustomizationConfig config3;
  config3.profile = CCHProfile::Bicycle;
  config3.avoidTolls = false;

  TEST(config1 == config2, ());
  TEST(config1 != config3, ());
}

UNIT_TEST(CCH_QueryEngine_Creation)
{
  CCHTopology topology = CreateSimpleTopology();
  CCHCustomizer customizer(topology);
  CCHQueryEngine engine(topology, customizer);

  // Engine should not be ready without customization
  TEST(!engine.IsReady(), ());
}

UNIT_TEST(CCH_QueryEngine_InvalidNodes)
{
  CCHTopology topology = CreateSimpleTopology();
  CCHCustomizer customizer(topology);
  CCHQueryEngine engine(topology, customizer);

  // Query with invalid nodes should fail
  CCHQueryRequest request;
  request.sourceNode = UINT32_MAX;
  request.targetNode = 0;

  CCHQueryResult result = engine.Query(request);
  TEST(!result.success, ());
  TEST(!result.errorMessage.empty(), ());
}

UNIT_TEST(CCH_QueryEngine_OutOfRangeNodes)
{
  CCHTopology topology = CreateSimpleTopology();
  CCHCustomizer customizer(topology);
  CCHQueryEngine engine(topology, customizer);

  // Query with out-of-range nodes should fail
  CCHQueryRequest request;
  request.sourceNode = 100;  // Out of range
  request.targetNode = 0;

  CCHQueryResult result = engine.Query(request);
  TEST(!result.success, ());
}

UNIT_TEST(CCH_Shortcut_Flags)
{
  CCHShortcut shortcut;
  shortcut.SetForward(true);
  shortcut.SetBackward(false);

  TEST(shortcut.IsForward(), ());
  TEST(!shortcut.IsBackward(), ());

  shortcut.SetBackward(true);
  TEST(shortcut.IsForward(), ());
  TEST(shortcut.IsBackward(), ());

  shortcut.SetForward(false);
  TEST(!shortcut.IsForward(), ());
  TEST(shortcut.IsBackward(), ());
}

UNIT_TEST(CCH_OriginalEdge_Flags)
{
  CCHOriginalEdge edge;
  edge.SetForward(true);
  TEST(edge.IsForward(), ());

  edge.SetForward(false);
  TEST(!edge.IsForward(), ());
}

UNIT_TEST(CCH_NodeOrder_Serialization)
{
  CCHNodeOrder original;
  original.originalId = 42;
  original.contractedId = 7;
  original.level = 3;

  // Serialize
  std::vector<uint8_t> buffer;
  MemWriter<std::vector<uint8_t>> writer(buffer);
  original.Serialize(writer);

  // Deserialize
  CCHNodeOrder restored;
  MemReader reader(buffer.data(), buffer.size());
  ReaderSource<MemReader> source(reader);
  restored.Deserialize(source);

  TEST_EQUAL(original.originalId, restored.originalId, ());
  TEST_EQUAL(original.contractedId, restored.contractedId, ());
  TEST_EQUAL(original.level, restored.level, ());
}

UNIT_TEST(CCH_Shortcut_Serialization)
{
  CCHShortcut original;
  original.fromNode = 10;
  original.toNode = 20;
  original.middleNode = 15;
  original.SetForward(true);
  original.SetBackward(true);

  // Serialize
  std::vector<uint8_t> buffer;
  MemWriter<std::vector<uint8_t>> writer(buffer);
  original.Serialize(writer);

  // Deserialize
  CCHShortcut restored;
  MemReader reader(buffer.data(), buffer.size());
  ReaderSource<MemReader> source(reader);
  restored.Deserialize(source);

  TEST_EQUAL(original.fromNode, restored.fromNode, ());
  TEST_EQUAL(original.toNode, restored.toNode, ());
  TEST_EQUAL(original.middleNode, restored.middleNode, ());
  TEST_EQUAL(original.IsForward(), restored.IsForward(), ());
  TEST_EQUAL(original.IsBackward(), restored.IsBackward(), ());
}

UNIT_TEST(CCH_OriginalEdge_Serialization)
{
  CCHOriginalEdge original;
  original.fromNode = 5;
  original.toNode = 10;
  original.featureId = 12345;
  original.segmentIdx = 7;
  original.SetForward(true);

  // Serialize
  std::vector<uint8_t> buffer;
  MemWriter<std::vector<uint8_t>> writer(buffer);
  original.Serialize(writer);

  // Deserialize
  CCHOriginalEdge restored;
  MemReader reader(buffer.data(), buffer.size());
  ReaderSource<MemReader> source(reader);
  restored.Deserialize(source);

  TEST_EQUAL(original.fromNode, restored.fromNode, ());
  TEST_EQUAL(original.toNode, restored.toNode, ());
  TEST_EQUAL(original.featureId, restored.featureId, ());
  TEST_EQUAL(original.segmentIdx, restored.segmentIdx, ());
  TEST_EQUAL(original.IsForward(), restored.IsForward(), ());
}

UNIT_TEST(CCH_EmptyTopology)
{
  CCHTopology topology;

  TEST(!topology.IsValid(), ());
  TEST_EQUAL(topology.GetNodeCount(), 0, ());
  TEST_EQUAL(topology.GetOriginalEdgeCount(), 0, ());
  TEST_EQUAL(topology.GetShortcutCount(), 0, ());
}

}  // namespace cch_tests

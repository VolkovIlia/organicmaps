#include "routing/cch_topology.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"

#include <algorithm>
#include <cstring>

namespace routing
{

uint32_t CCHTopology::GetLevel(uint32_t contractedId) const
{
  ASSERT_LESS(contractedId, m_contractedToLevel.size(), ());
  return m_contractedToLevel[contractedId];
}

uint32_t CCHTopology::GetOriginalId(uint32_t contractedId) const
{
  ASSERT_LESS(contractedId, m_contractedToOriginal.size(), ());
  return m_contractedToOriginal[contractedId];
}

uint32_t CCHTopology::GetContractedId(uint32_t originalId) const
{
  ASSERT_LESS(originalId, m_originalToContracted.size(), ());
  return m_originalToContracted[originalId];
}

CCHEdgeRange CCHTopology::GetOutgoingEdges(uint32_t contractedId) const
{
  CCHEdgeRange range;

  if (contractedId < m_outgoingOriginalOffsets.size() - 1)
  {
    range.originalBegin = m_outgoingOriginalOffsets[contractedId];
    range.originalEnd = m_outgoingOriginalOffsets[contractedId + 1];
  }

  if (contractedId < m_outgoingShortcutOffsets.size() - 1)
  {
    range.shortcutBegin = m_outgoingShortcutOffsets[contractedId];
    range.shortcutEnd = m_outgoingShortcutOffsets[contractedId + 1];
  }

  return range;
}

CCHEdgeRange CCHTopology::GetIncomingEdges(uint32_t contractedId) const
{
  CCHEdgeRange range;

  if (contractedId < m_incomingOriginalOffsets.size() - 1)
  {
    range.originalBegin = m_incomingOriginalOffsets[contractedId];
    range.originalEnd = m_incomingOriginalOffsets[contractedId + 1];
  }

  if (contractedId < m_incomingShortcutOffsets.size() - 1)
  {
    range.shortcutBegin = m_incomingShortcutOffsets[contractedId];
    range.shortcutEnd = m_incomingShortcutOffsets[contractedId + 1];
  }

  return range;
}

bool CCHTopology::LoadFromMappedMemory(void const * data, size_t size)
{
  if (data == nullptr || size < sizeof(uint32_t) * 3)
    return false;

  auto const * ptr = static_cast<uint8_t const *>(data);

  // Parse header
  std::memcpy(&m_version, ptr, sizeof(uint32_t));
  ptr += sizeof(uint32_t);

  if (m_version != kCCHVersion)
  {
    LOG(LWARNING, ("CCH version mismatch:", m_version, "expected:", kCCHVersion));
    return false;
  }

  std::memcpy(&m_nodeCount, ptr, sizeof(uint64_t));
  ptr += sizeof(uint64_t);

  std::memcpy(&m_levelCount, ptr, sizeof(uint32_t));
  ptr += sizeof(uint32_t);

  // For memory-mapped loading, we use MemReader
  MemReader reader(data, size);
  ReaderSource<MemReader> source(reader);

  // Skip header (already read)
  source.Skip(sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint32_t));

  // Read node ordering
  uint64_t nodeOrderSize = ReadPrimitiveFromSource<uint64_t>(source);
  m_nodeOrder.resize(nodeOrderSize);
  for (auto & node : m_nodeOrder)
    node.Deserialize(source);

  // Read original edges
  uint64_t edgeCount = ReadPrimitiveFromSource<uint64_t>(source);
  m_originalEdges.resize(edgeCount);
  for (auto & edge : m_originalEdges)
    edge.Deserialize(source);

  // Read shortcuts
  uint64_t shortcutCount = ReadPrimitiveFromSource<uint64_t>(source);
  m_shortcuts.resize(shortcutCount);
  for (auto & shortcut : m_shortcuts)
    shortcut.Deserialize(source);

  // Read adjacency offsets
  auto readOffsets = [&source](std::vector<uint32_t> & offsets) {
    uint64_t sz = ReadPrimitiveFromSource<uint64_t>(source);
    offsets.resize(sz);
    for (auto & val : offsets)
      val = ReadPrimitiveFromSource<uint32_t>(source);
  };

  readOffsets(m_outgoingOriginalOffsets);
  readOffsets(m_outgoingShortcutOffsets);
  readOffsets(m_incomingOriginalOffsets);
  readOffsets(m_incomingShortcutOffsets);

  // Build lookup tables
  m_originalToContracted.resize(m_nodeCount, UINT32_MAX);
  m_contractedToOriginal.resize(m_nodeCount, UINT32_MAX);
  m_contractedToLevel.resize(m_nodeCount, 0);

  for (auto const & node : m_nodeOrder)
  {
    if (node.originalId < m_nodeCount && node.contractedId < m_nodeCount)
    {
      m_originalToContracted[node.originalId] = node.contractedId;
      m_contractedToOriginal[node.contractedId] = node.originalId;
      m_contractedToLevel[node.contractedId] = node.level;
    }
  }

  LOG(LINFO, ("CCH topology loaded:", m_nodeCount, "nodes,",
              m_originalEdges.size(), "edges,", m_shortcuts.size(), "shortcuts"));

  return true;
}

void CCHTopology::BuildAdjacencyOffsets()
{
  // Initialize offset arrays
  m_outgoingOriginalOffsets.assign(m_nodeCount + 1, 0);
  m_outgoingShortcutOffsets.assign(m_nodeCount + 1, 0);
  m_incomingOriginalOffsets.assign(m_nodeCount + 1, 0);
  m_incomingShortcutOffsets.assign(m_nodeCount + 1, 0);

  // Count outgoing original edges per node
  for (auto const & edge : m_originalEdges)
  {
    if (edge.fromNode < m_nodeCount)
      ++m_outgoingOriginalOffsets[edge.fromNode + 1];
    if (edge.toNode < m_nodeCount)
      ++m_incomingOriginalOffsets[edge.toNode + 1];
  }

  // Count shortcuts per node
  for (auto const & shortcut : m_shortcuts)
  {
    if (shortcut.fromNode < m_nodeCount)
      ++m_outgoingShortcutOffsets[shortcut.fromNode + 1];
    if (shortcut.toNode < m_nodeCount)
      ++m_incomingShortcutOffsets[shortcut.toNode + 1];
  }

  // Convert counts to prefix sums (offsets)
  for (size_t i = 1; i <= m_nodeCount; ++i)
  {
    m_outgoingOriginalOffsets[i] += m_outgoingOriginalOffsets[i - 1];
    m_outgoingShortcutOffsets[i] += m_outgoingShortcutOffsets[i - 1];
    m_incomingOriginalOffsets[i] += m_incomingOriginalOffsets[i - 1];
    m_incomingShortcutOffsets[i] += m_incomingShortcutOffsets[i - 1];
  }

  // Sort edges by fromNode for outgoing, by toNode for incoming
  // This ensures edges are grouped by node in the arrays
  std::stable_sort(m_originalEdges.begin(), m_originalEdges.end(),
    [](CCHOriginalEdge const & a, CCHOriginalEdge const & b) {
      return a.fromNode < b.fromNode;
    });

  std::stable_sort(m_shortcuts.begin(), m_shortcuts.end(),
    [](CCHShortcut const & a, CCHShortcut const & b) {
      return a.fromNode < b.fromNode;
    });

  // Build lookup tables
  m_originalToContracted.resize(m_nodeCount, UINT32_MAX);
  m_contractedToOriginal.resize(m_nodeCount, UINT32_MAX);
  m_contractedToLevel.resize(m_nodeCount, 0);

  for (auto const & node : m_nodeOrder)
  {
    if (node.originalId < m_nodeCount && node.contractedId < m_nodeCount)
    {
      m_originalToContracted[node.originalId] = node.contractedId;
      m_contractedToOriginal[node.contractedId] = node.originalId;
      m_contractedToLevel[node.contractedId] = node.level;
    }
  }
}

}  // namespace routing

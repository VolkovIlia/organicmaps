// Library Documentation Verified: 2026-02-04
// Source: Internal Organic Maps APIs
// API Version: CCH v1 (kCCHVersion = 1)
#pragma once

#include "routing_common/num_mwm_id.hpp"

#include "coding/reader.hpp"
#include "coding/writer.hpp"

#include "base/assert.hpp"

#include <cstdint>
#include <vector>

namespace routing
{

/// @brief Version for CCH format compatibility
constexpr uint32_t kCCHVersion = 1;

/// @brief Node ordering information for CCH
struct CCHNodeOrder
{
  uint32_t originalId = 0;    ///< Original node ID in road graph
  uint32_t contractedId = 0;  ///< Position in contraction order
  uint32_t level = 0;         ///< Contraction level (higher = contracted later)

  template <typename Writer>
  void Serialize(Writer & writer) const
  {
    WriteToSink(writer, originalId);
    WriteToSink(writer, contractedId);
    WriteToSink(writer, level);
  }

  template <typename Source>
  void Deserialize(Source & source)
  {
    originalId = ReadPrimitiveFromSource<uint32_t>(source);
    contractedId = ReadPrimitiveFromSource<uint32_t>(source);
    level = ReadPrimitiveFromSource<uint32_t>(source);
  }
};

/// @brief Shortcut edge created during contraction
struct CCHShortcut
{
  uint32_t fromNode = 0;      ///< Source node (contracted ID)
  uint32_t toNode = 0;        ///< Target node (contracted ID)
  uint32_t middleNode = 0;    ///< Node contracted to create this shortcut
  uint8_t flags = 0;          ///< Direction flags (bit 0: forward, bit 1: backward)

  bool IsForward() const { return (flags & 0x01) != 0; }
  bool IsBackward() const { return (flags & 0x02) != 0; }

  void SetForward(bool val) { flags = val ? (flags | 0x01) : (flags & ~0x01); }
  void SetBackward(bool val) { flags = val ? (flags | 0x02) : (flags & ~0x02); }

  template <typename Writer>
  void Serialize(Writer & writer) const
  {
    WriteToSink(writer, fromNode);
    WriteToSink(writer, toNode);
    WriteToSink(writer, middleNode);
    WriteToSink(writer, flags);
  }

  template <typename Source>
  void Deserialize(Source & source)
  {
    fromNode = ReadPrimitiveFromSource<uint32_t>(source);
    toNode = ReadPrimitiveFromSource<uint32_t>(source);
    middleNode = ReadPrimitiveFromSource<uint32_t>(source);
    flags = ReadPrimitiveFromSource<uint8_t>(source);
  }
};

/// @brief Original edge in the road graph for CCH
struct CCHOriginalEdge
{
  uint32_t fromNode = 0;      ///< Source node (contracted ID)
  uint32_t toNode = 0;        ///< Target node (contracted ID)
  uint32_t featureId = 0;     ///< Feature ID for weight lookup
  uint16_t segmentIdx = 0;    ///< Segment index within feature
  uint8_t flags = 0;          ///< Direction flag (bit 0: forward)

  bool IsForward() const { return (flags & 0x01) != 0; }
  void SetForward(bool val) { flags = val ? (flags | 0x01) : (flags & ~0x01); }

  template <typename Writer>
  void Serialize(Writer & writer) const
  {
    WriteToSink(writer, fromNode);
    WriteToSink(writer, toNode);
    WriteToSink(writer, featureId);
    WriteToSink(writer, segmentIdx);
    WriteToSink(writer, flags);
  }

  template <typename Source>
  void Deserialize(Source & source)
  {
    fromNode = ReadPrimitiveFromSource<uint32_t>(source);
    toNode = ReadPrimitiveFromSource<uint32_t>(source);
    featureId = ReadPrimitiveFromSource<uint32_t>(source);
    segmentIdx = ReadPrimitiveFromSource<uint16_t>(source);
    flags = ReadPrimitiveFromSource<uint8_t>(source);
  }
};

/// @brief Range of edges for adjacency traversal
struct CCHEdgeRange
{
  uint32_t originalBegin = 0;
  uint32_t originalEnd = 0;
  uint32_t shortcutBegin = 0;
  uint32_t shortcutEnd = 0;
};

/// @brief CCH topology stored in MWM file
class CCHTopology
{
public:
  CCHTopology() = default;

  /// @brief Check if topology is valid and loaded
  bool IsValid() const { return m_version == kCCHVersion && m_nodeCount > 0; }

  /// @brief Get statistics
  uint64_t GetNodeCount() const { return m_nodeCount; }
  uint64_t GetOriginalEdgeCount() const { return m_originalEdges.size(); }
  uint64_t GetShortcutCount() const { return m_shortcuts.size(); }
  uint32_t GetLevelCount() const { return m_levelCount; }

  /// @brief Get node level (for query ordering)
  uint32_t GetLevel(uint32_t contractedId) const;

  /// @brief Get original ID from contracted ID
  uint32_t GetOriginalId(uint32_t contractedId) const;

  /// @brief Get contracted ID from original ID
  uint32_t GetContractedId(uint32_t originalId) const;

  /// @brief Get outgoing edges for a node (both original and shortcuts)
  CCHEdgeRange GetOutgoingEdges(uint32_t contractedId) const;

  /// @brief Get incoming edges for a node (both original and shortcuts)
  CCHEdgeRange GetIncomingEdges(uint32_t contractedId) const;

  /// @brief Access original edge by index
  CCHOriginalEdge const & GetOriginalEdge(uint32_t idx) const
  {
    ASSERT_LESS(idx, m_originalEdges.size(), ());
    return m_originalEdges[idx];
  }

  /// @brief Access shortcut by index
  CCHShortcut const & GetShortcut(uint32_t idx) const
  {
    ASSERT_LESS(idx, m_shortcuts.size(), ());
    return m_shortcuts[idx];
  }

  /// @brief Serialization to writer
  template <typename Writer>
  void Serialize(Writer & writer) const;

  /// @brief Deserialization from source
  template <typename Source>
  void Deserialize(Source & source);

  /// @brief Memory-mapped loading (preferred for mobile)
  bool LoadFromMappedMemory(void const * data, size_t size);

  /// @brief Builder access for construction
  void SetNodeCount(uint64_t count) { m_nodeCount = count; }
  void SetLevelCount(uint32_t count) { m_levelCount = count; }

  std::vector<CCHNodeOrder> & GetNodeOrderForBuilder() { return m_nodeOrder; }
  std::vector<CCHOriginalEdge> & GetOriginalEdgesForBuilder() { return m_originalEdges; }
  std::vector<CCHShortcut> & GetShortcutsForBuilder() { return m_shortcuts; }

  void BuildAdjacencyOffsets();

private:
  uint32_t m_version = kCCHVersion;
  uint64_t m_nodeCount = 0;
  uint32_t m_levelCount = 0;

  std::vector<CCHNodeOrder> m_nodeOrder;
  std::vector<CCHOriginalEdge> m_originalEdges;
  std::vector<CCHShortcut> m_shortcuts;

  // Adjacency list offsets for efficient traversal
  std::vector<uint32_t> m_outgoingOriginalOffsets;
  std::vector<uint32_t> m_outgoingShortcutOffsets;
  std::vector<uint32_t> m_incomingOriginalOffsets;
  std::vector<uint32_t> m_incomingShortcutOffsets;

  // Lookup tables
  std::vector<uint32_t> m_originalToContracted;
  std::vector<uint32_t> m_contractedToOriginal;
  std::vector<uint32_t> m_contractedToLevel;
};

// Template implementations

template <typename Writer>
void CCHTopology::Serialize(Writer & writer) const
{
  // Header
  WriteToSink(writer, m_version);
  WriteToSink(writer, m_nodeCount);
  WriteToSink(writer, m_levelCount);

  // Node ordering
  WriteToSink(writer, static_cast<uint64_t>(m_nodeOrder.size()));
  for (auto const & node : m_nodeOrder)
    node.Serialize(writer);

  // Original edges
  WriteToSink(writer, static_cast<uint64_t>(m_originalEdges.size()));
  for (auto const & edge : m_originalEdges)
    edge.Serialize(writer);

  // Shortcuts
  WriteToSink(writer, static_cast<uint64_t>(m_shortcuts.size()));
  for (auto const & shortcut : m_shortcuts)
    shortcut.Serialize(writer);

  // Adjacency offsets
  auto writeOffsets = [&writer](std::vector<uint32_t> const & offsets) {
    WriteToSink(writer, static_cast<uint64_t>(offsets.size()));
    for (auto const val : offsets)
      WriteToSink(writer, val);
  };

  writeOffsets(m_outgoingOriginalOffsets);
  writeOffsets(m_outgoingShortcutOffsets);
  writeOffsets(m_incomingOriginalOffsets);
  writeOffsets(m_incomingShortcutOffsets);
}

template <typename Source>
void CCHTopology::Deserialize(Source & source)
{
  // Header
  m_version = ReadPrimitiveFromSource<uint32_t>(source);
  m_nodeCount = ReadPrimitiveFromSource<uint64_t>(source);
  m_levelCount = ReadPrimitiveFromSource<uint32_t>(source);

  // Node ordering
  uint64_t nodeOrderSize = ReadPrimitiveFromSource<uint64_t>(source);
  m_nodeOrder.resize(nodeOrderSize);
  for (auto & node : m_nodeOrder)
    node.Deserialize(source);

  // Original edges
  uint64_t edgeCount = ReadPrimitiveFromSource<uint64_t>(source);
  m_originalEdges.resize(edgeCount);
  for (auto & edge : m_originalEdges)
    edge.Deserialize(source);

  // Shortcuts
  uint64_t shortcutCount = ReadPrimitiveFromSource<uint64_t>(source);
  m_shortcuts.resize(shortcutCount);
  for (auto & shortcut : m_shortcuts)
    shortcut.Deserialize(source);

  // Adjacency offsets
  auto readOffsets = [&source](std::vector<uint32_t> & offsets) {
    uint64_t size = ReadPrimitiveFromSource<uint64_t>(source);
    offsets.resize(size);
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
}

}  // namespace routing

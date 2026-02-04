// Library Documentation Verified: 2026-02-04
// Source: Internal Organic Maps APIs
// API Version: CCH v1 (kCCHVersion = 1)
#include "routing/cch_customizer.hpp"
#include "routing/routing_constants.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"
#include "base/timer.hpp"

#include <algorithm>

namespace routing
{

CCHCustomizer::CCHCustomizer(CCHTopology const & topology)
  : m_topology(topology)
{
  // Pre-allocate weight vectors
  m_originalEdgeWeights.resize(topology.GetOriginalEdgeCount(), kDefaultEdgeWeight);
  m_shortcutWeights.resize(topology.GetShortcutCount(), kDefaultEdgeWeight);
}

bool CCHCustomizer::Customize(CCHCustomizationConfig const & config,
                              Geometry & geometry,
                              EdgeEstimator const & estimator)
{
  if (IsCustomized(config))
    return true;  // Already customized for this profile

  base::Timer timer;
  LOG(LINFO, ("CCH customization started for profile:",
              static_cast<int>(config.profile)));

  // Step 1: Calculate weights for all original edges
  size_t const edgeCount = m_topology.GetOriginalEdgeCount();
  for (size_t i = 0; i < edgeCount; ++i)
  {
    auto const & edge = m_topology.GetOriginalEdge(static_cast<uint32_t>(i));
    m_originalEdgeWeights[i] = CalcOriginalEdgeWeight(edge, geometry, estimator);
  }

  // Step 2: Bottom-up weight propagation for shortcuts
  PropagateWeights();

  m_currentConfig = config;
  m_isCustomized = true;

  LOG(LINFO, ("CCH customization completed in", timer.ElapsedSeconds(), "seconds"));
  LOG(LINFO, ("Customized", edgeCount, "edges and",
              m_topology.GetShortcutCount(), "shortcuts"));

  return true;
}

bool CCHCustomizer::IsCustomized(CCHCustomizationConfig const & config) const
{
  return m_isCustomized && m_currentConfig == config;
}

double CCHCustomizer::GetOriginalEdgeWeight(uint32_t edgeIdx) const
{
  ASSERT(m_isCustomized, ());
  ASSERT_LESS(edgeIdx, m_originalEdgeWeights.size(), ());
  return m_originalEdgeWeights[edgeIdx];
}

double CCHCustomizer::GetShortcutWeight(uint32_t shortcutIdx) const
{
  ASSERT(m_isCustomized, ());
  ASSERT_LESS(shortcutIdx, m_shortcutWeights.size(), ());
  return m_shortcutWeights[shortcutIdx];
}

void CCHCustomizer::Reset()
{
  m_isCustomized = false;
  std::fill(m_originalEdgeWeights.begin(), m_originalEdgeWeights.end(),
            kDefaultEdgeWeight);
  std::fill(m_shortcutWeights.begin(), m_shortcutWeights.end(),
            kDefaultEdgeWeight);
}

double CCHCustomizer::CalcOriginalEdgeWeight(CCHOriginalEdge const & edge,
                                             Geometry & geometry,
                                             EdgeEstimator const & estimator) const
{
  // Create segment from edge info
  // Using fake MWM ID (0) for now - in production would need actual MWM context
  Segment const segment(kFakeNumMwmId, edge.featureId, edge.segmentIdx,
                        edge.IsForward());

  // Get road geometry for the segment
  auto const & road = geometry.GetRoad(edge.featureId);

  // Check if feature is valid
  if (!road.IsValid())
    return kInfinity;

  // Calculate weight using edge estimator
  double const weight = estimator.CalcSegmentWeight(
      segment, road, EdgeEstimator::Purpose::Weight);

  return weight > 0 ? weight : kDefaultEdgeWeight;
}

void CCHCustomizer::PropagateWeights()
{
  // Process levels from bottom to top
  // Shortcuts at level L depend only on edges/shortcuts at levels < L

  uint32_t const levelCount = m_topology.GetLevelCount();
  size_t const shortcutCount = m_topology.GetShortcutCount();

  // Build level -> shortcut index mapping
  std::vector<std::vector<uint32_t>> shortcutsAtLevel(levelCount);

  for (size_t i = 0; i < shortcutCount; ++i)
  {
    auto const & shortcut = m_topology.GetShortcut(static_cast<uint32_t>(i));
    uint32_t const middleLevel = m_topology.GetLevel(shortcut.middleNode);

    if (middleLevel < levelCount)
      shortcutsAtLevel[middleLevel].push_back(static_cast<uint32_t>(i));
  }

  // Process each level bottom-up
  for (uint32_t level = 0; level < levelCount; ++level)
  {
    for (uint32_t shortcutIdx : shortcutsAtLevel[level])
    {
      auto const & shortcut = m_topology.GetShortcut(shortcutIdx);

      // Shortcut weight = path through middle node
      // We need: weight(from -> middle) + weight(middle -> to)
      double const weight1 = FindPathWeight(shortcut.fromNode, shortcut.middleNode);
      double const weight2 = FindPathWeight(shortcut.middleNode, shortcut.toNode);

      if (weight1 < kInfinity && weight2 < kInfinity)
        m_shortcutWeights[shortcutIdx] = weight1 + weight2;
      else
        m_shortcutWeights[shortcutIdx] = kInfinity;
    }
  }
}

double CCHCustomizer::FindPathWeight(uint32_t fromNode, uint32_t toNode) const
{
  // Simple implementation: look for direct edge or shortcut
  // In full implementation, would use dynamic programming

  // Check original edges from fromNode
  auto const edges = m_topology.GetOutgoingEdges(fromNode);

  // Search in original edges
  for (uint32_t i = edges.originalBegin; i < edges.originalEnd; ++i)
  {
    auto const & edge = m_topology.GetOriginalEdge(i);
    if (edge.toNode == toNode)
      return m_originalEdgeWeights[i];
  }

  // Search in shortcuts
  for (uint32_t i = edges.shortcutBegin; i < edges.shortcutEnd; ++i)
  {
    auto const & shortcut = m_topology.GetShortcut(i);
    if (shortcut.toNode == toNode && shortcut.IsForward())
      return m_shortcutWeights[i];
  }

  // No direct path found
  return kInfinity;
}

}  // namespace routing

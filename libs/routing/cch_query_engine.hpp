#pragma once

#include "routing/cch_customizer.hpp"
#include "routing/cch_path_unpacker.hpp"
#include "routing/cch_topology.hpp"
#include "routing/segment.hpp"

#include "geometry/latlon.hpp"

#include "routing_common/num_mwm_id.hpp"

#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <vector>

namespace routing
{

/// @brief Result of CCH query
struct CCHQueryResult
{
  bool success = false;
  std::string errorMessage;
  double queryTimeMs = 0.0;

  double distanceMeters = 0.0;
  double durationSeconds = 0.0;

  // Path as sequence of segments (for UI display)
  std::vector<Segment> path;

  // For debugging: was fallback used?
  bool usedFallback = false;
};

/// @brief Query request parameters
struct CCHQueryRequest
{
  uint32_t sourceNode = UINT32_MAX;
  uint32_t targetNode = UINT32_MAX;
  CCHProfile profile = CCHProfile::Car;
  bool computeAlternatives = false;
  int maxAlternatives = 3;
};

/// @brief Interface for CCH query execution
class ICCHQueryEngine
{
public:
  virtual ~ICCHQueryEngine() = default;

  /// @brief Execute CCH query with node IDs
  virtual CCHQueryResult Query(CCHQueryRequest const & request) = 0;

  /// @brief Check if CCH is available for given MWM
  virtual bool IsCCHAvailable(NumMwmId mwmId) const = 0;

  /// @brief Customize CCH for profile
  virtual bool Customize(CCHCustomizationConfig const & config) = 0;

  /// @brief Check if engine is ready for queries
  virtual bool IsReady() const = 0;
};

/// @brief Production CCH query engine using bidirectional Dijkstra
class CCHQueryEngine : public ICCHQueryEngine
{
public:
  CCHQueryEngine(CCHTopology const & topology, CCHCustomizer & customizer);

  CCHQueryResult Query(CCHQueryRequest const & request) override;
  bool IsCCHAvailable(NumMwmId mwmId) const override;
  bool Customize(CCHCustomizationConfig const & config) override;
  bool IsReady() const override;

  /// @brief Reset query state for new query
  void ResetQueryState();

private:
  using PriorityQueue = std::priority_queue<
      CCHSearchState,
      std::vector<CCHSearchState>,
      std::greater<CCHSearchState>>;

  /// @brief Find path using bidirectional Dijkstra
  bool FindPath(uint32_t sourceNode, uint32_t targetNode,
                std::vector<CCHSearchState> & forwardTree,
                std::vector<CCHSearchState> & backwardTree,
                uint32_t & meetingNode);

  /// @brief Process forward search step
  void ProcessForwardStep(PriorityQueue & queue, double & bestDist,
                          uint32_t & meetingNode,
                          std::vector<CCHSearchState> & tree);

  /// @brief Process backward search step
  void ProcessBackwardStep(PriorityQueue & queue, double & bestDist,
                           uint32_t & meetingNode,
                           std::vector<CCHSearchState> & tree);

private:
  CCHTopology const & m_topology;
  CCHCustomizer & m_customizer;
  CCHPathUnpacker m_pathUnpacker;

  // Reusable data structures
  std::vector<double> m_forwardDist;
  std::vector<double> m_backwardDist;
  std::vector<bool> m_forwardVisited;
  std::vector<bool> m_backwardVisited;
  std::vector<CCHSearchState> m_forwardParent;
  std::vector<CCHSearchState> m_backwardParent;
};

/// @brief Comparison operator for priority queue
inline bool operator>(CCHSearchState const & a, CCHSearchState const & b)
{
  return a.distance > b.distance;
}

}  // namespace routing

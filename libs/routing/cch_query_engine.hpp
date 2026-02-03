// Library Documentation Verified: 2026-02-04
// Source: Internal Organic Maps APIs
// API Version: CCH v1 (kCCHVersion = 1)
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

enum class SearchDirection;

/// @brief Result of CCH query
struct CCHQueryResult
{
  bool success = false;
  std::string errorMessage;
  double queryTimeMs = 0.0;

  double distanceMeters = 0.0;
  double durationSeconds = 0.0;

  std::vector<Segment> path;
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
  virtual CCHQueryResult Query(CCHQueryRequest const & request) = 0;
  virtual bool IsCCHAvailable(NumMwmId mwmId) const = 0;
  virtual bool Customize(CCHCustomizationConfig const & config) = 0;
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

  void ResetQueryState();

private:
  using PriorityQueue = std::priority_queue<
      CCHSearchState,
      std::vector<CCHSearchState>,
      std::greater<CCHSearchState>>;

  /// @brief Validate query request parameters.
  bool ValidateQueryRequest(CCHQueryRequest const & request,
                            CCHQueryResult & result) const;

  /// @brief Initialize search queues for bidirectional Dijkstra.
  void InitializeSearch(uint32_t sourceNode, uint32_t targetNode,
                        PriorityQueue & forwardQueue,
                        PriorityQueue & backwardQueue);

  /// @brief Check if search can be terminated early.
  bool CheckTermination(PriorityQueue const & forwardQueue,
                        PriorityQueue const & backwardQueue,
                        double bestDist) const;

  /// @brief Find path using bidirectional Dijkstra.
  bool FindPath(uint32_t sourceNode, uint32_t targetNode,
                std::vector<CCHSearchState> & forwardTree,
                std::vector<CCHSearchState> & backwardTree,
                uint32_t & meetingNode);

  /// @brief Process single search step in given direction.
  void ProcessSearchStep(SearchDirection direction,
                         PriorityQueue & queue,
                         std::vector<CCHSearchState> & tree,
                         double & bestDist,
                         uint32_t & meetingNode);

  /// @brief Try to update meeting point if searches have met.
  void TryUpdateMeetingPoint(uint32_t node, double myNodeDist,
                             std::vector<bool> const & otherVisited,
                             std::vector<double> const & otherDist,
                             double & bestDist,
                             uint32_t & meetingNode,
                             SearchDirection direction) const;

  /// @brief Relax edges from current node.
  void RelaxEdges(SearchDirection direction,
                  CCHSearchState const & state,
                  PriorityQueue & queue,
                  std::vector<double> & myDist,
                  std::vector<bool> const & otherVisited,
                  std::vector<double> const & otherDist,
                  double & bestDist,
                  uint32_t & meetingNode);

  /// @brief Relax original edges.
  void RelaxOriginalEdges(SearchDirection direction,
                          CCHSearchState const & state,
                          CCHEdgeRange const & edges,
                          uint32_t nodeLevel,
                          PriorityQueue & queue,
                          std::vector<double> & myDist,
                          std::vector<bool> const & otherVisited,
                          std::vector<double> const & otherDist,
                          double & bestDist,
                          uint32_t & meetingNode);

  /// @brief Relax shortcut edges.
  void RelaxShortcuts(SearchDirection direction,
                      CCHSearchState const & state,
                      CCHEdgeRange const & edges,
                      uint32_t nodeLevel,
                      PriorityQueue & queue,
                      std::vector<double> & myDist,
                      std::vector<bool> const & otherVisited,
                      std::vector<double> const & otherDist,
                      double & bestDist,
                      uint32_t & meetingNode);

private:
  CCHTopology const & m_topology;
  CCHCustomizer & m_customizer;
  CCHPathUnpacker m_pathUnpacker;

  std::vector<double> m_forwardDist;
  std::vector<double> m_backwardDist;
  std::vector<bool> m_forwardVisited;
  std::vector<bool> m_backwardVisited;
  std::vector<CCHSearchState> m_forwardParent;
  std::vector<CCHSearchState> m_backwardParent;
};

inline bool operator>(CCHSearchState const & a, CCHSearchState const & b)
{
  return a.distance > b.distance;
}

}  // namespace routing

#pragma once

#include "mesh/connection_manager.hpp"

#include <memory>
#include <vector>

namespace mesh
{
/// \brief Coordinator for opportunistic P2P traffic sharing.
/// Manages when and what data to share with discovered peers.
class OpportunisticShare
{
public:
  explicit OpportunisticShare(std::shared_ptr<IConnectionManager> connectionManager);
  ~OpportunisticShare();

  /// \brief Start opportunistic sharing.
  void Start();

  /// \brief Stop opportunistic sharing.
  void Stop();

  /// \brief Check if sharing is active.
  bool IsActive() const;

  /// \brief Queue traffic data for sharing with peers.
  /// \param trafficData Aggregated traffic data to share.
  void QueueTrafficData(std::vector<uint8_t> const & trafficData);

  /// \brief Queue gossip gradients for sharing.
  /// \param gradients Gossip learning gradients.
  void QueueGossipGradients(std::vector<uint8_t> const & gradients);

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};
}  // namespace mesh

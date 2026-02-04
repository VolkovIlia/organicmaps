#pragma once

#include "mesh/ble_constants.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mesh
{
/// \brief Peer information for BLE connections.
struct PeerInfo
{
  std::string deviceId;
  std::string rollingId;
  ConnectionState state = ConnectionState::Disconnected;
  int rssi = 0;
  uint64_t lastSeenTimestamp = 0;
};

/// \brief Abstract interface for connection management.
/// Platform-specific implementations will be provided for Android/iOS.
class IConnectionManager
{
public:
  using OnPeerDiscovered = std::function<void(PeerInfo const &)>;
  using OnPeerConnected = std::function<void(std::string const & deviceId)>;
  using OnPeerDisconnected = std::function<void(std::string const & deviceId)>;
  using OnDataReceived = std::function<void(std::string const & deviceId,
                                            std::vector<uint8_t> const & data)>;

  virtual ~IConnectionManager() = default;

  /// \brief Start advertising and scanning for peers.
  virtual void Start() = 0;

  /// \brief Stop advertising and scanning.
  virtual void Stop() = 0;

  /// \brief Check if manager is running.
  virtual bool IsRunning() const = 0;

  /// \brief Connect to a discovered peer.
  /// \param deviceId Device identifier.
  /// \return True if connection initiated.
  virtual bool Connect(std::string const & deviceId) = 0;

  /// \brief Disconnect from a peer.
  /// \param deviceId Device identifier.
  virtual void Disconnect(std::string const & deviceId) = 0;

  /// \brief Send data to a connected peer.
  /// \param deviceId Device identifier.
  /// \param data Data to send.
  /// \return True if data was queued for sending.
  virtual bool SendData(std::string const & deviceId,
                        std::vector<uint8_t> const & data) = 0;

  /// \brief Get list of currently connected peers.
  virtual std::vector<PeerInfo> GetConnectedPeers() const = 0;

  /// \brief Set callback for peer discovery.
  virtual void SetOnPeerDiscovered(OnPeerDiscovered callback) = 0;

  /// \brief Set callback for peer connection.
  virtual void SetOnPeerConnected(OnPeerConnected callback) = 0;

  /// \brief Set callback for peer disconnection.
  virtual void SetOnPeerDisconnected(OnPeerDisconnected callback) = 0;

  /// \brief Set callback for data reception.
  virtual void SetOnDataReceived(OnDataReceived callback) = 0;
};

/// \brief Connection manager implementation with platform abstraction.
class ConnectionManager : public IConnectionManager
{
public:
  ConnectionManager();
  ~ConnectionManager() override;

  void Start() override;
  void Stop() override;
  bool IsRunning() const override;

  bool Connect(std::string const & deviceId) override;
  void Disconnect(std::string const & deviceId) override;
  bool SendData(std::string const & deviceId,
                std::vector<uint8_t> const & data) override;

  std::vector<PeerInfo> GetConnectedPeers() const override;

  void SetOnPeerDiscovered(OnPeerDiscovered callback) override;
  void SetOnPeerConnected(OnPeerConnected callback) override;
  void SetOnPeerDisconnected(OnPeerDisconnected callback) override;
  void SetOnDataReceived(OnDataReceived callback) override;

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};
}  // namespace mesh

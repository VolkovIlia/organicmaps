#include "mesh/connection_manager.hpp"

#include "base/logging.hpp"

#include <mutex>

namespace mesh
{
class ConnectionManager::Impl
{
public:
  Impl() = default;

  void Start()
  {
    std::lock_guard lock(m_mutex);
    if (m_running)
      return;

    LOG(LINFO, ("ConnectionManager starting"));
    m_running = true;
    // Platform-specific start will be implemented by subclasses
  }

  void Stop()
  {
    std::lock_guard lock(m_mutex);
    if (!m_running)
      return;

    LOG(LINFO, ("ConnectionManager stopping"));
    m_running = false;
    m_peers.clear();
  }

  bool IsRunning() const
  {
    std::lock_guard lock(m_mutex);
    return m_running;
  }

  bool Connect(std::string const & deviceId)
  {
    std::lock_guard lock(m_mutex);
    if (!m_running)
      return false;

    auto it = m_peers.find(deviceId);
    if (it == m_peers.end())
    {
      LOG(LWARNING, ("Unknown device:", deviceId));
      return false;
    }

    if (it->second.state != ConnectionState::Disconnected)
    {
      LOG(LWARNING, ("Device not in disconnected state:", deviceId));
      return false;
    }

    it->second.state = ConnectionState::Connecting;
    // Platform-specific connection will be implemented by subclasses
    return true;
  }

  void Disconnect(std::string const & deviceId)
  {
    std::lock_guard lock(m_mutex);
    auto it = m_peers.find(deviceId);
    if (it == m_peers.end())
      return;

    if (it->second.state == ConnectionState::Connected)
    {
      it->second.state = ConnectionState::Disconnecting;
      // Platform-specific disconnection will be implemented
    }
  }

  bool SendData(std::string const & deviceId, std::vector<uint8_t> const & data)
  {
    std::lock_guard lock(m_mutex);
    auto it = m_peers.find(deviceId);
    if (it == m_peers.end() || it->second.state != ConnectionState::Connected)
      return false;

    // Platform-specific send will be implemented
    return true;
  }

  std::vector<PeerInfo> GetConnectedPeers() const
  {
    std::lock_guard lock(m_mutex);
    std::vector<PeerInfo> result;
    for (auto const & [_, peer] : m_peers)
    {
      if (peer.state == ConnectionState::Connected)
        result.push_back(peer);
    }
    return result;
  }

  void SetOnPeerDiscovered(OnPeerDiscovered callback)
  {
    std::lock_guard lock(m_mutex);
    m_onPeerDiscovered = std::move(callback);
  }

  void SetOnPeerConnected(OnPeerConnected callback)
  {
    std::lock_guard lock(m_mutex);
    m_onPeerConnected = std::move(callback);
  }

  void SetOnPeerDisconnected(OnPeerDisconnected callback)
  {
    std::lock_guard lock(m_mutex);
    m_onPeerDisconnected = std::move(callback);
  }

  void SetOnDataReceived(OnDataReceived callback)
  {
    std::lock_guard lock(m_mutex);
    m_onDataReceived = std::move(callback);
  }

protected:
  void OnPeerDiscoveredInternal(PeerInfo const & peer)
  {
    std::lock_guard lock(m_mutex);
    m_peers[peer.deviceId] = peer;
    if (m_onPeerDiscovered)
      m_onPeerDiscovered(peer);
  }

  void OnPeerConnectedInternal(std::string const & deviceId)
  {
    std::lock_guard lock(m_mutex);
    auto it = m_peers.find(deviceId);
    if (it != m_peers.end())
    {
      it->second.state = ConnectionState::Connected;
      if (m_onPeerConnected)
        m_onPeerConnected(deviceId);
    }
  }

  void OnPeerDisconnectedInternal(std::string const & deviceId)
  {
    std::lock_guard lock(m_mutex);
    auto it = m_peers.find(deviceId);
    if (it != m_peers.end())
    {
      it->second.state = ConnectionState::Disconnected;
      if (m_onPeerDisconnected)
        m_onPeerDisconnected(deviceId);
    }
  }

  void OnDataReceivedInternal(std::string const & deviceId,
                              std::vector<uint8_t> const & data)
  {
    std::lock_guard lock(m_mutex);
    if (m_onDataReceived)
      m_onDataReceived(deviceId, data);
  }

private:
  mutable std::mutex m_mutex;
  bool m_running = false;
  std::unordered_map<std::string, PeerInfo> m_peers;

  OnPeerDiscovered m_onPeerDiscovered;
  OnPeerConnected m_onPeerConnected;
  OnPeerDisconnected m_onPeerDisconnected;
  OnDataReceived m_onDataReceived;
};

ConnectionManager::ConnectionManager() : m_impl(std::make_unique<Impl>()) {}

ConnectionManager::~ConnectionManager() = default;

void ConnectionManager::Start() { m_impl->Start(); }

void ConnectionManager::Stop() { m_impl->Stop(); }

bool ConnectionManager::IsRunning() const { return m_impl->IsRunning(); }

bool ConnectionManager::Connect(std::string const & deviceId)
{
  return m_impl->Connect(deviceId);
}

void ConnectionManager::Disconnect(std::string const & deviceId)
{
  m_impl->Disconnect(deviceId);
}

bool ConnectionManager::SendData(std::string const & deviceId,
                                 std::vector<uint8_t> const & data)
{
  return m_impl->SendData(deviceId, data);
}

std::vector<PeerInfo> ConnectionManager::GetConnectedPeers() const
{
  return m_impl->GetConnectedPeers();
}

void ConnectionManager::SetOnPeerDiscovered(OnPeerDiscovered callback)
{
  m_impl->SetOnPeerDiscovered(std::move(callback));
}

void ConnectionManager::SetOnPeerConnected(OnPeerConnected callback)
{
  m_impl->SetOnPeerConnected(std::move(callback));
}

void ConnectionManager::SetOnPeerDisconnected(OnPeerDisconnected callback)
{
  m_impl->SetOnPeerDisconnected(std::move(callback));
}

void ConnectionManager::SetOnDataReceived(OnDataReceived callback)
{
  m_impl->SetOnDataReceived(std::move(callback));
}
}  // namespace mesh

#include "mesh/opportunistic_share.hpp"

#include "mesh/ble_protocol.hpp"

#include "base/logging.hpp"

#include <mutex>
#include <queue>

namespace mesh
{
class OpportunisticShare::Impl
{
public:
  Impl(std::shared_ptr<IConnectionManager> connectionManager, OpportunisticShareConfig config)
    : m_connectionManager(std::move(connectionManager))
    , m_config(std::move(config))
  {
  }

  void Start()
  {
    std::lock_guard lock(m_mutex);
    if (m_active)
      return;

    LOG(LINFO, ("OpportunisticShare starting"));
    m_active = true;

    m_connectionManager->SetOnPeerConnected([this](std::string const & deviceId) {
      OnPeerConnected(deviceId);
    });

    m_connectionManager->SetOnDataReceived(
        [this](std::string const & deviceId, std::vector<uint8_t> const & data) {
          OnDataReceived(deviceId, data);
        });

    m_connectionManager->Start();
  }

  void Stop()
  {
    std::lock_guard lock(m_mutex);
    if (!m_active)
      return;

    LOG(LINFO, ("OpportunisticShare stopping"));
    m_connectionManager->Stop();
    m_active = false;

    // Clear queues
    while (!m_trafficQueue.empty())
      m_trafficQueue.pop();
    while (!m_gradientQueue.empty())
      m_gradientQueue.pop();
  }

  bool IsActive() const
  {
    std::lock_guard lock(m_mutex);
    return m_active;
  }

  void QueueTrafficData(std::vector<uint8_t> const & trafficData)
  {
    std::lock_guard lock(m_mutex);
    if (!m_active)
      return;

    // Drop oldest entries if queue is full
    while (m_trafficQueue.size() >= m_config.maxQueueSize)
      m_trafficQueue.pop();

    m_trafficQueue.push(trafficData);
    TrySendPendingData();
  }

  void QueueGossipGradients(std::vector<uint8_t> const & gradients)
  {
    std::lock_guard lock(m_mutex);
    if (!m_active)
      return;

    // Drop oldest entries if queue is full
    while (m_gradientQueue.size() >= m_config.maxQueueSize)
      m_gradientQueue.pop();

    m_gradientQueue.push(gradients);
    TrySendPendingData();
  }

private:
  void OnPeerConnected(std::string const & deviceId)
  {
    std::lock_guard lock(m_mutex);
    LOG(LINFO, ("Peer connected:", deviceId));
    TrySendPendingData();
  }

  void OnDataReceived(std::string const & deviceId, std::vector<uint8_t> const & data)
  {
    if (!BleProtocol::ValidateMessage(data))
    {
      LOG(LWARNING, ("Invalid message from:", deviceId));
      return;
    }

    auto header = BleProtocol::DeserializeHeader(data);
    if (!header)
      return;

    auto payload = BleProtocol::GetPayload(data);
    LOG(LINFO, ("Received", DebugPrint(header->type), "from", deviceId,
                "payload size:", payload.size()));

    // Process based on message type
    switch (header->type)
    {
    case MessageType::TrafficData:
      // TODO: Process received traffic data
      break;
    case MessageType::GossipGradients:
      // TODO: Process received gradients
      break;
    case MessageType::DeviceInfo:
    case MessageType::Ack:
      break;
    }
  }

  void TrySendPendingData()
  {
    // Get connected peers
    auto peers = m_connectionManager->GetConnectedPeers();
    if (peers.empty())
      return;

    // Send traffic data
    while (!m_trafficQueue.empty())
    {
      auto const & data = m_trafficQueue.front();
      auto message = BleProtocol::CreateMessage(MessageType::TrafficData, data);

      for (auto const & peer : peers)
        m_connectionManager->SendData(peer.deviceId, message);

      m_trafficQueue.pop();
    }

    // Send gradients
    while (!m_gradientQueue.empty())
    {
      auto const & data = m_gradientQueue.front();
      auto message = BleProtocol::CreateMessage(MessageType::GossipGradients, data);

      for (auto const & peer : peers)
        m_connectionManager->SendData(peer.deviceId, message);

      m_gradientQueue.pop();
    }
  }

  mutable std::mutex m_mutex;
  std::shared_ptr<IConnectionManager> m_connectionManager;
  OpportunisticShareConfig m_config;
  bool m_active = false;

  std::queue<std::vector<uint8_t>> m_trafficQueue;
  std::queue<std::vector<uint8_t>> m_gradientQueue;
};

OpportunisticShare::OpportunisticShare(
    std::shared_ptr<IConnectionManager> connectionManager)
  : m_impl(std::make_unique<Impl>(std::move(connectionManager), OpportunisticShareConfig{}))
{
}

OpportunisticShare::OpportunisticShare(
    std::shared_ptr<IConnectionManager> connectionManager,
    OpportunisticShareConfig config)
  : m_impl(std::make_unique<Impl>(std::move(connectionManager), std::move(config)))
{
}

OpportunisticShare::~OpportunisticShare() = default;

void OpportunisticShare::Start() { m_impl->Start(); }

void OpportunisticShare::Stop() { m_impl->Stop(); }

bool OpportunisticShare::IsActive() const { return m_impl->IsActive(); }

void OpportunisticShare::QueueTrafficData(std::vector<uint8_t> const & trafficData)
{
  m_impl->QueueTrafficData(trafficData);
}

void OpportunisticShare::QueueGossipGradients(std::vector<uint8_t> const & gradients)
{
  m_impl->QueueGossipGradients(gradients);
}
}  // namespace mesh

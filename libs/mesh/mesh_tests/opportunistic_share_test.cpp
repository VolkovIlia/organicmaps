#include "testing/testing.hpp"

#include "mesh/opportunistic_share.hpp"
#include "mesh/connection_manager.hpp"

#include <memory>
#include <vector>

namespace mesh
{
namespace
{
// Mock connection manager for testing
class MockConnectionManager : public IConnectionManager
{
public:
  void Start() override { m_running = true; }
  void Stop() override { m_running = false; }
  bool IsRunning() const override { return m_running; }

  std::vector<PeerInfo> GetConnectedPeers() const override { return m_peers; }
  bool Connect(std::string const &) override { return false; }
  void Disconnect(std::string const &) override {}
  bool SendData(std::string const & deviceId, std::vector<uint8_t> const & data) override
  {
    m_sentData.push_back({deviceId, data});
    return true;
  }

  void SetOnPeerDiscovered(OnPeerDiscovered) override {}
  void SetOnPeerConnected(OnPeerConnected) override {}
  void SetOnPeerDisconnected(OnPeerDisconnected) override {}
  void SetOnDataReceived(OnDataReceived cb) override { m_dataCallback = cb; }

  // Test helpers
  void AddPeer(std::string const & deviceId)
  {
    PeerInfo peer;
    peer.deviceId = deviceId;
    peer.state = ConnectionState::Connected;
    m_peers.push_back(peer);
  }

  void SimulateDataReceived(std::string const & deviceId, std::vector<uint8_t> const & data)
  {
    if (m_dataCallback)
      m_dataCallback(deviceId, data);
  }

  std::vector<std::pair<std::string, std::vector<uint8_t>>> const & GetSentData() const
  {
    return m_sentData;
  }

  void ClearSentData() { m_sentData.clear(); }

private:
  bool m_running = false;
  std::vector<PeerInfo> m_peers;
  std::vector<std::pair<std::string, std::vector<uint8_t>>> m_sentData;
  OnDataReceived m_dataCallback;
};
}  // namespace

UNIT_TEST(OpportunisticShare_StartStop)
{
  auto mockManager = std::make_shared<MockConnectionManager>();
  OpportunisticShare share(mockManager);

  TEST(!share.IsActive(), ());

  share.Start();
  TEST(share.IsActive(), ());
  TEST(mockManager->IsRunning(), ());

  share.Stop();
  TEST(!share.IsActive(), ());
}

UNIT_TEST(OpportunisticShare_DoubleStart)
{
  auto mockManager = std::make_shared<MockConnectionManager>();
  OpportunisticShare share(mockManager);

  share.Start();
  share.Start();  // Should be idempotent
  TEST(share.IsActive(), ());

  share.Stop();
  TEST(!share.IsActive(), ());
}

UNIT_TEST(OpportunisticShare_DoubleStop)
{
  auto mockManager = std::make_shared<MockConnectionManager>();
  OpportunisticShare share(mockManager);

  share.Start();
  share.Stop();
  share.Stop();  // Should be idempotent
  TEST(!share.IsActive(), ());
}

UNIT_TEST(OpportunisticShare_QueueTrafficData_NoPeers)
{
  auto mockManager = std::make_shared<MockConnectionManager>();
  OpportunisticShare share(mockManager);

  share.Start();

  std::vector<uint8_t> data = {0x01, 0x02, 0x03};
  share.QueueTrafficData(data);

  // No peers connected, nothing should be sent
  TEST(mockManager->GetSentData().empty(), ());

  share.Stop();
}

UNIT_TEST(OpportunisticShare_QueueTrafficData_WithPeers)
{
  auto mockManager = std::make_shared<MockConnectionManager>();
  mockManager->AddPeer("device1");
  mockManager->AddPeer("device2");

  OpportunisticShare share(mockManager);
  share.Start();

  std::vector<uint8_t> data = {0x01, 0x02, 0x03};
  share.QueueTrafficData(data);

  // Should send to both peers
  auto const & sentData = mockManager->GetSentData();
  TEST_EQUAL(sentData.size(), 2, ());

  share.Stop();
}

UNIT_TEST(OpportunisticShare_QueueGossipGradients_WithPeers)
{
  auto mockManager = std::make_shared<MockConnectionManager>();
  mockManager->AddPeer("device1");

  OpportunisticShare share(mockManager);
  share.Start();

  std::vector<uint8_t> gradients = {0xAA, 0xBB, 0xCC};
  share.QueueGossipGradients(gradients);

  auto const & sentData = mockManager->GetSentData();
  TEST_EQUAL(sentData.size(), 1, ());

  share.Stop();
}

UNIT_TEST(OpportunisticShare_QueueWhenNotActive)
{
  auto mockManager = std::make_shared<MockConnectionManager>();
  mockManager->AddPeer("device1");

  OpportunisticShare share(mockManager);
  // Not started

  std::vector<uint8_t> data = {0x01, 0x02, 0x03};
  share.QueueTrafficData(data);
  share.QueueGossipGradients(data);

  // Should not send anything when not active
  TEST(mockManager->GetSentData().empty(), ());
}

UNIT_TEST(OpportunisticShare_QueueSizeLimit)
{
  auto mockManager = std::make_shared<MockConnectionManager>();
  OpportunisticShare share(mockManager);
  share.Start();

  // Queue many items without peers (should not crash/grow unbounded)
  for (int i = 0; i < 200; ++i)
  {
    std::vector<uint8_t> data = {static_cast<uint8_t>(i)};
    share.QueueTrafficData(data);
    share.QueueGossipGradients(data);
  }

  // Now add a peer - should only send limited queue
  mockManager->AddPeer("device1");

  std::vector<uint8_t> triggerData = {0xFF};
  share.QueueTrafficData(triggerData);

  // Should have sent some data but not 200+ items
  auto const & sentData = mockManager->GetSentData();
  TEST(sentData.size() <= 200, ("Queue should be bounded"));

  share.Stop();
}

UNIT_TEST(OpportunisticShare_CustomQueueSize)
{
  auto mockManager = std::make_shared<MockConnectionManager>();

  OpportunisticShareConfig config;
  config.maxQueueSize = 5;

  OpportunisticShare share(mockManager, config);
  share.Start();

  // Queue more items than the custom limit
  for (int i = 0; i < 10; ++i)
  {
    std::vector<uint8_t> data = {static_cast<uint8_t>(i)};
    share.QueueTrafficData(data);
  }

  // Add peer to trigger sending
  mockManager->AddPeer("device1");

  std::vector<uint8_t> triggerData = {0xFF};
  share.QueueTrafficData(triggerData);

  // Should have sent at most maxQueueSize + 1 (trigger) items
  auto const & sentData = mockManager->GetSentData();
  TEST(sentData.size() <= 6, ("Queue should respect custom limit"));

  share.Stop();
}

}  // namespace mesh

#include "testing/testing.hpp"

#include "mesh/connection_manager.hpp"

#include <atomic>
#include <string>

namespace mesh
{
UNIT_TEST(ConnectionManager_StartStop)
{
  ConnectionManager manager;
  TEST(!manager.IsRunning(), ());

  manager.Start();
  TEST(manager.IsRunning(), ());

  manager.Stop();
  TEST(!manager.IsRunning(), ());
}

UNIT_TEST(ConnectionManager_DoubleStart)
{
  ConnectionManager manager;
  manager.Start();
  manager.Start();  // Should be idempotent
  TEST(manager.IsRunning(), ());

  manager.Stop();
  TEST(!manager.IsRunning(), ());
}

UNIT_TEST(ConnectionManager_DoubleStop)
{
  ConnectionManager manager;
  manager.Start();
  manager.Stop();
  manager.Stop();  // Should be idempotent
  TEST(!manager.IsRunning(), ());
}

UNIT_TEST(ConnectionManager_GetConnectedPeers_Empty)
{
  ConnectionManager manager;
  manager.Start();

  auto peers = manager.GetConnectedPeers();
  TEST(peers.empty(), ());

  manager.Stop();
}

UNIT_TEST(ConnectionManager_ConnectUnknownDevice)
{
  ConnectionManager manager;
  manager.Start();

  // Should fail - device not discovered
  TEST(!manager.Connect("unknown_device"), ());

  manager.Stop();
}

UNIT_TEST(ConnectionManager_SendDataNotConnected)
{
  ConnectionManager manager;
  manager.Start();

  std::vector<uint8_t> data = {0x01, 0x02, 0x03};
  // Should fail - device not connected
  TEST(!manager.SendData("unknown_device", data), ());

  manager.Stop();
}

UNIT_TEST(ConnectionManager_SetCallbacks)
{
  ConnectionManager manager;

  std::atomic<bool> discoveredCalled{false};
  std::atomic<bool> connectedCalled{false};
  std::atomic<bool> disconnectedCalled{false};
  std::atomic<bool> dataReceivedCalled{false};

  manager.SetOnPeerDiscovered([&](PeerInfo const &) {
    discoveredCalled = true;
  });

  manager.SetOnPeerConnected([&](std::string const &) {
    connectedCalled = true;
  });

  manager.SetOnPeerDisconnected([&](std::string const &) {
    disconnectedCalled = true;
  });

  manager.SetOnDataReceived([&](std::string const &, std::vector<uint8_t> const &) {
    dataReceivedCalled = true;
  });

  // Callbacks should be set but not called yet
  TEST(!discoveredCalled, ());
  TEST(!connectedCalled, ());
  TEST(!disconnectedCalled, ());
  TEST(!dataReceivedCalled, ());
}

UNIT_TEST(ConnectionState_DebugPrint)
{
  TEST_EQUAL(DebugPrint(ConnectionState::Disconnected), "Disconnected", ());
  TEST_EQUAL(DebugPrint(ConnectionState::Connecting), "Connecting", ());
  TEST_EQUAL(DebugPrint(ConnectionState::Connected), "Connected", ());
  TEST_EQUAL(DebugPrint(ConnectionState::Disconnecting), "Disconnecting", ());
}

UNIT_TEST(PeerInfo_DefaultState)
{
  PeerInfo peer;
  TEST_EQUAL(static_cast<uint8_t>(peer.state),
             static_cast<uint8_t>(ConnectionState::Disconnected), ());
  TEST_EQUAL(peer.rssi, 0, ());
  TEST_EQUAL(peer.lastSeenTimestamp, 0, ());
  TEST(peer.deviceId.empty(), ());
  TEST(peer.rollingId.empty(), ());
}
}  // namespace mesh

#include "testing/testing.hpp"

#include "p2p/gossip_learning.hpp"
#include "p2p/privacy_manager.hpp"

#include <cmath>

namespace gossip_learning_test
{
using namespace p2p;

namespace
{
std::shared_ptr<PrivacyManager> CreateContributeManager()
{
  return std::make_shared<PrivacyManager>(ConsentLevel::Contribute);
}
}  // namespace

UNIT_TEST(GradientUpdate_Validation)
{
  GradientUpdate update;
  TEST(!update.IsValid(), ());
  TEST_EQUAL(update.Size(), 0u, ());

  update.m_gradients = {0.1f, 0.2f, 0.3f};
  TEST(!update.IsValid(), ("No samples"));
  TEST_EQUAL(update.Size(), 3u, ());

  update.m_sampleCount = 10;
  TEST(update.IsValid(), ());
}

UNIT_TEST(GossipLearning_PrepareForSharing_ConsentLevels)
{
  // Off - should not share
  auto offManager = std::make_shared<PrivacyManager>(ConsentLevel::Off);
  GossipLearning offGossip(offManager);
  TEST(!offGossip.PrepareForSharing({0.1f, 0.2f}, 10).has_value(), ("Off"));

  // ViewOnly - should not share
  auto viewManager = std::make_shared<PrivacyManager>(ConsentLevel::ViewOnly);
  GossipLearning viewGossip(viewManager);
  TEST(!viewGossip.PrepareForSharing({0.1f, 0.2f}, 10).has_value(), ("ViewOnly"));
}

UNIT_TEST(GossipLearning_PrepareForSharing_Enabled)
{
  auto manager = CreateContributeManager();
  GossipLearning gossip(manager);

  auto result = gossip.PrepareForSharing({0.1f, 0.2f, 0.3f}, 10);
  TEST(result.has_value(), ());

  TEST_EQUAL(result->m_update.m_gradients.size(), 3u, ());
  TEST_EQUAL(result->m_update.m_sampleCount, 10u, ());
  TEST_EQUAL(result->m_hopCount, PrivacyConfig::kMaxHopCount, ());
}

UNIT_TEST(GossipLearning_PrepareForSharing_EdgeCases)
{
  auto manager = CreateContributeManager();
  GossipLearning gossip(manager);

  // Empty gradients - reject
  TEST(!gossip.PrepareForSharing({}, 10).has_value(), ("empty"));
  // Zero samples - reject
  TEST(!gossip.PrepareForSharing({0.1f, 0.2f}, 0).has_value(), ("zero samples"));
  // Too large - reject
  std::vector<float> largeGradients(GossipMessage::kMaxGradientSize + 1, 0.1f);
  TEST(!gossip.PrepareForSharing(largeGradients, 10).has_value(), ("too large"));
  // Max size - accept
  std::vector<float> maxGradients(GossipMessage::kMaxGradientSize, 0.1f);
  TEST(gossip.PrepareForSharing(maxGradients, 10).has_value(), ("max size"));
}

UNIT_TEST(GossipLearning_ProcessReceived_ConsentLevels)
{
  GossipMessage message;
  message.m_update.m_gradients = {0.5f};
  message.m_update.m_sampleCount = 5;
  message.m_hopCount = 2;

  // Off - should not process
  auto offManager = std::make_shared<PrivacyManager>(ConsentLevel::Off);
  GossipLearning offGossip(offManager);
  TEST(!offGossip.ProcessReceived(message).has_value(), ("Off"));

  // ViewOnly - process but don't forward
  auto viewManager = std::make_shared<PrivacyManager>(ConsentLevel::ViewOnly);
  GossipLearning viewGossip(viewManager);
  bool callbackCalled = false;
  viewGossip.SetReceiveCallback([&](GradientUpdate const &) { callbackCalled = true; });
  TEST(!viewGossip.ProcessReceived(message).has_value(), ("ViewOnly no forward"));
  TEST(callbackCalled, ("ViewOnly callback"));
}

UNIT_TEST(GossipLearning_ProcessReceived_Contribute)
{
  auto manager = CreateContributeManager();
  GossipLearning gossip(manager);

  bool callbackCalled = false;
  gossip.SetReceiveCallback([&](GradientUpdate const &) {
    callbackCalled = true;
  });

  GossipMessage message;
  message.m_update.m_gradients = {0.5f, 0.5f};
  message.m_update.m_sampleCount = 5;
  message.m_hopCount = 2;

  auto forward = gossip.ProcessReceived(message);

  TEST(callbackCalled, ("Callback should be called"));
  TEST(forward.has_value(), ("Should forward with hops remaining"));
  TEST_EQUAL(forward->m_hopCount, 1u, ("Hop count should decrement"));
}

UNIT_TEST(GossipLearning_ProcessReceived_InvalidMessage)
{
  auto manager = CreateContributeManager();
  GossipLearning gossip(manager);

  GossipMessage message;
  // Empty gradients = invalid
  message.m_hopCount = 2;

  auto forward = gossip.ProcessReceived(message);
  TEST(!forward.has_value(), ("Should reject invalid message"));

  auto stats = gossip.GetStats();
  TEST_EQUAL(stats.m_messagesDropped, 1u, ());
}

UNIT_TEST(GossipLearning_HopCountBehavior)
{
  auto manager = CreateContributeManager();
  GossipLearning gossip(manager);

  GossipMessage message;
  message.m_update.m_gradients = {0.5f};
  message.m_update.m_sampleCount = 5;

  // Zero hops - no forward
  message.m_hopCount = 0;
  TEST(!gossip.ProcessReceived(message).has_value(), ("zero hops"));

  // One hop - forward with zero
  message.m_hopCount = 1;
  auto forward = gossip.ProcessReceived(message);
  TEST(forward.has_value(), ("one hop"));
  TEST_EQUAL(forward->m_hopCount, 0u, ());
}

UNIT_TEST(GossipLearning_AggregateGradients)
{
  std::vector<float> local = {1.0f, 2.0f};
  std::vector<float> received = {3.0f, 2.0f};

  // Empty local - return received
  auto result1 = GossipLearning::AggregateGradients({}, 0, received, 10);
  TEST_EQUAL(result1, received, ("empty local"));

  // Empty received - return local
  auto result2 = GossipLearning::AggregateGradients(local, 10, {}, 0);
  TEST_EQUAL(result2, local, ("empty received"));

  // Size mismatch - return local
  auto result3 = GossipLearning::AggregateGradients(local, 10, {1.0f}, 10);
  TEST_EQUAL(result3, local, ("size mismatch"));

  // Zero total samples - return local
  auto result4 = GossipLearning::AggregateGradients({1.0f}, 0, {3.0f}, 0);
  TEST_EQUAL(result4.size(), 1u, ());

  // Equal weights: average
  auto result5 = GossipLearning::AggregateGradients(local, 10, received, 10);
  TEST(std::abs(result5[0] - 2.0f) < 0.001f, ("equal weights"));

  // Weighted: 75% local (30 samples vs 10)
  auto result6 = GossipLearning::AggregateGradients({1.0f}, 30, {3.0f}, 10);
  TEST(std::abs(result6[0] - 1.5f) < 0.001f, ("weighted"));
}

UNIT_TEST(GossipLearning_Stats)
{
  auto manager = CreateContributeManager();
  GossipLearning gossip(manager);

  // Send stats
  gossip.PrepareForSharing({0.1f}, 1);
  gossip.PrepareForSharing({0.2f}, 2);
  auto stats = gossip.GetStats();
  TEST_EQUAL(stats.m_messagesSent, 2u, ());

  // Receive stats
  GossipMessage message;
  message.m_update.m_gradients = {0.5f};
  message.m_update.m_sampleCount = 5;
  message.m_hopCount = 2;
  gossip.ProcessReceived(message);
  gossip.ProcessReceived(message);
  stats = gossip.GetStats();
  TEST_EQUAL(stats.m_messagesReceived, 2u, ());
  TEST_EQUAL(stats.m_messagesForwarded, 2u, ());

  // Reset
  gossip.ResetStats();
  stats = gossip.GetStats();
  TEST_EQUAL(stats.m_messagesSent, 0u, ());
  TEST_EQUAL(stats.m_messagesReceived, 0u, ());
}

UNIT_TEST(GossipMessage_Serialization_Basic)
{
  GossipMessage original;
  original.m_senderId.fill(0xAB);
  original.m_hopCount = 2;
  original.m_update.m_gradients = {1.5f, -0.5f, 0.0f};
  original.m_update.m_sampleCount = 100;
  original.m_update.m_timestamp = 12345;

  auto bytes = SerializeGossipMessage(original);
  auto deserialized = DeserializeGossipMessage(bytes.data(), bytes.size());

  TEST(deserialized.has_value(), ());
  TEST_EQUAL(deserialized->m_senderId, original.m_senderId, ());
  TEST_EQUAL(deserialized->m_hopCount, original.m_hopCount, ());
  TEST_EQUAL(deserialized->m_update.m_gradients.size(), 3u, ());
  TEST(std::abs(deserialized->m_update.m_gradients[0] - 1.5f) < 0.001f, ());
  TEST(std::abs(deserialized->m_update.m_gradients[1] - (-0.5f)) < 0.001f, ());
  TEST(std::abs(deserialized->m_update.m_gradients[2] - 0.0f) < 0.001f, ());
  TEST_EQUAL(deserialized->m_update.m_sampleCount, 100u, ());
  TEST_EQUAL(deserialized->m_update.m_timestamp, 12345u, ());
}

UNIT_TEST(GossipMessage_Serialization_EdgeCases)
{
  // Empty gradients
  GossipMessage empty;
  empty.m_hopCount = 1;
  empty.m_update.m_sampleCount = 5;
  auto emptyBytes = SerializeGossipMessage(empty);
  auto emptyResult = DeserializeGossipMessage(emptyBytes.data(), emptyBytes.size());
  TEST(emptyResult.has_value(), ());
  TEST_EQUAL(emptyResult->m_update.m_gradients.size(), 0u, ());

  // Max size
  GossipMessage maxMsg;
  maxMsg.m_senderId.fill(0xFF);
  maxMsg.m_update.m_gradients.resize(GossipMessage::kMaxGradientSize, 0.123f);
  maxMsg.m_update.m_sampleCount = 0xFFFFFFFF;
  auto maxBytes = SerializeGossipMessage(maxMsg);
  auto maxResult = DeserializeGossipMessage(maxBytes.data(), maxBytes.size());
  TEST(maxResult.has_value(), ());
  TEST_EQUAL(maxResult->m_update.m_gradients.size(), GossipMessage::kMaxGradientSize, ());
}

UNIT_TEST(GossipMessage_Deserialization_Errors)
{
  // Too small
  std::vector<uint8_t> smallData(10, 0);
  TEST(!DeserializeGossipMessage(smallData.data(), smallData.size()).has_value(), ("too small"));

  // Null
  TEST(!DeserializeGossipMessage(nullptr, 100).has_value(), ("null"));

  // Too many gradients (malformed)
  std::vector<uint8_t> data(27, 0);
  uint16_t tooMany = GossipMessage::kMaxGradientSize + 1;
  std::memcpy(data.data() + 25, &tooMany, 2);
  TEST(!DeserializeGossipMessage(data.data(), data.size()).has_value(), ("too many"));

  // Truncated gradients
  GossipMessage original;
  original.m_update.m_gradients = {1.0f, 2.0f, 3.0f};
  original.m_update.m_sampleCount = 1;
  auto bytes = SerializeGossipMessage(original);
  bytes.resize(bytes.size() - 4);
  TEST(!DeserializeGossipMessage(bytes.data(), bytes.size()).has_value(), ("truncated"));
}

UNIT_TEST(GossipLearning_DebugPrint)
{
  // GradientUpdate
  GradientUpdate update;
  update.m_gradients = {0.1f, 0.2f};
  update.m_sampleCount = 10;
  auto updateDbg = DebugPrint(update);
  TEST(updateDbg.find("GradientUpdate") != std::string::npos, ());

  // GossipMessage
  GossipMessage message;
  message.m_senderId.fill(0xAB);
  message.m_hopCount = 2;
  message.m_update.m_gradients = {0.5f};
  message.m_update.m_sampleCount = 5;
  auto msgDbg = DebugPrint(message);
  TEST(msgDbg.find("GossipMessage") != std::string::npos, ());

  // Stats
  GossipLearning::Stats stats;
  stats.m_messagesSent = 10;
  auto statsDbg = DebugPrint(stats);
  TEST(statsDbg.find("GossipStats") != std::string::npos, ());
}

UNIT_TEST(GossipLearning_LDPNoiseApplied)
{
  auto manager = CreateContributeManager();
  GossipLearning gossip(manager);

  std::vector<float> const gradients = {0.5f, 0.5f, 0.5f};

  // Prepare multiple times and check for variance (LDP should add noise)
  bool hasVariance = false;
  auto first = gossip.PrepareForSharing(gradients, 10);
  TEST(first.has_value(), ());

  for (int i = 0; i < 20; ++i)
  {
    auto result = gossip.PrepareForSharing(gradients, 10);
    TEST(result.has_value(), ());

    // Check if any gradient differs
    for (size_t j = 0; j < gradients.size(); ++j)
    {
      if (std::abs(result->m_update.m_gradients[j] - first->m_update.m_gradients[j]) > 0.001f)
      {
        hasVariance = true;
        break;
      }
    }
    if (hasVariance)
      break;
  }

  TEST(hasVariance, ("LDP should add random noise to gradients"));
}

UNIT_TEST(GossipLearning_NullPrivacyManager)
{
  GossipLearning gossip(nullptr);

  auto prepareResult = gossip.PrepareForSharing({0.1f}, 1);
  TEST(!prepareResult.has_value(), ("Should not share with null manager"));

  GossipMessage message;
  message.m_update.m_gradients = {0.5f};
  message.m_update.m_sampleCount = 5;
  message.m_hopCount = 2;

  auto receiveResult = gossip.ProcessReceived(message);
  TEST(!receiveResult.has_value(), ("Should not receive with null manager"));
}
}  // namespace gossip_learning_test

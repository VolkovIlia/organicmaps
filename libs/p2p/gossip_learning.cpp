#include "p2p/gossip_learning.hpp"

#include "p2p/aggregated_traffic_share.hpp"
#include "p2p/privacy_settings.hpp"

#include "base/logging.hpp"

#include <cstring>
#include <sstream>

namespace p2p
{
bool GradientUpdate::IsValid() const
{
  return !m_gradients.empty() && m_sampleCount > 0;
}

GossipLearning::GossipLearning(std::shared_ptr<PrivacyManager> privacyManager)
  : m_privacyManager(std::move(privacyManager))
{
}

void GossipLearning::SetReceiveCallback(GradientReceiveCallback callback)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_receiveCallback = std::move(callback);
}

std::optional<GossipMessage> GossipLearning::PrepareForSharing(
    std::vector<float> const & gradients, uint32_t sampleCount)
{
  if (!m_privacyManager || !m_privacyManager->CanShare())
    return std::nullopt;

  if (gradients.empty() || sampleCount == 0)
    return std::nullopt;

  if (gradients.size() > GossipMessage::kMaxGradientSize)
  {
    LOG(LWARNING, ("Gradient size exceeds maximum:", gradients.size()));
    return std::nullopt;
  }

  GossipMessage message;
  message.m_senderId = m_idGenerator.GetCurrentId();
  message.m_update.m_gradients = gradients;
  message.m_update.m_sampleCount = sampleCount;
  message.m_update.m_timestamp = GetCurrentTimestampMinutes();
  message.m_hopCount = PrivacyConfig::kMaxHopCount;

  // Apply LDP noise before sharing
  ApplyNoiseToGradients(message.m_update.m_gradients);

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_stats.m_messagesSent;
  }

  return message;
}

std::optional<GossipMessage> GossipLearning::ProcessReceived(GossipMessage const & message)
{
  if (!m_privacyManager || !m_privacyManager->CanReceive())
    return std::nullopt;

  if (!message.m_update.IsValid())
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_stats.m_messagesDropped;
    return std::nullopt;
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_stats.m_messagesReceived;
  }

  // Notify callback
  GradientReceiveCallback callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    callback = m_receiveCallback;
  }
  if (callback)
    callback(message.m_update);

  // Check if should forward
  if (!ShouldForward(message))
    return std::nullopt;

  // Create forwarding message with decremented hop count
  GossipMessage forward = message;
  if (forward.m_hopCount == 0)
    return std::nullopt;

  --forward.m_hopCount;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_stats.m_messagesForwarded;
  }

  return forward;
}

std::vector<float> GossipLearning::AggregateGradients(
    std::vector<float> const & local, uint32_t localSamples,
    std::vector<float> const & received, uint32_t receivedSamples)
{
  if (local.empty())
    return received;
  if (received.empty())
    return local;
  if (local.size() != received.size())
    return local;  // Size mismatch, keep local

  uint32_t const totalSamples = localSamples + receivedSamples;
  if (totalSamples == 0)
    return local;

  float const localWeight = static_cast<float>(localSamples) / static_cast<float>(totalSamples);
  float const receivedWeight =
      static_cast<float>(receivedSamples) / static_cast<float>(totalSamples);

  std::vector<float> result(local.size());
  for (size_t i = 0; i < local.size(); ++i)
    result[i] = local[i] * localWeight + received[i] * receivedWeight;

  return result;
}

GossipLearning::Stats GossipLearning::GetStats() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_stats;
}

void GossipLearning::ResetStats()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_stats = Stats{};
}

void GossipLearning::ApplyNoiseToGradients(std::vector<float> & gradients) const
{
  if (!m_privacyManager)
    return;

  // Sensitivity: max gradient magnitude (assume normalized to [-1, 1])
  constexpr float kSensitivity = 2.0f;

  for (float & g : gradients)
    g = m_privacyManager->ApplyLDPFloat(g, kSensitivity);
}

bool GossipLearning::ShouldForward(GossipMessage const & message) const
{
  // Only forward if we're in Contribute mode
  if (!m_privacyManager || !m_privacyManager->CanShare())
    return false;

  // Check hop count
  return message.m_hopCount > 0;
}

// Serialization

std::vector<uint8_t> SerializeGossipMessage(GossipMessage const & message)
{
  // Format: [senderId:16][hopCount:1][sampleCount:4][timestamp:4][gradientCount:2][gradients:N*4]
  size_t const gradientBytes = message.m_update.m_gradients.size() * sizeof(float);
  size_t const totalSize = 16 + 1 + 4 + 4 + 2 + gradientBytes;

  std::vector<uint8_t> data(totalSize);
  size_t offset = 0;

  std::memcpy(data.data() + offset, message.m_senderId.data(), 16);
  offset += 16;

  data[offset++] = message.m_hopCount;

  std::memcpy(data.data() + offset, &message.m_update.m_sampleCount, 4);
  offset += 4;

  std::memcpy(data.data() + offset, &message.m_update.m_timestamp, 4);
  offset += 4;

  auto const gradientCount = static_cast<uint16_t>(message.m_update.m_gradients.size());
  std::memcpy(data.data() + offset, &gradientCount, 2);
  offset += 2;

  if (!message.m_update.m_gradients.empty())
    std::memcpy(data.data() + offset, message.m_update.m_gradients.data(), gradientBytes);

  return data;
}

std::optional<GossipMessage> DeserializeGossipMessage(uint8_t const * data, size_t size)
{
  // Minimum size: senderId(16) + hopCount(1) + sampleCount(4) + timestamp(4) + gradientCount(2)
  constexpr size_t kMinSize = 16 + 1 + 4 + 4 + 2;
  if (!data || size < kMinSize)
    return std::nullopt;

  GossipMessage message;
  size_t offset = 0;

  std::memcpy(message.m_senderId.data(), data + offset, 16);
  offset += 16;

  message.m_hopCount = data[offset++];

  std::memcpy(&message.m_update.m_sampleCount, data + offset, 4);
  offset += 4;

  std::memcpy(&message.m_update.m_timestamp, data + offset, 4);
  offset += 4;

  uint16_t gradientCount = 0;
  std::memcpy(&gradientCount, data + offset, 2);
  offset += 2;

  if (gradientCount > GossipMessage::kMaxGradientSize)
    return std::nullopt;

  size_t const gradientBytes = gradientCount * sizeof(float);
  if (size < offset + gradientBytes)
    return std::nullopt;

  message.m_update.m_gradients.resize(gradientCount);
  if (gradientCount > 0)
    std::memcpy(message.m_update.m_gradients.data(), data + offset, gradientBytes);

  return message;
}

std::string DebugPrint(GradientUpdate const & update)
{
  std::ostringstream oss;
  oss << "GradientUpdate [size=" << update.m_gradients.size()
      << ", samples=" << update.m_sampleCount << ", time=" << update.m_timestamp << "]";
  return oss.str();
}

std::string DebugPrint(GossipMessage const & message)
{
  std::ostringstream oss;
  oss << "GossipMessage [sender=" << RollingIdToHex(message.m_senderId).substr(0, 8) << "..."
      << ", hops=" << static_cast<int>(message.m_hopCount) << ", " << DebugPrint(message.m_update)
      << "]";
  return oss.str();
}

std::string DebugPrint(GossipLearning::Stats const & stats)
{
  std::ostringstream oss;
  oss << "GossipStats [sent=" << stats.m_messagesSent << ", recv=" << stats.m_messagesReceived
      << ", fwd=" << stats.m_messagesForwarded << ", drop=" << stats.m_messagesDropped << "]";
  return oss.str();
}
}  // namespace p2p

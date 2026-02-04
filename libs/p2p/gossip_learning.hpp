#pragma once

#include "p2p/privacy_manager.hpp"
#include "p2p/rolling_id_generator.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace p2p
{
/// \brief Model gradient update for gossip exchange.
struct GradientUpdate
{
  std::vector<float> m_gradients;  // Model parameter gradients
  uint32_t m_sampleCount = 0;      // Training samples used
  uint32_t m_timestamp = 0;        // Update timestamp

  [[nodiscard]] bool IsValid() const;
  [[nodiscard]] size_t Size() const { return m_gradients.size(); }
};

/// \brief Serialized gradient message for P2P transmission.
struct GossipMessage
{
  RollingId m_senderId{};     // Anonymous sender ID
  GradientUpdate m_update;    // Gradient data
  uint8_t m_hopCount = 0;     // Remaining hops

  static constexpr size_t kMaxGradientSize = 256;  // Max gradients per message
};

/// \brief Gossip Learning protocol for P2P model parameter exchange.
/// Allows devices to collaboratively improve ML models without sharing raw data.
class GossipLearning
{
public:
  using GradientReceiveCallback = std::function<void(GradientUpdate const &)>;

  explicit GossipLearning(std::shared_ptr<PrivacyManager> privacyManager);

  /// \brief Set callback for received gradients.
  void SetReceiveCallback(GradientReceiveCallback callback);

  /// \brief Prepare local gradients for sharing.
  /// Applies LDP noise before creating message.
  /// \param gradients Local model gradients.
  /// \param sampleCount Number of training samples.
  /// \return Message ready for transmission, or nullopt if sharing disabled.
  [[nodiscard]] std::optional<GossipMessage> PrepareForSharing(
      std::vector<float> const & gradients, uint32_t sampleCount);

  /// \brief Process received gossip message.
  /// Validates, applies averaging, and optionally forwards.
  /// \param message Received message.
  /// \return Message to forward, or nullopt if should not forward.
  [[nodiscard]] std::optional<GossipMessage> ProcessReceived(GossipMessage const & message);

  /// \brief Aggregate received gradients with local ones.
  /// Uses weighted averaging based on sample counts.
  /// \param local Local gradients.
  /// \param localSamples Local sample count.
  /// \param received Received gradients.
  /// \param receivedSamples Received sample count.
  /// \return Aggregated gradients.
  [[nodiscard]] static std::vector<float> AggregateGradients(
      std::vector<float> const & local, uint32_t localSamples,
      std::vector<float> const & received, uint32_t receivedSamples);

  /// \brief Get statistics about gossip activity.
  struct Stats
  {
    uint32_t m_messagesSent = 0;
    uint32_t m_messagesReceived = 0;
    uint32_t m_messagesForwarded = 0;
    uint32_t m_messagesDropped = 0;
  };
  [[nodiscard]] Stats GetStats() const;

  /// \brief Reset statistics.
  void ResetStats();

private:
  /// \brief Apply LDP noise to gradients.
  void ApplyNoiseToGradients(std::vector<float> & gradients) const;

  /// \brief Check if message should be forwarded.
  [[nodiscard]] bool ShouldForward(GossipMessage const & message) const;

  std::shared_ptr<PrivacyManager> m_privacyManager;
  RollingIdGenerator m_idGenerator;
  GradientReceiveCallback m_receiveCallback;

  mutable std::mutex m_mutex;
  Stats m_stats;
};

/// \brief Serialize GossipMessage to bytes.
[[nodiscard]] std::vector<uint8_t> SerializeGossipMessage(GossipMessage const & message);

/// \brief Deserialize GossipMessage from bytes.
[[nodiscard]] std::optional<GossipMessage> DeserializeGossipMessage(
    uint8_t const * data, size_t size);

std::string DebugPrint(GradientUpdate const & update);
std::string DebugPrint(GossipMessage const & message);
std::string DebugPrint(GossipLearning::Stats const & stats);
}  // namespace p2p

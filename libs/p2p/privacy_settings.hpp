#pragma once

#include <cstdint>
#include <string>

namespace p2p
{
/// \brief User consent levels for P2P sharing.
enum class ConsentLevel : uint8_t
{
  Off = 0,        // No P2P activity
  ViewOnly = 1,   // Receive data, don't share
  Contribute = 2  // Full participation
};

/// \brief Privacy configuration constants.
struct PrivacyConfig
{
  // Local Differential Privacy (LDP) epsilon value selection rationale:
  // - epsilon=2.0 provides a balanced privacy/utility tradeoff
  // - Lower values (e.g., 0.1-1.0) provide stronger privacy but degrade data utility
  // - Higher values (e.g., 5-10) improve utility but weaken privacy guarantees
  // - Industry standard for location data is typically 1.0-3.0
  // - We chose 2.0 as it allows reasonable traffic speed aggregation
  //   while maintaining strong individual privacy protection
  // Reference: "The Algorithmic Foundations of Differential Privacy" (Dwork & Roth, 2014)
  static constexpr double kLDPEpsilon = 2.0;

  // K-anonymity threshold (min unique segments per H3 cell)
  static constexpr uint32_t kMinSegmentsForSharing = 5;

  // Rolling ID rotation interval (minutes)
  static constexpr uint32_t kRollingIdIntervalMinutes = 15;
  static constexpr uint32_t kRollingIdJitterMinutes = 2;

  // Data TTL
  static constexpr uint32_t kSharedDataTTLMinutes = 60;
  static constexpr uint8_t kMaxHopCount = 3;

  // H3 resolution for aggregation (~175m hexagons)
  static constexpr int kH3Resolution = 9;
};

std::string DebugPrint(ConsentLevel level);
}  // namespace p2p

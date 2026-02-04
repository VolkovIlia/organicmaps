#pragma once

#include "p2p/privacy_settings.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>

namespace p2p
{
/// \brief 16-byte rolling identifier for P2P communication.
/// Changes periodically to prevent tracking.
using RollingId = std::array<uint8_t, 16>;

/// \brief Generates rolling device identifiers that change periodically.
/// Based on GAEN (Google/Apple Exposure Notification) approach.
/// Thread-safe.
class RollingIdGenerator
{
public:
  RollingIdGenerator();

  /// \brief Get current rolling ID.
  /// Regenerates if interval has elapsed.
  [[nodiscard]] RollingId GetCurrentId();

  /// \brief Force regeneration of ID.
  void Regenerate();

  /// \brief Get time until next automatic rotation.
  [[nodiscard]] std::chrono::seconds GetTimeUntilRotation() const;

  /// \brief Check if ID needs rotation.
  [[nodiscard]] bool NeedsRotation() const;

  /// \brief Set rotation interval (for testing).
  void SetRotationInterval(std::chrono::minutes interval, std::chrono::minutes jitter);

  /// \brief Get rotation interval.
  [[nodiscard]] std::chrono::minutes GetRotationInterval() const;

private:
  /// \brief Generate new random ID.
  void GenerateNewId();

  /// \brief Calculate next rotation time with jitter.
  void ScheduleNextRotation();

  /// \brief Check rotation without acquiring lock.
  [[nodiscard]] bool NeedsRotationUnlocked() const;

  mutable std::mutex m_mutex;
  std::mt19937_64 m_rng;

  RollingId m_currentId{};
  std::chrono::steady_clock::time_point m_nextRotationTime;

  std::chrono::minutes m_rotationInterval{PrivacyConfig::kRollingIdIntervalMinutes};
  std::chrono::minutes m_jitter{PrivacyConfig::kRollingIdJitterMinutes};
};

/// \brief Convert RollingId to hex string for debugging.
[[nodiscard]] std::string RollingIdToHex(RollingId const & id);

/// \brief Check if two RollingIds are equal.
bool operator==(RollingId const & a, RollingId const & b);
bool operator!=(RollingId const & a, RollingId const & b);

std::string DebugPrint(RollingId const & id);
}  // namespace p2p

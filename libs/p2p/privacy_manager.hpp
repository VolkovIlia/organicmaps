#pragma once

#include "p2p/privacy_settings.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace p2p
{
/// \brief Manages privacy controls for P2P data sharing.
/// Thread-safe. All operations respect user consent level.
class PrivacyManager
{
public:
  using ConsentChangeCallback = std::function<void(ConsentLevel)>;

  PrivacyManager();
  explicit PrivacyManager(ConsentLevel initialLevel);

  /// \brief Get current consent level.
  [[nodiscard]] ConsentLevel GetConsentLevel() const;

  /// \brief Set consent level. Notifies callbacks.
  void SetConsentLevel(ConsentLevel level);

  /// \brief Register callback for consent changes.
  void RegisterConsentCallback(ConsentChangeCallback callback);

  /// \brief Check if sharing is allowed at current consent level.
  [[nodiscard]] bool CanShare() const;

  /// \brief Check if receiving is allowed at current consent level.
  [[nodiscard]] bool CanReceive() const;

  /// \brief Apply Local Differential Privacy to a value.
  /// Adds calibrated Laplace noise based on epsilon.
  /// \param value Original value.
  /// \param sensitivity Maximum change in value from one record.
  /// \return Noised value.
  [[nodiscard]] double ApplyLDP(double value, double sensitivity) const;

  /// \brief Apply LDP to a float value.
  [[nodiscard]] float ApplyLDPFloat(float value, float sensitivity) const;

  /// \brief Check k-anonymity for an H3 cell.
  /// \param cellId H3 cell identifier.
  /// \param uniqueSegments Number of unique road segments in the cell.
  /// \return true if sharing is safe (meets k-anonymity threshold).
  [[nodiscard]] bool CheckKAnonymity(uint64_t cellId, uint32_t uniqueSegments) const;

  /// \brief Record observation for k-anonymity tracking.
  void RecordObservation(uint64_t cellId, uint32_t segmentId);

  /// \brief Get unique segment count for a cell.
  [[nodiscard]] uint32_t GetUniqueSegmentCount(uint64_t cellId) const;

  /// \brief Clear all observation records.
  void ClearObservations();

  /// \brief Get LDP epsilon value.
  [[nodiscard]] double GetEpsilon() const { return m_epsilon; }

  /// \brief Get k-anonymity threshold.
  [[nodiscard]] uint32_t GetKThreshold() const { return m_kThreshold; }

private:
  /// \brief Generate Laplace noise for LDP.
  [[nodiscard]] double GenerateLaplaceNoise(double scale) const;

  mutable std::mutex m_mutex;
  ConsentLevel m_consentLevel = ConsentLevel::Off;
  std::vector<ConsentChangeCallback> m_callbacks;

  // LDP parameters
  double m_epsilon = PrivacyConfig::kLDPEpsilon;

  // K-anonymity tracking: cellId -> set of segment IDs
  std::unordered_map<uint64_t, std::unordered_set<uint32_t>> m_cellObservations;
  uint32_t m_kThreshold = PrivacyConfig::kMinSegmentsForSharing;

  // Random generator for LDP noise
  mutable std::mt19937 m_rng;
};

std::string DebugPrint(PrivacyManager const & manager);
}  // namespace p2p

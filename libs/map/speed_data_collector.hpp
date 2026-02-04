#pragma once

#include "traffic/personal_speed_storage.hpp"

#include "routing/segment.hpp"

#include "platform/location.hpp"

#include <atomic>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>

namespace map
{
/// \brief Collects user's driving speed during navigation and stores in PersonalSpeedStorage.
/// Thread-safe. Call OnLocationUpdate() from navigation thread,
/// data is accumulated and periodically flushed to storage.
class SpeedDataCollector
{
public:
  /// \brief Creates collector with storage at given path.
  /// \param storagePath Path to personal speed storage file.
  explicit SpeedDataCollector(std::string const & storagePath);

  /// \brief Creates collector with existing storage.
  explicit SpeedDataCollector(std::shared_ptr<traffic::PersonalSpeedStorage> storage);

  ~SpeedDataCollector();

  /// \brief Called on each location update during active navigation.
  /// Extracts speed and current segment, stores observation.
  /// \param info GPS location info with speed.
  /// \param segment Current road segment from route matching.
  /// \param isNavigating True if actively following route.
  void OnLocationUpdate(location::GpsInfo const & info, routing::Segment const & segment,
                        bool isNavigating);

  /// \brief Enable/disable data collection at runtime.
  /// \note Does NOT persist to settings. UI layer must call
  ///       settings::Set(kPersonalSpeedDataEnabled, value) separately.
  void SetEnabled(bool enabled);

  /// \brief Check if collection is enabled.
  [[nodiscard]] bool IsEnabled() const { return m_enabled; }

  /// \brief Flush collected data to storage.
  void Flush();

  /// \brief Clear all collected data.
  void Clear();

  /// \brief Get underlying storage for queries.
  [[nodiscard]] std::shared_ptr<traffic::PersonalSpeedStorage> GetStorage() const { return m_storage; }

  /// \brief Get number of observations in current session.
  [[nodiscard]] size_t GetSessionObservationCount() const { return m_sessionObservationCount; }

  /// \brief Run periodic cleanup of old records.
  void CleanupOldRecords();

private:
  /// \brief Initialize enabled state from user settings.
  void InitFromSettings();

  /// \brief Extract day of week and hour from timestamp.
  static void GetTimeComponents(std::time_t timestamp, uint8_t & dayOfWeek, uint8_t & hour);

  /// \brief Check if observation is valid for storage.
  /// \param info GPS location info with speed and accuracy.
  /// \param segment Road segment to check for throttling.
  /// \return true if observation passes all quality filters.
  [[nodiscard]] bool IsValidObservation(location::GpsInfo const & info,
                                         routing::Segment const & segment) const;

  std::shared_ptr<traffic::PersonalSpeedStorage> m_storage;
  std::atomic<bool> m_enabled{true};
  std::atomic<size_t> m_sessionObservationCount{0};

  // Throttling: minimum interval between observations for same segment
  static constexpr double kMinObservationIntervalSec = 5.0;
  mutable std::mutex m_lastObservationMutex;
  routing::Segment m_lastSegment;
  double m_lastObservationTime = 0.0;

  // Minimum speed threshold (km/h) - ignore stopped/crawling data
  static constexpr float kMinSpeedKmph = 5.0f;

  // Maximum speed threshold (km/h) - filter GPS glitches
  static constexpr float kMaxSpeedKmph = 250.0f;

  // Minimum horizontal accuracy for valid observation (meters)
  static constexpr double kMaxHorizontalAccuracy = 50.0;
};

}  // namespace map

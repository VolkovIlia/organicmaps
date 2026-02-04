#pragma once

#include "traffic/historical_speed_data.hpp"
#include "traffic/speed_groups.hpp"

#include "indexer/mwm_set.hpp"

#include <ctime>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace traffic
{
/// \brief Interface for providing historical speed data.
/// This allows for different implementations (MWM-based, testing, etc.)
class IHistoricalSpeedProvider
{
public:
  virtual ~IHistoricalSpeedProvider() = default;

  /// \brief Get speed percentage for a segment at given time.
  /// \param mwmId MWM identifier.
  /// \param featureId Feature ID within MWM.
  /// \param segmentIdx Segment index within feature.
  /// \param isForward Direction flag.
  /// \param time Time for which to get speed.
  /// \return Speed percentage (1-200), 0 if no data available.
  [[nodiscard]] virtual SpeedPercentage GetSpeed(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                                  uint16_t segmentIdx, bool isForward,
                                                  std::time_t time) const = 0;

  /// \brief Get speed group for a segment at given time (for rendering).
  [[nodiscard]] virtual SpeedGroup GetSpeedGroup(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                                  uint16_t segmentIdx, bool isForward,
                                                  std::time_t time) const = 0;

  /// \brief Check if historical data is available for an MWM.
  [[nodiscard]] virtual bool HasData(MwmSet::MwmId const & mwmId) const = 0;
};

/// \brief Provider that reads historical speed data from MWM files.
/// Thread-safe with lazy loading of data per MWM.
class HistoricalSpeedProvider : public IHistoricalSpeedProvider
{
public:
  HistoricalSpeedProvider() = default;

  /// \brief Get speed percentage for a segment.
  [[nodiscard]] SpeedPercentage GetSpeed(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                          uint16_t segmentIdx, bool isForward,
                                          std::time_t time) const override;

  /// \brief Get speed group for a segment.
  [[nodiscard]] SpeedGroup GetSpeedGroup(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                          uint16_t segmentIdx, bool isForward,
                                          std::time_t time) const override;

  /// \brief Check if historical data is available for an MWM.
  [[nodiscard]] bool HasData(MwmSet::MwmId const & mwmId) const override;

  /// \brief Preload historical data for an MWM.
  /// Call this to eagerly load data, otherwise it's loaded on first access.
  void Preload(MwmSet::MwmId const & mwmId);

  /// \brief Clear cached data for an MWM.
  void Clear(MwmSet::MwmId const & mwmId);

  /// \brief Clear all cached data.
  void ClearAll();

  /// \brief Get number of loaded MWMs.
  [[nodiscard]] size_t GetLoadedMwmCount() const;

private:
  /// \brief Load historical speed data from MWM file.
  /// \return Shared pointer to data, or nullptr if not available.
  std::shared_ptr<HistoricalSpeedData> LoadData(MwmSet::MwmId const & mwmId) const;

  /// \brief Get or load data for an MWM.
  std::shared_ptr<HistoricalSpeedData const> GetOrLoadData(MwmSet::MwmId const & mwmId) const;

  /// Cache of loaded data per MWM.
  /// Using shared_ptr to allow thread-safe reading while new data is being loaded.
  mutable std::unordered_map<MwmSet::MwmId, std::shared_ptr<HistoricalSpeedData const>,
                             MwmSet::MwmIdHasher> m_cache;

  /// Mutex for cache access.
  mutable std::mutex m_mutex;
};

/// \brief Provider for testing that uses in-memory data.
class TestHistoricalSpeedProvider : public IHistoricalSpeedProvider
{
public:
  TestHistoricalSpeedProvider() = default;

  /// \brief Add test data for an MWM.
  void SetData(MwmSet::MwmId const & mwmId, std::shared_ptr<HistoricalSpeedData> data);

  [[nodiscard]] SpeedPercentage GetSpeed(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                          uint16_t segmentIdx, bool isForward,
                                          std::time_t time) const override;

  [[nodiscard]] SpeedGroup GetSpeedGroup(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                          uint16_t segmentIdx, bool isForward,
                                          std::time_t time) const override;

  [[nodiscard]] bool HasData(MwmSet::MwmId const & mwmId) const override;

private:
  std::unordered_map<MwmSet::MwmId, std::shared_ptr<HistoricalSpeedData>,
                     MwmSet::MwmIdHasher> m_data;
};

}  // namespace traffic

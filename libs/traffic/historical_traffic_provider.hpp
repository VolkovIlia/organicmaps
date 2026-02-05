#pragma once

#include "traffic/historical_speed_data.hpp"
#include "traffic/traffic_info.hpp"

#include "routing_common/car_model.hpp"

#include "indexer/mwm_set.hpp"

#include "coding/files_container.hpp"

#include "base/thread_pool_computational.hpp"

#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace traffic
{
/// \brief Provides historical traffic coloring for visualization.
/// This class loads historical speed patterns from MWM files and converts them
/// to TrafficInfo::Coloring format for the current time.
class HistoricalTrafficProvider
{
public:
  HistoricalTrafficProvider() = default;

  /// \brief Gets traffic coloring based on historical patterns for given MWM.
  /// \param mwmId - the MWM to get historical traffic for
  /// \param timestamp - the time to get traffic for (defaults to current time)
  /// \returns Coloring map if historical data is available, empty optional otherwise.
  std::optional<TrafficInfo::Coloring> GetColoring(MwmSet::MwmId const & mwmId,
                                                    time_t timestamp = 0) const;

  /// \brief Checks if historical data is available for MWM.
  bool HasData(MwmSet::MwmId const & mwmId) const;

  /// \brief Clears cached data for MWM.
  void ClearCache(MwmSet::MwmId const & mwmId);

  /// \brief Clears all cached data.
  void Clear();

  /// \brief Gets the number of cached MWMs.
  size_t GetCachedMwmCount() const;

private:
  /// \brief Loads historical data from MWM file.
  /// \returns true if data was loaded successfully.
  bool LoadHistoricalData(MwmSet::MwmId const & mwmId) const;

  /// \brief Generates synthetic historical data for MWM based on road types.
  /// \returns true if data was generated successfully.
  bool GenerateSyntheticData(MwmSet::MwmId const & mwmId) const;

  /// \brief Converts HistoricalSpeedData to TrafficInfo::Coloring for given time.
  TrafficInfo::Coloring ConvertToColoring(HistoricalSpeedData const & data,
                                          time_t timestamp) const;

  // Cache of loaded historical data per MWM
  mutable std::map<MwmSet::MwmId, std::shared_ptr<HistoricalSpeedData>> m_cache;
  mutable std::mutex m_mutex;
};

/// \brief Creates a TrafficInfo object from historical data.
/// This is a convenience function to create TrafficInfo compatible with existing infrastructure.
TrafficInfo CreateHistoricalTrafficInfo(MwmSet::MwmId const & mwmId,
                                        TrafficInfo::Coloring && coloring);

}  // namespace traffic

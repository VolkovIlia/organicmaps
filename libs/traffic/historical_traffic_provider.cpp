#include "traffic/historical_traffic_provider.hpp"

#include "defines.hpp"

#include "coding/reader.hpp"

#include "base/logging.hpp"

namespace traffic
{
std::optional<TrafficInfo::Coloring> HistoricalTrafficProvider::GetColoring(
    MwmSet::MwmId const & mwmId, time_t timestamp) const
{
  if (!mwmId.IsAlive())
    return std::nullopt;

  // Use current time if not specified
  if (timestamp == 0)
    timestamp = std::time(nullptr);

  std::lock_guard<std::mutex> lock(m_mutex);

  // Check cache first
  auto it = m_cache.find(mwmId);
  if (it == m_cache.end())
  {
    // Try to load
    if (!LoadHistoricalData(mwmId))
      return std::nullopt;
    it = m_cache.find(mwmId);
    if (it == m_cache.end())
      return std::nullopt;
  }

  return ConvertToColoring(*it->second, timestamp);
}

bool HistoricalTrafficProvider::HasData(MwmSet::MwmId const & mwmId) const
{
  if (!mwmId.IsAlive())
    return false;

  std::lock_guard<std::mutex> lock(m_mutex);

  // Check cache
  if (m_cache.find(mwmId) != m_cache.end())
    return true;

  // Check if MWM has historical speeds section
  auto const & mwmValue = *mwmId.GetInfo();
  std::string const mwmPath = mwmValue.GetLocalFile().GetPath(MapFileType::Map);

  try
  {
    FilesContainerR container(mwmPath);
    return container.IsExist(HISTORICAL_SPEEDS_FILE_TAG);
  }
  catch (RootException const & e)
  {
    LOG(LWARNING, ("Error checking historical speeds in", mwmPath, ":", e.Msg()));
    return false;
  }
}

void HistoricalTrafficProvider::ClearCache(MwmSet::MwmId const & mwmId)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_cache.erase(mwmId);
}

void HistoricalTrafficProvider::Clear()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_cache.clear();
}

size_t HistoricalTrafficProvider::GetCachedMwmCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_cache.size();
}

bool HistoricalTrafficProvider::LoadHistoricalData(MwmSet::MwmId const & mwmId) const
{
  if (!mwmId.IsAlive())
    return false;

  auto const & mwmValue = *mwmId.GetInfo();
  std::string const mwmPath = mwmValue.GetLocalFile().GetPath(MapFileType::Map);

  try
  {
    FilesContainerR container(mwmPath);
    if (!container.IsExist(HISTORICAL_SPEEDS_FILE_TAG))
    {
      LOG(LINFO, ("No historical speeds data in", mwmPath));
      return false;
    }

    auto reader = container.GetReader(HISTORICAL_SPEEDS_FILE_TAG);
    ReaderSource<FilesContainerR::TReader> source(reader);

    auto data = std::make_shared<HistoricalSpeedData>();
    data->Deserialize(source);

    m_cache[mwmId] = std::move(data);

    LOG(LINFO, ("Loaded historical speeds for", mwmPath,
                "segments:", m_cache[mwmId]->GetSegmentCount()));
    return true;
  }
  catch (RootException const & e)
  {
    LOG(LWARNING, ("Error loading historical speeds from", mwmPath, ":", e.Msg()));
    return false;
  }
}

TrafficInfo::Coloring HistoricalTrafficProvider::ConvertToColoring(
    HistoricalSpeedData const & data, time_t timestamp) const
{
  TrafficInfo::Coloring coloring;

  // Get time components
  struct tm timeInfo;
#ifdef _WIN32
  localtime_s(&timeInfo, &timestamp);
#else
  localtime_r(&timestamp, &timeInfo);
#endif

  uint8_t const dayOfWeek = static_cast<uint8_t>(timeInfo.tm_wday);  // 0 = Sunday
  uint8_t const hour = static_cast<uint8_t>(timeInfo.tm_hour);

  // Iterate through all patterns and convert to coloring
  data.ForEachPattern([&](uint32_t featureId, uint16_t segmentIdx, bool forward,
                          SegmentSpeedPattern const & pattern) {
    SpeedPercentage const speed = pattern.GetSpeed(dayOfWeek, hour);
    if (speed == kNoSpeedData)
      return;

    SpeedGroup const group = SpeedPercentageToGroup(speed);
    if (group == SpeedGroup::Unknown)
      return;

    TrafficInfo::RoadSegmentId segId(
        featureId, segmentIdx,
        forward ? TrafficInfo::RoadSegmentId::kForwardDirection
                : TrafficInfo::RoadSegmentId::kReverseDirection);
    coloring[segId] = group;
  });

  return coloring;
}

TrafficInfo CreateHistoricalTrafficInfo(MwmSet::MwmId const & mwmId,
                                        TrafficInfo::Coloring && coloring)
{
  return TrafficInfo::BuildForTesting(std::move(coloring));
}

}  // namespace traffic

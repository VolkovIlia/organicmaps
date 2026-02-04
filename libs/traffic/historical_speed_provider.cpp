#include "traffic/historical_speed_provider.hpp"

#include "coding/files_container.hpp"
#include "coding/reader.hpp"

#include "base/logging.hpp"

#include "defines.hpp"

namespace traffic
{
// HistoricalSpeedProvider implementation

SpeedPercentage HistoricalSpeedProvider::GetSpeed(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                                   uint16_t segmentIdx, bool isForward,
                                                   std::time_t time) const
{
  auto data = GetOrLoadData(mwmId);
  if (!data)
    return kNoSpeedData;

  return data->GetSpeed(featureId, segmentIdx, isForward, time);
}

SpeedGroup HistoricalSpeedProvider::GetSpeedGroup(MwmSet::MwmId const & mwmId, uint32_t featureId,
                                                   uint16_t segmentIdx, bool isForward,
                                                   std::time_t time) const
{
  SpeedPercentage const speed = GetSpeed(mwmId, featureId, segmentIdx, isForward, time);
  return SpeedPercentageToGroup(speed);
}

bool HistoricalSpeedProvider::HasData(MwmSet::MwmId const & mwmId) const
{
  auto data = GetOrLoadData(mwmId);
  return data && data->GetSegmentCount() > 0;
}

void HistoricalSpeedProvider::Preload(MwmSet::MwmId const & mwmId)
{
  // Just trigger loading
  GetOrLoadData(mwmId);
}

void HistoricalSpeedProvider::Clear(MwmSet::MwmId const & mwmId)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_cache.erase(mwmId);
}

void HistoricalSpeedProvider::ClearAll()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_cache.clear();
}

size_t HistoricalSpeedProvider::GetLoadedMwmCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_cache.size();
}

std::shared_ptr<HistoricalSpeedData> HistoricalSpeedProvider::LoadData(
    MwmSet::MwmId const & mwmId) const
{
  if (!mwmId.IsAlive())
  {
    LOG(LWARNING, ("Attempt to load historical speeds for dead MWM"));
    return nullptr;
  }

  auto const & info = mwmId.GetInfo();
  if (!info)
    return nullptr;

  std::string const mwmPath = info->GetLocalFile().GetPath(MapFileType::Map);

  try
  {
    FilesContainerR container(mwmPath);

    if (!container.IsExist(HISTORICAL_SPEEDS_FILE_TAG))
    {
      LOG(LDEBUG, ("No historical speeds section in MWM:", mwmId));
      return nullptr;
    }

    auto reader = container.GetReader(HISTORICAL_SPEEDS_FILE_TAG);
    std::vector<uint8_t> buffer(static_cast<size_t>(reader.Size()));
    reader.Read(0, buffer.data(), buffer.size());

    auto data = std::make_shared<HistoricalSpeedData>();

    MemReaderWithExceptions memReader(buffer.data(), buffer.size());
    ReaderSource<MemReaderWithExceptions> source(memReader);
    data->Deserialize(source);

    LOG(LINFO, ("Loaded historical speeds for", mwmId, ":", data->GetSegmentCount(), "segments"));
    return data;
  }
  catch (RootException const & e)
  {
    LOG(LWARNING, ("Failed to load historical speeds for", mwmId, ":", e.Msg()));
    return nullptr;
  }
}

std::shared_ptr<HistoricalSpeedData const> HistoricalSpeedProvider::GetOrLoadData(
    MwmSet::MwmId const & mwmId) const
{
  // Fast path: check cache without exclusive lock
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cache.find(mwmId);
    if (it != m_cache.end())
      return it->second;
  }

  // Slow path: load data and update cache
  auto data = LoadData(mwmId);

  // Store in cache (even if nullptr to avoid repeated load attempts)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Double-check in case another thread loaded while we were loading
    auto it = m_cache.find(mwmId);
    if (it != m_cache.end())
      return it->second;

    m_cache[mwmId] = data;
  }

  return data;
}

// TestHistoricalSpeedProvider implementation

void TestHistoricalSpeedProvider::SetData(MwmSet::MwmId const & mwmId,
                                           std::shared_ptr<HistoricalSpeedData> data)
{
  m_data[mwmId] = std::move(data);
}

SpeedPercentage TestHistoricalSpeedProvider::GetSpeed(MwmSet::MwmId const & mwmId,
                                                       uint32_t featureId, uint16_t segmentIdx,
                                                       bool isForward, std::time_t time) const
{
  auto it = m_data.find(mwmId);
  if (it == m_data.end() || !it->second)
    return kNoSpeedData;

  return it->second->GetSpeed(featureId, segmentIdx, isForward, time);
}

SpeedGroup TestHistoricalSpeedProvider::GetSpeedGroup(MwmSet::MwmId const & mwmId,
                                                       uint32_t featureId, uint16_t segmentIdx,
                                                       bool isForward, std::time_t time) const
{
  SpeedPercentage const speed = GetSpeed(mwmId, featureId, segmentIdx, isForward, time);
  return SpeedPercentageToGroup(speed);
}

bool TestHistoricalSpeedProvider::HasData(MwmSet::MwmId const & mwmId) const
{
  auto it = m_data.find(mwmId);
  return it != m_data.end() && it->second && it->second->GetSegmentCount() > 0;
}

}  // namespace traffic

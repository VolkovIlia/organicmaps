#include "map/speed_data_collector.hpp"

#include "base/logging.hpp"

#include <ctime>

namespace map
{
namespace
{
// Conversion factor: m/s to km/h
constexpr double kMsToKmph = 3.6;
}  // namespace

SpeedDataCollector::SpeedDataCollector(std::string const & storagePath)
  : m_storage(std::make_shared<traffic::PersonalSpeedStorage>(storagePath))
{
}

SpeedDataCollector::SpeedDataCollector(std::shared_ptr<traffic::PersonalSpeedStorage> storage)
  : m_storage(std::move(storage))
{
}

SpeedDataCollector::~SpeedDataCollector()
{
  try
  {
    Flush();
  }
  catch (std::exception const & e)
  {
    LOG(LERROR, ("Failed to flush speed data collector:", e.what()));
  }
}

void SpeedDataCollector::OnLocationUpdate(location::GpsInfo const & info,
                                           routing::Segment const & segment, bool isNavigating)
{
  if (!m_enabled || !isNavigating)
    return;

  if (!IsValidObservation(info, segment))
    return;

  // Throttle observations for same segment
  {
    std::lock_guard<std::mutex> lock(m_lastObservationMutex);

    if (segment == m_lastSegment &&
        (info.m_timestamp - m_lastObservationTime) < kMinObservationIntervalSec)
    {
      return;
    }

    m_lastSegment = segment;
    m_lastObservationTime = info.m_timestamp;
  }

  // Convert speed from m/s to km/h
  float const speedKmph = static_cast<float>(info.m_speed * kMsToKmph);

  // Get time components
  auto const timestamp = static_cast<std::time_t>(info.m_timestamp);
  uint8_t dayOfWeek, hour;
  GetTimeComponents(timestamp, dayOfWeek, hour);

  // Store observation
  m_storage->AddObservation(segment.GetFeatureId(),
                            static_cast<uint16_t>(segment.GetSegmentIdx()),
                            segment.IsForward(), dayOfWeek, hour, speedKmph);

  ++m_sessionObservationCount;
}

void SpeedDataCollector::SetEnabled(bool enabled)
{
  m_enabled = enabled;
  if (!enabled)
    Flush();
}

void SpeedDataCollector::Flush()
{
  if (m_storage)
    m_storage->Flush();
}

void SpeedDataCollector::Clear()
{
  if (m_storage)
    m_storage->Clear();

  m_sessionObservationCount = 0;

  std::lock_guard<std::mutex> lock(m_lastObservationMutex);
  m_lastSegment = routing::Segment();
  m_lastObservationTime = 0.0;
}

void SpeedDataCollector::CleanupOldRecords()
{
  if (m_storage)
    m_storage->CleanupOldRecords();
}

void SpeedDataCollector::GetTimeComponents(std::time_t timestamp, uint8_t & dayOfWeek,
                                            uint8_t & hour)
{
  std::tm localTime;

#ifdef _WIN32
  if (localtime_s(&localTime, &timestamp) != 0)
  {
    dayOfWeek = 0;
    hour = 0;
    return;
  }
#else
  if (localtime_r(&timestamp, &localTime) == nullptr)
  {
    dayOfWeek = 0;
    hour = 0;
    return;
  }
#endif

  dayOfWeek = static_cast<uint8_t>(localTime.tm_wday);
  hour = static_cast<uint8_t>(localTime.tm_hour);
}

bool SpeedDataCollector::IsValidObservation(location::GpsInfo const & info,
                                             routing::Segment const & segment) const
{
  // Must have valid speed
  if (!info.HasSpeed() || info.m_speed < 0.0)
    return false;

  // Convert to km/h for threshold checks
  float const speedKmph = static_cast<float>(info.m_speed * kMsToKmph);

  // Filter out very slow speeds (stopped, parking, etc.)
  if (speedKmph < kMinSpeedKmph)
    return false;

  // Filter out unreasonably high speeds (GPS glitches)
  if (speedKmph > kMaxSpeedKmph)
    return false;

  // Check GPS accuracy
  if (info.m_horizontalAccuracy > kMaxHorizontalAccuracy)
    return false;

  // Must be a real segment (not fake start/end)
  if (!segment.IsRealSegment())
    return false;

  return true;
}

}  // namespace map

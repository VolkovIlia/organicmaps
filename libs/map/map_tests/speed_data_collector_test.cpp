#include "testing/testing.hpp"

#include "map/speed_data_collector.hpp"
#include "traffic/personal_speed_storage.hpp"

#include "routing/segment.hpp"

#include "platform/platform.hpp"

#include <cstdio>

namespace speed_data_collector_test
{
using namespace map;

namespace
{
std::string GetTestFilePath()
{
  return GetPlatform().TmpPathForFile("speed_collector_test.bin");
}

void CleanupTestFile(std::string const & path)
{
  std::remove(path.c_str());
}

location::GpsInfo MakeGpsInfo(double speed, double accuracy = 10.0, double timestamp = 1000.0)
{
  location::GpsInfo info;
  info.m_source = location::EAndroidNative;
  info.m_speed = speed;
  info.m_horizontalAccuracy = accuracy;
  info.m_timestamp = timestamp;
  info.m_latitude = 55.75;
  info.m_longitude = 37.62;
  return info;
}

routing::Segment MakeSegment(uint32_t featureId, uint32_t segmentIdx, bool forward)
{
  return routing::Segment(0 /* mwmId */, featureId, segmentIdx, forward);
}
}  // namespace

UNIT_TEST(SpeedDataCollector_BasicCollection)
{
  std::string const path = GetTestFilePath();
  CleanupTestFile(path);

  {
    SpeedDataCollector collector(path);

    // 60 km/h = 16.67 m/s
    auto info = MakeGpsInfo(16.67);
    auto segment = MakeSegment(100, 0, true);

    collector.OnLocationUpdate(info, segment, true /* isNavigating */);

    TEST_EQUAL(collector.GetSessionObservationCount(), 1, ());

    auto storage = collector.GetStorage();
    TEST_EQUAL(storage->GetRecordCount(), 1, ());
  }

  CleanupTestFile(path);
}

UNIT_TEST(SpeedDataCollector_DisabledCollection)
{
  std::string const path = GetTestFilePath();
  CleanupTestFile(path);

  {
    SpeedDataCollector collector(path);
    collector.SetEnabled(false);

    auto info = MakeGpsInfo(16.67);
    auto segment = MakeSegment(100, 0, true);

    collector.OnLocationUpdate(info, segment, true);

    TEST_EQUAL(collector.GetSessionObservationCount(), 0, ());
  }

  CleanupTestFile(path);
}

UNIT_TEST(SpeedDataCollector_NotNavigating)
{
  std::string const path = GetTestFilePath();
  CleanupTestFile(path);

  {
    SpeedDataCollector collector(path);

    auto info = MakeGpsInfo(16.67);
    auto segment = MakeSegment(100, 0, true);

    collector.OnLocationUpdate(info, segment, false /* isNavigating */);

    TEST_EQUAL(collector.GetSessionObservationCount(), 0, ());
  }

  CleanupTestFile(path);
}

UNIT_TEST(SpeedDataCollector_FilterSlowSpeed)
{
  std::string const path = GetTestFilePath();
  CleanupTestFile(path);

  {
    SpeedDataCollector collector(path);

    // Very slow speed (2 km/h = 0.56 m/s) should be filtered
    auto info = MakeGpsInfo(0.56);
    auto segment = MakeSegment(100, 0, true);

    collector.OnLocationUpdate(info, segment, true);

    TEST_EQUAL(collector.GetSessionObservationCount(), 0, ());
  }

  CleanupTestFile(path);
}

UNIT_TEST(SpeedDataCollector_FilterHighSpeed)
{
  std::string const path = GetTestFilePath();
  CleanupTestFile(path);

  {
    SpeedDataCollector collector(path);

    // Unreasonably high speed (300 km/h = 83 m/s) should be filtered
    auto info = MakeGpsInfo(83.0);
    auto segment = MakeSegment(100, 0, true);

    collector.OnLocationUpdate(info, segment, true);

    TEST_EQUAL(collector.GetSessionObservationCount(), 0, ());
  }

  CleanupTestFile(path);
}

UNIT_TEST(SpeedDataCollector_FilterBadAccuracy)
{
  std::string const path = GetTestFilePath();
  CleanupTestFile(path);

  {
    SpeedDataCollector collector(path);

    // Bad GPS accuracy (100m) should be filtered
    auto info = MakeGpsInfo(16.67, 100.0 /* accuracy */);
    auto segment = MakeSegment(100, 0, true);

    collector.OnLocationUpdate(info, segment, true);

    TEST_EQUAL(collector.GetSessionObservationCount(), 0, ());
  }

  CleanupTestFile(path);
}

UNIT_TEST(SpeedDataCollector_ThrottleSameSegment)
{
  std::string const path = GetTestFilePath();
  CleanupTestFile(path);

  {
    SpeedDataCollector collector(path);

    auto segment = MakeSegment(100, 0, true);

    // First observation at t=1000
    auto info1 = MakeGpsInfo(16.67, 10.0, 1000.0);
    collector.OnLocationUpdate(info1, segment, true);

    // Second observation at t=1002 (2 sec later, should be throttled)
    auto info2 = MakeGpsInfo(17.0, 10.0, 1002.0);
    collector.OnLocationUpdate(info2, segment, true);

    // Only 1 observation should be recorded
    TEST_EQUAL(collector.GetSessionObservationCount(), 1, ());

    // Third observation at t=1010 (10 sec later, should pass)
    auto info3 = MakeGpsInfo(18.0, 10.0, 1010.0);
    collector.OnLocationUpdate(info3, segment, true);

    TEST_EQUAL(collector.GetSessionObservationCount(), 2, ());
  }

  CleanupTestFile(path);
}

UNIT_TEST(SpeedDataCollector_DifferentSegments)
{
  std::string const path = GetTestFilePath();
  CleanupTestFile(path);

  {
    SpeedDataCollector collector(path);

    auto info = MakeGpsInfo(16.67, 10.0, 1000.0);

    // Different segments at same time should all be recorded
    collector.OnLocationUpdate(info, MakeSegment(100, 0, true), true);
    info.m_timestamp = 1001.0;
    collector.OnLocationUpdate(info, MakeSegment(100, 1, true), true);
    info.m_timestamp = 1002.0;
    collector.OnLocationUpdate(info, MakeSegment(101, 0, true), true);

    TEST_EQUAL(collector.GetSessionObservationCount(), 3, ());
  }

  CleanupTestFile(path);
}

UNIT_TEST(SpeedDataCollector_Persistence)
{
  std::string const path = GetTestFilePath();
  CleanupTestFile(path);

  // Collect some data
  {
    SpeedDataCollector collector(path);

    auto info = MakeGpsInfo(16.67);
    collector.OnLocationUpdate(info, MakeSegment(100, 0, true), true);
    info.m_timestamp += 10.0;
    collector.OnLocationUpdate(info, MakeSegment(100, 1, true), true);

    collector.Flush();
  }

  // Read back
  {
    SpeedDataCollector collector(path);

    auto storage = collector.GetStorage();
    TEST_EQUAL(storage->GetRecordCount(), 2, ());
  }

  CleanupTestFile(path);
}

UNIT_TEST(SpeedDataCollector_Clear)
{
  std::string const path = GetTestFilePath();
  CleanupTestFile(path);

  {
    SpeedDataCollector collector(path);

    auto info = MakeGpsInfo(16.67);
    collector.OnLocationUpdate(info, MakeSegment(100, 0, true), true);

    TEST_EQUAL(collector.GetSessionObservationCount(), 1, ());
    TEST_EQUAL(collector.GetStorage()->GetRecordCount(), 1, ());

    collector.Clear();

    TEST_EQUAL(collector.GetSessionObservationCount(), 0, ());
    TEST_EQUAL(collector.GetStorage()->GetRecordCount(), 0, ());
  }

  CleanupTestFile(path);
}

}  // namespace speed_data_collector_test

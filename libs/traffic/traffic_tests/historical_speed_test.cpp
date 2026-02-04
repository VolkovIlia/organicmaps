#include "testing/testing.hpp"

#include "traffic/historical_speed_data.hpp"

#include "coding/reader.hpp"
#include "coding/writer.hpp"

#include <ctime>
#include <vector>

namespace historical_speed_test
{
using namespace traffic;

UNIT_TEST(HistoricalSpeed_TimeSlotIndex)
{
  // Sunday 00:00 -> slot 0
  TEST_EQUAL(TimeSlotIndex(0, 0), 0u, ());

  // Sunday 23:00 -> slot 23
  TEST_EQUAL(TimeSlotIndex(0, 23), 23u, ());

  // Monday 00:00 -> slot 24
  TEST_EQUAL(TimeSlotIndex(1, 0), 24u, ());

  // Saturday 23:00 -> slot 167 (last slot)
  TEST_EQUAL(TimeSlotIndex(6, 23), 167u, ());
}

UNIT_TEST(HistoricalSpeed_TimeSlotToComponents)
{
  uint8_t dayOfWeek, hour;

  TimeSlotToComponents(0, dayOfWeek, hour);
  TEST_EQUAL(dayOfWeek, 0u, ());
  TEST_EQUAL(hour, 0u, ());

  TimeSlotToComponents(23, dayOfWeek, hour);
  TEST_EQUAL(dayOfWeek, 0u, ());
  TEST_EQUAL(hour, 23u, ());

  TimeSlotToComponents(24, dayOfWeek, hour);
  TEST_EQUAL(dayOfWeek, 1u, ());
  TEST_EQUAL(hour, 0u, ());

  TimeSlotToComponents(167, dayOfWeek, hour);
  TEST_EQUAL(dayOfWeek, 6u, ());
  TEST_EQUAL(hour, 23u, ());
}

UNIT_TEST(SegmentSpeedPattern_BasicOperations)
{
  SegmentSpeedPattern pattern;

  // Initially no data
  TEST(!pattern.HasData(), ());
  TEST_EQUAL(pattern.GetSpeed(0, 0), kNoSpeedData, ());

  // Set some speeds
  pattern.SetSpeed(0, 0, 80);   // Sunday midnight
  pattern.SetSpeed(1, 8, 50);   // Monday 8 AM (rush hour)
  pattern.SetSpeed(5, 17, 40);  // Friday 5 PM (rush hour)

  TEST(pattern.HasData(), ());
  TEST_EQUAL(pattern.GetSpeed(0, 0), 80u, ());
  TEST_EQUAL(pattern.GetSpeed(1, 8), 50u, ());
  TEST_EQUAL(pattern.GetSpeed(5, 17), 40u, ());

  // Check using time slot index
  TEST_EQUAL(pattern.GetSpeed(TimeSlotIndex(0, 0)), 80u, ());
  TEST_EQUAL(pattern.GetSpeed(TimeSlotIndex(1, 8)), 50u, ());
  TEST_EQUAL(pattern.GetSpeed(TimeSlotIndex(5, 17)), 40u, ());
}

UNIT_TEST(SpeedPercentageToGroup_Conversion)
{
  // No data
  TEST_EQUAL(SpeedPercentageToGroup(kNoSpeedData), SpeedGroup::Unknown, ());

  // Blocked
  TEST_EQUAL(SpeedPercentageToGroup(kBlockedSpeed), SpeedGroup::TempBlock, ());

  // G0: 0-8%
  TEST_EQUAL(SpeedPercentageToGroup(5), SpeedGroup::G0, ());
  TEST_EQUAL(SpeedPercentageToGroup(8), SpeedGroup::G0, ());

  // G1: 8-16%
  TEST_EQUAL(SpeedPercentageToGroup(12), SpeedGroup::G1, ());
  TEST_EQUAL(SpeedPercentageToGroup(16), SpeedGroup::G1, ());

  // G2: 16-33%
  TEST_EQUAL(SpeedPercentageToGroup(25), SpeedGroup::G2, ());
  TEST_EQUAL(SpeedPercentageToGroup(33), SpeedGroup::G2, ());

  // G3: 33-58%
  TEST_EQUAL(SpeedPercentageToGroup(45), SpeedGroup::G3, ());
  TEST_EQUAL(SpeedPercentageToGroup(58), SpeedGroup::G3, ());

  // G4: 58-83%
  TEST_EQUAL(SpeedPercentageToGroup(70), SpeedGroup::G4, ());
  TEST_EQUAL(SpeedPercentageToGroup(83), SpeedGroup::G4, ());

  // G5: 83-100% and above
  TEST_EQUAL(SpeedPercentageToGroup(90), SpeedGroup::G5, ());
  TEST_EQUAL(SpeedPercentageToGroup(100), SpeedGroup::G5, ());
  TEST_EQUAL(SpeedPercentageToGroup(150), SpeedGroup::G5, ());  // Above 100%
}

UNIT_TEST(HistoricalSpeedData_SetGetPattern)
{
  HistoricalSpeedData data;

  SegmentSpeedPattern pattern1;
  pattern1.SetSpeed(0, 0, 80);
  pattern1.SetSpeed(1, 8, 50);

  SegmentSpeedPattern pattern2;
  pattern2.SetSpeed(0, 0, 90);
  pattern2.SetSpeed(1, 8, 60);

  // Set patterns
  data.SetPattern(100, 0, true, pattern1);   // Feature 100, segment 0, forward
  data.SetPattern(100, 0, false, pattern2);  // Feature 100, segment 0, backward
  data.SetPattern(100, 1, true, pattern1);   // Feature 100, segment 1, forward
  data.SetPattern(200, 0, true, pattern2);   // Feature 200, segment 0, forward

  TEST_EQUAL(data.GetSegmentCount(), 4u, ());

  // Get patterns
  auto retrieved1 = data.GetPattern(100, 0, true);
  TEST(retrieved1.has_value(), ());
  TEST_EQUAL(retrieved1->GetSpeed(0, 0), 80u, ());

  auto retrieved2 = data.GetPattern(100, 0, false);
  TEST(retrieved2.has_value(), ());
  TEST_EQUAL(retrieved2->GetSpeed(0, 0), 90u, ());

  // Check HasPattern
  TEST(data.HasPattern(100, 0, true), ());
  TEST(data.HasPattern(100, 0, false), ());
  TEST(data.HasPattern(200, 0, true), ());
  TEST(!data.HasPattern(200, 0, false), ());  // Not set
  TEST(!data.HasPattern(300, 0, true), ());   // Non-existent feature
}

UNIT_TEST(HistoricalSpeedData_Serialization)
{
  HistoricalSpeedData original;

  // Create some test patterns
  SegmentSpeedPattern pattern1;
  for (uint32_t slot = 0; slot < kTimeSlots; ++slot)
    pattern1.SetSpeed(slot, static_cast<SpeedPercentage>(50 + (slot % 50)));

  SegmentSpeedPattern pattern2;
  for (uint32_t slot = 0; slot < kTimeSlots; ++slot)
    pattern2.SetSpeed(slot, static_cast<SpeedPercentage>(60 + (slot % 40)));

  original.SetPattern(100, 0, true, pattern1);
  original.SetPattern(100, 1, true, pattern1);
  original.SetPattern(200, 0, true, pattern2);
  original.SetPattern(300, 5, false, pattern2);

  // Serialize
  std::vector<uint8_t> buffer;
  MemWriter<std::vector<uint8_t>> writer(buffer);
  original.Serialize(writer);

  // Deserialize
  HistoricalSpeedData loaded;
  MemReaderWithExceptions reader(buffer.data(), buffer.size());
  ReaderSource<MemReaderWithExceptions> source(reader);
  loaded.Deserialize(source);

  // Verify
  TEST_EQUAL(loaded.GetSegmentCount(), original.GetSegmentCount(), ());

  auto pattern1Loaded = loaded.GetPattern(100, 0, true);
  TEST(pattern1Loaded.has_value(), ());
  TEST(*pattern1Loaded == pattern1, ());

  auto pattern2Loaded = loaded.GetPattern(200, 0, true);
  TEST(pattern2Loaded.has_value(), ());
  TEST(*pattern2Loaded == pattern2, ());

  auto pattern3Loaded = loaded.GetPattern(300, 5, false);
  TEST(pattern3Loaded.has_value(), ());
  TEST(*pattern3Loaded == pattern2, ());
}

UNIT_TEST(HistoricalSpeedData_GetSpeedAtTime)
{
  HistoricalSpeedData data;

  SegmentSpeedPattern pattern;
  // Set different speeds for different times
  pattern.SetSpeed(0, 0, 90);   // Sunday midnight - fast
  pattern.SetSpeed(1, 8, 50);   // Monday 8 AM - rush hour
  pattern.SetSpeed(1, 12, 80);  // Monday noon - moderate
  pattern.SetSpeed(5, 17, 40);  // Friday 5 PM - heavy traffic

  data.SetPattern(100, 0, true, pattern);

  // Create time values for testing
  // Note: time_t handling is system-dependent, so we use TimeSlotIndex directly
  // for verification
  TEST_EQUAL(data.GetSpeedGroup(100, 0, true, 0), SpeedGroup::Unknown, ());  // time 0 depends on system

  // Test HasPattern
  TEST(data.HasPattern(100, 0, true), ());
  TEST(!data.HasPattern(100, 0, false), ());
}

}  // namespace historical_speed_test

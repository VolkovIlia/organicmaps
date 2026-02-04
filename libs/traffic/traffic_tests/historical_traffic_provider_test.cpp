#include "testing/testing.hpp"

#include "traffic/historical_speed_data.hpp"
#include "traffic/historical_traffic_provider.hpp"

#include <ctime>

namespace historical_traffic_provider_test
{
using namespace traffic;

UNIT_TEST(HistoricalTrafficProvider_ForEachPattern)
{
  HistoricalSpeedData data;

  // Create test patterns
  SegmentSpeedPattern pattern1;
  pattern1.SetSpeed(0, 0, 80);   // Sunday midnight
  pattern1.SetSpeed(1, 8, 50);   // Monday 8 AM

  SegmentSpeedPattern pattern2;
  pattern2.SetSpeed(5, 17, 40);  // Friday 5 PM

  data.SetPattern(100, 0, true, pattern1);
  data.SetPattern(100, 0, false, pattern2);
  data.SetPattern(200, 1, true, pattern1);

  // Test ForEachPattern
  size_t count = 0;
  data.ForEachPattern([&](uint32_t featureId, uint16_t segmentIdx, bool isForward,
                          SegmentSpeedPattern const & pattern) {
    ++count;
    if (featureId == 100 && segmentIdx == 0 && isForward)
    {
      TEST_EQUAL(pattern.GetSpeed(0, 0), 80u, ());
      TEST_EQUAL(pattern.GetSpeed(1, 8), 50u, ());
    }
    else if (featureId == 100 && segmentIdx == 0 && !isForward)
    {
      TEST_EQUAL(pattern.GetSpeed(5, 17), 40u, ());
    }
    else if (featureId == 200 && segmentIdx == 1 && isForward)
    {
      TEST_EQUAL(pattern.GetSpeed(0, 0), 80u, ());
    }
    else
    {
      TEST(false, ("Unexpected pattern"));
    }
  });

  TEST_EQUAL(count, 3u, ());
}

UNIT_TEST(HistoricalTrafficProvider_ConvertToColoring)
{
  HistoricalSpeedData data;

  // Create patterns with known speeds
  SegmentSpeedPattern pattern;
  // Set speeds for all time slots to test conversion
  for (uint8_t day = 0; day < 7; ++day)
  {
    for (uint8_t hour = 0; hour < 24; ++hour)
    {
      // Different speeds for different times
      SpeedPercentage speed = 0;
      if (hour >= 7 && hour <= 9)       // Morning rush
        speed = 40;
      else if (hour >= 17 && hour <= 19) // Evening rush
        speed = 35;
      else if (hour >= 22 || hour < 6)   // Night
        speed = 95;
      else                                // Day
        speed = 70;

      pattern.SetSpeed(day, hour, speed);
    }
  }

  data.SetPattern(100, 0, true, pattern);
  data.SetPattern(100, 1, false, pattern);

  // Test SpeedPercentageToGroup conversion
  TEST_EQUAL(SpeedPercentageToGroup(40), SpeedGroup::G3, ());
  TEST_EQUAL(SpeedPercentageToGroup(35), SpeedGroup::G3, ());
  TEST_EQUAL(SpeedPercentageToGroup(95), SpeedGroup::G5, ());
  TEST_EQUAL(SpeedPercentageToGroup(70), SpeedGroup::G4, ());
}

UNIT_TEST(SpeedPercentageToGroup_EdgeCases)
{
  // Test edge cases for SpeedPercentageToGroup

  // No data
  TEST_EQUAL(SpeedPercentageToGroup(kNoSpeedData), SpeedGroup::Unknown, ());

  // Blocked
  TEST_EQUAL(SpeedPercentageToGroup(kBlockedSpeed), SpeedGroup::TempBlock, ());

  // Boundary values
  TEST_EQUAL(SpeedPercentageToGroup(1), SpeedGroup::G0, ());
  TEST_EQUAL(SpeedPercentageToGroup(8), SpeedGroup::G0, ());
  TEST_EQUAL(SpeedPercentageToGroup(9), SpeedGroup::G1, ());
  TEST_EQUAL(SpeedPercentageToGroup(100), SpeedGroup::G5, ());
  TEST_EQUAL(SpeedPercentageToGroup(150), SpeedGroup::G5, ());  // Above 100%
}

UNIT_TEST(HistoricalTrafficProvider_EmptyProvider)
{
  HistoricalTrafficProvider provider;

  // Empty MwmId should return no data
  MwmSet::MwmId emptyId;
  TEST(!provider.HasData(emptyId), ());

  auto coloring = provider.GetColoring(emptyId);
  TEST(!coloring.has_value(), ());

  TEST_EQUAL(provider.GetCachedMwmCount(), 0u, ());
}

}  // namespace historical_traffic_provider_test

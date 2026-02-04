#include "testing/testing.hpp"

#include "p2p/aggregated_traffic_share.hpp"

using namespace p2p;
using namespace traffic;

namespace aggregated_traffic_share_test
{
UNIT_TEST(AggregatedTrafficShare_IsValid)
{
  AggregatedTrafficShare share;
  TEST(!share.IsValid(), ("Empty share should be invalid"));

  share.m_h3Cell = 0x8928308280fffff;
  share.m_confidence = 5;
  TEST(share.IsValid(), ("Share with cell and confidence should be valid"));
}

UNIT_TEST(AggregatedTrafficShare_SpeedGroup)
{
  AggregatedTrafficShare share;
  share.SetSpeedGroup(SpeedGroup::G3);
  TEST_EQUAL(share.GetSpeedGroup(), SpeedGroup::G3, ());
  TEST_EQUAL(share.m_speedBucket, static_cast<uint8_t>(SpeedGroup::G3), ());
}

UNIT_TEST(AggregatedTrafficShare_Confidence)
{
  AggregatedTrafficShare share;
  share.m_confidence = 254;
  share.IncrementConfidence();
  TEST_EQUAL(share.m_confidence, 255u, ());

  share.IncrementConfidence();  // Should cap at 255
  TEST_EQUAL(share.m_confidence, 255u, ());
}

UNIT_TEST(AggregatedTrafficShare_HopCount)
{
  AggregatedTrafficShare share;
  share.m_hopCount = 2;

  TEST(share.DecrementHop(), ());
  TEST_EQUAL(share.m_hopCount, 1u, ());

  TEST(share.DecrementHop(), ());
  TEST_EQUAL(share.m_hopCount, 0u, ());

  TEST(!share.DecrementHop(), ("Cannot decrement below 0"));
}

UNIT_TEST(AggregatedTrafficShare_Expiration)
{
  AggregatedTrafficShare share;
  share.m_timestampMinutes = 100;

  TEST(!share.IsExpired(105, 10), ("Within TTL"));
  TEST(!share.IsExpired(110, 10), ("At TTL boundary"));
  TEST(share.IsExpired(111, 10), ("Past TTL"));
}

UNIT_TEST(AggregatedTrafficShare_Expiration_FutureTimestamp)
{
  AggregatedTrafficShare share;
  share.m_timestampMinutes = 200;

  // Future timestamp (clock skew) should not be expired
  TEST(!share.IsExpired(100, 10), ("Future timestamp should not be expired"));
}

UNIT_TEST(AggregatedTrafficShare_InvalidSpeedBucket)
{
  AggregatedTrafficShare share;
  share.m_speedBucket = 100;  // Invalid value

  TEST_EQUAL(share.GetSpeedGroup(), SpeedGroup::Unknown, ());
}

UNIT_TEST(TrafficShareSerializer_RoundTrip)
{
  AggregatedTrafficShare original;
  original.m_h3Cell = 0x8928308280fffff;
  original.m_speedBucket = 3;
  original.m_confidence = 42;
  original.m_timestampMinutes = 12345678;
  original.m_hopCount = 2;

  auto serialized = TrafficShareSerializer::Serialize(original);
  TEST_EQUAL(serialized.size(), 15u, ());

  auto deserialized = TrafficShareSerializer::Deserialize(serialized);
  TEST(deserialized.has_value(), ());

  TEST_EQUAL(deserialized->m_h3Cell, original.m_h3Cell, ());
  TEST_EQUAL(deserialized->m_speedBucket, original.m_speedBucket, ());
  TEST_EQUAL(deserialized->m_confidence, original.m_confidence, ());
  TEST_EQUAL(deserialized->m_timestampMinutes, original.m_timestampMinutes, ());
  TEST_EQUAL(deserialized->m_hopCount, original.m_hopCount, ());
}

UNIT_TEST(TrafficShareSerializer_InvalidSize)
{
  uint8_t smallData[10] = {};
  auto result = TrafficShareSerializer::Deserialize(smallData, 10);
  TEST(!result.has_value(), ("Should fail with insufficient data"));
}

UNIT_TEST(TrafficShareSerializer_NullPointer)
{
  auto result = TrafficShareSerializer::Deserialize(nullptr, 15);
  TEST(!result.has_value(), ("Should fail with null pointer"));
}

UNIT_TEST(TrafficShareSerializer_ZeroSize)
{
  uint8_t data[15] = {};
  auto result = TrafficShareSerializer::Deserialize(data, 0);
  TEST(!result.has_value(), ("Should fail with zero size"));
}

UNIT_TEST(TrafficShareSerializer_ExactSize)
{
  uint8_t data[15] = {
      0xFF, 0xFF, 0x0F, 0x82, 0x08, 0x93, 0x28, 0x89,  // H3 cell
      0x03,                                            // Speed bucket
      0x2A,                                            // Confidence (42)
      0x4E, 0x61, 0xBC, 0x00,                          // Timestamp
      0x02                                             // Hop count
  };

  auto result = TrafficShareSerializer::Deserialize(data, 15);
  TEST(result.has_value(), ());
}

UNIT_TEST(TrafficShareBuilder_Fluent)
{
  auto share = TrafficShareBuilder()
      .SetCell(0x123456789)
      .SetSpeedGroup(SpeedGroup::G4)
      .SetConfidence(10)
      .SetTimestamp(1000)
      .SetHopCount(3)
      .Build();

  TEST_EQUAL(share.m_h3Cell, 0x123456789u, ());
  TEST_EQUAL(share.GetSpeedGroup(), SpeedGroup::G4, ());
  TEST_EQUAL(share.m_confidence, 10u, ());
  TEST_EQUAL(share.m_timestampMinutes, 1000u, ());
  TEST_EQUAL(share.m_hopCount, 3u, ());
}

UNIT_TEST(TrafficShareBuilder_CreateNow)
{
  auto share = TrafficShareBuilder::CreateNow(0xABCDEF, SpeedGroup::G2, 5);

  TEST_EQUAL(share.m_h3Cell, 0xABCDEFu, ());
  TEST_EQUAL(share.GetSpeedGroup(), SpeedGroup::G2, ());
  TEST_EQUAL(share.m_confidence, 5u, ());
  TEST_EQUAL(share.m_hopCount, PrivacyConfig::kMaxHopCount, ());
  TEST_GREATER(share.m_timestampMinutes, 0u, ());
}

UNIT_TEST(GetCurrentTimestampMinutes)
{
  uint32_t ts1 = GetCurrentTimestampMinutes();
  TEST_GREATER(ts1, 0u, ());

  // Should return same value if called quickly
  uint32_t ts2 = GetCurrentTimestampMinutes();
  TEST_EQUAL(ts1, ts2, ());
}

UNIT_TEST(AggregatedTrafficShare_DebugPrint)
{
  AggregatedTrafficShare share;
  share.m_h3Cell = 0x8928308280fffff;
  share.SetSpeedGroup(SpeedGroup::G3);
  share.m_confidence = 42;
  share.m_timestampMinutes = 1000;
  share.m_hopCount = 2;

  std::string debug = DebugPrint(share);
  TEST(debug.find("TrafficShare") != std::string::npos, ());
  TEST(debug.find("cell=") != std::string::npos, ());
  TEST(debug.find("G3") != std::string::npos, ());
}

UNIT_TEST(TrafficShareSerializer_AllSpeedGroups)
{
  // Test serialization for all valid SpeedGroup values
  for (uint8_t i = 0; i <= static_cast<uint8_t>(SpeedGroup::Unknown); ++i)
  {
    AggregatedTrafficShare original;
    original.m_h3Cell = 0x1234567890ABCDEF;
    original.m_speedBucket = i;
    original.m_confidence = 100;
    original.m_timestampMinutes = 5000;
    original.m_hopCount = 1;

    auto serialized = TrafficShareSerializer::Serialize(original);
    auto deserialized = TrafficShareSerializer::Deserialize(serialized);

    TEST(deserialized.has_value(), ());
    TEST_EQUAL(deserialized->m_speedBucket, i, ());
  }
}

UNIT_TEST(TrafficShareSerializer_EdgeValues)
{
  // Test with maximum values
  AggregatedTrafficShare maxShare;
  maxShare.m_h3Cell = UINT64_MAX;
  maxShare.m_speedBucket = 255;
  maxShare.m_confidence = 255;
  maxShare.m_timestampMinutes = UINT32_MAX;
  maxShare.m_hopCount = 255;

  auto serialized = TrafficShareSerializer::Serialize(maxShare);
  auto deserialized = TrafficShareSerializer::Deserialize(serialized);

  TEST(deserialized.has_value(), ());
  TEST_EQUAL(deserialized->m_h3Cell, UINT64_MAX, ());
  TEST_EQUAL(deserialized->m_speedBucket, 255u, ());
  TEST_EQUAL(deserialized->m_confidence, 255u, ());
  TEST_EQUAL(deserialized->m_timestampMinutes, UINT32_MAX, ());
  TEST_EQUAL(deserialized->m_hopCount, 255u, ());
}

UNIT_TEST(TrafficShareSerializer_ZeroValues)
{
  // Test with zero values
  AggregatedTrafficShare zeroShare;
  zeroShare.m_h3Cell = 0;
  zeroShare.m_speedBucket = 0;
  zeroShare.m_confidence = 0;
  zeroShare.m_timestampMinutes = 0;
  zeroShare.m_hopCount = 0;

  auto serialized = TrafficShareSerializer::Serialize(zeroShare);
  auto deserialized = TrafficShareSerializer::Deserialize(serialized);

  TEST(deserialized.has_value(), ());
  TEST_EQUAL(deserialized->m_h3Cell, 0u, ());
  TEST_EQUAL(deserialized->m_speedBucket, 0u, ());
  TEST_EQUAL(deserialized->m_confidence, 0u, ());
  TEST_EQUAL(deserialized->m_timestampMinutes, 0u, ());
  TEST_EQUAL(deserialized->m_hopCount, 0u, ());
}
}  // namespace aggregated_traffic_share_test

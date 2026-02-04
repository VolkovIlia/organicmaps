#include "testing/testing.hpp"

#include "p2p/rolling_id_generator.hpp"

namespace rolling_id_generator_test
{
UNIT_TEST(RollingIdGenerator_InitialId)
{
  p2p::RollingIdGenerator generator;
  auto id = generator.GetCurrentId();

  // ID should be non-zero
  bool allZero = true;
  for (uint8_t byte : id)
  {
    if (byte != 0)
    {
      allZero = false;
      break;
    }
  }
  TEST(!allZero, ("Initial ID should not be all zeros"));
}

UNIT_TEST(RollingIdGenerator_IdIsStable)
{
  p2p::RollingIdGenerator generator;
  auto id1 = generator.GetCurrentId();
  auto id2 = generator.GetCurrentId();

  TEST_EQUAL(id1, id2, ("ID should be stable within rotation period"));
}

UNIT_TEST(RollingIdGenerator_Regenerate)
{
  p2p::RollingIdGenerator generator;
  auto id1 = generator.GetCurrentId();
  generator.Regenerate();
  auto id2 = generator.GetCurrentId();

  TEST_NOT_EQUAL(id1, id2, ("ID should change after regeneration"));
}

UNIT_TEST(RollingIdGenerator_ToHex)
{
  p2p::RollingId id{};
  id[0] = 0xAB;
  id[1] = 0xCD;
  id[15] = 0xEF;

  std::string hex = p2p::RollingIdToHex(id);
  TEST_EQUAL(hex.size(), 32u, ("Hex string should be 32 characters"));
  TEST_EQUAL(hex.substr(0, 4), "abcd", ());
  TEST_EQUAL(hex.substr(30, 2), "ef", ());
}

UNIT_TEST(RollingIdGenerator_TimeUntilRotation)
{
  p2p::RollingIdGenerator generator;
  auto timeLeft = generator.GetTimeUntilRotation();

  // Should be positive (just initialized)
  TEST_GREATER(timeLeft.count(), 0, ());
}

UNIT_TEST(RollingIdGenerator_CustomInterval)
{
  p2p::RollingIdGenerator generator;
  generator.SetRotationInterval(std::chrono::minutes(5), std::chrono::minutes(1));

  TEST_EQUAL(generator.GetRotationInterval(), std::chrono::minutes(5), ());
}

UNIT_TEST(RollingIdGenerator_MultipleIds)
{
  // Generate multiple IDs and verify they're different
  p2p::RollingIdGenerator gen1, gen2, gen3;

  auto id1 = gen1.GetCurrentId();
  auto id2 = gen2.GetCurrentId();
  auto id3 = gen3.GetCurrentId();

  // High probability they're all different
  TEST_NOT_EQUAL(id1, id2, ());
  TEST_NOT_EQUAL(id2, id3, ());
  TEST_NOT_EQUAL(id1, id3, ());
}

UNIT_TEST(RollingIdGenerator_DebugPrint)
{
  p2p::RollingId id{};
  id[0] = 0x12;
  id[1] = 0x34;

  std::string debug = DebugPrint(id);
  TEST(debug.find("RollingId(") != std::string::npos, ());
  TEST(debug.find("1234") != std::string::npos, ());
}

UNIT_TEST(RollingIdGenerator_Equality)
{
  p2p::RollingId id1{}, id2{};
  id1[0] = 0xAA;
  id2[0] = 0xAA;

  TEST(id1 == id2, ("Equal IDs should compare equal"));

  id2[0] = 0xBB;
  TEST(id1 != id2, ("Different IDs should compare not equal"));
}
}  // namespace rolling_id_generator_test

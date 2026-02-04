#include "testing/testing.hpp"

#include "p2p/privacy_manager.hpp"

#include <cmath>

namespace privacy_manager_test
{
using namespace p2p;

UNIT_TEST(PrivacyManager_DefaultConsent)
{
  PrivacyManager manager;
  TEST_EQUAL(manager.GetConsentLevel(), ConsentLevel::Off, ());
  TEST(!manager.CanShare(), ());
  TEST(!manager.CanReceive(), ());
}

UNIT_TEST(PrivacyManager_InitialConsent)
{
  PrivacyManager manager(ConsentLevel::Contribute);
  TEST_EQUAL(manager.GetConsentLevel(), ConsentLevel::Contribute, ());
  TEST(manager.CanShare(), ());
  TEST(manager.CanReceive(), ());
}

UNIT_TEST(PrivacyManager_SetConsent)
{
  PrivacyManager manager;

  manager.SetConsentLevel(ConsentLevel::ViewOnly);
  TEST_EQUAL(manager.GetConsentLevel(), ConsentLevel::ViewOnly, ());
  TEST(!manager.CanShare(), ());
  TEST(manager.CanReceive(), ());

  manager.SetConsentLevel(ConsentLevel::Contribute);
  TEST_EQUAL(manager.GetConsentLevel(), ConsentLevel::Contribute, ());
  TEST(manager.CanShare(), ());
  TEST(manager.CanReceive(), ());

  manager.SetConsentLevel(ConsentLevel::Off);
  TEST_EQUAL(manager.GetConsentLevel(), ConsentLevel::Off, ());
  TEST(!manager.CanShare(), ());
  TEST(!manager.CanReceive(), ());
}

UNIT_TEST(PrivacyManager_ConsentCallback)
{
  PrivacyManager manager;
  ConsentLevel notifiedLevel = ConsentLevel::Off;
  int callCount = 0;

  manager.RegisterConsentCallback([&notifiedLevel, &callCount](ConsentLevel level) {
    notifiedLevel = level;
    ++callCount;
  });

  manager.SetConsentLevel(ConsentLevel::Contribute);
  TEST_EQUAL(notifiedLevel, ConsentLevel::Contribute, ());
  TEST_EQUAL(callCount, 1, ());

  // Setting same level should not trigger callback
  manager.SetConsentLevel(ConsentLevel::Contribute);
  TEST_EQUAL(callCount, 1, ());

  manager.SetConsentLevel(ConsentLevel::ViewOnly);
  TEST_EQUAL(notifiedLevel, ConsentLevel::ViewOnly, ());
  TEST_EQUAL(callCount, 2, ());
}

UNIT_TEST(PrivacyManager_LDP_AddsNoise)
{
  PrivacyManager manager(ConsentLevel::Contribute);

  double const original = 50.0;
  double const sensitivity = 10.0;

  // LDP should add noise - run multiple times to verify variance
  bool hasVariance = false;
  double firstResult = manager.ApplyLDP(original, sensitivity);

  for (int i = 0; i < 20; ++i)
  {
    double result = manager.ApplyLDP(original, sensitivity);
    if (std::abs(result - firstResult) > 0.001)
    {
      hasVariance = true;
      break;
    }
  }

  TEST(hasVariance, ("LDP should add random noise"));
}

UNIT_TEST(PrivacyManager_LDP_Float)
{
  PrivacyManager manager(ConsentLevel::Contribute);

  float const original = 25.0f;
  float const sensitivity = 5.0f;

  // Just verify it runs without error and returns a float
  float result = manager.ApplyLDPFloat(original, sensitivity);
  TEST(std::isfinite(result), ());
}

UNIT_TEST(PrivacyManager_KAnonymity_BelowThreshold)
{
  PrivacyManager manager;
  uint64_t const cellId = 0x8928308280fffff;  // Example H3 cell

  // Initially no segments
  TEST_EQUAL(manager.GetUniqueSegmentCount(cellId), 0u, ());
  TEST(!manager.CheckKAnonymity(cellId, 0), ());

  // Add segments below threshold (k=5)
  for (uint32_t i = 0; i < 4; ++i)
    manager.RecordObservation(cellId, i);

  TEST_EQUAL(manager.GetUniqueSegmentCount(cellId), 4u, ());
  TEST(!manager.CheckKAnonymity(cellId, 4), ());
}

UNIT_TEST(PrivacyManager_KAnonymity_AtThreshold)
{
  PrivacyManager manager;
  uint64_t const cellId = 0x8928308280fffff;

  // Add exactly k segments
  for (uint32_t i = 0; i < 5; ++i)
    manager.RecordObservation(cellId, i);

  TEST_EQUAL(manager.GetUniqueSegmentCount(cellId), 5u, ());
  TEST(manager.CheckKAnonymity(cellId, 5), ());
}

UNIT_TEST(PrivacyManager_KAnonymity_AboveThreshold)
{
  PrivacyManager manager;
  uint64_t const cellId = 0x8928308280fffff;

  // Add more than k segments
  for (uint32_t i = 0; i < 10; ++i)
    manager.RecordObservation(cellId, i);

  TEST_EQUAL(manager.GetUniqueSegmentCount(cellId), 10u, ());
  TEST(manager.CheckKAnonymity(cellId, 10), ());
}

UNIT_TEST(PrivacyManager_KAnonymity_DuplicateSegments)
{
  PrivacyManager manager;
  uint64_t const cellId = 0x8928308280fffff;

  // Add same segment multiple times - should count as one
  manager.RecordObservation(cellId, 1);
  manager.RecordObservation(cellId, 1);
  manager.RecordObservation(cellId, 1);

  TEST_EQUAL(manager.GetUniqueSegmentCount(cellId), 1u, ());
}

UNIT_TEST(PrivacyManager_KAnonymity_MultipleCells)
{
  PrivacyManager manager;
  uint64_t const cell1 = 0x8928308280fffff;
  uint64_t const cell2 = 0x8928308281fffff;

  // Add segments to different cells
  for (uint32_t i = 0; i < 3; ++i)
    manager.RecordObservation(cell1, i);

  for (uint32_t i = 0; i < 7; ++i)
    manager.RecordObservation(cell2, i);

  TEST_EQUAL(manager.GetUniqueSegmentCount(cell1), 3u, ());
  TEST_EQUAL(manager.GetUniqueSegmentCount(cell2), 7u, ());

  TEST(!manager.CheckKAnonymity(cell1, 3), ());
  TEST(manager.CheckKAnonymity(cell2, 7), ());
}

UNIT_TEST(PrivacyManager_ClearObservations)
{
  PrivacyManager manager;
  uint64_t const cellId = 0x8928308280fffff;

  manager.RecordObservation(cellId, 1);
  manager.RecordObservation(cellId, 2);
  TEST_EQUAL(manager.GetUniqueSegmentCount(cellId), 2u, ());

  manager.ClearObservations();
  TEST_EQUAL(manager.GetUniqueSegmentCount(cellId), 0u, ());
}

UNIT_TEST(PrivacyManager_DebugPrint)
{
  PrivacyManager manager(ConsentLevel::ViewOnly);
  std::string const dbg = DebugPrint(manager);

  TEST(dbg.find("PrivacyManager") != std::string::npos, ());
  TEST(dbg.find("ViewOnly") != std::string::npos, ());
  TEST(dbg.find("epsilon") != std::string::npos, ());
}

UNIT_TEST(PrivacyManager_ConfigValues)
{
  PrivacyManager manager;

  TEST_EQUAL(manager.GetEpsilon(), PrivacyConfig::kLDPEpsilon, ());
  TEST_EQUAL(manager.GetKThreshold(), PrivacyConfig::kMinSegmentsForSharing, ());
}
}  // namespace privacy_manager_test

#include "testing/testing.hpp"

#include "p2p/privacy_settings.hpp"

namespace privacy_settings_test
{
UNIT_TEST(ConsentLevel_DebugPrint)
{
  TEST_EQUAL(p2p::DebugPrint(p2p::ConsentLevel::Off), "Off", ());
  TEST_EQUAL(p2p::DebugPrint(p2p::ConsentLevel::ViewOnly), "ViewOnly", ());
  TEST_EQUAL(p2p::DebugPrint(p2p::ConsentLevel::Contribute), "Contribute", ());
}

UNIT_TEST(PrivacyConfig_Constants)
{
  // Verify privacy constants are reasonable
  TEST_GREATER(p2p::PrivacyConfig::kLDPEpsilon, 0.0, ());
  TEST_LESS_OR_EQUAL(p2p::PrivacyConfig::kLDPEpsilon, 10.0, ());

  TEST_GREATER(p2p::PrivacyConfig::kMinSegmentsForSharing, 0, ());

  TEST_GREATER(p2p::PrivacyConfig::kRollingIdIntervalMinutes, 0, ());
  TEST_LESS(p2p::PrivacyConfig::kRollingIdJitterMinutes,
            p2p::PrivacyConfig::kRollingIdIntervalMinutes, ());

  TEST_GREATER(p2p::PrivacyConfig::kSharedDataTTLMinutes, 0, ());
  TEST_GREATER(p2p::PrivacyConfig::kMaxHopCount, 0, ());

  // H3 resolution 9 gives ~175m hexagons
  TEST_EQUAL(p2p::PrivacyConfig::kH3Resolution, 9, ());
}
}  // namespace privacy_settings_test

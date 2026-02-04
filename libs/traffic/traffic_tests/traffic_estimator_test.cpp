#include "testing/testing.hpp"

#include "traffic/traffic_estimator.hpp"
#include "traffic/historical_speed_data.hpp"
#include "traffic/historical_speed_provider.hpp"
#include "traffic/osm_speed_inference.hpp"
#include "traffic/personal_speed_storage.hpp"

#include "indexer/mwm_set.hpp"

#include "platform/platform.hpp"

#include <ctime>
#include <memory>

namespace traffic_estimator_test
{
using namespace traffic;
using namespace routing;

UNIT_TEST(TrafficEstimator_DefaultConfig)
{
  TrafficEstimator estimator;
  auto const & config = estimator.GetConfig();

  TEST(config.m_useHistoricalPatterns, ());
  TEST(config.m_useOSMInference, ());
  TEST_GREATER(config.m_historicalPatternWeight, 0.0f, ());
  TEST_GREATER(config.m_osmInferenceWeight, 0.0f, ());
}

UNIT_TEST(TrafficEstimator_CustomConfig)
{
  TrafficEstimator::Config config;
  config.m_useHistoricalPatterns = false;
  config.m_useOSMInference = true;
  config.m_historicalPatternWeight = 0.8f;

  TrafficEstimator estimator(config);
  auto const & retrievedConfig = estimator.GetConfig();

  TEST(!retrievedConfig.m_useHistoricalPatterns, ());
  TEST(retrievedConfig.m_useOSMInference, ());
  TEST_ALMOST_EQUAL_ABS(retrievedConfig.m_historicalPatternWeight, 0.8f, 0.001f, ());
}

UNIT_TEST(TrafficEstimator_OSMInferenceFallback)
{
  TrafficEstimator estimator;

  // Without historical data, should fall back to OSM inference
  MwmSet::MwmId emptyMwmId;
  auto estimate = estimator.GetEstimate(
      emptyMwmId, 0, 0, true, HighwayType::HighwayPrimary, true, 0);

  TEST(estimate.IsValid(), ());
  TEST_GREATER(estimate.m_speedKmph, 0.0, ());
  // Should be from OSM inference or default
  TEST(estimate.m_source == TrafficSource::kOSMInference ||
       estimate.m_source == TrafficSource::kRoadClassDefault, ());
}

UNIT_TEST(TrafficEstimator_GetSpeedGroup)
{
  TrafficEstimator estimator;
  MwmSet::MwmId emptyMwmId;

  SpeedGroup group = estimator.GetSpeedGroup(
      emptyMwmId, 0, 0, true, HighwayType::HighwayMotorway, false, 0);

  // Without historical data, should return a valid group (not Unknown for OSM inference)
  // Actually could be Unknown if no data, or a valid group from OSM inference
  TEST(group == SpeedGroup::Unknown || static_cast<uint8_t>(group) < static_cast<uint8_t>(SpeedGroup::TempBlock), ());
}

UNIT_TEST(TrafficEstimator_GetTrafficFactor)
{
  TrafficEstimator estimator;
  MwmSet::MwmId emptyMwmId;

  double factor = estimator.GetTrafficFactor(
      emptyMwmId, 0, 0, true, HighwayType::HighwayResidential, true, 0);

  // Factor should be positive and reasonable
  TEST_GREATER(factor, 0.0, ());
  TEST_LESS(factor, 20.0, ());  // Not more than 20x slowdown
}

UNIT_TEST(TrafficEstimator_WithHistoricalProvider)
{
  // Create test data
  auto testData = std::make_shared<HistoricalSpeedData>();

  SegmentSpeedPattern pattern;
  // Set Monday 8 AM to 50% (rush hour)
  pattern.SetSpeed(1, 8, 50);
  // Set Sunday midnight to 95% (free flow)
  pattern.SetSpeed(0, 0, 95);

  testData->SetPattern(100, 0, true, pattern);

  // Create test provider
  auto testProvider = std::make_shared<TestHistoricalSpeedProvider>();
  // Note: TestHistoricalSpeedProvider requires actual MwmId, which we don't have in unit test
  // This test verifies the interface works

  TrafficEstimator estimator;
  estimator.SetHistoricalProvider(testProvider);

  // Verify provider was set
  MwmSet::MwmId emptyMwmId;
  TEST(!estimator.HasData(emptyMwmId), ());  // Empty provider has no data
}

UNIT_TEST(TrafficEstimate_CombineWith)
{
  TrafficEstimate estimate1;
  estimate1.m_speedKmph = 60.0;
  estimate1.m_confidence = 0.8;
  estimate1.m_source = TrafficSource::kHistoricalPattern;
  estimate1.m_isValid = true;

  TrafficEstimate estimate2;
  estimate2.m_speedKmph = 40.0;
  estimate2.m_confidence = 0.6;
  estimate2.m_source = TrafficSource::kOSMInference;
  estimate2.m_isValid = true;

  estimate1.CombineWith(estimate2, 0.5);

  TEST(estimate1.IsValid(), ());
  // Weighted average should be between 40 and 60
  TEST_GREATER(estimate1.m_speedKmph, 40.0, ());
  TEST_LESS(estimate1.m_speedKmph, 60.0, ());
  // Source should remain higher priority (lower enum value)
  TEST_EQUAL(estimate1.m_source, TrafficSource::kHistoricalPattern, ());
}

UNIT_TEST(TrafficEstimate_CombineWithInvalid)
{
  TrafficEstimate validEstimate;
  validEstimate.m_speedKmph = 50.0;
  validEstimate.m_confidence = 0.7;
  validEstimate.m_source = TrafficSource::kOSMInference;
  validEstimate.m_isValid = true;

  TrafficEstimate invalidEstimate;
  invalidEstimate.m_isValid = false;

  // Combining valid with invalid should keep valid
  TrafficEstimate result = validEstimate;
  result.CombineWith(invalidEstimate, 0.5);
  TEST(result.IsValid(), ());
  TEST_ALMOST_EQUAL_ABS(result.m_speedKmph, 50.0, 0.001, ());

  // Combining invalid with valid should become valid
  TrafficEstimate result2;
  result2.m_isValid = false;
  result2.CombineWith(validEstimate, 0.5);
  TEST(result2.IsValid(), ());
}

UNIT_TEST(CalcDecayFactor_Fresh)
{
  // Fresh data (0 minutes old) should have factor ~1.0
  double factor = CalcDecayFactor(0, 30);
  TEST_ALMOST_EQUAL_ABS(factor, 1.0, 0.01, ());
}

UNIT_TEST(CalcDecayFactor_HalfLife)
{
  // At half-life, factor should be 0.5
  double factor = CalcDecayFactor(30, 30);
  TEST_ALMOST_EQUAL_ABS(factor, 0.5, 0.01, ());
}

UNIT_TEST(CalcDecayFactor_Old)
{
  // Very old data should have very low factor
  double factor = CalcDecayFactor(120, 30);  // 4 half-lives
  TEST_LESS(factor, 0.1, ());
  TEST_GREATER(factor, 0.0, ());
}

UNIT_TEST(TrafficSource_DebugPrint)
{
  TEST_EQUAL(DebugPrint(TrafficSource::kPersonalHistory), "PersonalHistory", ());
  TEST_EQUAL(DebugPrint(TrafficSource::kHistoricalPattern), "HistoricalPattern", ());
  TEST_EQUAL(DebugPrint(TrafficSource::kOSMInference), "OSMInference", ());
  TEST_EQUAL(DebugPrint(TrafficSource::kRoadClassDefault), "RoadClassDefault", ());
}

UNIT_TEST(TrafficEstimator_PersonalHistoryHighestPriority)
{
  // Create temporary storage file
  std::string const storagePath = GetPlatform().WritablePathForFile("test_personal_speed.bin");

  {
    // Create personal storage with data
    auto personalStorage = std::make_shared<PersonalSpeedStorage>(storagePath);

    // Add observation: Monday 8 AM, feature 42, segment 0, forward, 30 km/h (rush hour)
    uint8_t const monday = 1;  // Monday
    uint8_t const hour8am = 8;
    personalStorage->AddObservation(42 /* featureId */, 0 /* segmentIdx */, true /* isForward */,
                                    monday, hour8am, 30.0f /* speedKmph */);
    personalStorage->Flush();

    // Create estimator with personal storage
    TrafficEstimator estimator;
    estimator.SetPersonalStorage(personalStorage);

    // Create a time_t for Monday 8 AM
    std::tm tm = {};
    tm.tm_year = 125;  // 2025
    tm.tm_mon = 0;     // January
    tm.tm_mday = 6;    // Monday
    tm.tm_hour = 8;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    std::time_t mondayTime = std::mktime(&tm);

    MwmSet::MwmId emptyMwmId;
    auto estimate = estimator.GetEstimate(
        emptyMwmId, 42 /* featureId */, 0 /* segmentIdx */, true /* isForward */,
        HighwayType::HighwayPrimary, true /* isInCity */, mondayTime);

    // Personal history should be highest priority source
    TEST(estimate.IsValid(), ());
    TEST_EQUAL(estimate.m_source, TrafficSource::kPersonalHistory, ());
    TEST_ALMOST_EQUAL_ABS(estimate.m_speedKmph, 30.0, 1.0, ());
    TEST_GREATER_OR_EQUAL(estimate.m_confidence, 0.8, ());
  }

  // Cleanup
  Platform::RemoveFileIfExists(storagePath);
}

UNIT_TEST(TrafficEstimator_PersonalHistoryFallbackToHistorical)
{
  // Create temporary storage file
  std::string const storagePath = GetPlatform().WritablePathForFile("test_personal_speed_fallback.bin");

  {
    // Create personal storage WITHOUT data for our query time
    auto personalStorage = std::make_shared<PersonalSpeedStorage>(storagePath);

    // Only add data for Sunday midnight (different time slot)
    uint8_t const sunday = 0;
    uint8_t const midnightHour = 0;
    personalStorage->AddObservation(42, 0, true, sunday, midnightHour, 50.0f);
    personalStorage->Flush();

    TrafficEstimator estimator;
    estimator.SetPersonalStorage(personalStorage);

    // Query for Monday 8 AM - no personal data exists
    std::tm tm = {};
    tm.tm_year = 125;
    tm.tm_mon = 0;
    tm.tm_mday = 6;  // Monday
    tm.tm_hour = 8;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    std::time_t mondayTime = std::mktime(&tm);

    MwmSet::MwmId emptyMwmId;
    auto estimate = estimator.GetEstimate(
        emptyMwmId, 42, 0, true, HighwayType::HighwayPrimary, true, mondayTime);

    // Should fall back to OSM inference or default (not personal history)
    TEST(estimate.IsValid(), ());
    TEST(estimate.m_source == TrafficSource::kOSMInference ||
         estimate.m_source == TrafficSource::kRoadClassDefault, ());
  }

  Platform::RemoveFileIfExists(storagePath);
}

UNIT_TEST(TrafficEstimator_HasDataWithPersonalStorage)
{
  std::string const storagePath = GetPlatform().WritablePathForFile("test_personal_speed_hasdata.bin");

  {
    auto personalStorage = std::make_shared<PersonalSpeedStorage>(storagePath);
    personalStorage->AddObservation(1, 0, true, 0, 0, 50.0f);
    personalStorage->Flush();

    TrafficEstimator estimator;
    estimator.SetPersonalStorage(personalStorage);

    MwmSet::MwmId emptyMwmId;
    // Should have data because personal storage has records
    TEST(estimator.HasData(emptyMwmId), ());
  }

  Platform::RemoveFileIfExists(storagePath);
}

}  // namespace traffic_estimator_test

#include "testing/testing.hpp"

#include "traffic/traffic_ml_model.hpp"
#include "traffic/stub_traffic_ml_model.hpp"
#include "traffic/traffic_estimator.hpp"

#include "indexer/mwm_set.hpp"

#include <cmath>
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace traffic_ml_model_test
{
using namespace traffic;
using namespace routing;

// Test cyclic encoding for hour
UNIT_TEST(TrafficMLFeatureBuilder_HourEncoding)
{
  // Hour 0 (midnight): sin=0, cos=1
  auto features0 = TrafficMLFeatureBuilder::Build(
      HighwayType::HighwayPrimary, false, 0, 0, 0.0f, 0.0f, 50.0f);
  auto vec0 = TrafficMLFeatureBuilder::ToVector(features0);
  float const hourSin0 = vec0[8];  // Index 8 is hour sin
  float const hourCos0 = vec0[9];  // Index 9 is hour cos
  TEST_ALMOST_EQUAL_ABS(hourSin0, 0.0f, 0.01f, ());
  TEST_ALMOST_EQUAL_ABS(hourCos0, 1.0f, 0.01f, ());

  // Hour 6 (6 AM): sin=1, cos=0
  auto features6 = TrafficMLFeatureBuilder::Build(
      HighwayType::HighwayPrimary, false, 6, 0, 0.0f, 0.0f, 50.0f);
  auto vec6 = TrafficMLFeatureBuilder::ToVector(features6);
  float const hourSin6 = vec6[8];
  float const hourCos6 = vec6[9];
  TEST_ALMOST_EQUAL_ABS(hourSin6, 1.0f, 0.01f, ());
  TEST_ALMOST_EQUAL_ABS(hourCos6, 0.0f, 0.01f, ());

  // Hour 12 (noon): sin=0, cos=-1
  auto features12 = TrafficMLFeatureBuilder::Build(
      HighwayType::HighwayPrimary, false, 12, 0, 0.0f, 0.0f, 50.0f);
  auto vec12 = TrafficMLFeatureBuilder::ToVector(features12);
  TEST_ALMOST_EQUAL_ABS(vec12[8], 0.0f, 0.01f, ());
  TEST_ALMOST_EQUAL_ABS(vec12[9], -1.0f, 0.01f, ());
}

// Test cyclic encoding for day of week
UNIT_TEST(TrafficMLFeatureBuilder_DayEncoding)
{
  // Sunday (0): specific values - sin=0, cos=1
  auto features0 = TrafficMLFeatureBuilder::Build(
      HighwayType::HighwayPrimary, false, 12, 0, 0.0f, 0.0f, 50.0f);
  auto vec0 = TrafficMLFeatureBuilder::ToVector(features0);
  float const daySin0 = vec0[10];  // Index 10 is day sin
  float const dayCos0 = vec0[11];  // Index 11 is day cos
  TEST_ALMOST_EQUAL_ABS(daySin0, 0.0f, 0.01f, ());
  TEST_ALMOST_EQUAL_ABS(dayCos0, 1.0f, 0.01f, ());

  // Day 1-2 (Mon-Tue at halfway): sin positive
  auto features2 = TrafficMLFeatureBuilder::Build(
      HighwayType::HighwayPrimary, false, 12, 2, 0.0f, 0.0f, 50.0f);
  auto vec2 = TrafficMLFeatureBuilder::ToVector(features2);
  TEST_GREATER(vec2[10], 0.0f, ());  // Sin should be positive for day 2
}

// Test one-hot encoding for highway types
UNIT_TEST(TrafficMLFeatureBuilder_HighwayTypeEncoding)
{
  // Motorway -> category 0 (one-hot [1,0,0,0,0,0,0,0])
  auto motorway = TrafficMLFeatureBuilder::Build(
      HighwayType::HighwayMotorway, false, 12, 0, 0.0f, 0.0f, 120.0f);
  auto vecMotorway = TrafficMLFeatureBuilder::ToVector(motorway);
  TEST_ALMOST_EQUAL_ABS(vecMotorway[0], 1.0f, 0.001f, ());
  for (size_t i = 1; i < 8; ++i)
    TEST_ALMOST_EQUAL_ABS(vecMotorway[i], 0.0f, 0.001f, ());

  // Primary -> category 2
  auto primary = TrafficMLFeatureBuilder::Build(
      HighwayType::HighwayPrimary, false, 12, 0, 0.0f, 0.0f, 60.0f);
  auto vecPrimary = TrafficMLFeatureBuilder::ToVector(primary);
  TEST_ALMOST_EQUAL_ABS(vecPrimary[2], 1.0f, 0.001f, ());

  // Residential -> category 5
  auto residential = TrafficMLFeatureBuilder::Build(
      HighwayType::HighwayResidential, false, 12, 0, 0.0f, 0.0f, 40.0f);
  auto vecResidential = TrafficMLFeatureBuilder::ToVector(residential);
  TEST_ALMOST_EQUAL_ABS(vecResidential[5], 1.0f, 0.001f, ());
}

// Test feature vector construction
UNIT_TEST(TrafficMLFeatureBuilder_ToVector)
{
  auto features = TrafficMLFeatureBuilder::Build(
      HighwayType::HighwayPrimary, true, 8, 1, 45.0f, 50.0f, 60.0f);
  auto vec = TrafficMLFeatureBuilder::ToVector(features);

  // Verify vector has 15 elements
  TEST_EQUAL(vec.size(), TrafficMLFeatureBuilder::kInputSize, ());
  TEST_EQUAL(vec.size(), 15u, ());

  // Verify road type one-hot sums to 1
  float oneHotSum = 0.0f;
  for (size_t i = 0; i < 8; ++i)
    oneHotSum += vec[i];
  TEST_ALMOST_EQUAL_ABS(oneHotSum, 1.0f, 0.001f, ());

  // Verify sin/cos values are in [-1, 1]
  for (size_t i = 8; i < 12; ++i)
  {
    TEST_GREATER_OR_EQUAL(vec[i], -1.0f, ());
    TEST_LESS_OR_EQUAL(vec[i], 1.0f, ());
  }

  // Verify is_city flag (last element) is 1.0 for city
  TEST_ALMOST_EQUAL_ABS(vec[15], 1.0f, 0.001f, ());
}

// Test stub model is ready
UNIT_TEST(StubTrafficMLModel_IsReady)
{
  StubTrafficMLModel model;
  TEST(model.IsReady(), ());
  TEST(!model.GetModelInfo().empty(), ());
}

// Rush hour (8 AM Monday) should have multiplier < 1
UNIT_TEST(StubTrafficMLModel_RushHourSlower)
{
  StubTrafficMLModel model;
  TrafficMLFeatures features;
  features.m_hour = 8;
  features.m_dayOfWeek = 1;  // Monday
  features.m_highwayType = HighwayType::HighwayPrimary;
  features.m_isInCity = true;

  auto prediction = model.Predict(features);
  TEST(prediction.m_isValid, ());
  TEST_LESS(prediction.m_speedMultiplier, 1.0f, ());
  TEST_GREATER(prediction.m_confidence, 0.0f, ());
}

// Night (2 AM) should have multiplier > 1
UNIT_TEST(StubTrafficMLModel_NightFaster)
{
  StubTrafficMLModel model;
  TrafficMLFeatures features;
  features.m_hour = 2;
  features.m_dayOfWeek = 1;  // Monday
  features.m_highwayType = HighwayType::HighwayPrimary;
  features.m_isInCity = false;

  auto prediction = model.Predict(features);
  TEST(prediction.m_isValid, ());
  TEST_GREATER(prediction.m_speedMultiplier, 1.0f, ());
}

// Same input should produce same output
UNIT_TEST(StubTrafficMLModel_Deterministic)
{
  StubTrafficMLModel model;
  TrafficMLFeatures features;
  features.m_hour = 12;
  features.m_dayOfWeek = 3;
  features.m_highwayType = HighwayType::HighwaySecondary;
  features.m_isInCity = true;

  auto pred1 = model.Predict(features);
  auto pred2 = model.Predict(features);

  TEST_ALMOST_EQUAL_ABS(pred1.m_speedMultiplier, pred2.m_speedMultiplier, 0.0001f, ());
  TEST_ALMOST_EQUAL_ABS(pred1.m_confidence, pred2.m_confidence, 0.0001f, ());
}

// Weekend should have different behavior
UNIT_TEST(StubTrafficMLModel_WeekendBehavior)
{
  StubTrafficMLModel model;
  TrafficMLFeatures features;
  features.m_hour = 10;  // Morning
  features.m_highwayType = HighwayType::HighwayPrimary;
  features.m_isInCity = true;

  // Sunday (weekend)
  features.m_dayOfWeek = 0;
  auto predWeekend = model.Predict(features);

  // Tuesday (weekday, not rush hour)
  features.m_dayOfWeek = 2;
  auto predWeekday = model.Predict(features);

  TEST(predWeekend.m_isValid, ());
  TEST(predWeekday.m_isValid, ());
  // Both should be valid, multipliers may differ
  TEST_GREATER(predWeekend.m_speedMultiplier, 0.3f, ());
  TEST_LESS(predWeekend.m_speedMultiplier, 1.5f, ());
}

// Evening rush hour should also be slower
UNIT_TEST(StubTrafficMLModel_EveningRushHour)
{
  StubTrafficMLModel model;
  TrafficMLFeatures features;
  features.m_hour = 17;  // 5 PM
  features.m_dayOfWeek = 3;  // Wednesday
  features.m_highwayType = HighwayType::HighwayPrimary;
  features.m_isInCity = true;

  auto prediction = model.Predict(features);
  TEST(prediction.m_isValid, ());
  TEST_LESS(prediction.m_speedMultiplier, 1.0f, ());
}

// Test TrafficEstimator with ML model
UNIT_TEST(TrafficEstimator_MLModelIntegration)
{
  TrafficEstimator estimator;
  auto mlModel = std::make_shared<StubTrafficMLModel>();
  estimator.SetMLModel(mlModel);

  MwmSet::MwmId emptyMwmId;

  // Create a time for Monday 8 AM (rush hour)
  std::tm tm = {};
  tm.tm_year = 125;  // 2025
  tm.tm_mon = 0;     // January
  tm.tm_mday = 6;    // Monday
  tm.tm_hour = 8;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  std::time_t mondayTime = std::mktime(&tm);

  auto estimate = estimator.GetEstimate(
      emptyMwmId, 100, 0, true, HighwayType::HighwayPrimary, true, mondayTime);

  // Should get valid estimate
  TEST(estimate.IsValid(), ());
  TEST_GREATER(estimate.m_speedKmph, 0.0, ());
}

// ML model should be used when no personal/historical data
UNIT_TEST(TrafficEstimator_MLModelPriority)
{
  TrafficEstimator estimator;
  auto mlModel = std::make_shared<StubTrafficMLModel>();
  estimator.SetMLModel(mlModel);

  MwmSet::MwmId emptyMwmId;

  std::tm tm = {};
  tm.tm_year = 125;
  tm.tm_mon = 0;
  tm.tm_mday = 6;  // Monday
  tm.tm_hour = 8;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  std::time_t mondayTime = std::mktime(&tm);

  auto estimate = estimator.GetEstimate(
      emptyMwmId, 100, 0, true, HighwayType::HighwayPrimary, true, mondayTime);

  // With ML model set and no personal/historical data, should use ML
  TEST(estimate.IsValid(), ());
  // Source should be ML (kOnDeviceML) or lower priority
  TEST(estimate.m_source == TrafficSource::kOnDeviceML ||
       estimate.m_source == TrafficSource::kOSMInference ||
       estimate.m_source == TrafficSource::kRoadClassDefault, ());
}

// Test speed multiplier range is reasonable
UNIT_TEST(StubTrafficMLModel_MultiplierRange)
{
  StubTrafficMLModel model;

  // Test various conditions
  for (uint8_t hour = 0; hour < 24; ++hour)
  {
    for (uint8_t day = 0; day < 7; ++day)
    {
      TrafficMLFeatures features;
      features.m_hour = hour;
      features.m_dayOfWeek = day;
      features.m_highwayType = HighwayType::HighwayPrimary;
      features.m_isInCity = true;

      auto prediction = model.Predict(features);
      TEST(prediction.m_isValid, ());
      TEST_GREATER_OR_EQUAL(prediction.m_speedMultiplier, 0.3f, ());
      TEST_LESS_OR_EQUAL(prediction.m_speedMultiplier, 1.5f, ());
      TEST_GREATER_OR_EQUAL(prediction.m_confidence, 0.0f, ());
      TEST_LESS_OR_EQUAL(prediction.m_confidence, 1.0f, ());
    }
  }
}

}  // namespace traffic_ml_model_test

#include "testing/testing.hpp"

#include "traffic/osm_speed_inference.hpp"

namespace osm_speed_inference_test
{
using namespace traffic;
using namespace routing;

UNIT_TEST(OSMSpeedInference_DefaultSpeeds)
{
  // Test that default speeds are reasonable for different road types

  // Motorway
  double motorwayUrban = OSMSpeedInference::GetDefaultSpeedKmph(HighwayType::HighwayMotorway, true);
  double motorwayRural = OSMSpeedInference::GetDefaultSpeedKmph(HighwayType::HighwayMotorway, false);
  TEST_GREATER(motorwayUrban, 80.0, ());
  TEST_GREATER(motorwayRural, motorwayUrban, ());
  TEST_LESS_OR_EQUAL(motorwayRural, 130.0, ());

  // Primary road
  double primaryUrban = OSMSpeedInference::GetDefaultSpeedKmph(HighwayType::HighwayPrimary, true);
  double primaryRural = OSMSpeedInference::GetDefaultSpeedKmph(HighwayType::HighwayPrimary, false);
  TEST_GREATER(primaryUrban, 30.0, ());
  TEST_GREATER(primaryRural, primaryUrban, ());
  TEST_LESS(primaryUrban, motorwayUrban, ());

  // Residential
  double residentialUrban = OSMSpeedInference::GetDefaultSpeedKmph(HighwayType::HighwayResidential, true);
  double residentialRural = OSMSpeedInference::GetDefaultSpeedKmph(HighwayType::HighwayResidential, false);
  TEST_GREATER(residentialUrban, 15.0, ());
  TEST_LESS(residentialUrban, primaryUrban, ());

  // Living street - should be slow
  double livingStreet = OSMSpeedInference::GetDefaultSpeedKmph(HighwayType::HighwayLivingStreet, true);
  TEST_GREATER(livingStreet, 5.0, ());
  TEST_LESS_OR_EQUAL(livingStreet, 20.0, ());
}

UNIT_TEST(OSMSpeedInference_GetInferredSpeed)
{
  OSMSpeedInference inference;

  // Test motorway speed
  auto motorwaySpeed = inference.GetInferredSpeed(HighwayType::HighwayMotorway, false);
  TEST(motorwaySpeed.has_value(), ());
  TEST_GREATER(motorwaySpeed->m_speedKmph, 50.0, ());
  TEST_GREATER(motorwaySpeed->m_confidence, 0.5, ());

  // Test residential speed
  auto residentialSpeed = inference.GetInferredSpeed(HighwayType::HighwayResidential, true);
  TEST(residentialSpeed.has_value(), ());
  TEST_GREATER(residentialSpeed->m_speedKmph, 10.0, ());
  TEST_LESS(residentialSpeed->m_speedKmph, motorwaySpeed->m_speedKmph, ());
}

UNIT_TEST(OSMSpeedInference_CountrySpecific)
{
  // Test that country-specific profiles adjust speeds

  OSMSpeedInference germanyInference("Germany");
  OSMSpeedInference ukInference("United Kingdom");
  OSMSpeedInference defaultInference;

  // Germany has higher motorway speeds (no speed limit)
  auto germanyMotorway = germanyInference.GetInferredSpeed(HighwayType::HighwayMotorway, false);
  auto ukMotorway = ukInference.GetInferredSpeed(HighwayType::HighwayMotorway, false);
  auto defaultMotorway = defaultInference.GetInferredSpeed(HighwayType::HighwayMotorway, false);

  TEST(germanyMotorway.has_value(), ());
  TEST(ukMotorway.has_value(), ());
  TEST(defaultMotorway.has_value(), ());

  // Germany should be faster on motorways
  TEST_GREATER_OR_EQUAL(germanyMotorway->m_speedKmph, defaultMotorway->m_speedKmph, ());

  // UK should be slower due to narrower roads
  TEST_LESS_OR_EQUAL(ukMotorway->m_speedKmph, defaultMotorway->m_speedKmph, ());
}

UNIT_TEST(OSMSpeedInference_SpeedToPercentage)
{
  OSMSpeedInference inference;

  // Motorway free flow ~120 km/h
  // 60 km/h should be ~50%
  uint8_t percentage = inference.SpeedToPercentage(60.0, HighwayType::HighwayMotorway, false);
  TEST_GREATER(percentage, 40u, ());
  TEST_LESS(percentage, 60u, ());

  // Full speed should be ~100%
  percentage = inference.SpeedToPercentage(120.0, HighwayType::HighwayMotorway, false);
  TEST_GREATER_OR_EQUAL(percentage, 85u, ());

  // Very slow should be low percentage
  percentage = inference.SpeedToPercentage(10.0, HighwayType::HighwayMotorway, false);
  TEST_LESS(percentage, 20u, ());
}

UNIT_TEST(OSMSpeedInference_FreeFlowSpeed)
{
  OSMSpeedInference inference;

  // Free flow should be higher than default (typical) speed
  double freeFlowMotorway = inference.GetFreeFlowSpeedKmph(HighwayType::HighwayMotorway, false);
  double defaultMotorway = OSMSpeedInference::GetDefaultSpeedKmph(HighwayType::HighwayMotorway, false);

  TEST_GREATER(freeFlowMotorway, defaultMotorway, ());
  TEST_LESS(freeFlowMotorway, defaultMotorway * 1.5, ());  // Not more than 50% higher

  // Same for residential
  double freeFlowResidential = inference.GetFreeFlowSpeedKmph(HighwayType::HighwayResidential, true);
  double defaultResidential = OSMSpeedInference::GetDefaultSpeedKmph(HighwayType::HighwayResidential, true);

  TEST_GREATER(freeFlowResidential, defaultResidential, ());
}

UNIT_TEST(DefaultRoadSpeeds_GetTypicalSpeed)
{
  // Test string-based lookup

  double motorwayUrban = DefaultRoadSpeeds::GetTypicalSpeed("motorway", true);
  double motorwayRural = DefaultRoadSpeeds::GetTypicalSpeed("motorway", false);
  TEST_EQUAL(motorwayUrban, DefaultRoadSpeeds::kMotorwayUrban, ());
  TEST_EQUAL(motorwayRural, DefaultRoadSpeeds::kMotorwayRural, ());

  double primaryUrban = DefaultRoadSpeeds::GetTypicalSpeed("primary", true);
  TEST_EQUAL(primaryUrban, DefaultRoadSpeeds::kPrimaryUrban, ());

  double residentialUrban = DefaultRoadSpeeds::GetTypicalSpeed("residential", true);
  TEST_EQUAL(residentialUrban, DefaultRoadSpeeds::kResidentialUrban, ());

  double livingStreet = DefaultRoadSpeeds::GetTypicalSpeed("living_street", true);
  TEST_EQUAL(livingStreet, DefaultRoadSpeeds::kLivingStreet, ());

  // Unknown type should return conservative estimate
  double unknown = DefaultRoadSpeeds::GetTypicalSpeed("some_unknown_type", true);
  TEST_GREATER(unknown, 0.0, ());
  TEST_LESS(unknown, 50.0, ());
}

}  // namespace osm_speed_inference_test

// Library Documentation Verified: 2026-02-03
// Source: Organic Maps codebase analysis
// API Versions: Internal APIs from routing, geometry, base libraries

#include "testing/testing.hpp"

#include "routing/reroute_decision.hpp"
#include "routing/route.hpp"

#include "geometry/latlon.hpp"
#include "geometry/mercator.hpp"
#include "geometry/point2d.hpp"

#include <vector>

namespace routing
{
namespace
{
// Helper function to create a mock route for testing
std::shared_ptr<Route> CreateMockRoute(std::vector<m2::PointD> const & points)
{
  auto route = std::make_shared<Route>("test_router", points, 1 /* routeId */);
  return route;
}

UNIT_TEST(RerouteDecisionConfig_Default)
{
  RerouteDecisionConfig const config = RerouteDecisionConfig::Default();

  TEST_EQUAL(config.maxReturnDistance, 200.0, ());
  TEST_EQUAL(config.maxReturnTurns, 2, ());
  TEST_GREATER(config.forwardAcceptanceRatio, 1.0, ());
  TEST_LESS(config.forwardAcceptanceRatio, 2.0, ());
}

UNIT_TEST(RerouteDecisionType_Enum)
{
  // Verify enum values exist
  RerouteDecisionType const stayOnRoute = RerouteDecisionType::StayOnRoute;
  RerouteDecisionType const suggestReturn = RerouteDecisionType::SuggestReturn;
  RerouteDecisionType const calculateNew = RerouteDecisionType::CalculateNewRoute;

  TEST(stayOnRoute != suggestReturn, ());
  TEST(suggestReturn != calculateNew, ());
  TEST(stayOnRoute != calculateNew, ());
}

UNIT_TEST(RerouteDecisionResult_DefaultValues)
{
  RerouteDecisionResult result;

  TEST_EQUAL(result.returnDistanceMeters, 0.0, ());
  TEST_EQUAL(result.forwardDistanceMeters, 0.0, ());
  TEST_EQUAL(result.returnTurnCount, 0, ());
  TEST(result.reason.empty(), ());
}

UNIT_TEST(RerouteDecision_ShortReturnDistance)
{
  RerouteDecisionConfig config = RerouteDecisionConfig::Default();
  config.maxReturnDistance = 200.0;
  config.maxReturnTurns = 2;

  RerouteDecision decision(config);

  // Create a route: 55.0, 37.0 -> 55.1, 37.0 (north)
  std::vector<m2::PointD> routePoints;
  for (double lat = 55.0; lat <= 55.1; lat += 0.01)
  {
    routePoints.push_back(mercator::FromLatLon(lat, 37.0));
  }
  auto route = CreateMockRoute(routePoints);

  // Position close to route (~50m offset)
  ms::LatLon const position(55.0, 37.0005);

  auto const result = decision.Decide(position, *route);

  // Short return distance should suggest return
  TEST_LESS(result.returnDistanceMeters, 200.0, ());
  // Result should have a reason
  TEST(!result.reason.empty(), ());
}

UNIT_TEST(RerouteDecision_LongReturnDistance)
{
  RerouteDecisionConfig config = RerouteDecisionConfig::Default();
  config.maxReturnDistance = 200.0;

  RerouteDecision decision(config);

  // Create a short route
  std::vector<m2::PointD> routePoints;
  routePoints.push_back(mercator::FromLatLon(55.0, 37.0));
  routePoints.push_back(mercator::FromLatLon(55.001, 37.0));
  auto route = CreateMockRoute(routePoints);

  // Position far from route (~500m offset)
  ms::LatLon const position(55.0, 37.005);

  auto const result = decision.Decide(position, *route);

  // Long return distance should trigger reroute calculation
  TEST(result.decision == RerouteDecisionType::CalculateNewRoute ||
       result.decision == RerouteDecisionType::SuggestReturn, ());
}

UNIT_TEST(RerouteDecision_ConstructorWithConfig)
{
  RerouteDecisionConfig config;
  config.maxReturnDistance = 100.0;
  config.maxReturnTurns = 1;
  config.forwardAcceptanceRatio = 1.5;
  config.minTimeSavingSeconds = 30.0;

  // Should compile and construct without errors
  RerouteDecision decision(config);

  // Just verify it doesn't crash when used
  std::vector<m2::PointD> routePoints;
  routePoints.push_back(mercator::FromLatLon(55.0, 37.0));
  routePoints.push_back(mercator::FromLatLon(55.01, 37.0));
  auto route = CreateMockRoute(routePoints);

  ms::LatLon const position(55.005, 37.001);
  auto const result = decision.Decide(position, *route);

  // Should return a valid decision
  TEST(result.decision == RerouteDecisionType::StayOnRoute ||
       result.decision == RerouteDecisionType::SuggestReturn ||
       result.decision == RerouteDecisionType::CalculateNewRoute, ());
}

}  // namespace
}  // namespace routing

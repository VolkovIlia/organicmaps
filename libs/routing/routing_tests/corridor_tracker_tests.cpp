// Library Documentation Verified: 2026-02-03
// Source: Organic Maps codebase analysis
// API Versions: Internal APIs from routing, geometry, base libraries

#include "testing/testing.hpp"

#include "routing/corridor_tracker.hpp"
#include "routing/route.hpp"

#include "geometry/latlon.hpp"
#include "geometry/mercator.hpp"
#include "geometry/point2d.hpp"

#include <chrono>
#include <thread>
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

// Helper function to create a simple north-south route line
std::shared_ptr<Route> CreateNorthSouthRoute(double startLat, double startLon, double lengthKm)
{
  std::vector<m2::PointD> points;
  // Create points along a north-south line
  double const latStep = lengthKm / 111.0;  // ~111 km per degree latitude
  for (double lat = startLat; lat <= startLat + latStep; lat += latStep / 10.0)
  {
    points.push_back(mercator::FromLatLon(lat, startLon));
  }
  return CreateMockRoute(points);
}

UNIT_TEST(CorridorTracker_InitialState)
{
  CorridorConfig const config = CorridorConfig::Default();
  CorridorTracker tracker(config);

  TEST_EQUAL(tracker.GetState(), CorridorState::OnRoute, ());
}

UNIT_TEST(CorridorTracker_Reset)
{
  CorridorTracker tracker;
  auto route = CreateNorthSouthRoute(55.0, 37.0, 1.0);

  // Move outside corridor to change state
  ms::LatLon const outsidePos(55.0, 37.001);  // ~80m east
  tracker.CheckPosition(outsidePos, 10.0, *route);

  // Reset should return to OnRoute
  tracker.Reset();
  TEST_EQUAL(tracker.GetState(), CorridorState::OnRoute, ());
}

UNIT_TEST(CorridorTracker_InsideCorridor_Residential)
{
  // Residential road: 15m base + 10m GPS = 25m corridor
  CorridorConfig config = CorridorConfig::Default();
  CorridorTracker tracker(config);

  auto route = CreateNorthSouthRoute(55.0, 37.0, 1.0);

  // Position ~20m from route (inside 25m corridor)
  // 0.0002 degrees longitude at lat 55 = ~12m
  ms::LatLon const position(55.0, 37.0002);

  auto const result = tracker.CheckPosition(position, 10.0, *route);

  TEST(result.isInsideCorridor, ());
  TEST_EQUAL(result.state, CorridorState::OnRoute, ());
  TEST_EQUAL(result.recommendation, CorridorCheckResult::Recommendation::Continue, ());
}

UNIT_TEST(CorridorTracker_OutsideCorridor_TransitionToMonitoring)
{
  CorridorConfig config = CorridorConfig::Default();
  CorridorTracker tracker(config);

  auto route = CreateNorthSouthRoute(55.0, 37.0, 1.0);

  // Position ~100m from route (outside any road class corridor)
  // 0.001 degrees longitude at lat 55 = ~63m
  ms::LatLon const position(55.0, 37.002);

  auto const result = tracker.CheckPosition(position, 10.0, *route);

  TEST(!result.isInsideCorridor, ());
  TEST_EQUAL(result.state, CorridorState::Monitoring, ());
  TEST_EQUAL(result.recommendation, CorridorCheckResult::Recommendation::Continue, ());
}

UNIT_TEST(CorridorTracker_ReturnToCorridor)
{
  CorridorTracker tracker;
  auto route = CreateNorthSouthRoute(55.0, 37.0, 1.0);

  // Go outside corridor
  ms::LatLon const outsidePos(55.0, 37.002);
  tracker.CheckPosition(outsidePos, 10.0, *route);
  TEST_EQUAL(tracker.GetState(), CorridorState::Monitoring, ());

  // Return inside corridor
  ms::LatLon const insidePos(55.0, 37.0001);
  auto const result = tracker.CheckPosition(insidePos, 10.0, *route);

  TEST_EQUAL(result.state, CorridorState::OnRoute, ());
  TEST(result.isInsideCorridor, ());
}

UNIT_TEST(CorridorTracker_TransitionToDeciding)
{
  // Use a very short threshold for testing
  CorridorConfig config = CorridorConfig::Default();
  config.offRouteTimeThreshold = 0.1;  // 100ms for testing

  CorridorTracker tracker(config);
  auto route = CreateNorthSouthRoute(55.0, 37.0, 1.0);

  // Position outside corridor
  ms::LatLon const position(55.0, 37.002);

  // First check - enters Monitoring
  auto result1 = tracker.CheckPosition(position, 10.0, *route);
  TEST_EQUAL(result1.state, CorridorState::Monitoring, ());

  // Wait for threshold
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // Second check - transitions to Deciding
  auto result2 = tracker.CheckPosition(position, 10.0, *route);
  TEST_EQUAL(result2.state, CorridorState::Deciding, ());
  TEST_EQUAL(result2.recommendation, CorridorCheckResult::Recommendation::Reroute, ());
}

UNIT_TEST(CorridorTracker_GPSAccuracyExpandsCorridor)
{
  CorridorConfig config = CorridorConfig::Default();
  CorridorTracker tracker(config);

  auto route = CreateNorthSouthRoute(55.0, 37.0, 1.0);

  // Position ~30m from route
  // 0.0005 degrees longitude at lat 55 = ~31m
  ms::LatLon const position(55.0, 37.0005);

  // With 10m GPS accuracy, residential corridor is 15 + 10 = 25m -> outside
  auto result1 = tracker.CheckPosition(position, 10.0, *route);
  TEST(!result1.isInsideCorridor, ());

  // Reset and try with 30m GPS accuracy, corridor is 15 + 30 = 45m -> inside
  tracker.Reset();
  auto result2 = tracker.CheckPosition(position, 30.0, *route);
  TEST(result2.isInsideCorridor, ());
}

UNIT_TEST(CorridorTracker_GetCorridorWidth)
{
  CorridorConfig config = CorridorConfig::Default();
  CorridorTracker tracker(config);

  auto route = CreateNorthSouthRoute(55.0, 37.0, 1.0);

  // Default road type should give reasonable width
  double const width = tracker.GetCorridorWidth(*route, 0);

  // Width should be base width + min GPS compensation (10m)
  // For unknown/default road type, expect ~30m (20m base + 10m GPS)
  TEST_GREATER(width, 20.0, ());
  TEST_LESS(width, 70.0, ());
}

UNIT_TEST(CorridorConfig_Default)
{
  CorridorConfig const config = CorridorConfig::Default();

  TEST_EQUAL(config.minGpsCompensation, 10.0, ());
  TEST_EQUAL(config.offRouteTimeThreshold, 10.0, ());
  TEST_EQUAL(config.returnDistanceThreshold, 200.0, ());
  TEST_EQUAL(config.returnTurnCountMax, 2, ());
}

}  // namespace
}  // namespace routing

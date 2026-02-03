// Library Documentation Verified: 2026-02-03
// Source: Organic Maps codebase analysis
// API Versions: mercator::, Route::GetFollowedPolyline(), Route::GetCurrentDistanceToEndMeters()

#include "routing/reroute_decision.hpp"

#include "geometry/mercator.hpp"

#include "base/logging.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace routing
{
namespace
{
// Average speed assumptions for time estimation (m/s)
double constexpr kAverageSpeedMps = 10.0;  // ~36 km/h average urban speed
}  // namespace

RerouteDecision::RerouteDecision(RerouteDecisionConfig config)
  : m_config(std::move(config))
{
}

RerouteDecisionResult RerouteDecision::Decide(
    ms::LatLon const & currentPosition,
    Route const & currentRoute)
{
  RerouteDecisionResult result;

  // Step 1: Calculate return distance to route
  double const returnDistance = CalcReturnDistance(currentPosition, currentRoute);
  result.returnDistanceMeters = returnDistance;

  // Step 2: Calculate forward distance to destination
  double const forwardDistance = CalcForwardDistance(currentPosition, currentRoute);
  result.forwardDistanceMeters = forwardDistance;

  // Step 3: Estimate return turn count (simplified: assume 1 turn for short return)
  result.returnTurnCount = (returnDistance < 100.0) ? 1 : 2;

  // Step 4: Calculate time difference
  // Time to return + remaining route time vs direct forward time
  double const remainingRouteTime = currentRoute.GetCurrentTimeToEndSec();
  double const returnTime = returnDistance / kAverageSpeedMps;
  double const returnTotalTime = returnTime + remainingRouteTime;
  double const forwardTime = forwardDistance / kAverageSpeedMps;

  result.timeDifferenceSeconds = forwardTime - returnTotalTime;

  // Step 5: Make decision based on analysis

  // Case 1: Return is short and simple
  if (returnDistance <= m_config.maxReturnDistance &&
      result.returnTurnCount <= m_config.maxReturnTurns)
  {
    // Suggest return if it saves significant time
    if (result.timeDifferenceSeconds >= m_config.minTimeSavingSeconds)
    {
      result.decision = RerouteDecisionType::SuggestReturn;
      result.reason = "Return route is short (" +
          std::to_string(static_cast<int>(returnDistance)) +
          "m) and saves " +
          std::to_string(static_cast<int>(result.timeDifferenceSeconds / 60.0)) +
          " minutes";
      return result;
    }
  }

  // Case 2: Forward is acceptable (not much longer than return + remaining)
  double const returnPlusRemaining = returnDistance + currentRoute.GetCurrentDistanceToEndMeters();
  if (returnPlusRemaining > 0)
  {
    double const ratio = forwardDistance / returnPlusRemaining;
    if (ratio <= m_config.forwardAcceptanceRatio)
    {
      result.decision = RerouteDecisionType::CalculateNewRoute;
      result.reason = "Forward route is acceptable (ratio: " +
          std::to_string(ratio).substr(0, 4) + ")";
      return result;
    }
  }

  // Case 3: Return is recommended but may be complex
  if (returnDistance <= m_config.maxReturnDistance * 2)
  {
    result.decision = RerouteDecisionType::SuggestReturn;
    result.reason = "Return is recommended (" +
        std::to_string(static_cast<int>(returnDistance)) + "m)";
    return result;
  }

  // Case 4: Default to rerouting
  result.decision = RerouteDecisionType::CalculateNewRoute;
  result.reason = "Far from route, calculating new route";
  return result;
}

double RerouteDecision::CalcReturnDistance(ms::LatLon const & position, Route const & route) const
{
  m2::PointD const posPoint = mercator::FromLatLon(position);

  auto const & poly = route.GetFollowedPolyline();
  if (poly.GetSize() < 2)
    return std::numeric_limits<double>::max();

  double minDistanceMeters = std::numeric_limits<double>::max();

  // Find closest point on route
  for (size_t i = 0; i + 1 < poly.GetSize(); ++i)
  {
    m2::PointD const & p1 = poly.GetPolyline().GetPoint(i);
    m2::PointD const & p2 = poly.GetPolyline().GetPoint(i + 1);

    // Project point onto segment
    m2::PointD const segment = p2 - p1;
    double const segmentLengthSq = segment.SquaredLength();

    if (segmentLengthSq < 1e-10)
      continue;

    double const t = std::clamp(
        m2::DotProduct(posPoint - p1, segment) / segmentLengthSq,
        0.0, 1.0);

    m2::PointD const projection = p1 + segment * t;
    double const distance = mercator::DistanceOnEarth(posPoint, projection);

    if (distance < minDistanceMeters)
      minDistanceMeters = distance;
  }

  return minDistanceMeters;
}

double RerouteDecision::CalcForwardDistance(ms::LatLon const & position, Route const & route) const
{
  // Get destination from route (last point of polyline)
  auto const & poly = route.GetFollowedPolyline();
  if (poly.GetSize() == 0)
    return std::numeric_limits<double>::max();

  m2::PointD const posPoint = mercator::FromLatLon(position);
  m2::PointD const destPoint = poly.GetPolyline().Back();

  // Simplified: direct distance to destination
  // In production, this would use actual routing to get real distance
  return mercator::DistanceOnEarth(posPoint, destPoint);
}

std::string DebugPrint(RerouteDecisionType type)
{
  switch (type)
  {
  case RerouteDecisionType::StayOnRoute: return "StayOnRoute";
  case RerouteDecisionType::SuggestReturn: return "SuggestReturn";
  case RerouteDecisionType::CalculateNewRoute: return "CalculateNewRoute";
  }
  return "Unknown";
}

}  // namespace routing

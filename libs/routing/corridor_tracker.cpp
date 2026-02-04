// Library Documentation Verified: 2026-02-03
// Source: Organic Maps codebase analysis
// API Versions: mercator::, m2::PointD, Route::GetFollowedPolyline()

#include "routing/corridor_tracker.hpp"

#include "geometry/mercator.hpp"

#include "base/logging.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace routing
{
namespace
{
// Default base corridor widths by road class (meters)
double constexpr kMotorwayWidth = 50.0;
double constexpr kTrunkWidth = 40.0;
double constexpr kPrimaryWidth = 30.0;
double constexpr kSecondaryWidth = 25.0;
double constexpr kTertiaryWidth = 20.0;
double constexpr kResidentialWidth = 15.0;
double constexpr kDefaultWidth = 20.0;
}  // namespace

CorridorConfig CorridorConfig::Default()
{
  CorridorConfig config;

  // Base widths by highway type
  config.baseWidths[HighwayType::HighwayMotorway] = kMotorwayWidth;
  config.baseWidths[HighwayType::HighwayMotorwayLink] = kMotorwayWidth;
  config.baseWidths[HighwayType::HighwayTrunk] = kTrunkWidth;
  config.baseWidths[HighwayType::HighwayTrunkLink] = kTrunkWidth;
  config.baseWidths[HighwayType::HighwayPrimary] = kPrimaryWidth;
  config.baseWidths[HighwayType::HighwayPrimaryLink] = kPrimaryWidth;
  config.baseWidths[HighwayType::HighwaySecondary] = kSecondaryWidth;
  config.baseWidths[HighwayType::HighwaySecondaryLink] = kSecondaryWidth;
  config.baseWidths[HighwayType::HighwayTertiary] = kTertiaryWidth;
  config.baseWidths[HighwayType::HighwayTertiaryLink] = kTertiaryWidth;
  config.baseWidths[HighwayType::HighwayResidential] = kResidentialWidth;
  config.baseWidths[HighwayType::HighwayLivingStreet] = kResidentialWidth;
  config.baseWidths[HighwayType::HighwayService] = kResidentialWidth;
  config.baseWidths[HighwayType::HighwayUnclassified] = kTertiaryWidth;

  config.minGpsCompensation = 10.0;
  config.offRouteTimeThreshold = 10.0;
  config.returnDistanceThreshold = 200.0;
  config.returnTurnCountMax = 2;
  config.forwardRatioThreshold = 1.2;

  return config;
}

CorridorTracker::CorridorTracker(CorridorConfig config)
  : m_config(std::move(config))
{
}

CorridorCheckResult CorridorTracker::CheckPosition(
    ms::LatLon const & position,
    double gpsAccuracy,
    Route const & route)
{
  CorridorCheckResult result;
  m_lastGpsAccuracy = gpsAccuracy;

  // Step 1: Calculate lateral distance to route
  double const lateralDistance = CalcLateralDistance(position, route);
  result.lateralDistanceMeters = lateralDistance;

  // Step 2: Get corridor width (base + GPS compensation)
  // Use default width for now (route doesn't expose highway type per segment easily)
  double const baseWidth = kDefaultWidth;
  double const gpsCompensation = std::max(gpsAccuracy, m_config.minGpsCompensation);
  double const corridorWidth = baseWidth + gpsCompensation;
  result.corridorWidthMeters = corridorWidth;

  // Step 3: Check if inside corridor
  result.isInsideCorridor = (lateralDistance <= corridorWidth);

  // Step 4: State machine transitions
  auto const now = std::chrono::steady_clock::now();

  switch (m_state)
  {
  case CorridorState::OnRoute:
    if (!result.isInsideCorridor)
    {
      TransitionTo(CorridorState::Monitoring);
      m_outsideStartTime = now;
    }
    result.recommendation = CorridorCheckResult::Recommendation::Continue;
    break;

  case CorridorState::Monitoring:
    if (result.isInsideCorridor)
    {
      TransitionTo(CorridorState::OnRoute);
      result.recommendation = CorridorCheckResult::Recommendation::Continue;
    }
    else
    {
      auto const elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
          now - m_outsideStartTime).count();
      result.timeOutsideSeconds = elapsed;

      if (elapsed >= m_config.offRouteTimeThreshold)
      {
        TransitionTo(CorridorState::Deciding);
        result.recommendation = CorridorCheckResult::Recommendation::Reroute;
      }
      else
      {
        result.recommendation = CorridorCheckResult::Recommendation::Continue;
      }
    }
    break;

  case CorridorState::Deciding:
    // Transition to appropriate state based on RerouteDecision
    // This state is transient - RerouteDecision determines next state
    result.recommendation = CorridorCheckResult::Recommendation::Reroute;
    break;

  case CorridorState::SuggestingReturn:
    if (result.isInsideCorridor)
    {
      TransitionTo(CorridorState::OnRoute);
      result.recommendation = CorridorCheckResult::Recommendation::Continue;
    }
    else
    {
      result.recommendation = CorridorCheckResult::Recommendation::SuggestReturn;
    }
    break;

  case CorridorState::Rerouting:
    // Stay in rerouting until new route is set
    result.recommendation = CorridorCheckResult::Recommendation::Reroute;
    break;
  }

  result.state = m_state;
  m_lastLateralDistance = lateralDistance;

  return result;
}

double CorridorTracker::GetCorridorWidth(Route const & route, size_t segmentIndex) const
{
  // Get base width (using default for now as route doesn't expose highway type easily)
  double const baseWidth = kDefaultWidth;

  // Add minimum GPS compensation
  return baseWidth + m_config.minGpsCompensation;
}

void CorridorTracker::Reset()
{
  m_state = CorridorState::OnRoute;
  m_lastLateralDistance = 0.0;
}

double CorridorTracker::CalcLateralDistance(ms::LatLon const & position, Route const & route) const
{
  // Convert position to mercator
  m2::PointD const posPoint = mercator::FromLatLon(position);

  auto const & poly = route.GetFollowedPolyline();
  auto const & polyline = poly.GetPolyline();
  if (polyline.GetSize() < 2)
    return std::numeric_limits<double>::max();

  double minDistanceMercator = std::numeric_limits<double>::max();

  // Check distance to each segment
  for (size_t i = 0; i + 1 < polyline.GetSize(); ++i)
  {
    m2::PointD const & p1 = polyline.GetPoint(i);
    m2::PointD const & p2 = polyline.GetPoint(i + 1);

    // Project point onto segment
    m2::PointD const segment = p2 - p1;
    double const segmentLengthSq = segment.SquaredLength();

    if (segmentLengthSq < 1e-10)
      continue;

    // Calculate projection parameter t (clamped to [0, 1])
    double const t = std::clamp(
        m2::DotProduct(posPoint - p1, segment) / segmentLengthSq,
        0.0, 1.0);

    // Calculate projection point on segment
    m2::PointD const projection = p1 + segment * t;

    // Calculate distance in mercator
    double const distance = posPoint.Length(projection);

    if (distance < minDistanceMercator)
      minDistanceMercator = distance;
  }

  // Convert from Mercator to meters
  // Use approximate conversion: at the latitude of the position
  return mercator::DistanceOnEarth(posPoint, posPoint + m2::PointD(minDistanceMercator, 0));
}

double CorridorTracker::GetBaseWidthForHighwayType(HighwayType type) const
{
  auto const it = m_config.baseWidths.find(type);
  if (it != m_config.baseWidths.end())
    return it->second;

  return kDefaultWidth;
}

void CorridorTracker::TransitionTo(CorridorState newState)
{
  LOG(LDEBUG, ("CorridorTracker: Transition", DebugPrint(m_state), "->", DebugPrint(newState)));
  m_state = newState;
}

std::string DebugPrint(CorridorState state)
{
  switch (state)
  {
  case CorridorState::OnRoute: return "OnRoute";
  case CorridorState::Monitoring: return "Monitoring";
  case CorridorState::Deciding: return "Deciding";
  case CorridorState::SuggestingReturn: return "SuggestingReturn";
  case CorridorState::Rerouting: return "Rerouting";
  }
  return "Unknown";
}

}  // namespace routing

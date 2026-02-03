#include "routing/turn_cost_model.hpp"

#include <algorithm>
#include <cmath>

namespace routing
{
namespace
{
// Maximum angle cost in seconds (for U-turn / 180 degrees).
double constexpr kMaxAngleCostSeconds = 45.0;

// Junction delays in seconds.
double constexpr kTrafficSignalsDelay = 30.0;
double constexpr kStopSignDelay = 10.0;
double constexpr kGiveWayDelay = 5.0;
double constexpr kRoundaboutDelay = 10.0;
double constexpr kUncontrolledDelay = 2.0;

// Downgrade penalty: seconds per level dropped when turning to lower class road.
double constexpr kDowngradePenaltyPerLevel = 5.0;

/// \brief Get road class rank (higher = more important road).
int GetRoadClassRank(HighwayType type)
{
  switch (type)
  {
  case HighwayType::HighwayMotorway:
  case HighwayType::HighwayMotorwayLink:
    return 7;
  case HighwayType::HighwayTrunk:
  case HighwayType::HighwayTrunkLink:
    return 6;
  case HighwayType::HighwayPrimary:
  case HighwayType::HighwayPrimaryLink:
    return 5;
  case HighwayType::HighwaySecondary:
  case HighwayType::HighwaySecondaryLink:
    return 4;
  case HighwayType::HighwayTertiary:
  case HighwayType::HighwayTertiaryLink:
    return 3;
  case HighwayType::HighwayResidential:
  case HighwayType::HighwayLivingStreet:
    return 2;
  case HighwayType::HighwayService:
  case HighwayType::HighwayUnclassified:
    return 1;
  default:
    return 0;
  }
}

/// \brief Get U-turn penalty based on road class.
/// Higher class roads have larger penalties (U-turns are more dangerous/difficult).
double GetUTurnPenaltyForRoadClass(HighwayType type)
{
  switch (type)
  {
  case HighwayType::HighwayMotorway:
  case HighwayType::HighwayMotorwayLink:
    return 180.0;  // 3 minutes - rarely safe on motorways
  case HighwayType::HighwayTrunk:
  case HighwayType::HighwayTrunkLink:
    return 150.0;  // 2.5 minutes
  case HighwayType::HighwayPrimary:
  case HighwayType::HighwayPrimaryLink:
    return 120.0;  // 2 minutes
  case HighwayType::HighwaySecondary:
  case HighwayType::HighwaySecondaryLink:
    return 90.0;   // 1.5 minutes
  case HighwayType::HighwayTertiary:
  case HighwayType::HighwayTertiaryLink:
    return 75.0;   // 1.25 minutes
  case HighwayType::HighwayResidential:
  case HighwayType::HighwayLivingStreet:
    return 60.0;   // 1 minute
  default:
    return 45.0;   // 45 seconds for minor roads
  }
}
}  // namespace

double DefaultTurnCostModel::CalcBaseAngleCost(double angleDegrees) const
{
  // Normalize angle to [0, 180].
  angleDegrees = std::clamp(angleDegrees, 0.0, 180.0);

  // Use (1 - cos(angle)) / 2 to map [0, 180] degrees to [0, 1].
  // This gives smooth transition: 0 at 0 degrees, 1 at 180 degrees.
  double const angleRad = angleDegrees * M_PI / 180.0;
  double const factor = (1.0 - std::cos(angleRad)) / 2.0;

  return factor * kMaxAngleCostSeconds;
}

double DefaultTurnCostModel::GetJunctionDelay(JunctionType junctionType) const
{
  switch (junctionType)
  {
  case JunctionType::TrafficSignals:
    return kTrafficSignalsDelay;
  case JunctionType::StopSign:
    return kStopSignDelay;
  case JunctionType::GiveWay:
    return kGiveWayDelay;
  case JunctionType::Roundabout:
    return kRoundaboutDelay;
  case JunctionType::Uncontrolled:
    return kUncontrolledDelay;
  case JunctionType::Unknown:
  default:
    return 0.0;  // Unknown junctions get no penalty
  }
}

double DefaultTurnCostModel::CalcRoadClassPenalty(HighwayType from, HighwayType to) const
{
  int const fromRank = GetRoadClassRank(from);
  int const toRank = GetRoadClassRank(to);

  // Only penalize downgrade (going from higher to lower class road).
  if (toRank < fromRank)
    return static_cast<double>(fromRank - toRank) * kDowngradePenaltyPerLevel;

  return 0.0;
}

TurnCostResult DefaultTurnCostModel::CalcTurnCost(TurnCostParams const & params) const
{
  TurnCostResult result;

  result.baseAngleCost = CalcBaseAngleCost(params.turnAngleDegrees);
  result.junctionDelay = GetJunctionDelay(params.junctionType);
  result.roadClassPenalty = CalcRoadClassPenalty(params.fromRoadClass, params.toRoadClass);

  result.totalCostSeconds =
      result.baseAngleCost + result.junctionDelay + result.roadClassPenalty;

  return result;
}

double DefaultTurnCostModel::GetUTurnPenalty(HighwayType roadClass) const
{
  return GetUTurnPenaltyForRoadClass(roadClass);
}

}  // namespace routing

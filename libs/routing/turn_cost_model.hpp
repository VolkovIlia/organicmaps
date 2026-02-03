#pragma once

#include "routing_common/vehicle_model.hpp"

#include <cstdint>
#include <memory>

namespace routing
{

/// \brief Types of junctions for turn cost calculation.
enum class JunctionType : uint8_t
{
  Unknown = 0,       // Junction type not determined
  Uncontrolled,      // Simple intersection without signs
  GiveWay,           // Give way / yield sign
  StopSign,          // Stop sign
  TrafficSignals,    // Traffic lights
  Roundabout,        // Roundabout junction
};

/// \brief Parameters for turn cost calculation.
struct TurnCostParams
{
  double turnAngleDegrees = 0.0;       // 0 = straight, 180 = U-turn
  HighwayType fromRoadClass = HighwayType::HighwayUnclassified;
  HighwayType toRoadClass = HighwayType::HighwayUnclassified;
  JunctionType junctionType = JunctionType::Unknown;
  bool hasTrafficSignals = false;
};

/// \brief Result of turn cost calculation with cost breakdown.
struct TurnCostResult
{
  double totalCostSeconds = 0.0;    // Sum of all costs
  double baseAngleCost = 0.0;       // Cost based on turn angle
  double junctionDelay = 0.0;       // Delay from junction type
  double roadClassPenalty = 0.0;    // Penalty for road class downgrade
};

/// \brief Interface for turn cost calculation models.
class ITurnCostModel
{
public:
  virtual ~ITurnCostModel() = default;

  /// \brief Calculate turn cost based on parameters.
  /// \param params Turn parameters including angle, road classes, junction type.
  /// \return TurnCostResult with breakdown of costs.
  virtual TurnCostResult CalcTurnCost(TurnCostParams const & params) const = 0;

  /// \brief Get U-turn penalty for a specific road class.
  /// \param roadClass Highway type where U-turn is performed.
  /// \return Penalty in seconds. Higher for major roads (motorway > trunk > primary > residential).
  virtual double GetUTurnPenalty(HighwayType roadClass) const = 0;
};

/// \brief Default implementation of turn cost model.
/// Uses angle-based costs, junction delays, and road class penalties.
class DefaultTurnCostModel final : public ITurnCostModel
{
public:
  TurnCostResult CalcTurnCost(TurnCostParams const & params) const override;
  double GetUTurnPenalty(HighwayType roadClass) const override;

private:
  /// \brief Calculate base angle cost using cosine function.
  /// Maps [0, 180] degrees to [0, kMaxAngleCostSeconds] seconds.
  double CalcBaseAngleCost(double angleDegrees) const;

  /// \brief Get delay for specific junction type.
  double GetJunctionDelay(JunctionType junctionType) const;

  /// \brief Calculate penalty for road class downgrade.
  /// Penalizes turning from higher class road to lower class.
  double CalcRoadClassPenalty(HighwayType from, HighwayType to) const;
};

}  // namespace routing

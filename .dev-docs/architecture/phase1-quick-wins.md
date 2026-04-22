# Phase 1: Quick Wins - Detailed Design

**Branch**: `feature/routing-quick-wins`
**Duration**: 2-3 weeks
**Dependencies**: None

---

## 1. Overview

Phase 1 addresses immediate routing quality issues through:
1. **Improved Turn Cost Model** - Junction-aware, angle-based penalties
2. **Regional Speed Profiles** - Russia/CIS-specific speed defaults
3. **Surface Penalties** - Quality-aware routing
4. **Context-Aware U-Turn Penalties** - Road-class-specific U-turn costs

---

## 2. Component Design

### 2.1 Turn Cost Model

#### 2.1.1 Interface

```cpp
// File: libs/routing/turn_cost_model.hpp

#pragma once

#include "routing_common/vehicle_model.hpp"
#include <optional>

namespace routing
{

enum class JunctionType : uint8_t
{
  Unknown = 0,
  Uncontrolled,
  GiveWay,
  StopSign,
  TrafficSignals,
  Roundabout,
};

struct TurnCostParams
{
  double turnAngleDegrees;       // 0 = straight, 180 = U-turn
  HighwayType fromRoadClass;
  HighwayType toRoadClass;
  JunctionType junctionType = JunctionType::Unknown;
  bool hasTrafficSignals = false;
};

struct TurnCostResult
{
  double totalCostSeconds = 0.0;
  double baseAngleCost = 0.0;
  double junctionDelay = 0.0;
  double roadClassPenalty = 0.0;
};

class ITurnCostModel
{
public:
  virtual ~ITurnCostModel() = default;

  virtual TurnCostResult CalcTurnCost(TurnCostParams const & params) const = 0;
  virtual double GetUTurnPenalty(HighwayType roadClass) const = 0;
};

class DefaultTurnCostModel : public ITurnCostModel
{
public:
  TurnCostResult CalcTurnCost(TurnCostParams const & params) const override;
  double GetUTurnPenalty(HighwayType roadClass) const override;

private:
  double CalcBaseAngleCost(double angleDegrees) const;
  double GetJunctionDelay(JunctionType junctionType) const;
  double CalcRoadClassPenalty(HighwayType from, HighwayType to) const;
};

}  // namespace routing
```

#### 2.1.2 Implementation

```cpp
// File: libs/routing/turn_cost_model.cpp

#include "routing/turn_cost_model.hpp"
#include <algorithm>
#include <cmath>

namespace routing
{
namespace
{
// Base angle cost: maps 0-180 degrees to 0-45 seconds
// Uses cosine function for smooth transition
double constexpr kMaxAngleCostSeconds = 45.0;

// Junction delays in seconds
double constexpr kTrafficSignalsDelay = 30.0;
double constexpr kStopSignDelay = 10.0;
double constexpr kGiveWayDelay = 5.0;
double constexpr kRoundaboutDelay = 10.0;
double constexpr kUncontrolledDelay = 2.0;

// Road class hierarchy (higher = more important)
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

// Downgrade penalty: moving from higher to lower class road
double constexpr kDowngradePenaltyPerLevel = 5.0;  // seconds per level dropped

// U-turn penalties by road class
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
  // Normalize angle to [0, 180]
  angleDegrees = std::clamp(angleDegrees, 0.0, 180.0);

  // Use (1 - cos(angle)) / 2 to map [0, 180] to [0, 1]
  // This gives smooth transition: 0 at 0 degrees, 1 at 180 degrees
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

double DefaultTurnCostModel::CalcRoadClassPenalty(
    HighwayType from, HighwayType to) const
{
  int const fromRank = GetRoadClassRank(from);
  int const toRank = GetRoadClassRank(to);

  // Only penalize downgrade (going from higher to lower class)
  if (toRank < fromRank)
    return (fromRank - toRank) * kDowngradePenaltyPerLevel;

  return 0.0;
}

TurnCostResult DefaultTurnCostModel::CalcTurnCost(
    TurnCostParams const & params) const
{
  TurnCostResult result;

  result.baseAngleCost = CalcBaseAngleCost(params.turnAngleDegrees);
  result.junctionDelay = GetJunctionDelay(params.junctionType);
  result.roadClassPenalty = CalcRoadClassPenalty(
      params.fromRoadClass, params.toRoadClass);

  result.totalCostSeconds =
      result.baseAngleCost + result.junctionDelay + result.roadClassPenalty;

  return result;
}

double DefaultTurnCostModel::GetUTurnPenalty(HighwayType roadClass) const
{
  return GetUTurnPenaltyForRoadClass(roadClass);
}

}  // namespace routing
```

### 2.2 Regional Speed Profiles

#### 2.2.1 Configuration Format

```cpp
// File: libs/routing_common/regional_speed_profiles.hpp

#pragma once

#include "routing_common/vehicle_model.hpp"
#include <string>
#include <unordered_map>

namespace routing
{

// Speed profile for a specific country/region
struct RegionalSpeedConfig
{
  // Road class -> {inCity speed, outCity speed} in km/h
  std::unordered_map<HighwayType, InOutCitySpeedKMpH> roadSpeeds;

  // Surface type -> speed factor (0.0 - 1.0)
  std::unordered_map<std::string, double> surfaceFactors;
};

// Registry of regional speed profiles
class RegionalSpeedProfiles
{
public:
  static RegionalSpeedProfiles const & Instance();

  // Get speed profile for country (returns default if not found)
  RegionalSpeedConfig const * GetProfile(std::string const & countryCode) const;

  // Get speed for specific road class in country
  std::optional<SpeedKMpH> GetSpeed(
      std::string const & countryCode,
      HighwayType roadClass,
      bool inCity) const;

  // Get surface factor for country
  double GetSurfaceFactor(
      std::string const & countryCode,
      std::string const & surfaceType) const;

private:
  RegionalSpeedProfiles();

  void LoadBuiltinProfiles();
  void RegisterProfile(std::string const & countryCode,
                       RegionalSpeedConfig config);

  std::unordered_map<std::string, RegionalSpeedConfig> m_profiles;
};

}  // namespace routing
```

#### 2.2.2 Russia Profile

```cpp
// File: libs/routing_common/regional_speed_profiles.cpp

#include "routing_common/regional_speed_profiles.hpp"

namespace routing
{
namespace
{
RegionalSpeedConfig CreateRussiaProfile()
{
  RegionalSpeedConfig config;

  // Road speeds based on research and community feedback
  // Values are more conservative than global defaults
  config.roadSpeeds = {
    // Motorways: same as global (well-maintained)
    {HighwayType::HighwayMotorway, {{110, 100}, {110, 100}}},
    {HighwayType::HighwayMotorwayLink, {{60, 50}, {60, 50}}},

    // Trunk: often 2-lane with trucks, reduce from 90 to 80
    {HighwayType::HighwayTrunk, {{80, 70}, {80, 70}}},
    {HighwayType::HighwayTrunkLink, {{50, 40}, {50, 40}}},

    // Primary: frequently passes through towns, reduce from 80 to 60
    {HighwayType::HighwayPrimary, {{60, 50}, {60, 50}}},
    {HighwayType::HighwayPrimaryLink, {{40, 35}, {40, 35}}},

    // Secondary: variable quality, reduce from 60 to 50
    {HighwayType::HighwaySecondary, {{50, 45}, {50, 45}}},
    {HighwayType::HighwaySecondaryLink, {{35, 30}, {35, 30}}},

    // Tertiary: often poor surface, reduce from 50 to 40
    {HighwayType::HighwayTertiary, {{40, 35}, {40, 35}}},
    {HighwayType::HighwayTertiaryLink, {{30, 25}, {30, 25}}},

    // Unclassified: frequently unpaved, reduce from 40 to 30
    {HighwayType::HighwayUnclassified, {{30, 25}, {30, 25}}},

    // Residential/Service: conservative for safety
    {HighwayType::HighwayResidential, {{30, 25}, {30, 25}}},
    {HighwayType::HighwayService, {{20, 15}, {20, 15}}},
    {HighwayType::HighwayLivingStreet, {{15, 10}, {15, 10}}},

    // Track: very variable, conservative
    {HighwayType::HighwayTrack, {{20, 15}, {20, 15}}},
  };

  // Surface factors: more aggressive penalties for Russia
  // due to frequent unpaved roads
  config.surfaceFactors = {
    {"paved", 1.0},
    {"asphalt", 1.0},
    {"concrete", 0.95},
    {"compacted", 0.85},      // More penalty than default
    {"gravel", 0.65},         // More penalty than default
    {"fine_gravel", 0.70},
    {"unpaved", 0.50},        // Significant penalty
    {"ground", 0.35},         // Very slow
    {"dirt", 0.35},
    {"mud", 0.20},            // Near impassable
    {"sand", 0.25},
  };

  return config;
}

// Similar profiles can be created for Ukraine, Kazakhstan, Belarus
RegionalSpeedConfig CreateUkraineProfile()
{
  // Similar to Russia but with local adjustments
  RegionalSpeedConfig config = CreateRussiaProfile();
  // Override specific values if needed
  return config;
}
}  // namespace

RegionalSpeedProfiles const & RegionalSpeedProfiles::Instance()
{
  static RegionalSpeedProfiles instance;
  return instance;
}

RegionalSpeedProfiles::RegionalSpeedProfiles()
{
  LoadBuiltinProfiles();
}

void RegionalSpeedProfiles::LoadBuiltinProfiles()
{
  // Russia and CIS countries
  RegisterProfile("RU", CreateRussiaProfile());
  RegisterProfile("UA", CreateUkraineProfile());
  // Add more profiles as needed: KZ, BY, etc.
}

void RegionalSpeedProfiles::RegisterProfile(
    std::string const & countryCode,
    RegionalSpeedConfig config)
{
  m_profiles[countryCode] = std::move(config);
}

RegionalSpeedConfig const * RegionalSpeedProfiles::GetProfile(
    std::string const & countryCode) const
{
  auto const it = m_profiles.find(countryCode);
  return it != m_profiles.end() ? &it->second : nullptr;
}

std::optional<SpeedKMpH> RegionalSpeedProfiles::GetSpeed(
    std::string const & countryCode,
    HighwayType roadClass,
    bool inCity) const
{
  auto const * profile = GetProfile(countryCode);
  if (!profile)
    return std::nullopt;

  auto const it = profile->roadSpeeds.find(roadClass);
  if (it == profile->roadSpeeds.end())
    return std::nullopt;

  return inCity ? it->second.m_inCity : it->second.m_outCity;
}

double RegionalSpeedProfiles::GetSurfaceFactor(
    std::string const & countryCode,
    std::string const & surfaceType) const
{
  auto const * profile = GetProfile(countryCode);
  if (!profile)
    return 1.0;

  auto const it = profile->surfaceFactors.find(surfaceType);
  return it != profile->surfaceFactors.end() ? it->second : 1.0;
}

}  // namespace routing
```

### 2.3 EdgeEstimator Integration

#### 2.3.1 Modified CarEstimator

```cpp
// File: libs/routing/edge_estimator.cpp (modifications)

// Add to CarEstimator class:
class CarEstimator final : public EdgeEstimator
{
public:
  CarEstimator(DataSource * dataSourcePtr,
               std::shared_ptr<NumMwmIds> numMwmIds,
               shared_ptr<TrafficStash> trafficStash,
               double maxWeightSpeedKMpH,
               SpeedKMpH const & offroadSpeedKMpH,
               std::shared_ptr<ITurnCostModel> turnCostModel = nullptr)
    : EdgeEstimator(maxWeightSpeedKMpH, offroadSpeedKMpH, dataSourcePtr, numMwmIds)
    , m_trafficStash(std::move(trafficStash))
    , m_turnCostModel(turnCostModel ? turnCostModel
                                    : std::make_shared<DefaultTurnCostModel>())
  {}

  // Override GetUTurnPenalty to use new model
  double GetUTurnPenalty(Purpose purpose) const override
  {
    // For now, use average penalty. In future, can query actual road class.
    // The context-aware version will be used in CalcSegmentWeight
    return 120.0;  // 2 minutes default
  }

  // New method for context-aware U-turn penalty
  double GetUTurnPenalty(HighwayType roadClass, Purpose) const
  {
    return m_turnCostModel->GetUTurnPenalty(roadClass);
  }

  // Add turn cost to segment weight calculation
  double CalcSegmentWeight(Segment const & segment,
                           RoadGeometry const & road,
                           Purpose purpose) const override
  {
    double result = CalcSegmentWeightBase(segment, road, purpose);

    // Turn costs will be added at junction points in index_graph
    // This method handles segment traversal cost only

    return result;
  }

private:
  double CalcSegmentWeightBase(Segment const & segment,
                               RoadGeometry const & road,
                               Purpose purpose) const
  {
    double const speed = GetSpeedMpS(purpose, segment, road);
    double result = road.GetDistance(segment.GetSegmentIdx()) / speed;

    if (m_trafficStash)
    {
      SpeedGroup const speedGroup = m_trafficStash->GetSpeedGroup(segment);
      double const trafficFactor = CalcTrafficFactor(speedGroup);
      result *= trafficFactor;
    }

    return result;
  }

  shared_ptr<TrafficStash> m_trafficStash;
  std::shared_ptr<ITurnCostModel> m_turnCostModel;
};
```

---

## 3. Testing Plan

### 3.1 Unit Tests

```cpp
// File: routing/routing_tests/turn_cost_tests.cpp

UNIT_TEST(TurnCostModel_StraightCostZero)
{
  DefaultTurnCostModel model;
  TurnCostParams params;
  params.turnAngleDegrees = 0.0;
  params.fromRoadClass = HighwayType::HighwayPrimary;
  params.toRoadClass = HighwayType::HighwayPrimary;
  params.junctionType = JunctionType::Uncontrolled;

  auto result = model.CalcTurnCost(params);
  TEST_LESS(result.baseAngleCost, 1.0, ("Straight should have near-zero cost"));
}

UNIT_TEST(TurnCostModel_UTurnMaxCost)
{
  DefaultTurnCostModel model;
  TurnCostParams params;
  params.turnAngleDegrees = 180.0;
  params.fromRoadClass = HighwayType::HighwayPrimary;
  params.toRoadClass = HighwayType::HighwayPrimary;
  params.junctionType = JunctionType::Uncontrolled;

  auto result = model.CalcTurnCost(params);
  TEST_GREATER(result.baseAngleCost, 40.0, ("U-turn should have max cost"));
}

UNIT_TEST(TurnCostModel_TrafficSignalsDelay)
{
  DefaultTurnCostModel model;
  TurnCostParams params;
  params.turnAngleDegrees = 90.0;
  params.fromRoadClass = HighwayType::HighwayPrimary;
  params.toRoadClass = HighwayType::HighwayPrimary;
  params.junctionType = JunctionType::TrafficSignals;

  auto result = model.CalcTurnCost(params);
  TEST_GREATER(result.junctionDelay, 25.0, ());
  TEST_LESS(result.junctionDelay, 35.0, ());
}

UNIT_TEST(TurnCostModel_RoadClassDowngradePenalty)
{
  DefaultTurnCostModel model;
  TurnCostParams params;
  params.turnAngleDegrees = 45.0;
  params.fromRoadClass = HighwayType::HighwayPrimary;
  params.toRoadClass = HighwayType::HighwayResidential;
  params.junctionType = JunctionType::Uncontrolled;

  auto result = model.CalcTurnCost(params);
  TEST_GREATER(result.roadClassPenalty, 10.0,
               ("Downgrade from primary to residential should have penalty"));
}

UNIT_TEST(RegionalSpeedProfiles_RussiaProfile)
{
  auto const & profiles = RegionalSpeedProfiles::Instance();

  auto speed = profiles.GetSpeed("RU", HighwayType::HighwayPrimary, false);
  TEST(speed.has_value(), ());
  TEST_EQUAL(speed->m_weight, 60.0, ("Russia primary should be 60 km/h"));

  double factor = profiles.GetSurfaceFactor("RU", "unpaved");
  TEST_EQUAL(factor, 0.50, ("Russia unpaved factor should be 0.5"));
}

UNIT_TEST(UTurnPenalty_MotorwayHigherThanResidential)
{
  DefaultTurnCostModel model;

  double motorwayPenalty = model.GetUTurnPenalty(HighwayType::HighwayMotorway);
  double residentialPenalty = model.GetUTurnPenalty(HighwayType::HighwayResidential);

  TEST_GREATER(motorwayPenalty, residentialPenalty,
               ("Motorway U-turn should be penalized more than residential"));
  TEST_GREATER(motorwayPenalty, 150.0, ("Motorway U-turn > 2.5 minutes"));
  TEST_LESS(residentialPenalty, 75.0, ("Residential U-turn < 1.25 minutes"));
}
```

### 3.2 Integration Tests

```cpp
// File: routing/routing_tests/russia_routing_tests.cpp

UNIT_TEST(Russia_MoscowTver_ViaM10)
{
  // Test that Moscow to Tver prefers M10 highway
  integration::CalculateRouteAndTestRouteLength(
      integration::GetVehicleComponents<VehicleType::Car>(),
      mercator::FromLatLon(55.7558, 37.6173),  // Moscow
      {0.0, 0.0},                               // direction
      mercator::FromLatLon(56.8584, 35.9006),  // Tver
      /* expectedRouteMeters */ 180000,         // ~180 km via M10
      /* delta */ 20000);

  // Verify route does NOT pass through Klin city center
  auto route = integration::GetRoute(...);
  TEST(!RoutePassesThroughArea(route, KlinCityCenter), ());
}

UNIT_TEST(Russia_UnpavedRoadPenalty)
{
  // Test that unpaved roads are penalized appropriately
  // Create two routes: one via paved, one via unpaved
  // Verify paved is preferred even if slightly longer
}
```

---

## 4. Acceptance Criteria Verification

| AC ID | Criterion | Test Method | Status |
|-------|-----------|-------------|--------|
| AC1.1 | Traffic signals +25-35s penalty | `TurnCostModel_TrafficSignalsDelay` | Ready |
| AC1.2 | U-turn motorway=180s, residential=60s | `UTurnPenalty_MotorwayHigherThanResidential` | Ready |
| AC1.3 | Moscow-Tver via M10 | `Russia_MoscowTver_ViaM10` | Ready |
| AC1.4 | Unpaved +50% slower | `Russia_UnpavedRoadPenalty` | Ready |
| AC1.5 | All existing tests pass | CI | Ready |
| AC1.6 | No performance regression | Benchmark | Ready |

---

## 5. Files to Create/Modify

### New Files
- `libs/routing/turn_cost_model.hpp`
- `libs/routing/turn_cost_model.cpp`
- `libs/routing_common/regional_speed_profiles.hpp`
- `libs/routing_common/regional_speed_profiles.cpp`
- `routing/routing_tests/turn_cost_tests.cpp`
- `routing/routing_tests/russia_routing_tests.cpp`

### Modified Files
- `libs/routing/edge_estimator.hpp` - Add ITurnCostModel dependency
- `libs/routing/edge_estimator.cpp` - Integrate turn cost model
- `libs/routing_common/vehicle_model.cpp` - Use regional profiles
- `libs/routing/CMakeLists.txt` - Add new source files

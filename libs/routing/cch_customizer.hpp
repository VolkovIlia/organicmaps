#pragma once

#include "routing/cch_topology.hpp"
#include "routing/edge_estimator.hpp"
#include "routing/geometry.hpp"

#include "routing_common/vehicle_model.hpp"

#include <memory>
#include <vector>

namespace routing
{

/// @brief Routing profile for CCH customization
enum class CCHProfile : uint8_t
{
  Car = 0,
  Bicycle = 1,
  Pedestrian = 2
};

/// @brief Configuration for CCH customization
struct CCHCustomizationConfig
{
  CCHProfile profile = CCHProfile::Car;
  bool avoidTolls = false;
  bool avoidHighways = false;
  bool avoidFerries = false;

  bool operator==(CCHCustomizationConfig const & other) const
  {
    return profile == other.profile &&
           avoidTolls == other.avoidTolls &&
           avoidHighways == other.avoidHighways &&
           avoidFerries == other.avoidFerries;
  }

  bool operator!=(CCHCustomizationConfig const & other) const
  {
    return !(*this == other);
  }
};

/// @brief Customizes CCH topology with profile-specific weights
class CCHCustomizer
{
public:
  explicit CCHCustomizer(CCHTopology const & topology);

  /// @brief Customize CCH for given profile
  /// @param config Profile configuration
  /// @param geometry Road geometry for weight calculation
  /// @param estimator Edge estimator for weight calculation
  /// @return True if customization succeeded
  bool Customize(CCHCustomizationConfig const & config,
                 Geometry const & geometry,
                 EdgeEstimator const & estimator);

  /// @brief Check if CCH is customized for given config
  bool IsCustomized(CCHCustomizationConfig const & config) const;

  /// @brief Check if any customization has been done
  bool IsCustomized() const { return m_isCustomized; }

  /// @brief Get weight for original edge
  double GetOriginalEdgeWeight(uint32_t edgeIdx) const;

  /// @brief Get weight for shortcut
  double GetShortcutWeight(uint32_t shortcutIdx) const;

  /// @brief Get current profile config
  CCHCustomizationConfig const & GetCurrentConfig() const { return m_currentConfig; }

  /// @brief Reset customization state
  void Reset();

private:
  /// @brief Calculate weight for a single original edge
  double CalcOriginalEdgeWeight(CCHOriginalEdge const & edge,
                                Geometry const & geometry,
                                EdgeEstimator const & estimator) const;

  /// @brief Bottom-up weight propagation through contraction levels
  void PropagateWeights();

  /// @brief Find weight for path from->to through contracted graph
  double FindPathWeight(uint32_t fromNode, uint32_t toNode) const;

private:
  CCHTopology const & m_topology;
  CCHCustomizationConfig m_currentConfig;
  bool m_isCustomized = false;

  // Weights indexed by edge/shortcut index
  std::vector<double> m_originalEdgeWeights;
  std::vector<double> m_shortcutWeights;
};

}  // namespace routing

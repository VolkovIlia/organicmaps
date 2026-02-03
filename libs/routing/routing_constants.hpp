// Library Documentation Verified: 2026-02-04
// Source: Internal Organic Maps APIs
// API Version: CCH v1 (kCCHVersion = 1)
#pragma once

#include <cstdint>
#include <limits>

namespace routing
{

/// @brief Represents infinity for distance/weight calculations.
constexpr double kInfinity = std::numeric_limits<double>::max();

/// @brief Small epsilon for floating-point comparisons.
constexpr double kEpsilon = 1e-9;

/// @brief Invalid node identifier.
constexpr uint32_t kInvalidNode = UINT32_MAX;

/// @brief Default edge weight when no specific weight is available.
constexpr double kDefaultEdgeWeight = 1.0;

}  // namespace routing

#pragma once

#include <string>

namespace traffic
{
/// \brief Represents traffic deviation from historical baseline.
enum class TrafficDeviation
{
  Faster,   // >15% faster than historical
  Normal,   // Within +/-15% of historical
  Slower    // >15% slower than historical
};

/// \brief Threshold for deviation detection (15%).
constexpr double kDeviationThreshold = 0.15;

/// \brief Returns traffic deviation from historical baseline.
/// \param currentSpeed Current or predicted speed in km/h.
/// \param historicalSpeed Historical average speed for this segment/time.
/// \return TrafficDeviation enum value.
TrafficDeviation GetDeviation(double currentSpeed, double historicalSpeed);

/// \brief Returns localized string key for deviation.
/// \param deviation Traffic deviation to get string key for.
/// \return "faster_than_usual", "slower_than_usual", or empty string for normal.
std::string GetDeviationStringKey(TrafficDeviation deviation);

/// \brief Debug print support.
std::string DebugPrint(TrafficDeviation deviation);

}  // namespace traffic

#include "traffic/traffic_deviation.hpp"

namespace traffic
{
TrafficDeviation GetDeviation(double currentSpeed, double historicalSpeed)
{
  if (historicalSpeed <= 0.0)
    return TrafficDeviation::Normal;

  double const ratio = currentSpeed / historicalSpeed;

  if (ratio > 1.0 + kDeviationThreshold)
    return TrafficDeviation::Faster;
  if (ratio < 1.0 - kDeviationThreshold)
    return TrafficDeviation::Slower;
  return TrafficDeviation::Normal;
}

std::string GetDeviationStringKey(TrafficDeviation deviation)
{
  switch (deviation)
  {
  case TrafficDeviation::Faster: return "faster_than_usual";
  case TrafficDeviation::Slower: return "slower_than_usual";
  case TrafficDeviation::Normal: return "";
  }
  return "";
}

std::string DebugPrint(TrafficDeviation deviation)
{
  switch (deviation)
  {
  case TrafficDeviation::Faster: return "Faster";
  case TrafficDeviation::Normal: return "Normal";
  case TrafficDeviation::Slower: return "Slower";
  }
  return "Unknown";
}

}  // namespace traffic

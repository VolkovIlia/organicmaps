#include "p2p/h3_aggregator.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace p2p
{
namespace
{
// Approximate cell sizes by resolution (in degrees)
// Based on H3 hexagon edge lengths, approximated for square grid.
// Resolution 9 targets ~175m cells.
constexpr double kCellSizesDegrees[] = {
    4.0,       // 0: ~450km
    1.5,       // 1: ~170km
    0.5,       // 2: ~55km
    0.17,      // 3: ~18km
    0.06,      // 4: ~6km
    0.02,      // 5: ~2km
    0.007,     // 6: ~700m
    0.0025,    // 7: ~250m
    0.0009,    // 8: ~90m
    0.00157,   // 9: ~175m (primary resolution for traffic)
    0.00055,   // 10: ~60m
    0.0002,    // 11: ~22m
    0.00007,   // 12: ~8m
    0.000025,  // 13: ~3m
    0.000009,  // 14: ~1m
    0.000003   // 15: ~0.3m
};

constexpr int kMaxResolution = 15;
constexpr int kMinResolution = 0;

double GetCellSizeDegrees(int resolution)
{
  resolution = std::clamp(resolution, kMinResolution, kMaxResolution);
  return kCellSizesDegrees[resolution];
}

double NormalizeLongitude(double lon)
{
  while (lon > 180.0)
    lon -= 360.0;
  while (lon < -180.0)
    lon += 360.0;
  return lon;
}

double ClampLatitude(double lat)
{
  return std::clamp(lat, -90.0, 90.0);
}
}  // namespace

uint64_t H3Aggregator::LatLonToCell(ms::LatLon const & latLon)
{
  return LatLonToCell(latLon, kDefaultResolution);
}

uint64_t H3Aggregator::LatLonToCell(ms::LatLon const & latLon, int resolution)
{
  return EncodeCell(latLon.m_lat, latLon.m_lon, resolution);
}

ms::LatLon H3Aggregator::CellToLatLon(uint64_t cellId)
{
  double lat = 0.0;
  double lon = 0.0;
  int resolution = 0;
  DecodeCell(cellId, lat, lon, resolution);
  return ms::LatLon(lat, lon);
}

std::vector<uint64_t> H3Aggregator::GetNeighbors(uint64_t cellId)
{
  double lat = 0.0;
  double lon = 0.0;
  int resolution = 0;
  DecodeCell(cellId, lat, lon, resolution);

  double const cellSize = GetCellSizeDegrees(resolution);
  std::vector<uint64_t> neighbors;
  neighbors.reserve(8);

  // 8-connected neighbors (grid approximation)
  for (int dy = -1; dy <= 1; ++dy)
  {
    for (int dx = -1; dx <= 1; ++dx)
    {
      if (dx == 0 && dy == 0)
        continue;
      double const nLat = lat + dy * cellSize;
      double const nLon = lon + dx * cellSize;
      neighbors.push_back(EncodeCell(nLat, nLon, resolution));
    }
  }
  return neighbors;
}

double H3Aggregator::GetCellEdgeMeters(int resolution)
{
  // Earth circumference at equator ~ 40075km
  // 1 degree latitude ~ 111km
  constexpr double kMetersPerDegree = 111000.0;
  return GetCellSizeDegrees(resolution) * kMetersPerDegree;
}

bool H3Aggregator::AreNeighbors(uint64_t cellA, uint64_t cellB)
{
  if (cellA == cellB)
    return false;

  auto const neighbors = GetNeighbors(cellA);
  for (uint64_t n : neighbors)
  {
    if (n == cellB)
      return true;
  }
  return false;
}

int H3Aggregator::GetResolution(uint64_t cellId)
{
  // Resolution stored in bits 59-62 (4 bits)
  return static_cast<int>((cellId >> 59) & 0xF);
}

uint64_t H3Aggregator::EncodeCell(double lat, double lon, int resolution)
{
  // Normalize coordinates
  lon = NormalizeLongitude(lon);
  lat = ClampLatitude(lat);
  resolution = std::clamp(resolution, kMinResolution, kMaxResolution);

  double const cellSize = GetCellSizeDegrees(resolution);

  // Convert to grid coordinates
  // Latitude range: [-90, 90] -> [0, 180] -> divide by cell size
  // Longitude range: [-180, 180] -> [0, 360] -> divide by cell size
  auto const latIdx = static_cast<uint32_t>(std::floor((lat + 90.0) / cellSize));
  auto const lonIdx = static_cast<uint32_t>(std::floor((lon + 180.0) / cellSize));

  // Pack into 64-bit index:
  // Bits 0-28: latitude index (29 bits)
  // Bits 29-58: longitude index (30 bits)
  // Bits 59-62: resolution (4 bits)
  // Bit 63: reserved (0)
  uint64_t cellId = 0;
  cellId |= static_cast<uint64_t>(latIdx & 0x1FFFFFFF);
  cellId |= static_cast<uint64_t>(lonIdx & 0x3FFFFFFF) << 29;
  cellId |= static_cast<uint64_t>(resolution & 0xF) << 59;

  return cellId;
}

void H3Aggregator::DecodeCell(uint64_t cellId, double & lat, double & lon, int & resolution)
{
  resolution = static_cast<int>((cellId >> 59) & 0xF);
  auto const latIdx = static_cast<uint32_t>(cellId & 0x1FFFFFFF);
  auto const lonIdx = static_cast<uint32_t>((cellId >> 29) & 0x3FFFFFFF);

  double const cellSize = GetCellSizeDegrees(resolution);

  // Convert back to coordinates (cell center)
  lat = (static_cast<double>(latIdx) + 0.5) * cellSize - 90.0;
  lon = (static_cast<double>(lonIdx) + 0.5) * cellSize - 180.0;
}

std::string DebugPrint(uint64_t cellId)
{
  std::ostringstream oss;
  oss << "H3Cell(0x" << std::hex << std::setfill('0') << std::setw(16) << cellId
      << ", res=" << std::dec << H3Aggregator::GetResolution(cellId) << ")";
  return oss.str();
}
}  // namespace p2p

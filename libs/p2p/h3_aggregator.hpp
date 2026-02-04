#pragma once

#include "p2p/privacy_settings.hpp"

#include "geometry/latlon.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace p2p
{
/// \brief Lightweight spatial indexing for traffic data aggregation.
/// Uses grid-based approximation of H3 resolution 9 (~175m cells).
/// Can be upgraded to full Uber H3 library later.
class H3Aggregator
{
public:
  /// \brief Convert lat/lon to cell index.
  /// \param latLon Geographic coordinates.
  /// \return 64-bit cell index (compatible with H3 format).
  [[nodiscard]] static uint64_t LatLonToCell(ms::LatLon const & latLon);

  /// \brief Convert lat/lon to cell index with explicit resolution.
  /// \param latLon Geographic coordinates.
  /// \param resolution H3 resolution (0-15), default 9.
  /// \return 64-bit cell index.
  [[nodiscard]] static uint64_t LatLonToCell(ms::LatLon const & latLon, int resolution);

  /// \brief Get cell center coordinates.
  /// \param cellId Cell index.
  /// \return Center point of the cell.
  [[nodiscard]] static ms::LatLon CellToLatLon(uint64_t cellId);

  /// \brief Get neighboring cells.
  /// \param cellId Cell index.
  /// \return Vector of neighboring cell indices.
  [[nodiscard]] static std::vector<uint64_t> GetNeighbors(uint64_t cellId);

  /// \brief Get approximate cell edge length in meters.
  /// \param resolution H3 resolution.
  /// \return Edge length in meters.
  [[nodiscard]] static double GetCellEdgeMeters(int resolution);

  /// \brief Check if two cells are neighbors.
  [[nodiscard]] static bool AreNeighbors(uint64_t cellA, uint64_t cellB);

  /// \brief Get resolution from cell index.
  [[nodiscard]] static int GetResolution(uint64_t cellId);

  /// \brief Default resolution for traffic aggregation.
  static constexpr int kDefaultResolution = PrivacyConfig::kH3Resolution;

private:
  /// \brief Encode lat/lon to cell index.
  [[nodiscard]] static uint64_t EncodeCell(double lat, double lon, int resolution);

  /// \brief Decode cell index to lat/lon.
  static void DecodeCell(uint64_t cellId, double & lat, double & lon, int & resolution);
};

std::string DebugPrint(uint64_t cellId);
}  // namespace p2p

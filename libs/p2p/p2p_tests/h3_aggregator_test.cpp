#include "testing/testing.hpp"

#include "p2p/h3_aggregator.hpp"

#include "geometry/latlon.hpp"

#include <cmath>

namespace h3_aggregator_test
{
UNIT_TEST(H3Aggregator_LatLonToCell)
{
  ms::LatLon moscow(55.7558, 37.6173);
  uint64_t cellId = p2p::H3Aggregator::LatLonToCell(moscow);

  TEST_NOT_EQUAL(cellId, 0u, ());
  TEST_EQUAL(p2p::H3Aggregator::GetResolution(cellId), 9, ());
}

UNIT_TEST(H3Aggregator_CellToLatLon)
{
  ms::LatLon original(55.7558, 37.6173);
  uint64_t cellId = p2p::H3Aggregator::LatLonToCell(original);
  ms::LatLon center = p2p::H3Aggregator::CellToLatLon(cellId);

  // Center should be within cell size of original
  double const maxDiff = 0.002;  // ~200m
  TEST_LESS(std::abs(original.m_lat - center.m_lat), maxDiff, ());
  TEST_LESS(std::abs(original.m_lon - center.m_lon), maxDiff, ());
}

UNIT_TEST(H3Aggregator_SameCell)
{
  // Two close points should be in same cell
  ms::LatLon p1(55.7558, 37.6173);
  ms::LatLon p2(55.7559, 37.6174);  // ~15m away

  uint64_t cell1 = p2p::H3Aggregator::LatLonToCell(p1);
  uint64_t cell2 = p2p::H3Aggregator::LatLonToCell(p2);

  TEST_EQUAL(cell1, cell2, ());
}

UNIT_TEST(H3Aggregator_DifferentCells)
{
  // Two far points should be in different cells
  ms::LatLon moscow(55.7558, 37.6173);
  ms::LatLon berlin(52.5200, 13.4050);

  uint64_t cell1 = p2p::H3Aggregator::LatLonToCell(moscow);
  uint64_t cell2 = p2p::H3Aggregator::LatLonToCell(berlin);

  TEST_NOT_EQUAL(cell1, cell2, ());
}

UNIT_TEST(H3Aggregator_GetNeighbors)
{
  ms::LatLon moscow(55.7558, 37.6173);
  uint64_t cellId = p2p::H3Aggregator::LatLonToCell(moscow);
  auto neighbors = p2p::H3Aggregator::GetNeighbors(cellId);

  TEST_EQUAL(neighbors.size(), 8u, ());

  // All neighbors should be different from center
  for (uint64_t n : neighbors)
    TEST_NOT_EQUAL(n, cellId, ());
}

UNIT_TEST(H3Aggregator_AreNeighbors)
{
  ms::LatLon p1(55.7558, 37.6173);
  uint64_t cell1 = p2p::H3Aggregator::LatLonToCell(p1);
  auto neighbors = p2p::H3Aggregator::GetNeighbors(cell1);

  TEST(p2p::H3Aggregator::AreNeighbors(cell1, neighbors[0]), ());

  // Far away cell should not be neighbor
  ms::LatLon farAway(55.8, 37.7);
  uint64_t farCell = p2p::H3Aggregator::LatLonToCell(farAway);
  TEST(!p2p::H3Aggregator::AreNeighbors(cell1, farCell), ());
}

UNIT_TEST(H3Aggregator_Resolution)
{
  ms::LatLon point(55.7558, 37.6173);

  uint64_t cell5 = p2p::H3Aggregator::LatLonToCell(point, 5);
  uint64_t cell9 = p2p::H3Aggregator::LatLonToCell(point, 9);
  uint64_t cell12 = p2p::H3Aggregator::LatLonToCell(point, 12);

  TEST_EQUAL(p2p::H3Aggregator::GetResolution(cell5), 5, ());
  TEST_EQUAL(p2p::H3Aggregator::GetResolution(cell9), 9, ());
  TEST_EQUAL(p2p::H3Aggregator::GetResolution(cell12), 12, ());
}

UNIT_TEST(H3Aggregator_CellEdgeMeters)
{
  double edge9 = p2p::H3Aggregator::GetCellEdgeMeters(9);

  // Resolution 9 should be approximately 150-200m
  TEST_GREATER(edge9, 150.0, ());
  TEST_LESS(edge9, 200.0, ());
}

UNIT_TEST(H3Aggregator_NormalizeLongitude)
{
  // Test longitude wrapping at dateline
  ms::LatLon eastPacific(0.0, 179.9);
  ms::LatLon westPacific(0.0, -179.9);

  uint64_t cellEast = p2p::H3Aggregator::LatLonToCell(eastPacific);
  uint64_t cellWest = p2p::H3Aggregator::LatLonToCell(westPacific);

  // Should be different cells near dateline
  TEST_NOT_EQUAL(cellEast, cellWest, ());
}

UNIT_TEST(H3Aggregator_EdgeCases)
{
  // North pole
  ms::LatLon northPole(90.0, 0.0);
  uint64_t cellNorth = p2p::H3Aggregator::LatLonToCell(northPole);
  TEST_NOT_EQUAL(cellNorth, 0u, ());

  // South pole
  ms::LatLon southPole(-90.0, 0.0);
  uint64_t cellSouth = p2p::H3Aggregator::LatLonToCell(southPole);
  TEST_NOT_EQUAL(cellSouth, 0u, ());

  // Prime meridian and equator
  ms::LatLon origin(0.0, 0.0);
  uint64_t cellOrigin = p2p::H3Aggregator::LatLonToCell(origin);
  TEST_NOT_EQUAL(cellOrigin, 0u, ());
}

UNIT_TEST(H3Aggregator_DebugPrint)
{
  ms::LatLon point(55.7558, 37.6173);
  uint64_t cellId = p2p::H3Aggregator::LatLonToCell(point);

  std::string debug = p2p::DebugPrint(cellId);
  TEST(debug.find("H3Cell(") != std::string::npos, ());
  TEST(debug.find("res=9") != std::string::npos, ());
}

UNIT_TEST(H3Aggregator_RoundTrip)
{
  // Multiple locations around the world
  std::vector<ms::LatLon> locations = {
      ms::LatLon(55.7558, 37.6173),   // Moscow
      ms::LatLon(40.7128, -74.0060),  // New York
      ms::LatLon(-33.8688, 151.2093), // Sydney
      ms::LatLon(35.6762, 139.6503),  // Tokyo
      ms::LatLon(-22.9068, -43.1729)  // Rio de Janeiro
  };

  for (auto const & loc : locations)
  {
    uint64_t cellId = p2p::H3Aggregator::LatLonToCell(loc);
    ms::LatLon center = p2p::H3Aggregator::CellToLatLon(cellId);

    // Center should be close to original
    TEST_LESS(std::abs(loc.m_lat - center.m_lat), 0.01, ());
    TEST_LESS(std::abs(loc.m_lon - center.m_lon), 0.01, ());
  }
}

UNIT_TEST(H3Aggregator_SelfNotNeighbor)
{
  ms::LatLon point(55.7558, 37.6173);
  uint64_t cellId = p2p::H3Aggregator::LatLonToCell(point);

  // Cell should not be its own neighbor
  TEST(!p2p::H3Aggregator::AreNeighbors(cellId, cellId), ());
}

UNIT_TEST(H3Aggregator_NeighborSymmetry)
{
  ms::LatLon point(55.7558, 37.6173);
  uint64_t cellA = p2p::H3Aggregator::LatLonToCell(point);
  auto neighbors = p2p::H3Aggregator::GetNeighbors(cellA);

  // If B is neighbor of A, then A should be neighbor of B
  for (uint64_t cellB : neighbors)
  {
    TEST(p2p::H3Aggregator::AreNeighbors(cellA, cellB), ());
    TEST(p2p::H3Aggregator::AreNeighbors(cellB, cellA), ());
  }
}

UNIT_TEST(H3Aggregator_DifferentResolutionsDifferentCells)
{
  ms::LatLon point(55.7558, 37.6173);

  // Same point at different resolutions should produce different cells
  uint64_t cell5 = p2p::H3Aggregator::LatLonToCell(point, 5);
  uint64_t cell9 = p2p::H3Aggregator::LatLonToCell(point, 9);

  TEST_NOT_EQUAL(cell5, cell9, ());
}
}  // namespace h3_aggregator_test

#include "testing/testing.hpp"

#include "routing/alternative_finder.hpp"
#include "routing/alternative_route.hpp"
#include "routing/route.hpp"
#include "routing/segment.hpp"

#include <cmath>
#include <vector>

namespace alternative_finder_tests
{
using namespace routing;

namespace
{
// Helper to create test segments
std::vector<Segment> CreateTestPath(std::vector<uint32_t> const & featureIds)
{
  std::vector<Segment> path;
  path.reserve(featureIds.size());
  for (uint32_t fid : featureIds)
    path.emplace_back(NumMwmId(0), fid, 0 /* segmentIdx */, true /* forward */);
  return path;
}

}  // namespace

// Test DecisionPoint::IsGoodTimeToShow
UNIT_TEST(DecisionPoint_IsGoodTimeToShow_WithinWindow)
{
  DecisionPoint point;
  point.distanceFromStartMeters = 30000;  // 30km from start

  // 500m before = good (within 500m-2km window)
  TEST(point.IsGoodTimeToShow(29500), ("Should show at 500m before"));

  // 1km before = good
  TEST(point.IsGoodTimeToShow(29000), ("Should show at 1km before"));

  // 1.5km before = good
  TEST(point.IsGoodTimeToShow(28500), ("Should show at 1.5km before"));

  // 2km before = good (edge of window)
  TEST(point.IsGoodTimeToShow(28000), ("Should show at 2km before"));
}

UNIT_TEST(DecisionPoint_IsGoodTimeToShow_OutsideWindow)
{
  DecisionPoint point;
  point.distanceFromStartMeters = 30000;

  // 200m before = too close (< 500m)
  TEST(!point.IsGoodTimeToShow(29800), ("Should not show at 200m before"));

  // 5km before = too early (> 2km)
  TEST(!point.IsGoodTimeToShow(25000), ("Should not show at 5km before"));

  // After passing = no
  TEST(!point.IsGoodTimeToShow(31000), ("Should not show after passing"));

  // Exactly at point = no
  TEST(!point.IsGoodTimeToShow(30000), ("Should not show at point"));
}

// Test AlternativeRoute::GetStretchRatio
UNIT_TEST(AlternativeRoute_GetStretchRatio_Normal)
{
  AlternativeRoute alt;
  alt.distanceMeters = 130000;  // 130km

  double const primaryDistance = 100000;  // 100km
  double const ratio = alt.GetStretchRatio(primaryDistance);

  TEST_ALMOST_EQUAL_ABS(ratio, 1.3, 0.001, ("130km / 100km = 1.3"));
}

UNIT_TEST(AlternativeRoute_GetStretchRatio_SameLength)
{
  AlternativeRoute alt;
  alt.distanceMeters = 100000;

  double const ratio = alt.GetStretchRatio(100000);
  TEST_ALMOST_EQUAL_ABS(ratio, 1.0, 0.001, ("Same length = 1.0"));
}

UNIT_TEST(AlternativeRoute_GetStretchRatio_ZeroPrimary)
{
  AlternativeRoute alt;
  alt.distanceMeters = 100000;

  double const ratio = alt.GetStretchRatio(0.0);
  TEST_ALMOST_EQUAL_ABS(ratio, 0.0, 0.001, ("Zero primary = 0.0"));
}

// Test AlternativeRoute::IsAcceptable
UNIT_TEST(AlternativeRoute_IsAcceptable_ValidRoute)
{
  AlternativeRoute alt;
  alt.distanceMeters = 120000;     // 120km
  alt.overlapWithPrimary = 0.5;    // 50% overlap

  double const primaryDistance = 100000;  // 100km
  double const maxOverlap = 0.6;          // 60%
  double const maxStretch = 1.3;          // 130%

  TEST(alt.IsAcceptable(maxOverlap, maxStretch, primaryDistance),
       ("50% overlap, 120% stretch should be acceptable"));
}

UNIT_TEST(AlternativeRoute_IsAcceptable_TooMuchOverlap)
{
  AlternativeRoute alt;
  alt.distanceMeters = 110000;
  alt.overlapWithPrimary = 0.7;    // 70% overlap - too much

  double const primaryDistance = 100000;
  double const maxOverlap = 0.6;
  double const maxStretch = 1.3;

  TEST(!alt.IsAcceptable(maxOverlap, maxStretch, primaryDistance),
       ("70% overlap should not be acceptable"));
}

UNIT_TEST(AlternativeRoute_IsAcceptable_TooLong)
{
  AlternativeRoute alt;
  alt.distanceMeters = 140000;     // 140km - too long
  alt.overlapWithPrimary = 0.4;

  double const primaryDistance = 100000;
  double const maxOverlap = 0.6;
  double const maxStretch = 1.3;   // Max 130%

  TEST(!alt.IsAcceptable(maxOverlap, maxStretch, primaryDistance),
       ("140% stretch should not be acceptable"));
}

UNIT_TEST(AlternativeRoute_IsAcceptable_EdgeCases)
{
  AlternativeRoute alt;
  alt.distanceMeters = 130000;     // Exactly 130%
  alt.overlapWithPrimary = 0.6;    // Exactly 60%

  double const primaryDistance = 100000;
  double const maxOverlap = 0.6;
  double const maxStretch = 1.3;

  // Edge case: exactly at threshold should be acceptable (<=)
  TEST(alt.IsAcceptable(maxOverlap, maxStretch, primaryDistance),
       ("Exactly at threshold should be acceptable"));
}

// Test AlternativeParams defaults
UNIT_TEST(AlternativeParams_DefaultValues)
{
  AlternativeParams params = AlternativeParams::Default();

  TEST_EQUAL(params.k, 3, ("Default k should be 3"));
  TEST_ALMOST_EQUAL_ABS(params.overlapThreshold, 0.6, 0.001, ("Default overlap 60%"));
  TEST_ALMOST_EQUAL_ABS(params.maxLengthRatio, 1.3, 0.001, ("Default stretch 130%"));
  TEST_ALMOST_EQUAL_ABS(params.minLengthMeters, 50000, 0.1, ("Default min length 50km"));
  TEST_EQUAL(params.maxViaNodeCandidates, 100, ("Default max candidates 100"));
}

UNIT_TEST(AlternativeParams_ForMediumRoutes)
{
  AlternativeParams params = AlternativeParams::ForMediumRoutes();

  TEST_EQUAL(params.k, 2, ("Medium routes k should be 2"));
  TEST_ALMOST_EQUAL_ABS(params.minLengthMeters, 20000, 0.1, ("Medium min length 20km"));
  TEST_EQUAL(params.maxViaNodeCandidates, 50, ("Medium max candidates 50"));
}

// Test IAlternativeFinder::CalcOverlap via the factory
UNIT_TEST(AlternativeFinder_CalcOverlap_IdenticalPaths)
{
  auto finder = CreateAlternativeFinder();

  std::vector<Segment> path1 = CreateTestPath({1, 2, 3, 4, 5});
  std::vector<Segment> path2 = CreateTestPath({1, 2, 3, 4, 5});

  double const overlap = finder->CalcOverlap(path1, path2);
  TEST_ALMOST_EQUAL_ABS(overlap, 1.0, 0.001, ("Identical paths = 100% overlap"));
}

UNIT_TEST(AlternativeFinder_CalcOverlap_NoOverlap)
{
  auto finder = CreateAlternativeFinder();

  std::vector<Segment> path1 = CreateTestPath({1, 2, 3});
  std::vector<Segment> path2 = CreateTestPath({4, 5, 6});

  double const overlap = finder->CalcOverlap(path1, path2);
  TEST_ALMOST_EQUAL_ABS(overlap, 0.0, 0.001, ("Disjoint paths = 0% overlap"));
}

UNIT_TEST(AlternativeFinder_CalcOverlap_PartialOverlap)
{
  auto finder = CreateAlternativeFinder();

  // Path1: {1, 2, 3, 4}
  // Path2: {3, 4, 5, 6}
  // Intersection: {3, 4} = 2 segments
  // Union: {1, 2, 3, 4, 5, 6} = 6 segments
  // Jaccard = 2/6 = 0.333...

  std::vector<Segment> path1 = CreateTestPath({1, 2, 3, 4});
  std::vector<Segment> path2 = CreateTestPath({3, 4, 5, 6});

  double const overlap = finder->CalcOverlap(path1, path2);
  TEST_ALMOST_EQUAL_ABS(overlap, 2.0 / 6.0, 0.001, ("Partial overlap = 33%"));
}

UNIT_TEST(AlternativeFinder_CalcOverlap_EmptyPaths)
{
  auto finder = CreateAlternativeFinder();

  std::vector<Segment> empty;
  std::vector<Segment> path = CreateTestPath({1, 2, 3});

  double const overlap1 = finder->CalcOverlap(empty, path);
  TEST_ALMOST_EQUAL_ABS(overlap1, 0.0, 0.001, ("Empty path1 = 0% overlap"));

  double const overlap2 = finder->CalcOverlap(path, empty);
  TEST_ALMOST_EQUAL_ABS(overlap2, 0.0, 0.001, ("Empty path2 = 0% overlap"));

  double const overlap3 = finder->CalcOverlap(empty, empty);
  TEST_ALMOST_EQUAL_ABS(overlap3, 0.0, 0.001, ("Both empty = 0% overlap"));
}

// Test AlternativeRoute::IsValid
UNIT_TEST(AlternativeRoute_IsValid)
{
  AlternativeRoute valid;
  valid.routeIndex = 1;
  valid.distanceMeters = 100000;
  valid.path = CreateTestPath({1, 2, 3});

  TEST(valid.IsValid(), ("Valid route should return true"));

  AlternativeRoute invalidIndex;
  invalidIndex.routeIndex = 0;  // Primary, not alternative
  invalidIndex.distanceMeters = 100000;
  invalidIndex.path = CreateTestPath({1, 2, 3});

  TEST(!invalidIndex.IsValid(), ("Index 0 should be invalid"));

  AlternativeRoute invalidDistance;
  invalidDistance.routeIndex = 1;
  invalidDistance.distanceMeters = 0;
  invalidDistance.path = CreateTestPath({1, 2, 3});

  TEST(!invalidDistance.IsValid(), ("Zero distance should be invalid"));

  AlternativeRoute invalidPath;
  invalidPath.routeIndex = 1;
  invalidPath.distanceMeters = 100000;
  // Empty path

  TEST(!invalidPath.IsValid(), ("Empty path should be invalid"));
}

}  // namespace alternative_finder_tests

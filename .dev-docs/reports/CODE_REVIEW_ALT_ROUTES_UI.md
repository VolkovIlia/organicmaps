# Code Review: Alternative Routes UI Integration

**Commit**: `12fed8fd347e`
**Branch**: `develop`
**Reviewer**: Reviewer Suckless Agent
**Date**: 2026-02-04

---

## Summary

**Overall Assessment**: REQUEST_CHANGES

**Code Health Impact**: IMPROVES (with fixes)

**Complexity Score**: LOW-MEDIUM

**Suckless Compliance**: 7/10

### Key Findings

- **Strengths**:
  - Clean architecture with proper separation between C++/JNI/Java layers
  - Good JNI memory management (DeleteLocalRef for local references)
  - Follows existing codebase patterns for adapters and layouts
  - Proper use of thread checking in RoutingSession
  - Externalized strings (no hardcoded UI strings)

- **Concerns**:
  - CRITICAL: Duplicate panel inclusion causing potential UI bugs
  - Thread safety gap in GetAlternatives() accessor
  - Missing accessibility attributes (contentDescription)
  - Missing null safety annotations on some parameters

- **Suggestions**:
  - Add night/dark mode colors for faster/slower indicators
  - Consider extracting duration formatting to shared utility

---

## Critical Issues (must fix)

### 1. Duplicate Panel Inclusion

**Files**: `fragment_routing.xml:17` + `routing_plan.xml:181`

**Issue**: The `alternative_routes_panel.xml` is included in BOTH layouts. Since `fragment_routing.xml` already includes `routing_plan.xml` (line 9), this creates duplicate views with same IDs.

**Impact**:
- `findViewById()` may return wrong view instance
- Memory waste (duplicate layouts inflated)
- Potential UI state inconsistency

**Fix Required**:
Remove one of the includes. Since `routing_plan.xml` is more specific to the routing plan UI, remove the include from `fragment_routing.xml` OR `routing_plan.xml`:

```xml
<!-- Remove from routing_plan.xml:181 OR fragment_routing.xml:17 -->
<include layout="@layout/alternative_routes_panel" />
```

**Recommendation**: Keep in `fragment_routing.xml` (after altitude chart), remove from `routing_plan.xml`.

---

### 2. Thread Safety in GetAlternatives()

**File**: `libs/map/routing_manager.cpp:88`

**Issue**: `GetAlternativeRoutes()` returns a const reference to internal data without thread safety:

```cpp
std::vector<AlternativeRoute> const & RoutingManager::GetAlternativeRoutes() const
{
  return m_routingSession.GetAlternatives();  // No lock!
}
```

While `RoutingSession::GetAlternatives()` uses `CHECK_THREAD_CHECKER`, it only asserts in debug builds. If called from JNI thread while routing thread modifies alternatives, data race occurs.

**Impact**: Potential crash or data corruption when switching routes.

**Fix Required**: Either:
1. Return a copy instead of reference:
```cpp
std::vector<AlternativeRoute> RoutingManager::GetAlternativeRoutes() const
{
  return m_routingSession.GetAlternatives();  // Returns copy
}
```

2. Or ensure JNI calls are marshaled to the routing thread.

---

## Major Issues (should fix)

### 3. Missing @Nullable/@NonNull Annotations

**File**: `AlternativeRoutesAdapter.java`

**Issue**: Class fields and method parameters lack nullability annotations which is inconsistent with codebase patterns seen in `RoutingBottomMenuController.java`.

**Lines**: 22-25 (fields), 32-37 (method parameters)

```java
// Current (missing annotations)
private AlternativeRouteInfo[] mRoutes;
private OnRouteSelectedListener mListener;

public void setRoutes(@NonNull AlternativeRouteInfo[] routes, int selectedIndex)  // routes could be null from caller
```

**Fix**:
```java
@Nullable
private AlternativeRouteInfo[] mRoutes;
@Nullable
private OnRouteSelectedListener mListener;

public void setRoutes(@NonNull AlternativeRouteInfo[] routes, int selectedIndex)
```

---

### 4. Missing contentDescription for Accessibility

**File**: `item_alternative_route.xml`

**Issue**: The MaterialCardView is clickable/focusable but has no `contentDescription` for screen readers.

**Impact**: Accessibility violation - screen reader users cannot understand card purpose.

**Fix**: Add to MaterialCardView:
```xml
android:contentDescription="@string/route_card_description"
```

And add string:
```xml
<string name="route_card_description">Alternative route option</string>
```

Or set dynamically in adapter with route details.

---

### 5. Bounds Check Off-by-One

**File**: `libs/map/routing_manager.cpp:30`

**Issue**: The bounds check uses `>` instead of `>=`:

```cpp
if (index > 0 && static_cast<size_t>(index) > alternatives.size())
  return;
```

If `alternatives.size() == 2` and `index == 2`, the check passes but `alternatives[1]` would be accessed (valid), however the logic seems to expect 1-based indexing. This is confusing.

**Clarification Needed**: Document whether index is 0-based or 1-based. Current code mixes conventions:
- UI uses 0 = primary, 1+ = alternatives (0-based in array)
- Session uses 1-based in `SwitchToAlternative`

**Recommendation**: Add clear documentation and consider consistent indexing.

---

## Minor Issues (nice to fix)

### 6. Missing Night Mode Colors

**File**: `colors.xml:98-99`

**Issue**: `routing_route_faster` and `routing_route_slower` are defined only in values/colors.xml. Should also be in values-night/colors.xml for proper dark theme support.

**Current**:
```xml
<color name="routing_route_faster">#4CAF50</color>  <!-- Green -->
<color name="routing_route_slower">#F44336</color>  <!-- Red -->
```

**Fix**: Add to values-night/colors.xml with adjusted values for dark backgrounds if needed.

---

### 7. Duration Formatting Duplication

**File**: `AlternativeRoutesAdapter.java:114-120`

**Issue**: Duration formatting logic is duplicated. Similar formatting likely exists elsewhere in codebase.

```java
private String formatDuration(int seconds)
{
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  if (hours > 0)
    return hours + mContext.getString(R.string.hour) + " " + minutes + mContext.getString(R.string.minute);
  return minutes + " " + mContext.getString(R.string.minute);
}
```

**Suggestion**: Consider extracting to a shared utility class (e.g., `RoutingFormatUtils`).

---

### 8. Hardcoded Dimensions

**File**: `item_alternative_route.xml:8`

**Issue**: Card width uses hardcoded `160dp` instead of dimension resource.

```xml
android:layout_width="160dp"
```

**Suggestion**: Extract to dimens.xml:
```xml
<dimen name="alternative_route_card_width">160dp</dimen>
```

---

### 9. Spacing Inconsistency

**File**: `alternative_routes_panel.xml:29`

**Issue**: Uses `paddingStart="16dp"` but other places use `@dimen/margin_base`. Check if `16dp == margin_base`.

**Suggestion**: Use dimension resources for consistency.

---

## Suggestions (optional improvements)

### 10. Add Unit Test Placeholder

**Files**: No test files in commit

**Suggestion**: Add unit tests for:
- `AlternativeRouteInfo.isFaster()` / `getAbsTimeDifference()`
- Duration formatting logic
- Adapter selection state management

---

### 11. Consider Horizontal Scroll Snap

**File**: `alternative_routes_panel.xml`

**Suggestion**: Add `LinearSnapHelper` to RecyclerView for better UX:
```java
new LinearSnapHelper().attachToRecyclerView(mAlternativesRecycler);
```

---

### 12. RTL Layout Review

**Files**: XML layouts

**Observation**: Good use of `Start/End` instead of `Left/Right`. Verified RTL-safe.

---

## Positive Observations

1. **Proper JNI Memory Management**: `DeleteLocalRef()` called for local references in `CreateAlternativeRouteInfo()` and `CreateAlternativeRouteInfoArray()`.

2. **Thread Checking**: `CHECK_THREAD_CHECKER` used in RoutingSession methods prevents silent threading bugs in debug builds.

3. **Clean Adapter Pattern**: `AlternativeRoutesAdapter` follows existing patterns in codebase (compare with `ManageRouteAdapter`, `TransitStepAdapter`).

4. **Style Integration**: Alternative route colors added to all 6 theme variants (default/outdoors/vehicle, light/dark).

5. **Proper Function Sizes**: All functions are under 50 lines (suckless compliant).

6. **Clear Naming**: Method and variable names are descriptive (`InsertAlternativeRoutes`, `SelectAlternativeRoute`, `timeDifferenceSeconds`).

7. **Defensive Programming**: Null/empty checks present in most entry points.

---

## Acceptance Criteria Validation

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC1: Show alternative routes panel | PARTIAL | Works but duplicate include issue |
| AC2: Display route time/distance | PASS | Properly formatted |
| AC3: Show time difference | PASS | Faster/slower with colors |
| AC4: Route selection | PASS | Click handler + visual state |
| QC1: Functions under 50 lines | PASS | All functions compliant |
| QC2: Thread safety | FAIL | GetAlternatives() thread safety |
| QC3: Accessibility | FAIL | Missing contentDescription |
| QC4: RTL support | PASS | Uses Start/End margins |

---

## Action Items

### Must Fix Before Merge (Blocking)

1. [ ] Remove duplicate `alternative_routes_panel` include from one of the layouts
2. [ ] Fix thread safety in `GetAlternativeRoutes()` (return copy or marshal calls)
3. [ ] Add `contentDescription` to `item_alternative_route.xml` MaterialCardView

### Should Address (Important)

4. [ ] Add `@Nullable`/`@NonNull` annotations to `AlternativeRoutesAdapter`
5. [ ] Clarify and document indexing convention (0-based vs 1-based)
6. [ ] Add night mode colors for faster/slower indicators

### Optional Improvements (Nits)

7. [ ] Extract duration formatting to utility class
8. [ ] Use dimension resources instead of hardcoded values
9. [ ] Add unit tests for AlternativeRouteInfo helper methods

---

## Verdict

**REQUEST_CHANGES** - 3 blocking issues must be resolved before merge.

The implementation is well-structured and follows codebase patterns. Once the duplicate include and thread safety issues are addressed, this is ready for approval.

---

## Metrics

| Metric | Value | Target | Status |
|--------|-------|--------|--------|
| New Files | 7 | N/A | OK |
| Modified Files | 16 | N/A | OK |
| Lines Added | 670 | <400 ideal | WARN (large change) |
| Max Function Size | ~30 lines | <=50 | PASS |
| New Dependencies | 0 | <3 | PASS |

---

*Review generated by Reviewer Suckless Agent v2.0*

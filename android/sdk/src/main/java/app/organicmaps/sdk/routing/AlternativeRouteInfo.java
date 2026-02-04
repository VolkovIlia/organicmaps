package app.organicmaps.sdk.routing;

import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import app.organicmaps.sdk.util.Distance;

/**
 * Information about an alternative route.
 * Used by JNI.
 */
@Keep
@SuppressWarnings("unused")
public final class AlternativeRouteInfo
{
  /** Route index (0 = primary, 1+ = alternatives) */
  public final int routeIndex;

  /** Total distance with formatting */
  @NonNull
  public final Distance distance;

  /** Duration in seconds */
  public final int durationSeconds;

  /** Overlap percentage with primary route (0-100) */
  public final int overlapPercent;

  /** Time difference vs primary in seconds (negative = faster) */
  public final int timeDifferenceSeconds;

  // Constructor used by JNI
  @Keep
  public AlternativeRouteInfo(int routeIndex, @NonNull Distance distance, int durationSeconds,
                              int overlapPercent, int timeDifferenceSeconds)
  {
    this.routeIndex = routeIndex;
    this.distance = distance;
    this.durationSeconds = durationSeconds;
    this.overlapPercent = overlapPercent;
    this.timeDifferenceSeconds = timeDifferenceSeconds;
  }

  /**
   * Check if this alternative is faster than primary.
   * @return true if this route saves time
   */
  public boolean isFaster()
  {
    return timeDifferenceSeconds < 0;
  }

  /**
   * Get absolute time difference in seconds.
   * @return time difference (always positive)
   */
  public int getAbsTimeDifference()
  {
    return Math.abs(timeDifferenceSeconds);
  }
}

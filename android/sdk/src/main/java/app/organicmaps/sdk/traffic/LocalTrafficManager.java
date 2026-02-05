package app.organicmaps.sdk.traffic;

import androidx.annotation.MainThread;

/**
 * Manager for local traffic learning features.
 * Controls personal driving history collection and provides access
 * to personalized traffic predictions.
 */
@MainThread
public final class LocalTrafficManager
{
  private LocalTrafficManager() {}

  // TODO: These are stub implementations until native bindings are complete.
  // For now, return safe defaults to prevent crashes.

  /**
   * Check if learning from driving is enabled.
   * When enabled, the app collects anonymous driving speed data
   * to improve future traffic predictions.
   */
  public static boolean isLearningEnabled()
  {
    // Stub: return false until native implementation is ready
    return false;
  }

  /**
   * Enable or disable learning from driving.
   * @param enabled true to enable learning, false to disable
   */
  public static void setLearningEnabled(boolean enabled)
  {
    // Stub: no-op until native implementation is ready
  }

  /**
   * Clear all stored driving history data.
   * This permanently deletes personal speed observations.
   */
  public static void clearDrivingHistory()
  {
    // Stub: no-op until native implementation is ready
  }

  /**
   * Get the number of stored driving history records.
   */
  public static long getRecordCount()
  {
    // Stub: return 0 until native implementation is ready
    return 0;
  }

  /**
   * Get the approximate storage size in bytes.
   */
  public static long getStorageSizeBytes()
  {
    // Stub: return 0 until native implementation is ready
    return 0;
  }
}

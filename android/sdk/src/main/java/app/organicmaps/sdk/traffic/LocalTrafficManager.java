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

  /**
   * Check if learning from driving is enabled.
   * When enabled, the app collects anonymous driving speed data
   * to improve future traffic predictions.
   */
  public static boolean isLearningEnabled()
  {
    return nativeIsLearningEnabled();
  }

  /**
   * Enable or disable learning from driving.
   * @param enabled true to enable learning, false to disable
   */
  public static void setLearningEnabled(boolean enabled)
  {
    nativeSetLearningEnabled(enabled);
  }

  /**
   * Clear all stored driving history data.
   * This permanently deletes personal speed observations.
   */
  public static void clearDrivingHistory()
  {
    nativeClearDrivingHistory();
  }

  /**
   * Get the number of stored driving history records.
   */
  public static long getRecordCount()
  {
    return nativeGetRecordCount();
  }

  /**
   * Get the approximate storage size in bytes.
   */
  public static long getStorageSizeBytes()
  {
    return nativeGetStorageSizeBytes();
  }

  // Native methods
  private static native boolean nativeIsLearningEnabled();
  private static native void nativeSetLearningEnabled(boolean enabled);
  private static native void nativeClearDrivingHistory();
  private static native long nativeGetRecordCount();
  private static native long nativeGetStorageSizeBytes();
}

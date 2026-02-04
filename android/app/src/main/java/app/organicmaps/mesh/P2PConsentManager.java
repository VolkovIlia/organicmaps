package app.organicmaps.mesh;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

import app.organicmaps.sdk.util.log.Logger;

/**
 * Manages user consent for P2P traffic sharing.
 * Syncs with native PrivacyManager from libs/p2p/privacy_manager.hpp.
 *
 * Consent levels match p2p::ConsentLevel enum:
 * - Off (0): No P2P activity
 * - ViewOnly (1): Receive data, don't share
 * - Contribute (2): Full participation
 */
public class P2PConsentManager
{
  private static final String TAG = P2PConsentManager.class.getSimpleName();
  private static final String PREFS_NAME = "p2p_consent";
  private static final String KEY_CONSENT_LEVEL = "consent_level";
  private static final String KEY_DEBUG_MODE = "debug_mode";
  private static final String KEY_EXCHANGE_COUNT = "exchange_count_24h";
  private static final String KEY_EXCHANGE_TIMESTAMP = "exchange_timestamp";

  // Consent level constants matching p2p::ConsentLevel
  public static final int CONSENT_OFF = 0;
  public static final int CONSENT_VIEW_ONLY = 1;
  public static final int CONSENT_CONTRIBUTE = 2;

  @IntDef({CONSENT_OFF, CONSENT_VIEW_ONLY, CONSENT_CONTRIBUTE})
  @Retention(RetentionPolicy.SOURCE)
  public @interface ConsentLevel {}

  private final SharedPreferences mPrefs;
  private static P2PConsentManager sInstance;

  private P2PConsentManager(@NonNull Context context)
  {
    mPrefs = context.getApplicationContext()
        .getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
  }

  /**
   * Get singleton instance.
   * @param context Application context.
   * @return P2PConsentManager instance.
   */
  @NonNull
  public static synchronized P2PConsentManager getInstance(@NonNull Context context)
  {
    if (sInstance == null)
      sInstance = new P2PConsentManager(context);
    return sInstance;
  }

  /**
   * Get singleton instance. Must have been initialized with context first.
   * @return P2PConsentManager instance.
   * @throws IllegalStateException if not initialized.
   */
  @NonNull
  public static synchronized P2PConsentManager getInstance()
  {
    if (sInstance == null)
      throw new IllegalStateException("P2PConsentManager not initialized");
    return sInstance;
  }

  /**
   * Get current consent level.
   * @return One of CONSENT_OFF, CONSENT_VIEW_ONLY, CONSENT_CONTRIBUTE.
   */
  @ConsentLevel
  public int getConsentLevel()
  {
    int level = mPrefs.getInt(KEY_CONSENT_LEVEL, CONSENT_OFF);
    // Validate stored value
    if (level < CONSENT_OFF || level > CONSENT_CONTRIBUTE)
    {
      Logger.w(TAG, "Invalid consent level in prefs: " + level + ", resetting to OFF");
      level = CONSENT_OFF;
      mPrefs.edit().putInt(KEY_CONSENT_LEVEL, level).apply();
    }
    return level;
  }

  /**
   * Set consent level. Updates both local storage and native PrivacyManager.
   * @param level One of CONSENT_OFF, CONSENT_VIEW_ONLY, CONSENT_CONTRIBUTE.
   */
  public void setConsentLevel(@ConsentLevel int level)
  {
    if (level < CONSENT_OFF || level > CONSENT_CONTRIBUTE)
    {
      Logger.e(TAG, "Invalid consent level: " + level);
      return;
    }

    int oldLevel = getConsentLevel();
    if (oldLevel == level)
      return;

    mPrefs.edit().putInt(KEY_CONSENT_LEVEL, level).apply();
    nativeSetConsentLevel(level);
    Logger.i(TAG, "Consent level changed: " + getConsentLevelName(oldLevel) +
             " -> " + getConsentLevelName(level));
  }

  /**
   * Check if user consented to share traffic data.
   * @return true if consent level is CONSENT_CONTRIBUTE.
   */
  public boolean canShare()
  {
    return getConsentLevel() == CONSENT_CONTRIBUTE;
  }

  /**
   * Check if user consented to receive traffic data.
   * @return true if consent level is CONSENT_VIEW_ONLY or CONSENT_CONTRIBUTE.
   */
  public boolean canReceive()
  {
    int level = getConsentLevel();
    return level == CONSENT_VIEW_ONLY || level == CONSENT_CONTRIBUTE;
  }

  /**
   * Check if P2P is enabled at all.
   * @return true if consent level is not CONSENT_OFF.
   */
  public boolean isEnabled()
  {
    return getConsentLevel() != CONSENT_OFF;
  }

  /**
   * Get human-readable name for consent level.
   * @param level Consent level constant.
   * @return Human-readable name.
   */
  @NonNull
  public static String getConsentLevelName(@ConsentLevel int level)
  {
    switch (level)
    {
      case CONSENT_OFF:
        return "Off";
      case CONSENT_VIEW_ONLY:
        return "ViewOnly";
      case CONSENT_CONTRIBUTE:
        return "Contribute";
      default:
        return "Unknown(" + level + ")";
    }
  }

  /**
   * Sync local consent level with native PrivacyManager.
   * Should be called on app startup after native initialization.
   */
  public void syncWithNative()
  {
    int level = getConsentLevel();
    nativeSetConsentLevel(level);
    Logger.d(TAG, "Synced consent level with native: " + getConsentLevelName(level));
  }

  /**
   * Enable or disable debug mode.
   * @param enabled true to enable debug mode.
   */
  public void setDebugMode(boolean enabled)
  {
    mPrefs.edit().putBoolean(KEY_DEBUG_MODE, enabled).apply();
  }

  /**
   * Check if debug mode is enabled.
   * @return true if debug mode is enabled.
   */
  public boolean isDebugMode()
  {
    return mPrefs.getBoolean(KEY_DEBUG_MODE, false);
  }

  /**
   * Get count of data exchanges in last 24 hours.
   * @return Number of exchanges.
   */
  public int getExchangeCount24h()
  {
    long lastTimestamp = mPrefs.getLong(KEY_EXCHANGE_TIMESTAMP, 0);
    long now = System.currentTimeMillis();
    // Reset count if more than 24 hours passed
    if (now - lastTimestamp > 24 * 60 * 60 * 1000)
    {
      mPrefs.edit()
          .putInt(KEY_EXCHANGE_COUNT, 0)
          .putLong(KEY_EXCHANGE_TIMESTAMP, now)
          .apply();
      return 0;
    }
    return mPrefs.getInt(KEY_EXCHANGE_COUNT, 0);
  }

  /**
   * Increment exchange count.
   */
  public void incrementExchangeCount()
  {
    int count = getExchangeCount24h() + 1;
    mPrefs.edit()
        .putInt(KEY_EXCHANGE_COUNT, count)
        .putLong(KEY_EXCHANGE_TIMESTAMP, System.currentTimeMillis())
        .apply();
  }

  // Native methods
  private static native void nativeSetConsentLevel(int level);

  public static native int nativeGetConsentLevel();

  static
  {
    System.loadLibrary("organicmaps");
  }
}

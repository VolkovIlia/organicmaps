package app.organicmaps.settings;

import android.os.Bundle;
import android.view.View;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.preference.ListPreference;
import androidx.preference.Preference;
import androidx.preference.TwoStatePreference;
import app.organicmaps.R;
import app.organicmaps.mesh.P2PConsentManager;
import app.organicmaps.mesh.P2PTrafficService;
import app.organicmaps.sdk.traffic.LocalTrafficManager;
import android.widget.Toast;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;

/**
 * Privacy dashboard for P2P traffic sharing.
 * Shows current status, consent controls, and privacy information.
 */
public class P2PPrivacySettingsFragment extends BaseXmlSettingsFragment
{
  private ListPreference mConsentLevelPref;
  private Preference mPeersStatusPref;
  private Preference mExchangesStatusPref;
  private Preference mDisableNowPref;
  private TwoStatePreference mDebugModePref;
  private TwoStatePreference mLearnFromDrivingPref;
  private Preference mClearHistoryPref;

  @Override
  protected int getXmlResources()
  {
    return R.xml.prefs_p2p_privacy;
  }

  @Override
  public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState)
  {
    super.onViewCreated(view, savedInstanceState);
    initConsentLevelPref();
    initStatusPrefs();
    initLocalTrafficPrefs();
    initActionPrefs();
  }

  @Override
  public void onResume()
  {
    super.onResume();
    updateStatusDisplay();
  }

  private void initConsentLevelPref()
  {
    mConsentLevelPref = getPreference(getString(R.string.pref_p2p_consent_level));
    int currentLevel = P2PConsentManager.getInstance(requireContext()).getConsentLevel();
    mConsentLevelPref.setValue(String.valueOf(currentLevel));
    updateConsentLevelSummary(currentLevel);

    mConsentLevelPref.setOnPreferenceChangeListener((preference, newValue) -> {
      int level = Integer.parseInt((String) newValue);
      P2PConsentManager.getInstance(requireContext()).setConsentLevel(level);
      updateConsentLevelSummary(level);
      updateStatusDisplay();
      return true;
    });
  }

  private void updateConsentLevelSummary(int level)
  {
    String[] entries = getResources().getStringArray(R.array.p2p_consent_levels);
    if (level >= 0 && level < entries.length)
      mConsentLevelPref.setSummary(entries[level]);
  }

  private void initStatusPrefs()
  {
    mPeersStatusPref = getPreference(getString(R.string.pref_p2p_status_peers));
    mExchangesStatusPref = getPreference(getString(R.string.pref_p2p_status_exchanges));
  }

  private void initLocalTrafficPrefs()
  {
    mLearnFromDrivingPref = getPreference(getString(R.string.pref_learn_from_driving));
    mLearnFromDrivingPref.setChecked(LocalTrafficManager.isLearningEnabled());
    mLearnFromDrivingPref.setOnPreferenceChangeListener((preference, newValue) -> {
      boolean enabled = (Boolean) newValue;
      LocalTrafficManager.setLearningEnabled(enabled);
      return true;
    });

    mClearHistoryPref = getPreference(getString(R.string.pref_clear_driving_history));
    mClearHistoryPref.setOnPreferenceClickListener(preference -> {
      showClearHistoryConfirmation();
      return true;
    });
  }

  private void showClearHistoryConfirmation()
  {
    new MaterialAlertDialogBuilder(requireContext(), R.style.MwmTheme_AlertDialog)
        .setTitle(R.string.clear_history_confirm_title)
        .setMessage(R.string.clear_history_confirm_message)
        .setPositiveButton(R.string.delete, (dialog, which) -> {
          LocalTrafficManager.clearDrivingHistory();
          Toast.makeText(requireContext(), R.string.clear_history_success, Toast.LENGTH_SHORT).show();
        })
        .setNegativeButton(R.string.cancel, null)
        .show();
  }

  private void initActionPrefs()
  {
    mDisableNowPref = getPreference(getString(R.string.pref_p2p_disable_now));
    mDisableNowPref.setOnPreferenceClickListener(preference -> {
      P2PConsentManager.getInstance(requireContext()).setConsentLevel(P2PConsentManager.CONSENT_OFF);
      P2PTrafficService.stopService(requireContext());
      mConsentLevelPref.setValue("0");
      updateConsentLevelSummary(0);
      updateStatusDisplay();
      return true;
    });

    mDebugModePref = getPreference(getString(R.string.pref_p2p_debug_mode));
    mDebugModePref.setOnPreferenceChangeListener((preference, newValue) -> {
      boolean enabled = (Boolean) newValue;
      P2PConsentManager.getInstance(requireContext()).setDebugMode(enabled);
      return true;
    });
    mDebugModePref.setChecked(P2PConsentManager.getInstance(requireContext()).isDebugMode());
  }

  private void updateStatusDisplay()
  {
    int consentLevel = P2PConsentManager.getInstance(requireContext()).getConsentLevel();
    boolean isActive = consentLevel > P2PConsentManager.CONSENT_OFF;

    // Update peers status
    if (isActive)
    {
      int peerCount = P2PTrafficService.getDiscoveredPeerCount(requireContext());
      if (peerCount > 0)
        mPeersStatusPref.setSummary(getString(R.string.p2p_status_peers_count, peerCount));
      else
        mPeersStatusPref.setSummary(R.string.p2p_status_peers_none);
    }
    else
    {
      mPeersStatusPref.setSummary(R.string.p2p_status_peers_none);
    }

    // Update exchanges status
    if (isActive)
    {
      int exchangeCount = P2PConsentManager.getInstance(requireContext()).getExchangeCount24h();
      if (exchangeCount > 0)
        mExchangesStatusPref.setSummary(getString(R.string.p2p_status_exchanges_count, exchangeCount));
      else
        mExchangesStatusPref.setSummary(R.string.p2p_status_exchanges_none);
    }
    else
    {
      mExchangesStatusPref.setSummary(R.string.p2p_status_exchanges_none);
    }

    // Update disable button visibility
    mDisableNowPref.setEnabled(isActive);
  }
}

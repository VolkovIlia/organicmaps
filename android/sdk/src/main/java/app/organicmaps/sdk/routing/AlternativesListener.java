package app.organicmaps.sdk.routing;

import androidx.annotation.Keep;
import androidx.annotation.MainThread;

public interface AlternativesListener
{
  // Called from JNI when alternative routes are computed
  @Keep
  @SuppressWarnings("unused")
  @MainThread
  void onAlternativesReady();
}

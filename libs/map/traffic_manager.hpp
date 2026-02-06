#pragma once

#include "traffic/historical_traffic_provider.hpp"
#include "traffic/traffic_info.hpp"

#include "drape_frontend/drape_engine_safe_ptr.hpp"
#include "drape_frontend/traffic_generator.hpp"

#include "drape/pointers.hpp"

#include "indexer/mwm_set.hpp"

#include "geometry/point2d.hpp"
#include "geometry/polyline2d.hpp"
#include "geometry/screenbase.hpp"

#include "base/thread.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

class TrafficManager final
{
public:
  enum class TrafficState
  {
    Disabled,
    Enabled,
    WaitingData,
    Outdated,
    NoData,
    NetworkError,
    ExpiredData,
    ExpiredApp
  };

  /// @brief Controls what traffic data is shown on the map.
  /// RouteOnly: Historical traffic is used only for route coloring (not pushed to map layer).
  /// MapWide: Historical traffic is shown for all roads on the visible map area.
  enum class TrafficDisplayMode
  {
    RouteOnly,  ///< Default: traffic predictions on route only
    MapWide     ///< Traffic layer button: show traffic on all visible roads
  };

  struct MyPosition
  {
    m2::PointD m_position = m2::PointD(0.0, 0.0);
    bool m_knownPosition = false;

    MyPosition() = default;
    MyPosition(m2::PointD const & position) : m_position(position), m_knownPosition(true) {}
  };

  using TrafficStateChangedFn = std::function<void(TrafficState)>;
  using GetMwmsByRectFn = std::function<std::vector<MwmSet::MwmId>(m2::RectD const &)>;

  TrafficManager(GetMwmsByRectFn const & getMwmsByRectFn, size_t maxCacheSizeBytes,
                 traffic::TrafficObserver & observer);
  ~TrafficManager();

  void Teardown();

  TrafficState GetState() const;
  void SetStateListener(TrafficStateChangedFn const & onStateChangedFn);

  void SetDrapeEngine(ref_ptr<df::DrapeEngine> engine);
  void SetCurrentDataVersion(int64_t dataVersion);

  void SetEnabled(bool enabled);
  bool IsEnabled() const;

  void UpdateViewport(ScreenBase const & screen);
  void UpdateMyPosition(MyPosition const & myPosition);

  void Invalidate();

  void OnDestroySurface();
  void OnRecoverSurface();

  void OnMwmDeregistered(platform::LocalCountryFile const & countryFile);
  void Clear();

  void OnEnterForeground();
  void OnEnterBackground();

  void SetSimplifiedColorScheme(bool simplified);
  bool HasSimplifiedColorScheme() const { return m_hasSimplifiedColorScheme; }

  /// \brief Enable/disable historical traffic fallback when real-time is unavailable.
  void SetHistoricalFallbackEnabled(bool enabled);
  bool IsHistoricalFallbackEnabled() const { return m_historicalFallbackEnabled; }

  /// \brief Set traffic display mode (RouteOnly vs MapWide).
  void SetTrafficDisplayMode(TrafficDisplayMode mode);
  TrafficDisplayMode GetTrafficDisplayMode() const { return m_displayMode; }

  /// \brief Check if historical data is available for an MWM.
  bool HasHistoricalData(MwmSet::MwmId const & mwmId) const;

private:
  struct CacheEntry
  {
    CacheEntry();
    explicit CacheEntry(std::chrono::time_point<std::chrono::steady_clock> const & requestTime);

    bool m_isLoaded;
    size_t m_dataSize;

    std::chrono::time_point<std::chrono::steady_clock> m_lastActiveTime;
    std::chrono::time_point<std::chrono::steady_clock> m_lastRequestTime;
    std::chrono::time_point<std::chrono::steady_clock> m_lastResponseTime;

    int m_retriesCount;
    bool m_isWaitingForResponse;

    traffic::TrafficInfo::Availability m_lastAvailability;
  };

  void ThreadRoutine();
  bool WaitForRequest(std::vector<MwmSet::MwmId> & mwms);

  void OnTrafficDataResponse(traffic::TrafficInfo && info);
  void OnTrafficRequestFailed(traffic::TrafficInfo && info);

  /// \brief Updates |activeMwms| and request traffic data.
  /// \param rect is a rectangle covering a new active mwm set.
  /// \note |lastMwmsByRect|/|activeMwms| may be either |m_lastDrapeMwmsByRect/|m_activeDrapeMwms|
  /// or |m_lastRoutingMwmsByRect|/|m_activeRoutingMwms|.
  /// \note |m_mutex| is locked inside the method. So the method should be called without |m_mutex|.
  void UpdateActiveMwms(m2::RectD const & rect, std::vector<MwmSet::MwmId> & lastMwmsByRect,
                        std::set<MwmSet::MwmId> & activeMwms);

  // This is a group of methods that haven't their own synchronization inside.
  void RequestTrafficData();
  void RequestTrafficData(MwmSet::MwmId const & mwmId, bool force);

  void ClearImpl();
  void ClearCache(MwmSet::MwmId const & mwmId);
  void ShrinkCacheToAllowableSize();

  void UpdateState();
  void ChangeState(TrafficState newState);

  bool IsInvalidState() const;

  void UniteActiveMwms(std::set<MwmSet::MwmId> & activeMwms) const;

  void Pause();
  void Resume();

  template <class F>
  void ForEachActiveMwm(F && f) const
  {
    std::set<MwmSet::MwmId> activeMwms;
    UniteActiveMwms(activeMwms);
    std::for_each(activeMwms.begin(), activeMwms.end(), std::forward<F>(f));
  }

  GetMwmsByRectFn m_getMwmsByRectFn;
  traffic::TrafficObserver & m_observer;

  df::DrapeEngineSafePtr m_drapeEngine;
  std::atomic<int64_t> m_currentDataVersion;

  // These fields have a flag of their initialization.
  std::pair<MyPosition, bool> m_currentPosition = {MyPosition(), false};
  std::pair<ScreenBase, bool> m_currentModelView = {ScreenBase(), false};

  std::atomic<TrafficState> m_state;
  TrafficStateChangedFn m_onStateChangedFn;

  bool m_hasSimplifiedColorScheme = true;

  size_t m_maxCacheSizeBytes;
  size_t m_currentCacheSizeBytes = 0;

  std::map<MwmSet::MwmId, CacheEntry> m_mwmCache;

  bool m_isRunning;
  std::condition_variable m_condition;

  std::vector<MwmSet::MwmId> m_lastDrapeMwmsByRect;
  std::set<MwmSet::MwmId> m_activeDrapeMwms;
  std::vector<MwmSet::MwmId> m_lastRoutingMwmsByRect;
  std::set<MwmSet::MwmId> m_activeRoutingMwms;

  // The ETag or entity tag is part of HTTP, the protocol for the World Wide Web.
  // It is one of several mechanisms that HTTP provides for web cache validation,
  // which allows a client to make conditional requests.
  std::map<MwmSet::MwmId, std::string> m_trafficETags;

  std::atomic<bool> m_isPaused;

  std::vector<MwmSet::MwmId> m_requestedMwms;
  std::mutex m_mutex;
  threads::SimpleThread m_thread;

  // Historical traffic fallback
  std::atomic<bool> m_historicalFallbackEnabled{true};
  std::unique_ptr<traffic::HistoricalTrafficProvider> m_historicalProvider;

  // Traffic display mode: RouteOnly (default) or MapWide (traffic layer button)
  std::atomic<TrafficDisplayMode> m_displayMode{TrafficDisplayMode::RouteOnly};
};

extern std::string DebugPrint(TrafficManager::TrafficState state);

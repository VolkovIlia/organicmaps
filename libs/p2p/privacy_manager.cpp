#include "p2p/privacy_manager.hpp"

#include "base/logging.hpp"

#include <cmath>
#include <random>
#include <sstream>

namespace p2p
{
PrivacyManager::PrivacyManager()
  : m_rng(std::random_device{}())
{
}

PrivacyManager::PrivacyManager(ConsentLevel initialLevel)
  : m_consentLevel(initialLevel)
  , m_rng(std::random_device{}())
{
}

ConsentLevel PrivacyManager::GetConsentLevel() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_consentLevel;
}

void PrivacyManager::SetConsentLevel(ConsentLevel level)
{
  std::vector<ConsentChangeCallback> callbacks;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_consentLevel == level)
      return;
    m_consentLevel = level;
    callbacks = m_callbacks;
  }

  // Notify outside lock to avoid deadlocks
  for (auto const & cb : callbacks)
    cb(level);

  LOG(LINFO, ("P2P consent level changed to:", DebugPrint(level)));
}

void PrivacyManager::RegisterConsentCallback(ConsentChangeCallback callback)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_callbacks.push_back(std::move(callback));
}

bool PrivacyManager::CanShare() const
{
  return GetConsentLevel() == ConsentLevel::Contribute;
}

bool PrivacyManager::CanReceive() const
{
  auto const level = GetConsentLevel();
  return level == ConsentLevel::ViewOnly || level == ConsentLevel::Contribute;
}

double PrivacyManager::ApplyLDP(double value, double sensitivity) const
{
  if (m_epsilon <= 0.0)
    return value;

  double const scale = sensitivity / m_epsilon;
  double const noise = GenerateLaplaceNoise(scale);
  return value + noise;
}

float PrivacyManager::ApplyLDPFloat(float value, float sensitivity) const
{
  return static_cast<float>(ApplyLDP(static_cast<double>(value),
                                      static_cast<double>(sensitivity)));
}

double PrivacyManager::GenerateLaplaceNoise(double scale) const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  // Laplace distribution via inverse CDF: X = mu - b*sign(U)*ln(1-2|U|)
  // where U ~ Uniform(-0.5, 0.5)
  std::uniform_real_distribution<double> dist(-0.5, 0.5);
  double const u = dist(m_rng);

  if (u == 0.0)
    return 0.0;

  double const sign = (u < 0.0) ? -1.0 : 1.0;
  return -scale * sign * std::log(1.0 - 2.0 * std::abs(u));
}

bool PrivacyManager::CheckKAnonymity(uint64_t /* cellId */, uint32_t uniqueSegments) const
{
  return uniqueSegments >= m_kThreshold;
}

void PrivacyManager::RecordObservation(uint64_t cellId, uint32_t segmentId)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_cellObservations[cellId].insert(segmentId);
}

uint32_t PrivacyManager::GetUniqueSegmentCount(uint64_t cellId) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto const it = m_cellObservations.find(cellId);
  if (it == m_cellObservations.end())
    return 0;
  return static_cast<uint32_t>(it->second.size());
}

void PrivacyManager::ClearObservations()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_cellObservations.clear();
}

std::string DebugPrint(PrivacyManager const & manager)
{
  std::ostringstream oss;
  oss << "PrivacyManager ["
      << "consent=" << DebugPrint(manager.GetConsentLevel())
      << ", epsilon=" << manager.GetEpsilon()
      << ", k=" << manager.GetKThreshold()
      << "]";
  return oss.str();
}
}  // namespace p2p

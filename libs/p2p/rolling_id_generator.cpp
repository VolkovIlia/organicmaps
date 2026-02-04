#include "p2p/rolling_id_generator.hpp"

#include "base/logging.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace p2p
{
RollingIdGenerator::RollingIdGenerator()
  : m_rng(std::random_device{}())
{
  GenerateNewId();
  ScheduleNextRotation();
}

RollingId RollingIdGenerator::GetCurrentId()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (NeedsRotationUnlocked())
  {
    GenerateNewId();
    ScheduleNextRotation();
  }

  return m_currentId;
}

void RollingIdGenerator::Regenerate()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  GenerateNewId();
  ScheduleNextRotation();
  LOG(LINFO, ("Rolling ID regenerated manually"));
}

std::chrono::seconds RollingIdGenerator::GetTimeUntilRotation() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto const now = std::chrono::steady_clock::now();
  if (now >= m_nextRotationTime)
    return std::chrono::seconds(0);
  return std::chrono::duration_cast<std::chrono::seconds>(m_nextRotationTime - now);
}

bool RollingIdGenerator::NeedsRotation() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return NeedsRotationUnlocked();
}

bool RollingIdGenerator::NeedsRotationUnlocked() const
{
  return std::chrono::steady_clock::now() >= m_nextRotationTime;
}

void RollingIdGenerator::SetRotationInterval(std::chrono::minutes interval,
                                              std::chrono::minutes jitter)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_rotationInterval = interval;
  m_jitter = jitter;
}

std::chrono::minutes RollingIdGenerator::GetRotationInterval() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_rotationInterval;
}

void RollingIdGenerator::GenerateNewId()
{
  // Generate 16 random bytes using two 64-bit values
  std::uniform_int_distribution<uint64_t> dist;
  uint64_t const part1 = dist(m_rng);
  uint64_t const part2 = dist(m_rng);

  // Copy to array
  std::memcpy(m_currentId.data(), &part1, 8);
  std::memcpy(m_currentId.data() + 8, &part2, 8);
}

void RollingIdGenerator::ScheduleNextRotation()
{
  // Add random jitter to interval
  std::uniform_int_distribution<int> jitterDist(
      -static_cast<int>(m_jitter.count()),
      static_cast<int>(m_jitter.count()));

  auto const jitterMinutes = std::chrono::minutes(jitterDist(m_rng));
  auto const interval = m_rotationInterval + jitterMinutes;

  m_nextRotationTime = std::chrono::steady_clock::now() + interval;
}

std::string RollingIdToHex(RollingId const & id)
{
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (uint8_t byte : id)
    oss << std::setw(2) << static_cast<int>(byte);
  return oss.str();
}

bool operator==(RollingId const & a, RollingId const & b)
{
  // std::array has built-in operator==, but we define explicitly for namespace
  for (size_t i = 0; i < a.size(); ++i)
  {
    if (a[i] != b[i])
      return false;
  }
  return true;
}

bool operator!=(RollingId const & a, RollingId const & b)
{
  return !(a == b);
}

std::string DebugPrint(RollingId const & id)
{
  return "RollingId(" + RollingIdToHex(id) + ")";
}
}  // namespace p2p

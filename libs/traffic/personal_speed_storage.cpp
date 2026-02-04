#include "traffic/personal_speed_storage.hpp"

#include "coding/endianness.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace traffic
{
namespace
{
// File format version
uint32_t constexpr kCurrentVersion = 1;
uint32_t constexpr kHeaderSize = sizeof(uint32_t);

// Record layout (19 bytes total):
// featureId(4) + segmentIdx(2) + isForward(1) + dayOfWeek(1) + hour(1) +
// speedKmph(4) + sampleCount(2) + lastUpdatedDays(4)
size_t constexpr kRecordSize = 4 + 2 + 1 + 1 + 1 + 4 + 2 + 4;

// Seconds per day for conversion
uint32_t constexpr kSecondsPerDay = 24 * 60 * 60;

// EMA alpha for combining observations (0.3 = 30% new, 70% old)
float constexpr kEmaAlpha = 0.3f;

template <typename T>
void MemWrite(void * ptr, T value)
{
  value = SwapIfBigEndianMacroBased(value);
  memcpy(ptr, &value, sizeof(T));
}

template <typename T>
T MemRead(void const * ptr)
{
  T value;
  memcpy(&value, ptr, sizeof(T));
  return SwapIfBigEndianMacroBased(value);
}

void Pack(char * p, PersonalSpeedRecord const & record)
{
  size_t offset = 0;

  MemWrite<uint32_t>(p + offset, record.m_key.m_featureId);
  offset += sizeof(uint32_t);

  MemWrite<uint16_t>(p + offset, record.m_key.m_segmentIdx);
  offset += sizeof(uint16_t);

  MemWrite<uint8_t>(p + offset, record.m_key.m_isForward);
  offset += sizeof(uint8_t);

  MemWrite<uint8_t>(p + offset, record.m_key.m_dayOfWeek);
  offset += sizeof(uint8_t);

  MemWrite<uint8_t>(p + offset, record.m_key.m_hour);
  offset += sizeof(uint8_t);

  // Pack float as uint32_t bit pattern
  uint32_t speedBits;
  memcpy(&speedBits, &record.m_speedKmph, sizeof(float));
  MemWrite<uint32_t>(p + offset, speedBits);
  offset += sizeof(uint32_t);

  MemWrite<uint16_t>(p + offset, record.m_sampleCount);
  offset += sizeof(uint16_t);

  MemWrite<uint32_t>(p + offset, record.m_lastUpdatedDays);
}

void Unpack(char const * p, PersonalSpeedRecord & record)
{
  size_t offset = 0;

  record.m_key.m_featureId = MemRead<uint32_t>(p + offset);
  offset += sizeof(uint32_t);

  record.m_key.m_segmentIdx = MemRead<uint16_t>(p + offset);
  offset += sizeof(uint16_t);

  record.m_key.m_isForward = MemRead<uint8_t>(p + offset);
  offset += sizeof(uint8_t);

  record.m_key.m_dayOfWeek = MemRead<uint8_t>(p + offset);
  offset += sizeof(uint8_t);

  record.m_key.m_hour = MemRead<uint8_t>(p + offset);
  offset += sizeof(uint8_t);

  uint32_t const speedBits = MemRead<uint32_t>(p + offset);
  memcpy(&record.m_speedKmph, &speedBits, sizeof(float));
  offset += sizeof(uint32_t);

  record.m_sampleCount = MemRead<uint16_t>(p + offset);
  offset += sizeof(uint16_t);

  record.m_lastUpdatedDays = MemRead<uint32_t>(p + offset);
}
}  // namespace

// PersonalSpeedKey operators

bool PersonalSpeedKey::operator<(PersonalSpeedKey const & rhs) const
{
  if (m_featureId != rhs.m_featureId)
    return m_featureId < rhs.m_featureId;
  if (m_segmentIdx != rhs.m_segmentIdx)
    return m_segmentIdx < rhs.m_segmentIdx;
  if (m_isForward != rhs.m_isForward)
    return m_isForward < rhs.m_isForward;
  if (m_dayOfWeek != rhs.m_dayOfWeek)
    return m_dayOfWeek < rhs.m_dayOfWeek;
  return m_hour < rhs.m_hour;
}

bool PersonalSpeedKey::operator==(PersonalSpeedKey const & rhs) const
{
  return m_featureId == rhs.m_featureId &&
         m_segmentIdx == rhs.m_segmentIdx &&
         m_isForward == rhs.m_isForward &&
         m_dayOfWeek == rhs.m_dayOfWeek &&
         m_hour == rhs.m_hour;
}

// PersonalSpeedRecord

void PersonalSpeedRecord::AddObservation(float speedKmph, uint32_t currentDays)
{
  if (m_sampleCount == 0)
  {
    m_speedKmph = speedKmph;
    m_sampleCount = 1;
  }
  else
  {
    // Exponential moving average
    m_speedKmph = kEmaAlpha * speedKmph + (1.0f - kEmaAlpha) * m_speedKmph;
    if (m_sampleCount < UINT16_MAX)
      ++m_sampleCount;
  }
  m_lastUpdatedDays = currentDays;
}

// PersonalSpeedStorage

PersonalSpeedStorage::PersonalSpeedStorage(std::string const & filePath)
  : m_filePath(filePath)
{
  Load();
}

PersonalSpeedStorage::~PersonalSpeedStorage()
{
  try
  {
    Flush();
  }
  catch (std::exception const & e)
  {
    LOG(LERROR, ("Failed to flush personal speed storage:", e.what()));
  }
}

void PersonalSpeedStorage::AddObservation(uint32_t featureId, uint16_t segmentIdx, bool isForward,
                                           uint8_t dayOfWeek, uint8_t hour, float speedKmph)
{
  ASSERT_LESS(dayOfWeek, 7, ());
  ASSERT_LESS(hour, 24, ());

  PersonalSpeedKey key;
  key.m_featureId = featureId;
  key.m_segmentIdx = segmentIdx;
  key.m_isForward = isForward ? 1 : 0;
  key.m_dayOfWeek = dayOfWeek;
  key.m_hour = hour;

  uint32_t const currentDays = GetCurrentDaysSinceEpoch();

  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = FindRecord(key);
  if (it != m_records.end() && it->m_key == key)
  {
    it->AddObservation(speedKmph, currentDays);
  }
  else
  {
    PersonalSpeedRecord record;
    record.m_key = key;
    record.AddObservation(speedKmph, currentDays);
    m_records.insert(it, record);
  }

  m_isDirty = true;
}

float PersonalSpeedStorage::GetSpeed(uint32_t featureId, uint16_t segmentIdx, bool isForward,
                                      uint8_t dayOfWeek, uint8_t hour) const
{
  PersonalSpeedKey key;
  key.m_featureId = featureId;
  key.m_segmentIdx = segmentIdx;
  key.m_isForward = isForward ? 1 : 0;
  key.m_dayOfWeek = dayOfWeek;
  key.m_hour = hour;

  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = FindRecord(key);
  if (it != m_records.end() && it->m_key == key)
    return it->m_speedKmph;

  return 0.0f;
}

bool PersonalSpeedStorage::HasData(uint32_t featureId, uint16_t segmentIdx, bool isForward,
                                    uint8_t dayOfWeek, uint8_t hour) const
{
  return GetSpeed(featureId, segmentIdx, isForward, dayOfWeek, hour) > 0.0f;
}

size_t PersonalSpeedStorage::GetRecordCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_records.size();
}

size_t PersonalSpeedStorage::GetStorageSizeBytes() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return kHeaderSize + m_records.size() * kRecordSize;
}

void PersonalSpeedStorage::Clear()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_records.clear();
  m_isDirty = true;
}

void PersonalSpeedStorage::Flush()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_isDirty)
  {
    Save();
    m_isDirty = false;
  }
}

void PersonalSpeedStorage::CleanupOldRecords(uint32_t retentionDays)
{
  uint32_t const currentDays = GetCurrentDaysSinceEpoch();
  uint32_t const threshold = currentDays > retentionDays ? currentDays - retentionDays : 0;

  std::lock_guard<std::mutex> lock(m_mutex);

  auto const oldSize = m_records.size();
  m_records.erase(
      std::remove_if(m_records.begin(), m_records.end(),
                     [threshold](PersonalSpeedRecord const & r) {
                       return r.m_lastUpdatedDays < threshold;
                     }),
      m_records.end());

  if (m_records.size() != oldSize)
  {
    LOG(LINFO, ("Cleaned up", oldSize - m_records.size(), "old personal speed records"));
    m_isDirty = true;
  }
}

void PersonalSpeedStorage::ForEach(std::function<void(PersonalSpeedRecord const &)> const & fn) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto const & record : m_records)
    fn(record);
}

void PersonalSpeedStorage::Load()
{
  std::ifstream file(m_filePath, std::ios::binary);
  if (!file)
  {
    LOG(LINFO, ("Personal speed storage file not found, starting fresh:", m_filePath));
    return;
  }

  if (!ReadHeader(file))
    return;

  size_t const recordCount = GetRecordCountFromFile(file);
  if (recordCount == 0)
    return;

  ReadRecords(file, recordCount);

  // Ensure sorted (should be sorted from save, but verify)
  std::sort(m_records.begin(), m_records.end(),
            [](PersonalSpeedRecord const & a, PersonalSpeedRecord const & b) {
              return a.m_key < b.m_key;
            });

  LOG(LINFO, ("Loaded", m_records.size(), "personal speed records from storage"));
}

bool PersonalSpeedStorage::ReadHeader(std::ifstream & file)
{
  uint32_t version = 0;
  file.read(reinterpret_cast<char *>(&version), sizeof(version));
  version = SwapIfBigEndianMacroBased(version);

  if (!file.good())
  {
    LOG(LWARNING, ("Failed to read version from personal speed storage:", m_filePath));
    return false;
  }

  if (version != kCurrentVersion)
  {
    LOG(LWARNING, ("Unsupported personal speed storage version:", version, "expected:", kCurrentVersion));
    return false;
  }
  return true;
}

size_t PersonalSpeedStorage::GetRecordCountFromFile(std::ifstream & file)
{
  file.seekg(0, std::ios::end);
  auto const fileSize = static_cast<size_t>(file.tellg());
  file.seekg(kHeaderSize, std::ios::beg);

  if (fileSize < kHeaderSize)
  {
    LOG(LWARNING, ("Personal speed storage file too small:", fileSize));
    return 0;
  }
  return (fileSize - kHeaderSize) / kRecordSize;
}

void PersonalSpeedStorage::ReadRecords(std::ifstream & file, size_t recordCount)
{
  m_records.reserve(recordCount);
  std::vector<char> buffer(kRecordSize);

  for (size_t i = 0; i < recordCount; ++i)
  {
    file.read(buffer.data(), kRecordSize);
    if (!file.good())
    {
      LOG(LWARNING, ("Failed to read record", i, "from personal speed storage"));
      break;
    }

    PersonalSpeedRecord record;
    Unpack(buffer.data(), record);
    m_records.push_back(record);
  }
}

void PersonalSpeedStorage::Save() const
{
  std::ofstream file(m_filePath, std::ios::binary | std::ios::trunc);
  if (!file)
    MYTHROW(WriteException, ("Cannot open file for writing:", m_filePath));

  // Write version
  uint32_t version = SwapIfBigEndianMacroBased(kCurrentVersion);
  file.write(reinterpret_cast<char const *>(&version), sizeof(version));

  if (!file.good())
    MYTHROW(WriteException, ("Failed to write version:", m_filePath));

  // Write records
  std::vector<char> buffer(kRecordSize);
  for (auto const & record : m_records)
  {
    Pack(buffer.data(), record);
    file.write(buffer.data(), kRecordSize);

    if (!file.good())
      MYTHROW(WriteException, ("Failed to write record:", m_filePath));
  }

  file.flush();
  if (!file.good())
    MYTHROW(WriteException, ("Failed to flush:", m_filePath));

  LOG(LINFO, ("Saved", m_records.size(), "personal speed records to storage"));
}

std::vector<PersonalSpeedRecord>::iterator PersonalSpeedStorage::FindRecord(PersonalSpeedKey const & key)
{
  return std::lower_bound(m_records.begin(), m_records.end(), key,
                          [](PersonalSpeedRecord const & r, PersonalSpeedKey const & k) {
                            return r.m_key < k;
                          });
}

std::vector<PersonalSpeedRecord>::const_iterator PersonalSpeedStorage::FindRecord(
    PersonalSpeedKey const & key) const
{
  return std::lower_bound(m_records.begin(), m_records.end(), key,
                          [](PersonalSpeedRecord const & r, PersonalSpeedKey const & k) {
                            return r.m_key < k;
                          });
}

uint32_t GetCurrentDaysSinceEpoch()
{
  auto const now = std::time(nullptr);
  return static_cast<uint32_t>(now / kSecondsPerDay);
}

std::string DebugPrint(PersonalSpeedKey const & key)
{
  std::ostringstream oss;
  oss << "PersonalSpeedKey [feature=" << key.m_featureId
      << ", seg=" << key.m_segmentIdx
      << ", fwd=" << static_cast<int>(key.m_isForward)
      << ", day=" << static_cast<int>(key.m_dayOfWeek)
      << ", hour=" << static_cast<int>(key.m_hour) << "]";
  return oss.str();
}

std::string DebugPrint(PersonalSpeedRecord const & record)
{
  std::ostringstream oss;
  oss << "PersonalSpeedRecord [" << DebugPrint(record.m_key)
      << ", speed=" << record.m_speedKmph << " km/h"
      << ", samples=" << record.m_sampleCount
      << ", lastUpdated=" << record.m_lastUpdatedDays << " days]";
  return oss.str();
}

}  // namespace traffic

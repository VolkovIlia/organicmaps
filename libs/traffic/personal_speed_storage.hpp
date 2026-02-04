#pragma once

#include "base/exception.hpp"

#include <cstdint>
#include <ctime>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace traffic
{
/// \brief Key identifying a road segment for personal speed data.
/// \note Uses uint8_t for boolean/small enum fields to minimize serialized record size (19 bytes).
struct PersonalSpeedKey
{
  uint32_t m_featureId = 0;
  uint16_t m_segmentIdx = 0;
  uint8_t m_isForward = 0;  ///< Direction: 0 = backward, 1 = forward. uint8_t for compact serialization.
  uint8_t m_dayOfWeek = 0;  ///< Day of week: 0 = Sunday, 6 = Saturday.
  uint8_t m_hour = 0;       ///< Hour: 0-23.

  bool operator<(PersonalSpeedKey const & rhs) const;
  bool operator==(PersonalSpeedKey const & rhs) const;
};

/// \brief Personal speed record for a segment at specific time.
struct PersonalSpeedRecord
{
  PersonalSpeedKey m_key;
  float m_speedKmph = 0.0f;      // Average observed speed
  uint16_t m_sampleCount = 0;    // Number of observations
  uint32_t m_lastUpdatedDays = 0; // Days since Unix epoch

  /// \brief Update with new observation using exponential moving average.
  void AddObservation(float speedKmph, uint32_t currentDays);
};

/// \brief Thread-safe storage for personal driving speed history.
/// Stores user's observed speeds per road segment and time slot.
/// Data is kept in memory and periodically flushed to disk.
/// Implements 90-day rolling window retention.
class PersonalSpeedStorage
{
public:
  DECLARE_EXCEPTION(OpenException, RootException);
  DECLARE_EXCEPTION(WriteException, RootException);
  DECLARE_EXCEPTION(ReadException, RootException);

  /// Default retention period in days.
  static constexpr uint32_t kDefaultRetentionDays = 90;

  /// \brief Opens or creates storage file.
  /// \param filePath Path to storage file.
  explicit PersonalSpeedStorage(std::string const & filePath);

  ~PersonalSpeedStorage();

  /// \brief Add or update speed observation for a segment.
  /// Thread-safe. Uses exponential moving average for combining observations.
  void AddObservation(uint32_t featureId, uint16_t segmentIdx, bool isForward,
                      uint8_t dayOfWeek, uint8_t hour, float speedKmph);

  /// \brief Get stored speed for a segment at given time.
  /// \return Speed in km/h, or 0 if no data.
  [[nodiscard]] float GetSpeed(uint32_t featureId, uint16_t segmentIdx, bool isForward,
                                uint8_t dayOfWeek, uint8_t hour) const;

  /// \brief Check if data exists for a segment at given time.
  [[nodiscard]] bool HasData(uint32_t featureId, uint16_t segmentIdx, bool isForward,
                              uint8_t dayOfWeek, uint8_t hour) const;

  /// \brief Get number of stored records.
  [[nodiscard]] size_t GetRecordCount() const;

  /// \brief Get approximate storage size in bytes.
  [[nodiscard]] size_t GetStorageSizeBytes() const;

  /// \brief Clear all stored data.
  void Clear();

  /// \brief Flush changes to disk.
  /// Called automatically on destruction.
  void Flush();

  /// \brief Remove records older than retention period.
  /// \param retentionDays Number of days to retain data.
  void CleanupOldRecords(uint32_t retentionDays = kDefaultRetentionDays);

  /// \brief Iterate over all records.
  void ForEach(std::function<void(PersonalSpeedRecord const &)> const & fn) const;

private:
  /// \brief Load data from file.
  void Load();

  /// \brief Read and validate file header.
  /// \return true if header is valid, false otherwise.
  bool ReadHeader(std::ifstream & file);

  /// \brief Get record count from file size.
  /// \return Number of records, or 0 if file is invalid.
  size_t GetRecordCountFromFile(std::ifstream & file);

  /// \brief Read records from file into m_records.
  void ReadRecords(std::ifstream & file, size_t recordCount);

  /// \brief Save data to file.
  void Save() const;

  /// \brief Find record by key using binary search.
  /// \return Iterator to record, or end() if not found.
  std::vector<PersonalSpeedRecord>::iterator FindRecord(PersonalSpeedKey const & key);
  std::vector<PersonalSpeedRecord>::const_iterator FindRecord(PersonalSpeedKey const & key) const;

  std::string m_filePath;
  std::vector<PersonalSpeedRecord> m_records;  // Sorted by key
  mutable std::mutex m_mutex;
  bool m_isDirty = false;
};

/// \brief Get current day as days since Unix epoch.
uint32_t GetCurrentDaysSinceEpoch();

std::string DebugPrint(PersonalSpeedKey const & key);
std::string DebugPrint(PersonalSpeedRecord const & record);

}  // namespace traffic

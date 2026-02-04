#pragma once

#include <string>

namespace traffic
{
/// \brief Generate historical speed patterns section for MWM file.
///
/// This function reads speed patterns from an external data source and writes
/// them to the MWM file's HISTORICAL_SPEEDS_FILE_TAG section.
///
/// \param mwmPath Path to the MWM file to update.
/// \param speedPatternsPath Path to the speed patterns data file.
///        Format: CSV with columns: feature_id, segment_idx, direction (0/1),
///                followed by 168 speed percentages (one per hour per day of week).
/// \return true if successful, false otherwise.
bool GenerateHistoricalSpeedsFromCSV(std::string const & mwmPath,
                                      std::string const & speedPatternsPath);

/// \brief Generate historical speed patterns from Uber Movement format.
/// \param mwmPath Path to the MWM file to update.
/// \param uberMovementPath Path to Uber Movement data directory.
/// \param mappingPath Path to OSM-to-Uber segment mapping file.
/// \return true if successful, false otherwise.
bool GenerateHistoricalSpeedsFromUberMovement(std::string const & mwmPath,
                                               std::string const & uberMovementPath,
                                               std::string const & mappingPath);

/// \brief Generate synthetic historical speed patterns based on OSM road classification.
/// Useful for regions without real traffic data. Creates patterns based on:
/// - Road type (highway classification)
/// - Urban/rural context
/// - Time of day patterns (rush hour, nighttime, etc.)
/// \param mwmPath Path to the MWM file to update.
/// \return true if successful, false otherwise.
bool GenerateSyntheticHistoricalSpeeds(std::string const & mwmPath);

/// \brief Check if MWM file already has historical speeds section.
/// \param mwmPath Path to the MWM file.
/// \return true if section exists, false otherwise.
bool HasHistoricalSpeedsSection(std::string const & mwmPath);

}  // namespace traffic

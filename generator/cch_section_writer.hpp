// Library Documentation Verified: 2026-02-04
// Source: Internal Organic Maps APIs
// API Version: CCH v1 (kCCHVersion = 1)
#pragma once

#include "routing/cch_topology.hpp"

#include <string>

namespace generator
{

/// @brief Section tag for CCH topology in MWM file
constexpr char kCCHTopologySection[] = "CCH_TOPOLOGY";

/// @brief Writes CCH topology section to MWM file
class CCHSectionWriter
{
public:
  /// @brief Write CCH topology to MWM file
  /// @param mwmPath Path to MWM file
  /// @param topology CCH topology to write
  /// @return True if write succeeded
  static bool Write(std::string const & mwmPath,
                    routing::CCHTopology const & topology);
};

/// @brief Reads CCH topology section from MWM file
class CCHSectionReader
{
public:
  /// @brief Read CCH topology from MWM file
  /// @param mwmPath Path to MWM file
  /// @param topology Output topology
  /// @return True if read succeeded
  static bool Read(std::string const & mwmPath,
                   routing::CCHTopology & topology);

  /// @brief Check if MWM has CCH section
  /// @param mwmPath Path to MWM file
  /// @return True if CCH section exists
  static bool HasCCHSection(std::string const & mwmPath);
};

/// @brief Build CCH topology and write to MWM file
/// @param mwmPath Path to MWM file
/// @param countryName Country name for logging
/// @return True if build and write succeeded
bool BuildCCHSection(std::string const & mwmPath,
                     std::string const & countryName);

}  // namespace generator

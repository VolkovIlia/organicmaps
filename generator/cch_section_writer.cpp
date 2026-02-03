// Library Documentation Verified: 2026-02-04
// Source: Internal Organic Maps APIs
// API Version: CCH v1 (kCCHVersion = 1)
#include "generator/cch_section_writer.hpp"
#include "generator/cch_topology_builder.hpp"

#include "coding/files_container.hpp"
#include "coding/file_writer.hpp"
#include "coding/reader.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"
#include "base/timer.hpp"

#include <vector>

namespace generator
{

bool CCHSectionWriter::Write(std::string const & mwmPath,
                             routing::CCHTopology const & topology)
{
  LOG(LINFO, ("Writing CCH topology section to", mwmPath));

  if (!topology.IsValid())
  {
    LOG(LERROR, ("Invalid CCH topology, cannot write"));
    return false;
  }

  try
  {
    // Serialize topology to buffer
    std::vector<uint8_t> buffer;
    MemWriter<std::vector<uint8_t>> writer(buffer);
    topology.Serialize(writer);

    LOG(LINFO, ("CCH topology serialized:", buffer.size(), "bytes"));

    // Write to MWM file
    FilesContainerW container(mwmPath, FileWriter::OP_WRITE_EXISTING);
    container.Write(buffer, kCCHTopologySection);

    LOG(LINFO, ("CCH topology section written successfully"));
    return true;
  }
  catch (std::exception const & e)
  {
    LOG(LERROR, ("Failed to write CCH section:", e.what()));
    return false;
  }
}

bool CCHSectionReader::Read(std::string const & mwmPath,
                            routing::CCHTopology & topology)
{
  LOG(LINFO, ("Reading CCH topology section from", mwmPath));

  try
  {
    FilesContainerR container(mwmPath);

    if (!container.IsExist(kCCHTopologySection))
    {
      LOG(LWARNING, ("CCH section not found in", mwmPath));
      return false;
    }

    auto reader = container.GetReader(kCCHTopologySection);
    ReaderSource<decltype(reader)> source(reader);
    topology.Deserialize(source);

    if (!topology.IsValid())
    {
      LOG(LERROR, ("Invalid CCH topology read from", mwmPath));
      return false;
    }

    LOG(LINFO, ("CCH topology read:", topology.GetNodeCount(), "nodes,",
                topology.GetOriginalEdgeCount(), "edges,",
                topology.GetShortcutCount(), "shortcuts"));

    return true;
  }
  catch (std::exception const & e)
  {
    LOG(LERROR, ("Failed to read CCH section:", e.what()));
    return false;
  }
}

bool CCHSectionReader::HasCCHSection(std::string const & mwmPath)
{
  try
  {
    FilesContainerR container(mwmPath);
    return container.IsExist(kCCHTopologySection);
  }
  catch (std::exception const &)
  {
    return false;
  }
}

bool BuildCCHSection(std::string const & mwmPath,
                     std::string const & countryName)
{
  base::Timer timer;
  LOG(LINFO, ("Building CCH topology for", countryName, "from", mwmPath));

  // Extract road graph
  CCHRoadGraph graph = CCHTopologyBuilder::ExtractRoadGraph(mwmPath);

  if (graph.nodeCount == 0)
  {
    LOG(LWARNING, ("No road graph found in", mwmPath));
    return false;
  }

  // Build CCH topology
  CCHTopologyBuilder builder;
  routing::CCHTopology topology = builder.Build(graph);

  if (!topology.IsValid())
  {
    LOG(LERROR, ("Failed to build CCH topology for", countryName));
    return false;
  }

  // Write to MWM
  if (!CCHSectionWriter::Write(mwmPath, topology))
  {
    LOG(LERROR, ("Failed to write CCH section for", countryName));
    return false;
  }

  LOG(LINFO, ("CCH section built for", countryName, "in",
              timer.ElapsedSeconds(), "seconds"));
  LOG(LINFO, ("Statistics:", topology.GetNodeCount(), "nodes,",
              topology.GetOriginalEdgeCount(), "edges,",
              topology.GetShortcutCount(), "shortcuts,",
              topology.GetLevelCount(), "levels"));

  return true;
}

}  // namespace generator

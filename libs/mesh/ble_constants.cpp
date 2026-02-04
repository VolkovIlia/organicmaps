#include "mesh/ble_constants.hpp"

namespace mesh
{
std::string DebugPrint(ConnectionState state)
{
  switch (state)
  {
  case ConnectionState::Disconnected: return "Disconnected";
  case ConnectionState::Connecting: return "Connecting";
  case ConnectionState::Connected: return "Connected";
  case ConnectionState::Disconnecting: return "Disconnecting";
  }
  return "Unknown";
}
}  // namespace mesh

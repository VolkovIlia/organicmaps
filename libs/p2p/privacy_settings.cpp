#include "p2p/privacy_settings.hpp"

namespace p2p
{
std::string DebugPrint(ConsentLevel level)
{
  switch (level)
  {
  case ConsentLevel::Off: return "Off";
  case ConsentLevel::ViewOnly: return "ViewOnly";
  case ConsentLevel::Contribute: return "Contribute";
  }
  return "Unknown";
}
}  // namespace p2p

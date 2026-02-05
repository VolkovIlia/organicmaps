#include "P2PBridge.hpp"

#include "base/logging.hpp"

#include <span>

namespace p2p_bridge
{
std::optional<p2p::AggregatedTrafficShare> ParseReceivedMessage(
    uint8_t const * data, size_t size)
{
  if (!data || size == 0)
    return std::nullopt;

  std::span<uint8_t const> dataSpan(data, size);

  if (!mesh::BleProtocol::ValidateMessage(dataSpan))
    return std::nullopt;

  auto header = mesh::BleProtocol::DeserializeHeader(dataSpan);
  if (!header || header->type != mesh::MessageType::TrafficData)
    return std::nullopt;

  auto payload = mesh::BleProtocol::GetPayload(dataSpan);
  return p2p::TrafficShareSerializer::Deserialize(payload.data(), payload.size());
}

std::vector<uint8_t> CreateTrafficMessage(p2p::AggregatedTrafficShare const & share)
{
  auto payload = p2p::TrafficShareSerializer::Serialize(share);
  std::span<uint8_t const> payloadSpan(payload.data(), payload.size());
  return mesh::BleProtocol::CreateMessage(mesh::MessageType::TrafficData, payloadSpan);
}

bool CanShare(std::shared_ptr<p2p::PrivacyManager> const & manager)
{
  return manager && manager->CanShare();
}

bool CanReceive(std::shared_ptr<p2p::PrivacyManager> const & manager)
{
  return manager && manager->CanReceive();
}

}  // namespace p2p_bridge

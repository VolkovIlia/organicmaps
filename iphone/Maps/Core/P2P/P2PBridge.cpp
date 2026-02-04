#include "P2PBridge.hpp"

#include "base/logging.hpp"

namespace p2p_bridge
{
std::optional<p2p::AggregatedTrafficShare> ParseReceivedMessage(
    uint8_t const * data, size_t size)
{
  if (!data || size == 0)
    return std::nullopt;

  auto parsed = mesh::BleProtocol::ParseMessage(data, size);
  if (!parsed)
    return std::nullopt;

  if (parsed->m_type != mesh::MessageType::TrafficData)
    return std::nullopt;

  return p2p::TrafficShareSerializer::Deserialize(
      parsed->m_payload.data(), parsed->m_payload.size());
}

std::vector<uint8_t> CreateTrafficMessage(p2p::AggregatedTrafficShare const & share)
{
  auto payload = p2p::TrafficShareSerializer::Serialize(share);

  mesh::BleMessage message;
  message.m_type = mesh::MessageType::TrafficData;
  message.m_payload = std::move(payload);

  return mesh::BleProtocol::CreateMessage(message);
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

#include "mesh/ble_protocol.hpp"

#include "base/assert.hpp"

namespace mesh
{
std::string DebugPrint(MessageType type)
{
  switch (type)
  {
  case MessageType::TrafficData: return "TrafficData";
  case MessageType::GossipGradients: return "GossipGradients";
  case MessageType::DeviceInfo: return "DeviceInfo";
  case MessageType::Ack: return "Ack";
  }
  return "Unknown";
}

std::vector<uint8_t> BleProtocol::SerializeHeader(MessageHeader const & header)
{
  std::vector<uint8_t> result(4);
  result[0] = static_cast<uint8_t>(header.type);
  result[1] = header.version;
  result[2] = static_cast<uint8_t>(header.payloadLength & 0xFF);
  result[3] = static_cast<uint8_t>((header.payloadLength >> 8) & 0xFF);
  return result;
}

std::optional<MessageHeader> BleProtocol::DeserializeHeader(std::span<uint8_t const> data)
{
  if (data.size() < 4)
    return std::nullopt;

  MessageHeader header;
  header.type = static_cast<MessageType>(data[0]);
  header.version = data[1];
  header.payloadLength = static_cast<uint16_t>(data[2]) |
                         (static_cast<uint16_t>(data[3]) << 8);

  // Version check
  if (header.version != kProtocolVersion)
    return std::nullopt;

  return header;
}

std::vector<uint8_t> BleProtocol::CreateMessage(MessageType type,
                                                std::span<uint8_t const> payload)
{
  CHECK_LESS_OR_EQUAL(payload.size(), BleMessageSize::kMaxCharacteristicValue - 4,
                      ("Payload too large for BLE characteristic"));

  MessageHeader header;
  header.type = type;
  header.version = kProtocolVersion;
  header.payloadLength = static_cast<uint16_t>(payload.size());

  std::vector<uint8_t> message = SerializeHeader(header);
  message.insert(message.end(), payload.begin(), payload.end());
  return message;
}

bool BleProtocol::ValidateMessage(std::span<uint8_t const> data)
{
  auto header = DeserializeHeader(data);
  if (!header)
    return false;

  return data.size() == 4 + header->payloadLength;
}

std::span<uint8_t const> BleProtocol::GetPayload(std::span<uint8_t const> data)
{
  if (!ValidateMessage(data))
    return {};

  return data.subspan(4);
}
}  // namespace mesh

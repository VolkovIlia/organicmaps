#pragma once

#include "mesh/ble_constants.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mesh
{
/// \brief Message types for BLE protocol.
enum class MessageType : uint8_t
{
  TrafficData = 0x01,
  GossipGradients = 0x02,
  DeviceInfo = 0x03,
  Ack = 0x04
};

std::string DebugPrint(MessageType type);

/// \brief BLE message header.
struct MessageHeader
{
  MessageType type;
  uint8_t version;
  uint16_t payloadLength;
};

/// \brief BLE protocol for message serialization/deserialization.
class BleProtocol
{
public:
  static constexpr uint8_t kProtocolVersion = 1;

  /// \brief Serialize message header.
  /// \param header Message header to serialize.
  /// \return Serialized bytes.
  static std::vector<uint8_t> SerializeHeader(MessageHeader const & header);

  /// \brief Deserialize message header.
  /// \param data Raw bytes (at least 4 bytes required).
  /// \return Deserialized header or nullopt if invalid.
  static std::optional<MessageHeader> DeserializeHeader(std::span<uint8_t const> data);

  /// \brief Create a complete message with header and payload.
  /// \param type Message type.
  /// \param payload Message payload.
  /// \return Complete serialized message.
  static std::vector<uint8_t> CreateMessage(MessageType type,
                                            std::span<uint8_t const> payload);

  /// \brief Validate message integrity.
  /// \param data Complete message data.
  /// \return True if message is valid.
  static bool ValidateMessage(std::span<uint8_t const> data);

  /// \brief Get payload from a complete message.
  /// \param data Complete message data.
  /// \return Payload span or empty span if invalid.
  static std::span<uint8_t const> GetPayload(std::span<uint8_t const> data);
};
}  // namespace mesh

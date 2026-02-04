#include "testing/testing.hpp"

#include "mesh/ble_protocol.hpp"

#include <vector>

namespace mesh
{
UNIT_TEST(BleProtocol_SerializeDeserializeHeader)
{
  MessageHeader header;
  header.type = MessageType::TrafficData;
  header.version = BleProtocol::kProtocolVersion;
  header.payloadLength = 100;

  auto serialized = BleProtocol::SerializeHeader(header);
  TEST_EQUAL(serialized.size(), 4, ());

  auto deserialized = BleProtocol::DeserializeHeader(serialized);
  TEST(deserialized.has_value(), ());
  TEST_EQUAL(static_cast<uint8_t>(deserialized->type),
             static_cast<uint8_t>(MessageType::TrafficData), ());
  TEST_EQUAL(deserialized->version, BleProtocol::kProtocolVersion, ());
  TEST_EQUAL(deserialized->payloadLength, 100, ());
}

UNIT_TEST(BleProtocol_DeserializeHeader_TooShort)
{
  std::vector<uint8_t> data = {0x01, 0x01};  // Only 2 bytes
  auto header = BleProtocol::DeserializeHeader(data);
  TEST(!header.has_value(), ());
}

UNIT_TEST(BleProtocol_DeserializeHeader_WrongVersion)
{
  std::vector<uint8_t> data = {0x01, 0xFF, 0x00, 0x00};  // Wrong version
  auto header = BleProtocol::DeserializeHeader(data);
  TEST(!header.has_value(), ());
}

UNIT_TEST(BleProtocol_CreateMessage)
{
  std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
  auto message = BleProtocol::CreateMessage(MessageType::TrafficData, payload);

  TEST_EQUAL(message.size(), 4 + payload.size(), ());

  auto header = BleProtocol::DeserializeHeader(message);
  TEST(header.has_value(), ());
  TEST_EQUAL(static_cast<uint8_t>(header->type),
             static_cast<uint8_t>(MessageType::TrafficData), ());
  TEST_EQUAL(header->payloadLength, 4, ());
}

UNIT_TEST(BleProtocol_ValidateMessage_Valid)
{
  std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
  auto message = BleProtocol::CreateMessage(MessageType::GossipGradients, payload);
  TEST(BleProtocol::ValidateMessage(message), ());
}

UNIT_TEST(BleProtocol_ValidateMessage_Invalid)
{
  // Header says 10 bytes payload, but only 3 bytes follow
  std::vector<uint8_t> invalid = {0x01, 0x01, 0x0A, 0x00, 0x01, 0x02, 0x03};
  TEST(!BleProtocol::ValidateMessage(invalid), ());
}

UNIT_TEST(BleProtocol_GetPayload)
{
  std::vector<uint8_t> payload = {0xCA, 0xFE, 0xBA, 0xBE};
  auto message = BleProtocol::CreateMessage(MessageType::DeviceInfo, payload);

  auto extractedPayload = BleProtocol::GetPayload(message);
  TEST_EQUAL(extractedPayload.size(), payload.size(), ());
  for (size_t i = 0; i < payload.size(); ++i)
    TEST_EQUAL(extractedPayload[i], payload[i], ());
}

UNIT_TEST(BleProtocol_GetPayload_Invalid)
{
  std::vector<uint8_t> invalid = {0x01};  // Too short
  auto payload = BleProtocol::GetPayload(invalid);
  TEST(payload.empty(), ());
}

UNIT_TEST(BleProtocol_EmptyPayload)
{
  std::vector<uint8_t> emptyPayload;
  auto message = BleProtocol::CreateMessage(MessageType::Ack, emptyPayload);

  TEST_EQUAL(message.size(), 4, ());
  TEST(BleProtocol::ValidateMessage(message), ());

  auto extractedPayload = BleProtocol::GetPayload(message);
  TEST(extractedPayload.empty(), ());
}

UNIT_TEST(BleProtocol_MessageTypes)
{
  TEST_EQUAL(DebugPrint(MessageType::TrafficData), "TrafficData", ());
  TEST_EQUAL(DebugPrint(MessageType::GossipGradients), "GossipGradients", ());
  TEST_EQUAL(DebugPrint(MessageType::DeviceInfo), "DeviceInfo", ());
  TEST_EQUAL(DebugPrint(MessageType::Ack), "Ack", ());
}
}  // namespace mesh

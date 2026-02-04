#pragma once

#include "mesh/ble_protocol.hpp"
#include "p2p/aggregated_traffic_share.hpp"
#include "p2p/privacy_manager.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace p2p_bridge
{
/// Parse received BLE message and extract traffic data.
/// @param data Raw BLE message bytes.
/// @param size Size of the data buffer.
/// @return Parsed traffic share if valid, nullopt otherwise.
std::optional<p2p::AggregatedTrafficShare> ParseReceivedMessage(
    uint8_t const * data, size_t size);

/// Create BLE message from traffic share for transmission.
/// @param share Traffic share to serialize.
/// @return Serialized BLE message ready for transmission.
std::vector<uint8_t> CreateTrafficMessage(p2p::AggregatedTrafficShare const & share);

/// Check if sharing is currently allowed.
/// @param manager Privacy manager instance.
/// @return true if sharing is allowed by current consent.
bool CanShare(std::shared_ptr<p2p::PrivacyManager> const & manager);

/// Check if receiving is currently allowed.
/// @param manager Privacy manager instance.
/// @return true if receiving is allowed by current consent.
bool CanReceive(std::shared_ptr<p2p::PrivacyManager> const & manager);

}  // namespace p2p_bridge

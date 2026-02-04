#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "mesh/ble_protocol.hpp"
#include "p2p/privacy_manager.hpp"
#include "p2p/privacy_settings.hpp"

#include "base/logging.hpp"

#include <memory>
#include <mutex>

namespace
{
std::mutex g_mutex;
std::shared_ptr<p2p::PrivacyManager> g_privacyManager;

p2p::PrivacyManager & GetPrivacyManager()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_privacyManager)
    g_privacyManager = std::make_shared<p2p::PrivacyManager>();
  return *g_privacyManager;
}
}  // namespace

extern "C"
{
// P2PConsentManager JNI methods

JNIEXPORT void JNICALL Java_app_organicmaps_mesh_P2PConsentManager_nativeSetConsentLevel(JNIEnv * env, jclass,
                                                                                         jint level)
{
  if (level < 0 || level > 2)
  {
    LOG(LWARNING, ("Invalid consent level:", level));
    return;
  }

  auto consentLevel = static_cast<p2p::ConsentLevel>(level);
  GetPrivacyManager().SetConsentLevel(consentLevel);
  LOG(LINFO, ("P2P consent level set to:", DebugPrint(consentLevel)));
}

JNIEXPORT jint JNICALL Java_app_organicmaps_mesh_P2PConsentManager_nativeGetConsentLevel(JNIEnv * env, jclass)
{
  return static_cast<jint>(GetPrivacyManager().GetConsentLevel());
}

// P2PTrafficService JNI methods

JNIEXPORT void JNICALL Java_app_organicmaps_mesh_P2PTrafficService_nativeOnDataReceived(JNIEnv * env, jobject,
                                                                                        jbyteArray data,
                                                                                        jstring deviceAddress,
                                                                                        jint rssi)
{
  auto & privacyManager = GetPrivacyManager();

  // Check if receiving is allowed
  if (!privacyManager.CanReceive())
  {
    LOG(LDEBUG, ("P2P data received but receiving not allowed"));
    return;
  }

  // Get data array
  jsize dataLen = env->GetArrayLength(data);
  if (dataLen <= 0)
  {
    LOG(LWARNING, ("Empty P2P data received"));
    return;
  }

  std::vector<uint8_t> bytes(dataLen);
  env->GetByteArrayRegion(data, 0, dataLen, reinterpret_cast<jbyte *>(bytes.data()));

  // Get device address
  std::string address = jni::ToNativeString(env, deviceAddress);

  // Validate message structure
  if (!mesh::BleProtocol::ValidateMessage(bytes))
  {
    LOG(LWARNING, ("Invalid BLE message from:", address));
    return;
  }

  // Parse header
  auto header = mesh::BleProtocol::DeserializeHeader(bytes);
  if (!header)
  {
    LOG(LWARNING, ("Failed to parse BLE message header from:", address));
    return;
  }

  LOG(LINFO, ("Received P2P data from:", address, "rssi:", rssi, "type:", DebugPrint(header->type), "size:", dataLen));

  // Get payload
  auto payload = mesh::BleProtocol::GetPayload(bytes);
  if (payload.empty())
  {
    LOG(LDEBUG, ("Empty payload in BLE message"));
    return;
  }

  // Process based on message type
  switch (header->type)
  {
  case mesh::MessageType::TrafficData:
    // TODO: Process traffic data via TrafficCache
    LOG(LDEBUG, ("Traffic data received, payload size:", payload.size()));
    break;

  case mesh::MessageType::GossipGradients:
    // TODO: Process gossip gradients via GossipLearning
    LOG(LDEBUG, ("Gossip gradients received, payload size:", payload.size()));
    break;

  case mesh::MessageType::DeviceInfo: LOG(LDEBUG, ("Device info received from:", address)); break;

  case mesh::MessageType::Ack: LOG(LDEBUG, ("Ack received from:", address)); break;

  default: LOG(LWARNING, ("Unknown message type:", static_cast<int>(header->type))); break;
  }
}

}  // extern "C"

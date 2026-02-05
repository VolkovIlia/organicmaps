#import "MWMLocalTrafficManager.h"

#include "platform/settings.hpp"

namespace
{
// Settings key for learning enabled state
std::string const kLearningEnabledKey = "LocalTrafficLearningEnabled";
}  // namespace

@implementation MWMLocalTrafficManager

+ (BOOL)isLearningEnabled
{
  bool enabled = true;  // Default: enabled
  settings::Get(kLearningEnabledKey, enabled);
  return enabled ? YES : NO;
}

+ (void)setLearningEnabled:(BOOL)enabled
{
  settings::Set(kLearningEnabledKey, static_cast<bool>(enabled));
}

+ (void)clearDrivingHistory
{
  // TODO: Call native PersonalSpeedStorage::Clear() when JNI/bridge is ready
  // For now, this is a placeholder that will be connected to the native layer
  NSLog(@"[LocalTrafficManager] clearDrivingHistory called");
}

+ (NSInteger)recordCount
{
  // TODO: Connect to native PersonalSpeedStorage::GetRecordCount()
  return 0;
}

+ (NSInteger)storageSizeBytes
{
  // TODO: Connect to native PersonalSpeedStorage::GetStorageSizeBytes()
  return 0;
}

@end

#pragma once

#import <Foundation/Foundation.h>
#import <CoreBluetooth/CoreBluetooth.h>

#import "MWMP2PConsentManager.h"

NS_ASSUME_NONNULL_BEGIN

/// P2P Traffic Service state.
typedef NS_ENUM(NSInteger, MWMP2PServiceState) {
  MWMP2PServiceStateStopped = 0,
  MWMP2PServiceStateStarting,
  MWMP2PServiceStateRunning,
  MWMP2PServiceStateStopping
} NS_SWIFT_NAME(P2PServiceState);

/// Callback for received traffic data.
typedef void (^MWMP2PDataReceivedCallback)(NSData *data, NSString *deviceId, NSInteger rssi);

/// Callback for service state changes.
typedef void (^MWMP2PStateChangeCallback)(MWMP2PServiceState state);

/// P2P Traffic Service using CoreBluetooth for iOS.
/// Implements BLE peripheral (advertiser) and central (scanner) roles.
NS_SWIFT_NAME(P2PTrafficService)
@interface MWMP2PTrafficService : NSObject

/// Shared singleton instance.
@property (class, nonatomic, readonly) MWMP2PTrafficService *shared;

/// Current service state.
@property (nonatomic, readonly) MWMP2PServiceState state;

/// Whether Bluetooth is available and authorized.
@property (nonatomic, readonly) BOOL isBluetoothAvailable;

/// Number of discovered peers.
@property (nonatomic, readonly) NSUInteger discoveredPeerCount;

/// Start the P2P service (scanning and advertising).
/// Requires consent level >= ViewOnly.
- (void)start;

/// Stop the P2P service.
- (void)stop;

/// Broadcast traffic data to nearby peers.
/// @param data Traffic data to broadcast.
/// @return YES if broadcast was initiated.
- (BOOL)broadcastData:(NSData *)data;

/// Set callback for received traffic data.
/// @param callback Block to call when data is received.
- (void)setDataReceivedCallback:(nullable MWMP2PDataReceivedCallback)callback;

/// Set callback for service state changes.
/// @param stateCallback Block to call when state changes.
- (void)setStateChangeCallback:(nullable MWMP2PStateChangeCallback)stateCallback;

/// Request Bluetooth permissions if needed.
/// @param completion Called with authorization result.
- (void)requestBluetoothPermissionWithCompletion:(void (^)(BOOL authorized))completion;

@end

NS_ASSUME_NONNULL_END

#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Consent levels for P2P traffic sharing.
typedef NS_ENUM(NSInteger, MWMP2PConsentLevel) {
  MWMP2PConsentLevelOff = 0,       /// P2P disabled
  MWMP2PConsentLevelViewOnly = 1,  /// Receive only, no sharing
  MWMP2PConsentLevelContribute = 2 /// Full participation
} NS_SWIFT_NAME(P2PConsentLevel);

/// Callback for consent level changes.
typedef void (^MWMP2PConsentChangeCallback)(MWMP2PConsentLevel level);

/// Manages user consent for P2P traffic sharing.
/// Privacy-first: OFF by default, requires explicit opt-in.
NS_SWIFT_NAME(P2PConsentManager)
@interface MWMP2PConsentManager : NSObject

/// Shared singleton instance.
@property (class, nonatomic, readonly) MWMP2PConsentManager *shared;

/// Current consent level.
@property (nonatomic, readonly) MWMP2PConsentLevel consentLevel;

/// Whether P2P receiving is enabled.
@property (nonatomic, readonly) BOOL canReceive;

/// Whether P2P sharing is enabled.
@property (nonatomic, readonly) BOOL canShare;

/// Set consent level and sync with native layer.
/// @param level New consent level.
- (void)setConsentLevel:(MWMP2PConsentLevel)level;

/// Request consent from user if not already granted.
/// @param completion Called with result after user decision.
- (void)requestConsentWithCompletion:(void (^)(BOOL granted))completion;

/// Revoke consent and stop all P2P activity.
- (void)revokeConsent;

/// Add callback for consent level changes.
/// @param callback Block to call when consent changes.
- (void)addConsentChangeCallback:(MWMP2PConsentChangeCallback)callback;

/// Remove all consent change callbacks.
- (void)removeAllCallbacks;

#pragma mark - Debug Mode

/// Whether debug mode is enabled.
@property (nonatomic, readonly) BOOL isDebugMode;

/// Enable or disable debug mode.
/// @param enabled YES to enable debug mode.
- (void)setDebugMode:(BOOL)enabled;

#pragma mark - Exchange Tracking

/// Number of data exchanges in last 24 hours.
@property (nonatomic, readonly) NSInteger exchangeCount24h;

/// Increment exchange count. Call when data is exchanged with a peer.
- (void)incrementExchangeCount;

@end

NS_ASSUME_NONNULL_END

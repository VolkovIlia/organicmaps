#import "MWMP2PConsentManager.h"

#include "p2p/privacy_manager.hpp"
#include "p2p/privacy_settings.hpp"

#include "base/logging.hpp"

namespace
{
NSString * const kP2PConsentLevelKey = @"P2PConsentLevel";
NSString * const kP2PConsentTimestampKey = @"P2PConsentTimestamp";

p2p::ConsentLevel ToNativeLevel(MWMP2PConsentLevel level)
{
  switch (level)
  {
  case MWMP2PConsentLevelOff: return p2p::ConsentLevel::Off;
  case MWMP2PConsentLevelViewOnly: return p2p::ConsentLevel::ViewOnly;
  case MWMP2PConsentLevelContribute: return p2p::ConsentLevel::Contribute;
  }
  return p2p::ConsentLevel::Off;
}

MWMP2PConsentLevel FromNativeLevel(p2p::ConsentLevel level)
{
  switch (level)
  {
  case p2p::ConsentLevel::Off: return MWMP2PConsentLevelOff;
  case p2p::ConsentLevel::ViewOnly: return MWMP2PConsentLevelViewOnly;
  case p2p::ConsentLevel::Contribute: return MWMP2PConsentLevelContribute;
  }
  return MWMP2PConsentLevelOff;
}
}  // namespace

@interface MWMP2PConsentManager ()
{
  std::shared_ptr<p2p::PrivacyManager> m_privacyManager;
  NSMutableArray<MWMP2PConsentChangeCallback> *m_callbacks;
}
@end

@implementation MWMP2PConsentManager

+ (MWMP2PConsentManager *)shared
{
  static MWMP2PConsentManager *instance = nil;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    instance = [[MWMP2PConsentManager alloc] init];
  });
  return instance;
}

- (instancetype)init
{
  self = [super init];
  if (self)
  {
    m_callbacks = [NSMutableArray array];
    m_privacyManager = std::make_shared<p2p::PrivacyManager>();
    [self loadSavedConsent];
    [self setupNativeCallback];
  }
  return self;
}

- (void)loadSavedConsent
{
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  NSInteger savedLevel = [defaults integerForKey:kP2PConsentLevelKey];

  // Default is Off (privacy-first)
  MWMP2PConsentLevel level = (MWMP2PConsentLevel)savedLevel;
  m_privacyManager->SetConsentLevel(ToNativeLevel(level));

  LOG(LINFO, ("P2P consent loaded:", static_cast<int>(level)));
}

- (void)setupNativeCallback
{
  __weak typeof(self) weakSelf = self;
  m_privacyManager->RegisterConsentCallback([weakSelf](p2p::ConsentLevel level) {
    __strong typeof(weakSelf) strongSelf = weakSelf;
    if (!strongSelf)
      return;

    dispatch_async(dispatch_get_main_queue(), ^{
      [strongSelf notifyCallbacks:FromNativeLevel(level)];
    });
  });
}

- (MWMP2PConsentLevel)consentLevel
{
  return FromNativeLevel(m_privacyManager->GetConsentLevel());
}

- (BOOL)canReceive
{
  return m_privacyManager->CanReceive();
}

- (BOOL)canShare
{
  return m_privacyManager->CanShare();
}

- (void)setConsentLevel:(MWMP2PConsentLevel)level
{
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  [defaults setInteger:level forKey:kP2PConsentLevelKey];
  [defaults setDouble:[[NSDate date] timeIntervalSince1970] forKey:kP2PConsentTimestampKey];
  [defaults synchronize];

  m_privacyManager->SetConsentLevel(ToNativeLevel(level));

  LOG(LINFO, ("P2P consent set to:", static_cast<int>(level)));
}

- (void)requestConsentWithCompletion:(void (^)(BOOL))completion
{
  // Note: Actual UI should be shown by caller (Settings view controller)
  // This method is for programmatic consent flow
  if (self.consentLevel != MWMP2PConsentLevelOff)
  {
    if (completion)
      completion(YES);
    return;
  }

  // Return NO - consent not yet granted, UI needed
  if (completion)
    completion(NO);
}

- (void)revokeConsent
{
  [self setConsentLevel:MWMP2PConsentLevelOff];
  LOG(LINFO, ("P2P consent revoked"));
}

- (void)addConsentChangeCallback:(MWMP2PConsentChangeCallback)callback
{
  if (callback)
    [m_callbacks addObject:[callback copy]];
}

- (void)removeAllCallbacks
{
  [m_callbacks removeAllObjects];
}

- (void)notifyCallbacks:(MWMP2PConsentLevel)level
{
  for (MWMP2PConsentChangeCallback callback in m_callbacks)
    callback(level);
}

- (std::shared_ptr<p2p::PrivacyManager>)nativePrivacyManager
{
  return m_privacyManager;
}

@end

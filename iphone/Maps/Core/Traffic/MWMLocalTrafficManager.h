#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Manager for local traffic learning features.
/// Controls personal driving history collection and provides access
/// to personalized traffic predictions.
NS_SWIFT_NAME(LocalTrafficManager)
@interface MWMLocalTrafficManager : NSObject

/// Check if learning from driving is enabled.
/// When enabled, the app collects anonymous driving speed data
/// to improve future traffic predictions.
+ (BOOL)isLearningEnabled;

/// Enable or disable learning from driving.
/// @param enabled YES to enable learning, NO to disable.
+ (void)setLearningEnabled:(BOOL)enabled;

/// Clear all stored driving history data.
/// This permanently deletes personal speed observations.
+ (void)clearDrivingHistory;

/// Get the number of stored driving history records.
+ (NSInteger)recordCount;

/// Get the approximate storage size in bytes.
+ (NSInteger)storageSizeBytes;

@end

NS_ASSUME_NONNULL_END

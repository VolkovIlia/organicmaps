@class MWMRouterTransitStepInfo;

@interface MWMNavigationDashboardEntity : NSObject

@property(copy, nonatomic, readonly) NSArray<MWMRouterTransitStepInfo *> * transitSteps;
@property(copy, nonatomic, readonly) NSString * distanceToTurn;
@property(copy, nonatomic, readonly) NSString * streetName;
@property(copy, nonatomic, readonly) NSString * targetDistance;
@property(copy, nonatomic, readonly) NSString * targetUnits;
@property(copy, nonatomic, readonly) NSString * turnUnits;
@property(nonatomic, readonly) double speedLimitMps;
@property(nonatomic, readonly) CGFloat progress;
@property(nonatomic, readonly) NSUInteger roundExitNumber;
@property(nonatomic, readonly) NSUInteger timeToTarget;
@property(nonatomic, readonly) UIImage * nextTurnImage;
@property(nonatomic, readonly) UIImage * turnImage;

@property(nonatomic, readonly) NSString * arrival;

/// Traffic deviation info
/// @brief Usual route time in seconds, 0 if not available
@property(nonatomic, readonly) NSUInteger usualTimeToTarget;
/// @brief Traffic deviation type: 1 = faster, -1 = slower, 0 = normal
@property(nonatomic, readonly) NSInteger trafficDeviationType;

- (NSAttributedString *)estimate;

+ (NSAttributedString *)estimateDot;

+ (instancetype)new __attribute__((unavailable("init is not available")));

@end

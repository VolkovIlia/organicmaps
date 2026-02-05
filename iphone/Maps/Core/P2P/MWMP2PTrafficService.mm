#import "MWMP2PTrafficService.h"

#include "mesh/ble_constants.hpp"
#include "mesh/ble_protocol.hpp"
#include "p2p/privacy_settings.hpp"

#include "base/logging.hpp"

namespace
{
// Convert C++ service UUID to CBUUID
CBUUID *GetServiceUUID()
{
  NSString *uuidString = [NSString stringWithUTF8String:mesh::BleUuid::kServiceUuid];
  return [CBUUID UUIDWithString:uuidString];
}

CBUUID *GetCharacteristicUUID()
{
  NSString *uuidString = [NSString stringWithUTF8String:mesh::BleUuid::kTrafficDataUuid];
  return [CBUUID UUIDWithString:uuidString];
}
}  // namespace

@interface MWMP2PTrafficService () <CBCentralManagerDelegate, CBPeripheralManagerDelegate,
                                    CBPeripheralDelegate>
{
  CBCentralManager *m_centralManager;
  CBPeripheralManager *m_peripheralManager;
  CBMutableCharacteristic *m_trafficCharacteristic;
  NSMutableDictionary<NSUUID *, CBPeripheral *> *m_discoveredPeripherals;
  NSMutableSet<CBCentral *> *m_subscribedCentrals;

  MWMP2PDataReceivedCallback m_dataCallback;
  MWMP2PStateChangeCallback m_stateCallback;

  BOOL m_centralReady;
  BOOL m_peripheralReady;
  BOOL m_isStarting;
}
@end

@implementation MWMP2PTrafficService

+ (MWMP2PTrafficService *)shared
{
  static MWMP2PTrafficService *instance = nil;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    instance = [[MWMP2PTrafficService alloc] init];
  });
  return instance;
}

- (instancetype)init
{
  self = [super init];
  if (self)
  {
    m_discoveredPeripherals = [NSMutableDictionary dictionary];
    m_subscribedCentrals = [NSMutableSet set];
    _state = MWMP2PServiceStateStopped;
    m_centralReady = NO;
    m_peripheralReady = NO;
    m_isStarting = NO;

    [self setupBluetooth];
    [self observeConsentChanges];
  }
  return self;
}

- (void)setupBluetooth
{
  NSDictionary *centralOptions = @{
    CBCentralManagerOptionShowPowerAlertKey : @NO
  };
  m_centralManager = [[CBCentralManager alloc] initWithDelegate:self
                                                          queue:nil
                                                        options:centralOptions];

  NSDictionary *peripheralOptions = @{
    CBPeripheralManagerOptionShowPowerAlertKey : @NO
  };
  m_peripheralManager = [[CBPeripheralManager alloc] initWithDelegate:self
                                                                queue:nil
                                                              options:peripheralOptions];
}

- (void)observeConsentChanges
{
  __weak typeof(self) weakSelf = self;
  [[MWMP2PConsentManager shared]
      addConsentChangeCallback:^(MWMP2PConsentLevel level) {
        __strong typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf)
          return;

        if (level == MWMP2PConsentLevelOff)
          [strongSelf stop];
      }];
}

#pragma mark - Public Methods

- (BOOL)isBluetoothAvailable
{
  return m_centralManager.state == CBManagerStatePoweredOn;
}

- (NSUInteger)discoveredPeerCount
{
  return m_discoveredPeripherals.count;
}

- (void)start
{
  if (_state != MWMP2PServiceStateStopped)
    return;

  if ([MWMP2PConsentManager shared].consentLevel == MWMP2PConsentLevelOff)
  {
    LOG(LWARNING, ("Cannot start P2P: consent not granted"));
    return;
  }

  m_isStarting = YES;
  [self setState:MWMP2PServiceStateStarting];
  [self tryStartServices];
}

- (void)stop
{
  if (_state == MWMP2PServiceStateStopped)
    return;

  [self setState:MWMP2PServiceStateStopping];

  [self stopScanning];
  [self stopAdvertising];
  [self disconnectAllPeripherals];

  [self setState:MWMP2PServiceStateStopped];
  m_isStarting = NO;

  LOG(LINFO, ("P2P service stopped"));
}

- (BOOL)broadcastData:(NSData *)data
{
  if (_state != MWMP2PServiceStateRunning)
    return NO;

  if (![MWMP2PConsentManager shared].canShare)
    return NO;

  if (!m_trafficCharacteristic || m_subscribedCentrals.count == 0)
    return NO;

  BOOL success = [m_peripheralManager updateValue:data
                                forCharacteristic:m_trafficCharacteristic
                             onSubscribedCentrals:nil];

  if (success)
    LOG(LDEBUG, ("Broadcast data to", m_subscribedCentrals.count, "subscribers"));

  return success;
}

- (void)setDataReceivedCallback:(MWMP2PDataReceivedCallback)callback
{
  m_dataCallback = [callback copy];
}

- (void)setStateChangeCallback:(MWMP2PStateChangeCallback)stateCallback
{
  m_stateCallback = [stateCallback copy];
}

- (void)requestBluetoothPermissionWithCompletion:(void (^)(BOOL))completion
{
  CBManagerState state = m_centralManager.state;
  if (state == CBManagerStatePoweredOn)
  {
    if (completion)
      completion(YES);
    return;
  }

  if (state == CBManagerStateUnauthorized)
  {
    if (completion)
      completion(NO);
    return;
  }

  // State unknown/resetting - wait for delegate callback
  // For simplicity, return current availability
  if (completion)
    completion(state == CBManagerStatePoweredOn);
}

#pragma mark - Private Methods

- (void)setState:(MWMP2PServiceState)state
{
  if (_state == state)
    return;

  _state = state;
  if (m_stateCallback)
    m_stateCallback(state);
}

- (void)tryStartServices
{
  if (!m_isStarting)
    return;

  if (!m_centralReady || !m_peripheralReady)
    return;

  [self startScanning];
  [self startAdvertising];

  [self setState:MWMP2PServiceStateRunning];
  LOG(LINFO, ("P2P service started"));
}

- (void)startScanning
{
  if (m_centralManager.state != CBManagerStatePoweredOn)
    return;

  NSDictionary *options = @{CBCentralManagerScanOptionAllowDuplicatesKey : @NO};
  [m_centralManager scanForPeripheralsWithServices:@[ GetServiceUUID() ] options:options];
  LOG(LDEBUG, ("Started BLE scanning"));
}

- (void)stopScanning
{
  [m_centralManager stopScan];
}

- (void)startAdvertising
{
  if (m_peripheralManager.state != CBManagerStatePoweredOn)
    return;

  if (![MWMP2PConsentManager shared].canShare)
    return;

  [self setupGATTService];

  NSDictionary *advertisementData = @{
    CBAdvertisementDataServiceUUIDsKey : @[ GetServiceUUID() ],
    CBAdvertisementDataLocalNameKey : @"OM-P2P"
  };
  [m_peripheralManager startAdvertising:advertisementData];
  LOG(LDEBUG, ("Started BLE advertising"));
}

- (void)stopAdvertising
{
  [m_peripheralManager stopAdvertising];
  [m_peripheralManager removeAllServices];
  m_trafficCharacteristic = nil;
}

- (void)setupGATTService
{
  m_trafficCharacteristic = [[CBMutableCharacteristic alloc]
      initWithType:GetCharacteristicUUID()
        properties:CBCharacteristicPropertyNotify | CBCharacteristicPropertyRead
             value:nil
       permissions:CBAttributePermissionsReadable];

  CBMutableService *service =
      [[CBMutableService alloc] initWithType:GetServiceUUID() primary:YES];
  service.characteristics = @[ m_trafficCharacteristic ];

  [m_peripheralManager addService:service];
}

- (void)disconnectAllPeripherals
{
  for (CBPeripheral *peripheral in m_discoveredPeripherals.allValues)
  {
    if (peripheral.state == CBPeripheralStateConnected)
      [m_centralManager cancelPeripheralConnection:peripheral];
  }
  [m_discoveredPeripherals removeAllObjects];
  [m_subscribedCentrals removeAllObjects];
}

- (void)processReceivedData:(NSData *)data
                 fromDevice:(NSString *)deviceId
                       rssi:(NSInteger)rssi
{
  if (![MWMP2PConsentManager shared].canReceive)
    return;

  if (m_dataCallback)
    m_dataCallback(data, deviceId, rssi);
}

#pragma mark - CBCentralManagerDelegate

- (void)centralManagerDidUpdateState:(CBCentralManager *)central
{
  LOG(LINFO, ("Central manager state:", static_cast<int>(central.state)));

  m_centralReady = (central.state == CBManagerStatePoweredOn);

  if (m_centralReady && m_isStarting)
    [self tryStartServices];
  else if (!m_centralReady && _state == MWMP2PServiceStateRunning)
    [self stop];
}

- (void)centralManager:(CBCentralManager *)central
    didDiscoverPeripheral:(CBPeripheral *)peripheral
        advertisementData:(NSDictionary<NSString *, id> *)advertisementData
                     RSSI:(NSNumber *)RSSI
{
  NSUUID *identifier = peripheral.identifier;
  if (m_discoveredPeripherals[identifier])
    return;

  LOG(LDEBUG, ("Discovered peripheral:", peripheral.identifier.UUIDString.UTF8String));

  m_discoveredPeripherals[identifier] = peripheral;
  peripheral.delegate = self;

  [m_centralManager connectPeripheral:peripheral options:nil];
}

- (void)centralManager:(CBCentralManager *)central
    didConnectPeripheral:(CBPeripheral *)peripheral
{
  LOG(LDEBUG, ("Connected to:", peripheral.identifier.UUIDString.UTF8String));
  [peripheral discoverServices:@[ GetServiceUUID() ]];
}

- (void)centralManager:(CBCentralManager *)central
    didDisconnectPeripheral:(CBPeripheral *)peripheral
                      error:(NSError *)error
{
  [m_discoveredPeripherals removeObjectForKey:peripheral.identifier];
  LOG(LDEBUG, ("Disconnected from:", peripheral.identifier.UUIDString.UTF8String));
}

#pragma mark - CBPeripheralDelegate

- (void)peripheral:(CBPeripheral *)peripheral
    didDiscoverServices:(NSError *)error
{
  if (error)
    return;

  for (CBService *service in peripheral.services)
  {
    if ([service.UUID isEqual:GetServiceUUID()])
      [peripheral discoverCharacteristics:@[ GetCharacteristicUUID() ] forService:service];
  }
}

- (void)peripheral:(CBPeripheral *)peripheral
    didDiscoverCharacteristicsForService:(CBService *)service
                                   error:(NSError *)error
{
  if (error)
    return;

  for (CBCharacteristic *characteristic in service.characteristics)
  {
    if ([characteristic.UUID isEqual:GetCharacteristicUUID()])
      [peripheral setNotifyValue:YES forCharacteristic:characteristic];
  }
}

- (void)peripheral:(CBPeripheral *)peripheral
    didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic
                              error:(NSError *)error
{
  if (error || !characteristic.value)
    return;

  NSString *deviceId = peripheral.identifier.UUIDString;
  [self processReceivedData:characteristic.value fromDevice:deviceId rssi:-1];
}

#pragma mark - CBPeripheralManagerDelegate

- (void)peripheralManagerDidUpdateState:(CBPeripheralManager *)peripheral
{
  LOG(LINFO, ("Peripheral manager state:", static_cast<int>(peripheral.state)));

  m_peripheralReady = (peripheral.state == CBManagerStatePoweredOn);

  if (m_peripheralReady && m_isStarting)
    [self tryStartServices];
  else if (!m_peripheralReady && _state == MWMP2PServiceStateRunning)
    [self stop];
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral
            didAddService:(CBService *)service
                    error:(NSError *)error
{
  if (error)
    LOG(LWARNING, ("Failed to add service:", error.localizedDescription.UTF8String));
  else
    LOG(LDEBUG, ("GATT service added"));
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral
                  central:(CBCentral *)central
    didSubscribeToCharacteristic:(CBCharacteristic *)characteristic
{
  [m_subscribedCentrals addObject:central];
  LOG(LDEBUG, ("Central subscribed, total:", m_subscribedCentrals.count));
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral
                  central:(CBCentral *)central
    didUnsubscribeFromCharacteristic:(CBCharacteristic *)characteristic
{
  [m_subscribedCentrals removeObject:central];
  LOG(LDEBUG, ("Central unsubscribed, total:", m_subscribedCentrals.count));
}

@end

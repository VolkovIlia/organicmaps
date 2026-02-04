package app.organicmaps.mesh;

import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.AdvertiseCallback;
import android.bluetooth.le.AdvertiseData;
import android.bluetooth.le.AdvertiseSettings;
import android.bluetooth.le.BluetoothLeAdvertiser;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.content.Intent;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.ParcelUuid;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import app.organicmaps.sdk.util.log.Logger;

/**
 * Background service for P2P traffic data exchange via BLE.
 * Handles both advertising (sharing data) and scanning (receiving data).
 */
public class P2PTrafficService extends Service
{
  private static final String TAG = P2PTrafficService.class.getSimpleName();

  // Service UUID matching C++ BLE constants in mesh/ble_constants.hpp
  public static final UUID SERVICE_UUID =
      UUID.fromString("0000FF01-0000-1000-8000-00805F9B34FB");

  private BluetoothManager mBluetoothManager;
  private BluetoothAdapter mBluetoothAdapter;
  private BluetoothLeScanner mScanner;
  private BluetoothLeAdvertiser mAdvertiser;

  private boolean mIsScanning = false;
  private boolean mIsAdvertising = false;

  // Track discovered peers for status display
  private final Set<String> mDiscoveredPeers = new HashSet<>();

  private final Handler mHandler = new Handler(Looper.getMainLooper());
  private final IBinder mBinder = new LocalBinder();

  public class LocalBinder extends Binder
  {
    public P2PTrafficService getService()
    {
      return P2PTrafficService.this;
    }
  }

  @Override
  public void onCreate()
  {
    super.onCreate();
    sInstance = this;
    initBluetooth();
    Logger.i(TAG, "P2PTrafficService created");
  }

  @Override
  public int onStartCommand(Intent intent, int flags, int startId)
  {
    Logger.i(TAG, "P2PTrafficService started");
    return START_STICKY;
  }

  @Nullable
  @Override
  public IBinder onBind(Intent intent)
  {
    return mBinder;
  }

  @Override
  public void onDestroy()
  {
    stopScanning();
    stopAdvertising();
    Logger.i(TAG, "P2PTrafficService destroyed");
    super.onDestroy();
  }

  private void initBluetooth()
  {
    mBluetoothManager = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
    if (mBluetoothManager == null)
    {
      Logger.e(TAG, "BluetoothManager not available");
      return;
    }

    mBluetoothAdapter = mBluetoothManager.getAdapter();
    if (mBluetoothAdapter == null)
    {
      Logger.e(TAG, "BluetoothAdapter not available");
      return;
    }

    mScanner = mBluetoothAdapter.getBluetoothLeScanner();
    mAdvertiser = mBluetoothAdapter.getBluetoothLeAdvertiser();

    if (mScanner == null)
      Logger.w(TAG, "BLE Scanner not available");
    if (mAdvertiser == null)
      Logger.w(TAG, "BLE Advertiser not available");
  }

  public boolean isBluetoothEnabled()
  {
    return mBluetoothAdapter != null && mBluetoothAdapter.isEnabled();
  }

  public boolean isBluetoothSupported()
  {
    return mBluetoothAdapter != null;
  }

  public boolean isBleSupported()
  {
    return mScanner != null || mAdvertiser != null;
  }

  public boolean startScanning()
  {
    if (mScanner == null)
    {
      Logger.e(TAG, "Cannot start scanning: Scanner not available");
      return false;
    }

    if (mIsScanning)
    {
      Logger.w(TAG, "Scanning already in progress");
      return true;
    }

    ScanFilter filter = new ScanFilter.Builder()
        .setServiceUuid(new ParcelUuid(SERVICE_UUID))
        .build();

    List<ScanFilter> filters = new ArrayList<>();
    filters.add(filter);

    ScanSettings settings = new ScanSettings.Builder()
        .setScanMode(ScanSettings.SCAN_MODE_LOW_POWER)
        .setReportDelay(1000)
        .build();

    try
    {
      mScanner.startScan(filters, settings, mScanCallback);
      mIsScanning = true;
      Logger.i(TAG, "BLE scanning started");
      return true;
    }
    catch (SecurityException e)
    {
      Logger.e(TAG, "BLE scan permission denied: " + e.getMessage());
      return false;
    }
    catch (IllegalStateException e)
    {
      Logger.e(TAG, "BLE scan failed (Bluetooth off?): " + e.getMessage());
      return false;
    }
  }

  public void stopScanning()
  {
    if (mScanner == null || !mIsScanning)
      return;

    try
    {
      mScanner.stopScan(mScanCallback);
      Logger.i(TAG, "BLE scanning stopped");
    }
    catch (SecurityException e)
    {
      Logger.e(TAG, "Error stopping scan: " + e.getMessage());
    }
    catch (IllegalStateException e)
    {
      Logger.w(TAG, "Scan stop failed (already stopped?): " + e.getMessage());
    }
    finally
    {
      mIsScanning = false;
    }
  }

  public boolean startAdvertising(@NonNull byte[] data)
  {
    if (mAdvertiser == null)
    {
      Logger.e(TAG, "Cannot start advertising: Advertiser not available");
      return false;
    }

    if (mIsAdvertising)
    {
      Logger.w(TAG, "Advertising already in progress");
      return true;
    }

    if (data.length > 20)
    {
      Logger.e(TAG, "Advertisement data too large: " + data.length + " bytes (max 20)");
      return false;
    }

    AdvertiseSettings settings = new AdvertiseSettings.Builder()
        .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_POWER)
        .setConnectable(false)
        .setTimeout(0)
        .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_LOW)
        .build();

    AdvertiseData advertiseData = new AdvertiseData.Builder()
        .setIncludeDeviceName(false)
        .setIncludeTxPowerLevel(false)
        .addServiceUuid(new ParcelUuid(SERVICE_UUID))
        .addServiceData(new ParcelUuid(SERVICE_UUID), data)
        .build();

    try
    {
      mAdvertiser.startAdvertising(settings, advertiseData, mAdvertiseCallback);
      mIsAdvertising = true;
      Logger.i(TAG, "BLE advertising started with " + data.length + " bytes");
      return true;
    }
    catch (SecurityException e)
    {
      Logger.e(TAG, "BLE advertise permission denied: " + e.getMessage());
      return false;
    }
    catch (IllegalStateException e)
    {
      Logger.e(TAG, "BLE advertise failed (Bluetooth off?): " + e.getMessage());
      return false;
    }
  }

  public void stopAdvertising()
  {
    if (mAdvertiser == null || !mIsAdvertising)
      return;

    try
    {
      mAdvertiser.stopAdvertising(mAdvertiseCallback);
      Logger.i(TAG, "BLE advertising stopped");
    }
    catch (SecurityException e)
    {
      Logger.e(TAG, "Error stopping advertising: " + e.getMessage());
    }
    catch (IllegalStateException e)
    {
      Logger.w(TAG, "Advertise stop failed (already stopped?): " + e.getMessage());
    }
    finally
    {
      mIsAdvertising = false;
    }
  }

  public boolean isScanning()
  {
    return mIsScanning;
  }

  public boolean isAdvertising()
  {
    return mIsAdvertising;
  }

  // Native method to process received BLE data
  private native void nativeOnDataReceived(byte[] data, String deviceAddress, int rssi);

  private final ScanCallback mScanCallback = new ScanCallback()
  {
    @Override
    public void onScanResult(int callbackType, ScanResult result)
    {
      processScanResult(result);
    }

    @Override
    public void onBatchScanResults(List<ScanResult> results)
    {
      for (ScanResult result : results)
        processScanResult(result);
    }

    @Override
    public void onScanFailed(int errorCode)
    {
      Logger.e(TAG, "BLE scan failed with error code: " + errorCode);
      mIsScanning = false;
    }
  };

  private void processScanResult(@NonNull ScanResult result)
  {
    if (result.getScanRecord() == null)
      return;

    byte[] serviceData = result.getScanRecord().getServiceData(new ParcelUuid(SERVICE_UUID));
    if (serviceData == null || serviceData.length == 0)
      return;

    String address = result.getDevice().getAddress();
    int rssi = result.getRssi();

    // Track discovered peers
    mDiscoveredPeers.add(address);
    mDiscoveredPeerCount = mDiscoveredPeers.size();

    Logger.d(TAG, "Received P2P data from " + address + " rssi=" + rssi +
             " bytes=" + serviceData.length + " peers=" + mDiscoveredPeerCount);

    nativeOnDataReceived(serviceData, address, rssi);
  }

  private final AdvertiseCallback mAdvertiseCallback = new AdvertiseCallback()
  {
    @Override
    public void onStartSuccess(AdvertiseSettings settingsInEffect)
    {
      Logger.i(TAG, "BLE advertising started successfully");
    }

    @Override
    public void onStartFailure(int errorCode)
    {
      String errorMsg = getAdvertiseErrorMessage(errorCode);
      Logger.e(TAG, "BLE advertise failed: " + errorMsg + " (code " + errorCode + ")");
      mIsAdvertising = false;
    }
  };

  private static String getAdvertiseErrorMessage(int errorCode)
  {
    switch (errorCode)
    {
      case AdvertiseCallback.ADVERTISE_FAILED_DATA_TOO_LARGE:
        return "Data too large";
      case AdvertiseCallback.ADVERTISE_FAILED_TOO_MANY_ADVERTISERS:
        return "Too many advertisers";
      case AdvertiseCallback.ADVERTISE_FAILED_ALREADY_STARTED:
        return "Already started";
      case AdvertiseCallback.ADVERTISE_FAILED_INTERNAL_ERROR:
        return "Internal error";
      case AdvertiseCallback.ADVERTISE_FAILED_FEATURE_UNSUPPORTED:
        return "Feature unsupported";
      default:
        return "Unknown error";
    }
  }

  // Static reference for settings access
  private static P2PTrafficService sInstance;
  private int mDiscoveredPeerCount = 0;

  /**
   * Stop P2P service.
   * @param context Application context.
   */
  public static void stopService(@NonNull Context context)
  {
    context.stopService(new Intent(context, P2PTrafficService.class));
    Logger.i(TAG, "P2P service stop requested");
  }

  /**
   * Start P2P service.
   * @param context Application context.
   */
  public static void startService(@NonNull Context context)
  {
    context.startService(new Intent(context, P2PTrafficService.class));
    Logger.i(TAG, "P2P service start requested");
  }

  /**
   * Get count of discovered peers.
   * @param context Application context.
   * @return Number of discovered peers.
   */
  public static int getDiscoveredPeerCount(@NonNull Context context)
  {
    return sInstance != null ? sInstance.mDiscoveredPeerCount : 0;
  }

  static
  {
    System.loadLibrary("organicmaps");
  }
}

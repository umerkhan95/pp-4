import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../models/app_models.dart';
import '../models/device_models.dart';
import '../services/ble_service.dart';
import '../services/api_service.dart';
import '../services/storage_service.dart';
import '../utils/app_constants.dart';

// Theme Mode Provider
final themeModeProvider = StateNotifierProvider<ThemeModeNotifier, ThemeMode>((ref) {
  return ThemeModeNotifier();
});

class ThemeModeNotifier extends StateNotifier<ThemeMode> {
  ThemeModeNotifier() : super(ThemeMode.system) {
    _loadThemeMode();
  }

  void _loadThemeMode() async {
    final savedMode = await StorageService.getThemeMode();
    state = savedMode;
  }

  void setThemeMode(ThemeMode mode) async {
    state = mode;
    await StorageService.setThemeMode(mode);
  }
}

// App State Provider
final appStateProvider = StateNotifierProvider<AppStateNotifier, AppState>((ref) {
  return AppStateNotifier();
});

class AppStateNotifier extends StateNotifier<AppState> {
  AppStateNotifier() : super(AppState.initial()) {
    _initialize();
  }

  void _initialize() async {
    // Load saved app state
    final savedState = await StorageService.getAppState();
    if (savedState != null) {
      state = savedState;
    }
  }

  void setCurrentScreen(AppScreen screen) {
    state = state.copyWith(currentScreen: screen);
    _saveState();
  }

  void setConnectedDevice(PP4Device? device) {
    state = state.copyWith(connectedDevice: device);
    _saveState();
  }

  void setConnectionStatus(ConnectionStatus status) {
    state = state.copyWith(connectionStatus: status);
    _saveState();
  }

  void _saveState() async {
    await StorageService.setAppState(state);
  }
}

// BLE Service Provider
final bleServiceProvider = Provider<BLEService>((ref) {
  return BLEService();
});

// API Service Provider
final apiServiceProvider = Provider<APIService>((ref) {
  return APIService();
});

// Device Discovery Provider
final deviceDiscoveryProvider = StateNotifierProvider<DeviceDiscoveryNotifier, AsyncValue<List<PP4Device>>>((ref) {
  final bleService = ref.watch(bleServiceProvider);
  return DeviceDiscoveryNotifier(bleService);
});

class DeviceDiscoveryNotifier extends StateNotifier<AsyncValue<List<PP4Device>>> {
  final BLEService _bleService;
  
  DeviceDiscoveryNotifier(this._bleService) : super(const AsyncValue.loading()) {
    _startDiscovery();
  }

  void _startDiscovery() async {
    try {
      state = const AsyncValue.loading();
      final devices = await _bleService.scanForDevices();
      state = AsyncValue.data(devices);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  void refreshDiscovery() {
    _startDiscovery();
  }

  void stopDiscovery() {
    _bleService.stopScan();
  }
}

// Device Connection Provider
final deviceConnectionProvider = StateNotifierProvider<DeviceConnectionNotifier, AsyncValue<PP4Device?>>((ref) {
  final bleService = ref.watch(bleServiceProvider);
  return DeviceConnectionNotifier(bleService);
});

class DeviceConnectionNotifier extends StateNotifier<AsyncValue<PP4Device?>> {
  final BLEService _bleService;
  
  DeviceConnectionNotifier(this._bleService) : super(const AsyncValue.data(null));

  Future<void> connectToDevice(PP4Device device) async {
    try {
      state = const AsyncValue.loading();
      final connectedDevice = await _bleService.connectToDevice(device);
      state = AsyncValue.data(connectedDevice);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<void> disconnectDevice() async {
    try {
      await _bleService.disconnect();
      state = const AsyncValue.data(null);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<void> sendWiFiCredentials(String ssid, String password) async {
    try {
      await _bleService.sendWiFiCredentials(ssid, password);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }
}

// Device Status Provider
final deviceStatusProvider = StateNotifierProvider<DeviceStatusNotifier, AsyncValue<DeviceStatus>>((ref) {
  final apiService = ref.watch(apiServiceProvider);
  final appState = ref.watch(appStateProvider);
  return DeviceStatusNotifier(apiService, appState.connectedDevice?.id);
});

class DeviceStatusNotifier extends StateNotifier<AsyncValue<DeviceStatus>> {
  final APIService _apiService;
  final String? _deviceId;
  
  DeviceStatusNotifier(this._apiService, this._deviceId) : super(const AsyncValue.loading()) {
    if (_deviceId != null) {
      _loadDeviceStatus();
    }
  }

  void _loadDeviceStatus() async {
    if (_deviceId == null) return;
    
    try {
      state = const AsyncValue.loading();
      final status = await _apiService.getDeviceStatus(_deviceId!);
      state = AsyncValue.data(status);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<void> refreshStatus() async {
    _loadDeviceStatus();
  }

  Future<void> updatePumpStatus(int pumpId, bool isOn) async {
    if (_deviceId == null) return;
    
    try {
      await _apiService.controlPump(_deviceId!, pumpId, isOn);
      _loadDeviceStatus(); // Refresh after update
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }
}

// Sprinkler Schedule Provider
final sprinklerScheduleProvider = StateNotifierProvider<SprinklerScheduleNotifier, AsyncValue<SprinklerSchedule>>((ref) {
  final apiService = ref.watch(apiServiceProvider);
  final appState = ref.watch(appStateProvider);
  return SprinklerScheduleNotifier(apiService, appState.connectedDevice?.id);
});

class SprinklerScheduleNotifier extends StateNotifier<AsyncValue<SprinklerSchedule>> {
  final APIService _apiService;
  final String? _deviceId;
  
  SprinklerScheduleNotifier(this._apiService, this._deviceId) : super(const AsyncValue.loading()) {
    if (_deviceId != null) {
      _loadSchedule();
    }
  }

  void _loadSchedule() async {
    if (_deviceId == null) return;
    
    try {
      state = const AsyncValue.loading();
      final schedule = await _apiService.getSprinklerSchedule(_deviceId!);
      state = AsyncValue.data(schedule);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<void> updateSchedule(SprinklerSchedule schedule) async {
    if (_deviceId == null) return;
    
    try {
      await _apiService.setSprinklerSchedule(_deviceId!, schedule);
      state = AsyncValue.data(schedule);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<void> refreshSchedule() async {
    _loadSchedule();
  }
}

// Pet Settings Provider
final petSettingsProvider = StateNotifierProvider<PetSettingsNotifier, AsyncValue<PetSettings>>((ref) {
  final apiService = ref.watch(apiServiceProvider);
  return PetSettingsNotifier(apiService);
});

class PetSettingsNotifier extends StateNotifier<AsyncValue<PetSettings>> {
  final APIService _apiService;
  
  PetSettingsNotifier(this._apiService) : super(const AsyncValue.loading()) {
    _loadSettings();
  }

  void _loadSettings() async {
    try {
      state = const AsyncValue.loading();
      final settings = await _apiService.getPetSettings();
      state = AsyncValue.data(settings);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<void> updateDogSize(DogSize size) async {
    try {
      await _apiService.updateDogSize(size);
      final currentSettings = state.value;
      if (currentSettings != null) {
        state = AsyncValue.data(currentSettings.copyWith(dogSize: size));
      }
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<void> refreshSettings() async {
    _loadSettings();
  }
}

// User Authentication Provider
final authProvider = StateNotifierProvider<AuthNotifier, AsyncValue<User?>>((ref) {
  final apiService = ref.watch(apiServiceProvider);
  return AuthNotifier(apiService);
});

class AuthNotifier extends StateNotifier<AsyncValue<User?>> {
  final APIService _apiService;
  
  AuthNotifier(this._apiService) : super(const AsyncValue.loading()) {
    _checkAuthStatus();
  }

  void _checkAuthStatus() async {
    try {
      final token = await StorageService.getAuthToken();
      if (token != null) {
        final user = await _apiService.validateToken(token);
        state = AsyncValue.data(user);
      } else {
        state = const AsyncValue.data(null);
      }
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<void> login(String email, String password) async {
    try {
      state = const AsyncValue.loading();
      final user = await _apiService.login(email, password);
      await StorageService.setAuthToken(user.token);
      state = AsyncValue.data(user);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<void> logout() async {
    try {
      await StorageService.clearAuthToken();
      state = const AsyncValue.data(null);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<void> register(String email, String password, String name) async {
    try {
      state = const AsyncValue.loading();
      final user = await _apiService.register(email, password, name);
      await StorageService.setAuthToken(user.token);
      state = AsyncValue.data(user);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }
}

// Global Error Provider
final globalErrorProvider = StateProvider<AsyncValue<void>>((ref) {
  return const AsyncValue.data(null);
});

// Notification Provider
final notificationProvider = StateNotifierProvider<NotificationNotifier, List<AppNotification>>((ref) {
  return NotificationNotifier();
});

class NotificationNotifier extends StateNotifier<List<AppNotification>> {
  NotificationNotifier() : super([]);

  void addNotification(AppNotification notification) {
    state = [...state, notification];
    
    // Auto-remove after 5 seconds for info notifications
    if (notification.type == NotificationType.info) {
      Future.delayed(const Duration(seconds: 5), () {
        removeNotification(notification.id);
      });
    }
  }

  void removeNotification(String id) {
    state = state.where((notification) => notification.id != id).toList();
  }

  void clearAll() {
    state = [];
  }
}

// Device Telemetry Provider (Real-time data)
final deviceTelemetryProvider = StreamProvider.family<DeviceTelemetry, String>((ref, deviceId) {
  final apiService = ref.watch(apiServiceProvider);
  return apiService.getDeviceTelemetryStream(deviceId);
});

// OTA Update Provider
final otaUpdateProvider = StateNotifierProvider<OTAUpdateNotifier, AsyncValue<OTAUpdateInfo?>>((ref) {
  final apiService = ref.watch(apiServiceProvider);
  return OTAUpdateNotifier(apiService);
});

class OTAUpdateNotifier extends StateNotifier<AsyncValue<OTAUpdateInfo?>> {
  final APIService _apiService;
  
  OTAUpdateNotifier(this._apiService) : super(const AsyncValue.data(null));

  Future<void> checkForUpdates(String deviceId) async {
    try {
      state = const AsyncValue.loading();
      final updateInfo = await _apiService.checkForOTAUpdates(deviceId);
      state = AsyncValue.data(updateInfo);
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<void> startUpdate(String deviceId) async {
    try {
      await _apiService.startOTAUpdate(deviceId);
      // Update will be handled by device telemetry stream
    } catch (error, stackTrace) {
      state = AsyncValue.error(error, stackTrace);
    }
  }
}

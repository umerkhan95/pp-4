// =====================================================
// PP4 Smart Pet Care - Flutter BLE Integration
// WiFi Credential Setup via Bluetooth Low Energy
// =====================================================

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

// =====================================================
// BLE Service and Characteristic UUIDs (Match Arduino)
// =====================================================
class PP4BleConstants {
  static const String serviceUuid = "e4f09000-2d00-4625-988c-bc09a4963c44";
  static const String wifiStatusCharUuid = "e4f09000-2d01-4625-988c-bc09a4963c44";
  static const String ssidResponseCharUuid = "e4f09000-2d02-4625-988c-bc09a4963c44";
  static const String ssidRequestCharUuid = "e4f09000-2d03-4625-988c-bc09a4963c44";
  
  static const String deviceNamePrefix = "Porch-Potty-BLE-";
}

// =====================================================
// WiFi Network Model
// =====================================================
class WiFiNetwork {
  final String ssid;
  final int signalStrength;
  final bool isSecured;

  WiFiNetwork({
    required this.ssid,
    this.signalStrength = -50,
    this.isSecured = true,
  });

  @override
  String toString() => ssid;
}

// =====================================================
// BLE Connection States
// =====================================================
enum BleConnectionState {
  disconnected,
  scanning,
  connecting,
  connected,
  configuring,
  completed,
  error
}

// =====================================================
// Main BLE Manager Class
// =====================================================
class PP4BleManager {
  static final PP4BleManager _instance = PP4BleManager._internal();
  factory PP4BleManager() => _instance;
  PP4BleManager._internal();

  // BLE Objects
  BluetoothDevice? _connectedDevice;
  BluetoothCharacteristic? _wifiStatusChar;
  BluetoothCharacteristic? _ssidResponseChar;
  BluetoothCharacteristic? _ssidRequestChar;

  // State Management
  final StreamController<BleConnectionState> _stateController = 
      StreamController<BleConnectionState>.broadcast();
  final StreamController<List<WiFiNetwork>> _wifiNetworksController = 
      StreamController<List<WiFiNetwork>>.broadcast();
  final StreamController<String> _statusController = 
      StreamController<String>.broadcast();

  BleConnectionState _currentState = BleConnectionState.disconnected;
  List<WiFiNetwork> _availableNetworks = [];

  // Getters
  Stream<BleConnectionState> get stateStream => _stateController.stream;
  Stream<List<WiFiNetwork>> get wifiNetworksStream => _wifiNetworksController.stream;
  Stream<String> get statusStream => _statusController.stream;
  BleConnectionState get currentState => _currentState;
  List<WiFiNetwork> get availableNetworks => _availableNetworks;
  bool get isConnected => _currentState == BleConnectionState.connected;

  // =====================================================
  // Initialize BLE and Request Permissions
  // =====================================================
  Future<bool> initialize() async {
    try {
      // Request permissions
      Map<Permission, PermissionStatus> permissions = await [
        Permission.bluetooth,
        Permission.bluetoothScan,
        Permission.bluetoothConnect,
        Permission.location,
      ].request();

      bool allGranted = permissions.values.every(
        (status) => status == PermissionStatus.granted
      );

      if (!allGranted) {
        _updateStatus("Bluetooth permissions required");
        return false;
      }

      // Check if Bluetooth is available
      if (await FlutterBluePlus.isAvailable == false) {
        _updateStatus("Bluetooth not available");
        return false;
      }

      // Check if Bluetooth is on
      if (await FlutterBluePlus.adapterState.first != BluetoothAdapterState.on) {
        _updateStatus("Please turn on Bluetooth");
        return false;
      }

      _updateStatus("BLE initialized successfully");
      return true;
    } catch (e) {
      _updateStatus("BLE initialization failed: $e");
      return false;
    }
  }

  // =====================================================
  // Scan for PP4 Devices
  // =====================================================
  Future<void> scanForDevices() async {
    try {
      _updateState(BleConnectionState.scanning);
      _updateStatus("Scanning for PP4 devices...");

      // Stop any ongoing scan
      await FlutterBluePlus.stopScan();
      
      // Start scanning
      await FlutterBluePlus.startScan(
        timeout: const Duration(seconds: 10),
        withNames: [PP4BleConstants.deviceNamePrefix],
      );

      // Listen for scan results
      FlutterBluePlus.scanResults.listen((results) {
        for (ScanResult result in results) {
          if (result.device.platformName.startsWith(PP4BleConstants.deviceNamePrefix)) {
            _updateStatus("Found PP4 device: ${result.device.platformName}");
            connectToDevice(result.device);
            break;
          }
        }
      });

      // Handle scan completion
      await Future.delayed(const Duration(seconds: 10));
      if (_currentState == BleConnectionState.scanning) {
        _updateState(BleConnectionState.disconnected);
        _updateStatus("No PP4 devices found");
      }
    } catch (e) {
      _updateState(BleConnectionState.error);
      _updateStatus("Scan failed: $e");
    }
  }

  // =====================================================
  // Connect to Specific Device
  // =====================================================
  Future<void> connectToDevice(BluetoothDevice device) async {
    try {
      _updateState(BleConnectionState.connecting);
      _updateStatus("Connecting to ${device.platformName}...");

      // Stop scanning
      await FlutterBluePlus.stopScan();

      // Connect to device
      await device.connect(timeout: const Duration(seconds: 15));
      _connectedDevice = device;

      // Discover services
      List<BluetoothService> services = await device.discoverServices();
      
      // Find PP4 service
      BluetoothService? pp4Service;
      for (BluetoothService service in services) {
        if (service.uuid.toString().toLowerCase() == 
            PP4BleConstants.serviceUuid.toLowerCase()) {
          pp4Service = service;
          break;
        }
      }

      if (pp4Service == null) {
        throw Exception("PP4 service not found");
      }

      // Get characteristics
      for (BluetoothCharacteristic char in pp4Service.characteristics) {
        String charUuid = char.uuid.toString().toLowerCase();
        
        if (charUuid == PP4BleConstants.wifiStatusCharUuid.toLowerCase()) {
          _wifiStatusChar = char;
          await char.setNotifyValue(true);
          char.lastValueStream.listen(_handleWifiStatusUpdate);
        } else if (charUuid == PP4BleConstants.ssidResponseCharUuid.toLowerCase()) {
          _ssidResponseChar = char;
          await char.setNotifyValue(true);
          char.lastValueStream.listen(_handleSsidResponse);
        } else if (charUuid == PP4BleConstants.ssidRequestCharUuid.toLowerCase()) {
          _ssidRequestChar = char;
        }
      }

      _updateState(BleConnectionState.connected);
      _updateStatus("Connected to ${device.platformName}");

      // Request WiFi networks
      await requestWifiNetworks();

    } catch (e) {
      _updateState(BleConnectionState.error);
      _updateStatus("Connection failed: $e");
      await disconnect();
    }
  }

  // =====================================================
  // Request Available WiFi Networks
  // =====================================================
  Future<void> requestWifiNetworks() async {
    try {
      if (_ssidRequestChar == null) {
        throw Exception("SSID request characteristic not available");
      }

      _updateStatus("Requesting WiFi networks...");
      
      // Send reload command
      String command = "RELOAD_WIFI\n";
      await _ssidRequestChar!.write(utf8.encode(command));
      
    } catch (e) {
      _updateStatus("Failed to request WiFi networks: $e");
    }
  }

  // =====================================================
  // Send WiFi Credentials
  // =====================================================
  Future<bool> sendWifiCredentials(String ssid, String password) async {
    try {
      if (_ssidRequestChar == null) {
        throw Exception("SSID request characteristic not available");
      }

      _updateState(BleConnectionState.configuring);
      _updateStatus("Sending WiFi credentials...");

      // Format: WIFI:SSID:PASSWORD
      String command = "WIFI:$ssid:$password\n";
      await _ssidRequestChar!.write(utf8.encode(command));

      _updateStatus("WiFi credentials sent successfully");
      
      // Wait for connection confirmation
      await Future.delayed(const Duration(seconds: 5));
      
      return true;
    } catch (e) {
      _updateStatus("Failed to send WiFi credentials: $e");
      _updateState(BleConnectionState.error);
      return false;
    }
  }

  // =====================================================
  // Send User Credentials (Email/Password)
  // =====================================================
  Future<bool> sendUserCredentials(String email, String password) async {
    try {
      if (_ssidRequestChar == null) {
        throw Exception("SSID request characteristic not available");
      }

      _updateStatus("Sending user credentials...");

      // Format: EMAIL=email;PASS=password
      String command = "EMAIL=$email;PASS=$password\n";
      await _ssidRequestChar!.write(utf8.encode(command));

      _updateStatus("User credentials sent successfully");
      return true;
    } catch (e) {
      _updateStatus("Failed to send user credentials: $e");
      return false;
    }
  }

  // =====================================================
  // Handle WiFi Status Updates
  // =====================================================
  void _handleWifiStatusUpdate(List<int> value) {
    try {
      String status = utf8.decode(value);
      _updateStatus("Device status: $status");

      if (status.contains("WIFI_CONNECTED")) {
        _updateState(BleConnectionState.completed);
        _updateStatus("WiFi connection successful!");
      } else if (status.contains("WIFI_FAILED")) {
        _updateState(BleConnectionState.error);
        _updateStatus("WiFi connection failed");
      }
    } catch (e) {
      print("Error handling WiFi status: $e");
    }
  }

  // =====================================================
  // Handle SSID Response (Available Networks)
  // =====================================================
  void _handleSsidResponse(List<int> value) {
    try {
      String response = utf8.decode(value);
      _updateStatus("Received WiFi networks");

      // Parse comma-separated SSIDs
      List<String> ssids = response.split(',')
          .where((ssid) => ssid.trim().isNotEmpty)
          .toList();

      _availableNetworks = ssids.map((ssid) => WiFiNetwork(
        ssid: ssid.trim(),
        signalStrength: -50, // Default value
        isSecured: true,     // Assume secured
      )).toList();

      _wifiNetworksController.add(_availableNetworks);
      _updateStatus("Found ${_availableNetworks.length} WiFi networks");
    } catch (e) {
      _updateStatus("Error parsing WiFi networks: $e");
    }
  }

  // =====================================================
  // Disconnect from Device
  // =====================================================
  Future<void> disconnect() async {
    try {
      if (_connectedDevice != null) {
        await _connectedDevice!.disconnect();
        _connectedDevice = null;
      }
      
      _wifiStatusChar = null;
      _ssidResponseChar = null;
      _ssidRequestChar = null;
      
      _updateState(BleConnectionState.disconnected);
      _updateStatus("Disconnected");
    } catch (e) {
      _updateStatus("Disconnect error: $e");
    }
  }

  // =====================================================
  // Helper Methods
  // =====================================================
  void _updateState(BleConnectionState newState) {
    _currentState = newState;
    _stateController.add(newState);
  }

  void _updateStatus(String status) {
    print("PP4 BLE: $status");
    _statusController.add(status);
  }

  // =====================================================
  // Dispose Resources
  // =====================================================
  void dispose() {
    _stateController.close();
    _wifiNetworksController.close();
    _statusController.close();
    disconnect();
  }
}

// =====================================================
// Flutter Widget for WiFi Setup
// =====================================================
class PP4WifiSetupWidget extends StatefulWidget {
  final Function(String email, String password)? onUserCredentials;
  final VoidCallback? onSetupComplete;

  const PP4WifiSetupWidget({
    Key? key,
    this.onUserCredentials,
    this.onSetupComplete,
  }) : super(key: key);

  @override
  State<PP4WifiSetupWidget> createState() => _PP4WifiSetupWidgetState();
}

class _PP4WifiSetupWidgetState extends State<PP4WifiSetupWidget> {
  final PP4BleManager _bleManager = PP4BleManager();
  final TextEditingController _passwordController = TextEditingController();
  final TextEditingController _emailController = TextEditingController();
  final TextEditingController _userPasswordController = TextEditingController();
  
  WiFiNetwork? _selectedNetwork;
  bool _isPasswordVisible = false;
  bool _isUserPasswordVisible = false;

  @override
  void initState() {
    super.initState();
    _initializeBle();
  }

  Future<void> _initializeBle() async {
    await _bleManager.initialize();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('PP4 WiFi Setup'),
        backgroundColor: Colors.blue[600],
        foregroundColor: Colors.white,
      ),
      body: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // Status Card
            _buildStatusCard(),
            const SizedBox(height: 16),
            
            // Connection Controls
            _buildConnectionControls(),
            const SizedBox(height: 16),
            
            // WiFi Networks List
            _buildWifiNetworksList(),
            const SizedBox(height: 16),
            
            // WiFi Password Input
            if (_selectedNetwork != null) _buildWifiPasswordInput(),
            
            // User Credentials Input
            if (_bleManager.isConnected) _buildUserCredentialsInput(),
          ],
        ),
      ),
    );
  }

  Widget _buildStatusCard() {
    return StreamBuilder<String>(
      stream: _bleManager.statusStream,
      initialData: "Ready to connect",
      builder: (context, snapshot) {
        return Card(
          color: Colors.blue[50],
          child: Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              children: [
                Icon(
                  _getStatusIcon(),
                  size: 48,
                  color: _getStatusColor(),
                ),
                const SizedBox(height: 8),
                Text(
                  snapshot.data ?? "Unknown status",
                  style: const TextStyle(fontSize: 16),
                  textAlign: TextAlign.center,
                ),
              ],
            ),
          ),
        );
      },
    );
  }

  Widget _buildConnectionControls() {
    return StreamBuilder<BleConnectionState>(
      stream: _bleManager.stateStream,
      initialData: BleConnectionState.disconnected,
      builder: (context, snapshot) {
        BleConnectionState state = snapshot.data!;
        
        return Row(
          children: [
            Expanded(
              child: ElevatedButton.icon(
                onPressed: state == BleConnectionState.disconnected 
                    ? _bleManager.scanForDevices 
                    : null,
                icon: const Icon(Icons.bluetooth_searching),
                label: const Text('Scan for Devices'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.blue[600],
                  foregroundColor: Colors.white,
                ),
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: ElevatedButton.icon(
                onPressed: _bleManager.isConnected 
                    ? _bleManager.disconnect 
                    : null,
                icon: const Icon(Icons.bluetooth_disabled),
                label: const Text('Disconnect'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.red[600],
                  foregroundColor: Colors.white,
                ),
              ),
            ),
          ],
        );
      },
    );
  }

  Widget _buildWifiNetworksList() {
    return StreamBuilder<List<WiFiNetwork>>(
      stream: _bleManager.wifiNetworksStream,
      initialData: const [],
      builder: (context, snapshot) {
        List<WiFiNetwork> networks = snapshot.data!;
        
        if (networks.isEmpty) {
          return const Card(
            child: Padding(
              padding: EdgeInsets.all(16.0),
              child: Text(
                'No WiFi networks found. Connect to device first.',
                textAlign: TextAlign.center,
              ),
            ),
          );
        }

        return Card(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Padding(
                padding: EdgeInsets.all(16.0),
                child: Text(
                  'Available WiFi Networks',
                  style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
                ),
              ),
              ...networks.map((network) => ListTile(
                leading: Icon(
                  network.isSecured ? Icons.wifi_lock : Icons.wifi,
                  color: Colors.blue[600],
                ),
                title: Text(network.ssid),
                subtitle: Text('Signal: ${network.signalStrength} dBm'),
                trailing: _selectedNetwork == network 
                    ? Icon(Icons.check, color: Colors.green[600])
                    : null,
                onTap: () {
                  setState(() {
                    _selectedNetwork = network;
                    _passwordController.clear();
                  });
                },
              )),
            ],
          ),
        );
      },
    );
  }

  Widget _buildWifiPasswordInput() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'WiFi Password for "${_selectedNetwork!.ssid}"',
              style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 8),
            TextField(
              controller: _passwordController,
              obscureText: !_isPasswordVisible,
              decoration: InputDecoration(
                labelText: 'Password',
                border: const OutlineInputBorder(),
                suffixIcon: IconButton(
                  icon: Icon(_isPasswordVisible ? Icons.visibility : Icons.visibility_off),
                  onPressed: () {
                    setState(() {
                      _isPasswordVisible = !_isPasswordVisible;
                    });
                  },
                ),
              ),
            ),
            const SizedBox(height: 16),
            SizedBox(
              width: double.infinity,
              child: ElevatedButton.icon(
                onPressed: _sendWifiCredentials,
                icon: const Icon(Icons.wifi),
                label: const Text('Connect to WiFi'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.green[600],
                  foregroundColor: Colors.white,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildUserCredentialsInput() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'User Account Setup',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 8),
            TextField(
              controller: _emailController,
              keyboardType: TextInputType.emailAddress,
              decoration: const InputDecoration(
                labelText: 'Email',
                border: OutlineInputBorder(),
                prefixIcon: Icon(Icons.email),
              ),
            ),
            const SizedBox(height: 8),
            TextField(
              controller: _userPasswordController,
              obscureText: !_isUserPasswordVisible,
              decoration: InputDecoration(
                labelText: 'Password',
                border: const OutlineInputBorder(),
                prefixIcon: const Icon(Icons.lock),
                suffixIcon: IconButton(
                  icon: Icon(_isUserPasswordVisible ? Icons.visibility : Icons.visibility_off),
                  onPressed: () {
                    setState(() {
                      _isUserPasswordVisible = !_isUserPasswordVisible;
                    });
                  },
                ),
              ),
            ),
            const SizedBox(height: 16),
            SizedBox(
              width: double.infinity,
              child: ElevatedButton.icon(
                onPressed: _sendUserCredentials,
                icon: const Icon(Icons.person),
                label: const Text('Save User Credentials'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.orange[600],
                  foregroundColor: Colors.white,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Future<void> _sendWifiCredentials() async {
    if (_selectedNetwork == null || _passwordController.text.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Please select a network and enter password')),
      );
      return;
    }

    bool success = await _bleManager.sendWifiCredentials(
      _selectedNetwork!.ssid,
      _passwordController.text,
    );

    if (success) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('WiFi credentials sent successfully')),
      );
    }
  }

  Future<void> _sendUserCredentials() async {
    if (_emailController.text.isEmpty || _userPasswordController.text.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Please enter email and password')),
      );
      return;
    }

    bool success = await _bleManager.sendUserCredentials(
      _emailController.text,
      _userPasswordController.text,
    );

    if (success) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('User credentials sent successfully')),
      );
      
      if (widget.onUserCredentials != null) {
        widget.onUserCredentials!(_emailController.text, _userPasswordController.text);
      }
      
      if (widget.onSetupComplete != null) {
        widget.onSetupComplete!();
      }
    }
  }

  IconData _getStatusIcon() {
    switch (_bleManager.currentState) {
      case BleConnectionState.disconnected:
        return Icons.bluetooth_disabled;
      case BleConnectionState.scanning:
        return Icons.bluetooth_searching;
      case BleConnectionState.connecting:
        return Icons.bluetooth_connected;
      case BleConnectionState.connected:
        return Icons.bluetooth;
      case BleConnectionState.configuring:
        return Icons.settings;
      case BleConnectionState.completed:
        return Icons.check_circle;
      case BleConnectionState.error:
        return Icons.error;
    }
  }

  Color _getStatusColor() {
    switch (_bleManager.currentState) {
      case BleConnectionState.disconnected:
        return Colors.grey;
      case BleConnectionState.scanning:
        return Colors.blue;
      case BleConnectionState.connecting:
        return Colors.orange;
      case BleConnectionState.connected:
        return Colors.green;
      case BleConnectionState.configuring:
        return Colors.purple;
      case BleConnectionState.completed:
        return Colors.green;
      case BleConnectionState.error:
        return Colors.red;
    }
  }

  @override
  void dispose() {
    _passwordController.dispose();
    _emailController.dispose();
    _userPasswordController.dispose();
    super.dispose();
  }
}

// =====================================================
// Example Usage in Main App
// =====================================================
class PP4SetupApp extends StatelessWidget {
  const PP4SetupApp({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'PP4 WiFi Setup',
      theme: ThemeData(
        primarySwatch: Colors.blue,
        useMaterial3: true,
      ),
      home: PP4WifiSetupWidget(
        onUserCredentials: (email, password) {
          print('User credentials received: $email');
        },
        onSetupComplete: () {
          print('Setup completed successfully!');
        },
      ),
    );
  }
}

// =====================================================
// pubspec.yaml dependencies needed:
// =====================================================
/*
dependencies:
  flutter:
    sdk: flutter
  flutter_blue_plus: ^1.32.2
  permission_handler: ^11.3.1

dev_dependencies:
  flutter_test:
    sdk: flutter
  flutter_lints: ^3.0.0
*/

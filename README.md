# PP4 - Smart Pet Care System 🐕

An intelligent IoT-based automated pet toilet system built for ESP32, featuring WiFi connectivity, mobile app integration, and smart scheduling for optimal pet care.

## 🌟 Features

### 🔧 Hardware Control
- **Dual Pump System**: Automated sprinkler and filter pumps with PWM control
- **Smart LED Indicators**: RGB status LEDs for system monitoring
- **Precision Timing**: Hardware timer-based operations for accurate scheduling
- **Dog Size Adaptation**: Customizable operation cycles based on pet size

### 📱 Connectivity & Communication
- **WiFi Management**: Automatic connection with secure credential storage
- **BLE Configuration**: Bluetooth Low Energy setup for easy initial configuration
- **HTTP API Integration**: RESTful communication with cloud backend
- **JWT Authentication**: Secure token-based authentication system

### ⏰ Smart Scheduling
- **Multi-cycle Operation**: Configurable daily cycles based on pet needs
- **Weekly Scheduling**: 7-day programmable schedule with individual day control
- **Time Synchronization**: NTP time sync with manual override capability
- **Dual Operation Modes**: Automatic scheduling and manual control

### 🐕 Pet Size Configurations
| Dog Size | Filter Duration | Sprinkler Duration |
|----------|----------------|-------------------|
| Small    | 2 minutes      | 4 minutes         |
| Medium   | 3 minutes      | 5 minutes         |
| Large    | 4 minutes      | 7 minutes         |

## 🏗️ Architecture

### Core Components
- **`main.cpp`** - Main application logic and FreeRTOS task management
- **`WIFI_Class`** - WiFi connection management and BLE integration
- **`POST_GET`** - HTTP client with JWT authentication and retry logic
- **`BLE_Class`** - Bluetooth Low Energy configuration interface

### API Endpoints
The system communicates with backend services at `porchpotty.codefied.co`:
- `/api/login.php` - User authentication
- `/api/update_device_status.php` - Device status updates
- `/api/get_device_status.php` - Status retrieval
- `/api/get_sprinkler_schedule.php` - Schedule management

### Task Management
Multi-threaded FreeRTOS implementation:
- **Auto Control Task** - Scheduled pump operations
- **Manual Control Task** - Real-time manual control
- **HTTP Communication Task** - API communication handling
- **Time Sync Task** - Network time synchronization
- **Settings Management Task** - Configuration persistence

## 🔌 Hardware Requirements

### ESP32 Pin Configuration
```cpp
#define LED_GREEN_PIN 10    // Status LED - Green
#define LED_RED_PIN   3     // Status LED - Red  
#define LED_BLUE_PIN  0     // Status LED - Blue
#define FILTER_PIN    20    // Filter pump control
#define SPRINKLER_PIN 21    // Sprinkler pump control (PWM)
#define I2C_SDA       6     // I2C Data line
#define I2C_SCL       5     // I2C Clock line
#define TMP36         1     // Temperature sensor
#define DRDY          7     // Data ready pin
```

### Power Requirements
- **Voltage**: 3.3V - 5V DC
- **Current**: 500mA minimum (pumps require additional power)
- **WiFi**: 2.4GHz 802.11 b/g/n

## 🚀 Getting Started

### Prerequisites
- **Arduino IDE** or **PlatformIO**
- **ESP32 Development Board**
- **Arduino ESP32 Core** (v2.0.0 or later)

### Required Libraries
```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <nvs_flash.h>
```

### Installation

1. **Clone the repository**
   ```bash
   git clone -b integrateHTTP_branch https://github.com/anhtuhuhu/PP4.git
   cd PP4
   ```

2. **Open in Arduino IDE**
   - Open `PP4.ino`
   - Select your ESP32 board
   - Install required libraries via Library Manager

3. **Configure WiFi (via BLE)**
   - Flash the firmware to ESP32
   - Use BLE app to configure WiFi credentials
   - Device will auto-connect on subsequent boots

4. **Backend Configuration**
   - Register device with backend API
   - Configure user credentials and device ID
   - Set up scheduling via mobile app

### Quick Flash (Pre-compiled)
Use the pre-compiled binary for quick deployment:
```bash
esptool.py --chip esp32 --baud 921600 write_flash 0x0 Bin_files/PP4_v230725.ino.merged.bin
```

## 📱 Mobile App Integration

### BLE Configuration
1. Enable Bluetooth on mobile device
2. Scan for "PP4-XXXX" device
3. Connect and send WiFi credentials
4. Device will automatically connect to WiFi

### HTTP API Control
- **Real-time Status**: Monitor pump status and system health
- **Schedule Management**: Set daily/weekly operation schedules
- **Manual Control**: Override automatic operations
- **Settings Sync**: Cloud-based configuration backup

## ⚙️ Configuration

### System Settings
```cpp
// Timing Configuration
#define GET_TIME_PERIOD 60          // Time sync interval (seconds)
#define POST_INTERVAL 8000          // Status update interval (ms)
#define TOKEN_REFRESH_INTERVAL 3600000  // JWT refresh (ms)

// PWM Configuration  
#define PWM_FREQ 4000              // PWM frequency (Hz)
#define PWM_RESOLUTION 8           // PWM resolution (bits)
```

### Dog Size Configuration
Modify in `configurationForDog()` function:
```cpp
void configurationForDog(int type) {
  switch(type) {
    case SMALL_DOG:
      _filter.duration = FILTER_DURATION_SMALL_DOG * 60;
      _sprinkler.duration = SPRINKLER_DURATION_SMALL_DOG * 60;
      break;
    // ... additional cases
  }
}
```

## 🔧 Development

### Building from Source
```bash
# Using Arduino CLI
arduino-cli compile --fqbn esp32:esp32:esp32 PP4.ino

# Using PlatformIO
pio run -t upload
```

### Debugging
Enable debug output by defining `DEBUG` in `PP4.h`:
```cpp
#define DEBUG  // Enable serial debug output
```

### Testing
- **Serial Monitor**: 115200 baud for debug output
- **BLE Testing**: Use nRF Connect or similar BLE scanner
- **API Testing**: Monitor HTTP requests via serial output

## 📊 System Monitoring

### Status LEDs
- **Green**: System ready/WiFi connected
- **Red**: Error state/WiFi disconnected  
- **Blue**: BLE configuration mode

### Serial Debug Output
```
VERSION: v23.7.0
[WIFI] WiFi init
SSID: YourNetwork Pass: YourPassword
[HTTP] Login successful, token received
[AUTO] Sprinkler cycle 1/3 started
[AUTO] Filter operation completed
```

## 🛡️ Security Features

- **JWT Token Management**: Automatic token refresh and validation
- **Secure Storage**: WiFi credentials stored in NVS (encrypted)
- **Authentication Retry**: Robust error handling for network issues
- **Input Validation**: Sanitized BLE and HTTP inputs

## 🔄 Version History

### v23.7.0 (Current - integrateHTTP_branch)
- Enhanced HTTP client with improved error handling
- JWT token management improvements
- Better API communication reliability
- Optimized memory usage for HTTP operations

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🆘 Support

### Common Issues
- **WiFi Connection Failed**: Check credentials via BLE configuration
- **API Authentication Error**: Verify backend connectivity and credentials
- **Pump Not Operating**: Check power supply and pin connections
- **Time Sync Issues**: Ensure internet connectivity for NTP

### Getting Help
- **Issues**: [GitHub Issues](https://github.com/anhtuhuhu/PP4/issues)
- **Documentation**: Check inline code comments
- **Serial Debug**: Enable debug mode for detailed logging

## 🙏 Acknowledgments

- ESP32 Arduino Core team for excellent framework
- FreeRTOS for robust task management
- ArduinoJson library for efficient JSON handling
- BLE library contributors for seamless connectivity

---

**Made with ❤️ for pet owners who care about their furry friends' comfort and hygiene.**
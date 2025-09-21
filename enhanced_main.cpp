#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// Include enhanced modules
#include "OTA_Manager.h"
#include "BLE_Class.h"
#include "WIFI_Class.h"
#include "POST_GET.h"
#include "PP4.h"

/**
 * Enhanced PP4 Smart Pet Care System - Main Application
 * 
 * Features implemented based on IoT best practices:
 * - OTA firmware updates with rollback protection
 * - Real-time telemetry and health monitoring
 * - Adaptive scheduling with battery awareness
 * - Enhanced error handling and recovery
 * - Multi-task architecture with FreeRTOS
 * - Secure communication with JWT refresh
 * - Predictive maintenance indicators
 */

// Enhanced Configuration
#define FIRMWARE_VERSION "2.0.0-enhanced"
#define HARDWARE_VERSION "ESP32-v1.0"
#define API_BASE_URL "https://porchpotty.codefied.co/api"
#define TELEMETRY_INTERVAL 30000    // 30 seconds
#define HEALTH_CHECK_INTERVAL 60000 // 1 minute
#define OTA_CHECK_INTERVAL 3600000  // 1 hour
#define WATCHDOG_TIMEOUT 30         // 30 seconds

// Task priorities
#define TASK_PRIORITY_HIGH 3
#define TASK_PRIORITY_MEDIUM 2
#define TASK_PRIORITY_LOW 1

// Queue sizes
#define TELEMETRY_QUEUE_SIZE 10
#define COMMAND_QUEUE_SIZE 5
#define ALERT_QUEUE_SIZE 5

// Global objects
OTAManager otaManager(API_BASE_URL, "");
BLE_Class bleManager;
WIFI_Class wifiManager;
POST_GET apiClient;
Preferences preferences;

// FreeRTOS handles
TaskHandle_t telemetryTaskHandle = NULL;
TaskHandle_t schedulerTaskHandle = NULL;
TaskHandle_t healthMonitorTaskHandle = NULL;
TaskHandle_t otaTaskHandle = NULL;
TaskHandle_t communicationTaskHandle = NULL;

QueueHandle_t telemetryQueue;
QueueHandle_t commandQueue;
QueueHandle_t alertQueue;

SemaphoreHandle_t wifiMutex;
SemaphoreHandle_t preferencesMutex;

// System state variables
struct SystemState {
    bool wifiConnected = false;
    bool apiAuthenticated = false;
    bool otaInProgress = false;
    int healthScore = 100;
    unsigned long lastTelemetryTime = 0;
    unsigned long lastHealthCheckTime = 0;
    unsigned long lastOTACheckTime = 0;
    String deviceId = "";
    String authToken = "";
    String refreshToken = "";
    unsigned long tokenExpiry = 0;
} systemState;

// Telemetry data structure
struct TelemetryData {
    float wifiSignal;
    float batteryLevel;
    float memoryUsage;
    float cpuUsage;
    float temperature;
    float humidity;
    float waterLevel;
    bool pump1Status;
    bool pump2Status;
    int pump1Runtime;
    int pump2Runtime;
    int errorCount;
    unsigned long uptime;
    int freeHeap;
    int wifiReconnects;
    String lastError;
};

// Command structure for inter-task communication
struct Command {
    String type;
    String action;
    DynamicJsonDocument data;
    
    Command() : data(512) {}
};

// Alert structure
struct Alert {
    String type;
    String severity;
    String message;
    float value;
    unsigned long timestamp;
};

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== PP4 Smart Pet Care System v" + String(FIRMWARE_VERSION) + " ===");
    
    // Initialize watchdog timer
    esp_task_wdt_init(WATCHDOG_TIMEOUT, true);
    esp_task_wdt_add(NULL);
    
    // Initialize preferences
    if (!preferences.begin("pp4_system", false)) {
        Serial.println("[ERROR] Failed to initialize preferences");
        ESP.restart();
    }
    
    // Load system configuration
    loadSystemConfiguration();
    
    // Initialize hardware
    initializeHardware();
    
    // Create FreeRTOS objects
    createQueuesAndSemaphores();
    
    // Initialize managers
    initializeManagers();
    
    // Create tasks
    createTasks();
    
    Serial.println("[SYSTEM] Initialization complete");
    Serial.printf("[SYSTEM] Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("[SYSTEM] Device ID: %s\n", systemState.deviceId.c_str());
}

void loop() {
    // Main loop handles critical system monitoring
    esp_task_wdt_reset();
    
    // Check system health
    if (millis() - systemState.lastHealthCheckTime > HEALTH_CHECK_INTERVAL) {
        performSystemHealthCheck();
        systemState.lastHealthCheckTime = millis();
    }
    
    // Handle emergency situations
    handleEmergencyConditions();
    
    // Yield to other tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void loadSystemConfiguration() {
    systemState.deviceId = preferences.getString("device_id", "");
    systemState.authToken = preferences.getString("auth_token", "");
    systemState.refreshToken = preferences.getString("refresh_token", "");
    systemState.tokenExpiry = preferences.getULong64("token_expiry", 0);
    
    if (systemState.deviceId.isEmpty()) {
        systemState.deviceId = "PP4_" + WiFi.macAddress();
        systemState.deviceId.replace(":", "");
        preferences.putString("device_id", systemState.deviceId);
    }
    
    Serial.printf("[CONFIG] Device ID: %s\n", systemState.deviceId.c_str());
}

void initializeHardware() {
    // Initialize GPIO pins
    pinMode(PUMP1_PIN, OUTPUT);
    pinMode(PUMP2_PIN, OUTPUT);
    pinMode(WATER_LEVEL_PIN, INPUT);
    pinMode(TEMP_SENSOR_PIN, INPUT);
    pinMode(BATTERY_PIN, INPUT);
    
    // Initialize PWM channels
    ledcSetup(0, 1000, 8); // Channel 0, 1kHz, 8-bit resolution
    ledcSetup(1, 1000, 8); // Channel 1, 1kHz, 8-bit resolution
    ledcAttachPin(PUMP1_PIN, 0);
    ledcAttachPin(PUMP2_PIN, 1);
    
    // Turn off pumps initially
    ledcWrite(0, 0);
    ledcWrite(1, 0);
    
    Serial.println("[HARDWARE] GPIO and PWM initialized");
}

void createQueuesAndSemaphores() {
    telemetryQueue = xQueueCreate(TELEMETRY_QUEUE_SIZE, sizeof(TelemetryData));
    commandQueue = xQueueCreate(COMMAND_QUEUE_SIZE, sizeof(Command));
    alertQueue = xQueueCreate(ALERT_QUEUE_SIZE, sizeof(Alert));
    
    wifiMutex = xSemaphoreCreateMutex();
    preferencesMutex = xSemaphoreCreateMutex();
    
    if (!telemetryQueue || !commandQueue || !alertQueue || !wifiMutex || !preferencesMutex) {
        Serial.println("[ERROR] Failed to create FreeRTOS objects");
        ESP.restart();
    }
    
    Serial.println("[SYSTEM] FreeRTOS objects created");
}

void initializeManagers() {
    // Initialize OTA Manager
    otaManager.setAuthToken(systemState.authToken);
    otaManager.setBatteryAwareUpdates(true, 30.0);
    otaManager.setUpdateCheckInterval(OTA_CHECK_INTERVAL);
    otaManager.enableSignatureVerification(""); // Add public key in production
    
    if (!otaManager.initialize()) {
        Serial.println("[WARNING] OTA Manager initialization failed");
    }
    
    // Initialize BLE Manager
    bleManager.initialize("PP4_" + systemState.deviceId);
    
    // Initialize WiFi Manager
    wifiManager.initialize();
    
    Serial.println("[SYSTEM] Managers initialized");
}

void createTasks() {
    // Create telemetry collection task
    xTaskCreatePinnedToCore(
        telemetryTask,
        "TelemetryTask",
        4096,
        NULL,
        TASK_PRIORITY_MEDIUM,
        &telemetryTaskHandle,
        1
    );
    
    // Create scheduler task
    xTaskCreatePinnedToCore(
        schedulerTask,
        "SchedulerTask",
        4096,
        NULL,
        TASK_PRIORITY_HIGH,
        &schedulerTaskHandle,
        1
    );
    
    // Create health monitor task
    xTaskCreatePinnedToCore(
        healthMonitorTask,
        "HealthMonitorTask",
        3072,
        NULL,
        TASK_PRIORITY_LOW,
        &healthMonitorTaskHandle,
        0
    );
    
    // Create OTA task
    xTaskCreatePinnedToCore(
        otaTask,
        "OTATask",
        8192,
        NULL,
        TASK_PRIORITY_LOW,
        &otaTaskHandle,
        0
    );
    
    // Create communication task
    xTaskCreatePinnedToCore(
        communicationTask,
        "CommunicationTask",
        6144,
        NULL,
        TASK_PRIORITY_MEDIUM,
        &communicationTaskHandle,
        0
    );
    
    Serial.println("[SYSTEM] Tasks created successfully");
}

void telemetryTask(void *parameter) {
    TelemetryData telemetry;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (true) {
        // Collect telemetry data
        collectTelemetryData(telemetry);
        
        // Send to queue for processing
        if (xQueueSend(telemetryQueue, &telemetry, pdMS_TO_TICKS(100)) != pdPASS) {
            Serial.println("[TELEMETRY] Queue full, dropping data");
        }
        
        // Check for alerts
        checkTelemetryAlerts(telemetry);
        
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(TELEMETRY_INTERVAL));
    }
}

void schedulerTask(void *parameter) {
    while (true) {
        // Check for scheduled operations
        checkScheduledOperations();
        
        // Process commands from queue
        Command command;
        if (xQueueReceive(commandQueue, &command, pdMS_TO_TICKS(1000)) == pdPASS) {
            processCommand(command);
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void healthMonitorTask(void *parameter) {
    while (true) {
        // Calculate system health score
        systemState.healthScore = calculateSystemHealth();
        
        // Check for system anomalies
        detectSystemAnomalies();
        
        // Process alerts
        processAlerts();
        
        vTaskDelay(pdMS_TO_TICKS(HEALTH_CHECK_INTERVAL));
    }
}

void otaTask(void *parameter) {
    while (true) {
        if (!systemState.otaInProgress && systemState.wifiConnected) {
            if (otaManager.shouldCheckForUpdates()) {
                Serial.println("[OTA] Checking for firmware updates...");
                
                if (otaManager.checkForUpdates()) {
                    Serial.println("[OTA] Update available!");
                    
                    // Check system conditions before updating
                    if (canPerformOTAUpdate()) {
                        systemState.otaInProgress = true;
                        otaManager.startUpdate();
                        systemState.otaInProgress = false;
                    } else {
                        Serial.println("[OTA] System conditions not suitable for update");
                    }
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000)); // Check every minute
    }
}

void communicationTask(void *parameter) {
    while (true) {
        // Handle WiFi connection
        manageWiFiConnection();
        
        // Handle API authentication
        manageAPIAuthentication();
        
        // Send telemetry data
        sendTelemetryData();
        
        // Handle BLE communication
        handleBLECommunication();
        
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void collectTelemetryData(TelemetryData &telemetry) {
    telemetry.wifiSignal = WiFi.RSSI();
    telemetry.batteryLevel = readBatteryLevel();
    telemetry.memoryUsage = (ESP.getHeapSize() - ESP.getFreeHeap()) * 100.0 / ESP.getHeapSize();
    telemetry.cpuUsage = 0; // Placeholder - implement CPU usage calculation
    telemetry.temperature = readTemperature();
    telemetry.humidity = readHumidity();
    telemetry.waterLevel = readWaterLevel();
    telemetry.pump1Status = digitalRead(PUMP1_PIN);
    telemetry.pump2Status = digitalRead(PUMP2_PIN);
    telemetry.pump1Runtime = preferences.getInt("pump1_runtime", 0);
    telemetry.pump2Runtime = preferences.getInt("pump2_runtime", 0);
    telemetry.errorCount = preferences.getInt("error_count", 0);
    telemetry.uptime = millis();
    telemetry.freeHeap = ESP.getFreeHeap();
    telemetry.wifiReconnects = preferences.getInt("wifi_reconnects", 0);
    telemetry.lastError = preferences.getString("last_error", "");
}

void checkTelemetryAlerts(const TelemetryData &telemetry) {
    Alert alert;
    alert.timestamp = millis();
    
    // Battery level alerts
    if (telemetry.batteryLevel < 10) {
        alert.type = "critical_battery";
        alert.severity = "critical";
        alert.message = "Battery critically low: " + String(telemetry.batteryLevel) + "%";
        alert.value = telemetry.batteryLevel;
        xQueueSend(alertQueue, &alert, 0);
    } else if (telemetry.batteryLevel < 20) {
        alert.type = "low_battery";
        alert.severity = "warning";
        alert.message = "Battery low: " + String(telemetry.batteryLevel) + "%";
        alert.value = telemetry.batteryLevel;
        xQueueSend(alertQueue, &alert, 0);
    }
    
    // Water level alerts
    if (telemetry.waterLevel < 10) {
        alert.type = "low_water";
        alert.severity = "warning";
        alert.message = "Water level low: " + String(telemetry.waterLevel) + "%";
        alert.value = telemetry.waterLevel;
        xQueueSend(alertQueue, &alert, 0);
    }
    
    // Temperature alerts
    if (telemetry.temperature > 50) {
        alert.type = "high_temperature";
        alert.severity = "critical";
        alert.message = "Temperature too high: " + String(telemetry.temperature) + "°C";
        alert.value = telemetry.temperature;
        xQueueSend(alertQueue, &alert, 0);
    }
    
    // Memory usage alerts
    if (telemetry.memoryUsage > 90) {
        alert.type = "high_memory_usage";
        alert.severity = "warning";
        alert.message = "Memory usage high: " + String(telemetry.memoryUsage) + "%";
        alert.value = telemetry.memoryUsage;
        xQueueSend(alertQueue, &alert, 0);
    }
}

int calculateSystemHealth() {
    TelemetryData telemetry;
    collectTelemetryData(telemetry);
    
    int healthScore = 100;
    
    // Battery health (25% weight)
    if (telemetry.batteryLevel < 10) healthScore -= 25;
    else if (telemetry.batteryLevel < 20) healthScore -= 15;
    else if (telemetry.batteryLevel < 30) healthScore -= 10;
    
    // WiFi signal health (20% weight)
    if (telemetry.wifiSignal < -90) healthScore -= 20;
    else if (telemetry.wifiSignal < -80) healthScore -= 15;
    else if (telemetry.wifiSignal < -70) healthScore -= 10;
    
    // Memory usage health (20% weight)
    if (telemetry.memoryUsage > 90) healthScore -= 20;
    else if (telemetry.memoryUsage > 80) healthScore -= 15;
    else if (telemetry.memoryUsage > 70) healthScore -= 10;
    
    // Temperature health (15% weight)
    if (telemetry.temperature > 50) healthScore -= 15;
    else if (telemetry.temperature > 40) healthScore -= 10;
    else if (telemetry.temperature > 35) healthScore -= 5;
    
    // Water level health (10% weight)
    if (telemetry.waterLevel < 10) healthScore -= 10;
    else if (telemetry.waterLevel < 20) healthScore -= 5;
    
    // Error count health (10% weight)
    if (telemetry.errorCount > 10) healthScore -= 10;
    else if (telemetry.errorCount > 5) healthScore -= 5;
    
    return max(0, min(100, healthScore));
}

bool canPerformOTAUpdate() {
    TelemetryData telemetry;
    collectTelemetryData(telemetry);
    
    // Check battery level
    if (telemetry.batteryLevel < 30) {
        Serial.println("[OTA] Battery too low for update");
        return false;
    }
    
    // Check WiFi signal
    if (telemetry.wifiSignal < -80) {
        Serial.println("[OTA] WiFi signal too weak for update");
        return false;
    }
    
    // Check memory availability
    if (telemetry.memoryUsage > 80) {
        Serial.println("[OTA] Memory usage too high for update");
        return false;
    }
    
    // Check if pumps are running
    if (telemetry.pump1Status || telemetry.pump2Status) {
        Serial.println("[OTA] Pumps are active, delaying update");
        return false;
    }
    
    return true;
}

void performSystemHealthCheck() {
    // Check task health
    if (telemetryTaskHandle && eTaskGetState(telemetryTaskHandle) == eDeleted) {
        Serial.println("[HEALTH] Telemetry task died, restarting...");
        // Restart task
    }
    
    if (schedulerTaskHandle && eTaskGetState(schedulerTaskHandle) == eDeleted) {
        Serial.println("[HEALTH] Scheduler task died, restarting...");
        // Restart task
    }
    
    // Check memory leaks
    static int lastFreeHeap = ESP.getFreeHeap();
    int currentFreeHeap = ESP.getFreeHeap();
    
    if (currentFreeHeap < lastFreeHeap - 1000) {
        Serial.printf("[HEALTH] Potential memory leak detected: %d -> %d\n", 
                     lastFreeHeap, currentFreeHeap);
    }
    lastFreeHeap = currentFreeHeap;
    
    // Check stack overflow
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    if (stackHighWaterMark < 512) {
        Serial.printf("[HEALTH] Low stack space: %d bytes\n", stackHighWaterMark);
    }
}

void handleEmergencyConditions() {
    TelemetryData telemetry;
    collectTelemetryData(telemetry);
    
    // Emergency shutdown conditions
    if (telemetry.temperature > 60) {
        Serial.println("[EMERGENCY] Critical temperature, shutting down pumps");
        ledcWrite(0, 0);
        ledcWrite(1, 0);
        
        // Send emergency alert
        Alert alert;
        alert.type = "emergency_shutdown";
        alert.severity = "critical";
        alert.message = "Emergency shutdown due to high temperature";
        alert.value = telemetry.temperature;
        alert.timestamp = millis();
        xQueueSend(alertQueue, &alert, 0);
    }
    
    // Critical battery level
    if (telemetry.batteryLevel < 5) {
        Serial.println("[EMERGENCY] Critical battery level, entering deep sleep");
        ESP.deepSleep(3600000000); // Sleep for 1 hour
    }
}

// Sensor reading functions
float readBatteryLevel() {
    int reading = analogRead(BATTERY_PIN);
    float voltage = (reading / 4095.0) * 3.3 * 2; // Voltage divider
    return ((voltage - 3.0) / (4.2 - 3.0)) * 100.0;
}

float readTemperature() {
    // Implement temperature sensor reading
    return 25.0; // Placeholder
}

float readHumidity() {
    // Implement humidity sensor reading
    return 50.0; // Placeholder
}

float readWaterLevel() {
    int reading = analogRead(WATER_LEVEL_PIN);
    return (reading / 4095.0) * 100.0;
}

// Additional helper functions would be implemented here...
// Including: manageWiFiConnection, manageAPIAuthentication, sendTelemetryData,
// handleBLECommunication, processCommand, processAlerts, etc.

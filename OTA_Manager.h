#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

/**
 * @brief OTA Manager Class for Secure Firmware Updates
 * 
 * Implements best practices for IoT OTA updates:
 * - Secure HTTPS downloads with certificate validation
 * - Rollback protection with version checking
 * - Delta updates support for bandwidth optimization
 * - Automatic retry mechanism with exponential backoff
 * - Battery-aware update scheduling
 * - Cryptographic signature verification
 */
class OTAManager {
private:
    Preferences preferences;
    String currentVersion;
    String updateServerURL;
    String deviceID;
    String authToken;
    
    // Security settings
    bool verifySignature;
    String publicKey;
    
    // Update scheduling
    unsigned long lastUpdateCheck;
    unsigned long updateCheckInterval;
    bool batteryAwareUpdates;
    float minBatteryLevel;
    
    // Retry mechanism
    int maxRetries;
    unsigned long retryDelay;
    int currentRetries;
    
    // Status tracking
    bool updateInProgress;
    int updateProgress;
    String lastError;
    
    // Internal methods
    bool checkBatteryLevel();
    bool verifyFirmwareSignature(const uint8_t* firmware, size_t size, const String& signature);
    bool downloadFirmware(const String& url, const String& expectedHash);
    bool performUpdate(const uint8_t* firmware, size_t size);
    void rollbackOnFailure();
    String calculateSHA256(const uint8_t* data, size_t length);
    bool validateVersion(const String& newVersion);
    
public:
    // Constructor
    OTAManager(const String& serverURL, const String& deviceId);
    
    // Configuration methods
    void setAuthToken(const String& token);
    void setPublicKey(const String& key);
    void setBatteryAwareUpdates(bool enabled, float minLevel = 30.0);
    void setUpdateCheckInterval(unsigned long intervalMs);
    void setMaxRetries(int retries);
    
    // Core OTA methods
    bool initialize();
    bool checkForUpdates();
    bool startUpdate();
    bool isUpdateAvailable();
    bool isUpdateInProgress();
    
    // Status and monitoring
    int getUpdateProgress();
    String getCurrentVersion();
    String getLastError();
    void resetUpdateState();
    
    // Scheduled update management
    bool shouldCheckForUpdates();
    void scheduleUpdate(unsigned long delayMs);
    
    // Security methods
    bool enableSignatureVerification(const String& publicKey);
    void disableSignatureVerification();
    
    // Rollback functionality
    bool canRollback();
    bool performRollback();
    String getPreviousVersion();
    
    // Delta update support
    bool supportsDeltaUpdates();
    bool applyDeltaUpdate(const String& deltaUrl, const String& baseVersion);
    
    // Callback types
    typedef void (*UpdateProgressCallback)(int progress);
    typedef void (*UpdateCompleteCallback)(bool success, const String& error);
    typedef void (*UpdateAvailableCallback)(const String& version, const String& description);
    
    // Callback setters
    void setProgressCallback(UpdateProgressCallback callback);
    void setCompleteCallback(UpdateCompleteCallback callback);
    void setAvailableCallback(UpdateAvailableCallback callback);
    
    // Callbacks
    UpdateProgressCallback onProgress;
    UpdateCompleteCallback onComplete;
    UpdateAvailableCallback onAvailable;
};

// OTA Update Response Structure
struct OTAUpdateInfo {
    String version;
    String description;
    String downloadUrl;
    String sha256Hash;
    String signature;
    bool isDelta;
    String baseVersion;
    size_t fileSize;
    bool forceUpdate;
    unsigned long releaseDate;
};

// OTA Configuration Structure
struct OTAConfig {
    String serverURL;
    String deviceID;
    String authToken;
    String publicKey;
    bool batteryAware;
    float minBatteryLevel;
    unsigned long checkInterval;
    int maxRetries;
    bool verifySignature;
    bool enableDelta;
};

#endif // OTA_MANAGER_H

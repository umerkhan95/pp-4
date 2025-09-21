#include "OTA_Manager.h"
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

OTAManager::OTAManager(const String& serverURL, const String& deviceId) 
    : updateServerURL(serverURL), deviceID(deviceId) {
    
    // Initialize default values
    currentVersion = "1.0.0";
    lastUpdateCheck = 0;
    updateCheckInterval = 3600000; // 1 hour
    batteryAwareUpdates = true;
    minBatteryLevel = 30.0;
    maxRetries = 3;
    retryDelay = 5000; // 5 seconds
    currentRetries = 0;
    updateInProgress = false;
    updateProgress = 0;
    verifySignature = false;
    
    // Initialize callbacks to nullptr
    onProgress = nullptr;
    onComplete = nullptr;
    onAvailable = nullptr;
}

bool OTAManager::initialize() {
    Serial.println("[OTA] Initializing OTA Manager...");
    
    // Initialize preferences
    if (!preferences.begin("ota_manager", false)) {
        Serial.println("[OTA] Failed to initialize preferences");
        return false;
    }
    
    // Load current version from preferences or set default
    currentVersion = preferences.getString("current_ver", "1.0.0");
    lastUpdateCheck = preferences.getULong64("last_check", 0);
    
    Serial.printf("[OTA] Current firmware version: %s\n", currentVersion.c_str());
    Serial.printf("[OTA] Last update check: %lu\n", lastUpdateCheck);
    
    return true;
}

void OTAManager::setAuthToken(const String& token) {
    authToken = token;
    preferences.putString("auth_token", token);
}

void OTAManager::setPublicKey(const String& key) {
    publicKey = key;
    preferences.putString("public_key", key);
}

void OTAManager::setBatteryAwareUpdates(bool enabled, float minLevel) {
    batteryAwareUpdates = enabled;
    minBatteryLevel = minLevel;
    preferences.putBool("battery_aware", enabled);
    preferences.putFloat("min_battery", minLevel);
}

void OTAManager::setUpdateCheckInterval(unsigned long intervalMs) {
    updateCheckInterval = intervalMs;
    preferences.putULong64("check_interval", intervalMs);
}

void OTAManager::setMaxRetries(int retries) {
    maxRetries = retries;
    preferences.putInt("max_retries", retries);
}

bool OTAManager::shouldCheckForUpdates() {
    unsigned long currentTime = millis();
    return (currentTime - lastUpdateCheck) >= updateCheckInterval;
}

bool OTAManager::checkBatteryLevel() {
    if (!batteryAwareUpdates) {
        return true; // Skip battery check if not enabled
    }
    
    // Read battery voltage (assuming ADC on GPIO34)
    int batteryReading = analogRead(34);
    float batteryVoltage = (batteryReading / 4095.0) * 3.3 * 2; // Voltage divider
    float batteryPercentage = ((batteryVoltage - 3.0) / (4.2 - 3.0)) * 100.0;
    
    Serial.printf("[OTA] Battery level: %.1f%%\n", batteryPercentage);
    
    return batteryPercentage >= minBatteryLevel;
}

bool OTAManager::checkForUpdates() {
    if (updateInProgress) {
        Serial.println("[OTA] Update already in progress");
        return false;
    }
    
    if (!checkBatteryLevel()) {
        Serial.printf("[OTA] Battery level too low for update (%.1f%% < %.1f%%)\n", 
                     minBatteryLevel, minBatteryLevel);
        return false;
    }
    
    Serial.println("[OTA] Checking for firmware updates...");
    
    HTTPClient http;
    http.begin(updateServerURL + "/api/ota/check");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + authToken);
    http.addHeader("User-Agent", "PP4-OTA-Client/1.0");
    
    // Create request payload
    DynamicJsonDocument requestDoc(512);
    requestDoc["device_id"] = deviceID;
    requestDoc["current_version"] = currentVersion;
    requestDoc["hardware_version"] = "ESP32-v1.0";
    requestDoc["supports_delta"] = true;
    
    String requestBody;
    serializeJson(requestDoc, requestBody);
    
    int httpResponseCode = http.POST(requestBody);
    lastUpdateCheck = millis();
    preferences.putULong64("last_check", lastUpdateCheck);
    
    if (httpResponseCode == 200) {
        String response = http.getString();
        Serial.printf("[OTA] Server response: %s\n", response.c_str());
        
        DynamicJsonDocument responseDoc(1024);
        DeserializationError error = deserializeJson(responseDoc, response);
        
        if (error) {
            Serial.printf("[OTA] JSON parsing failed: %s\n", error.c_str());
            http.end();
            return false;
        }
        
        if (responseDoc["update_available"].as<bool>()) {
            Serial.println("[OTA] Update available!");
            
            // Store update info
            preferences.putString("update_version", responseDoc["version"].as<String>());
            preferences.putString("update_url", responseDoc["download_url"].as<String>());
            preferences.putString("update_hash", responseDoc["sha256"].as<String>());
            preferences.putString("update_signature", responseDoc["signature"].as<String>());
            preferences.putBool("update_available", true);
            
            // Trigger callback if set
            if (onAvailable) {
                onAvailable(responseDoc["version"].as<String>(), 
                           responseDoc["description"].as<String>());
            }
            
            http.end();
            return true;
        } else {
            Serial.println("[OTA] No updates available");
            preferences.putBool("update_available", false);
        }
    } else {
        Serial.printf("[OTA] HTTP error: %d\n", httpResponseCode);
        lastError = "HTTP Error: " + String(httpResponseCode);
    }
    
    http.end();
    return false;
}

bool OTAManager::isUpdateAvailable() {
    return preferences.getBool("update_available", false);
}

bool OTAManager::startUpdate() {
    if (updateInProgress) {
        Serial.println("[OTA] Update already in progress");
        return false;
    }
    
    if (!isUpdateAvailable()) {
        Serial.println("[OTA] No update available");
        return false;
    }
    
    if (!checkBatteryLevel()) {
        Serial.println("[OTA] Battery level too low for update");
        return false;
    }
    
    updateInProgress = true;
    updateProgress = 0;
    currentRetries = 0;
    
    String updateUrl = preferences.getString("update_url", "");
    String expectedHash = preferences.getString("update_hash", "");
    
    Serial.printf("[OTA] Starting firmware update from: %s\n", updateUrl.c_str());
    
    bool success = downloadFirmware(updateUrl, expectedHash);
    
    if (success) {
        Serial.println("[OTA] Firmware update completed successfully!");
        preferences.putString("current_ver", preferences.getString("update_version", ""));
        preferences.putBool("update_available", false);
        
        if (onComplete) {
            onComplete(true, "");
        }
        
        // Restart device to apply update
        delay(2000);
        ESP.restart();
    } else {
        Serial.printf("[OTA] Firmware update failed: %s\n", lastError.c_str());
        updateInProgress = false;
        
        if (onComplete) {
            onComplete(false, lastError);
        }
        
        // Attempt rollback if possible
        if (canRollback()) {
            Serial.println("[OTA] Attempting rollback...");
            performRollback();
        }
    }
    
    return success;
}

bool OTAManager::downloadFirmware(const String& url, const String& expectedHash) {
    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", "Bearer " + authToken);
    
    int httpCode = http.GET();
    
    if (httpCode != 200) {
        lastError = "Download failed: HTTP " + String(httpCode);
        http.end();
        return false;
    }
    
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        lastError = "Invalid content length";
        http.end();
        return false;
    }
    
    Serial.printf("[OTA] Firmware size: %d bytes\n", contentLength);
    
    // Initialize update
    if (!Update.begin(contentLength)) {
        lastError = "Update begin failed: " + String(Update.errorString());
        http.end();
        return false;
    }
    
    // Download and write firmware
    WiFiClient* client = http.getStreamPtr();
    uint8_t buffer[1024];
    int totalBytes = 0;
    
    // Initialize SHA256 for hash verification
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0); // 0 for SHA256
    
    while (http.connected() && totalBytes < contentLength) {
        size_t availableBytes = client->available();
        if (availableBytes > 0) {
            int bytesToRead = min((int)availableBytes, (int)sizeof(buffer));
            int bytesRead = client->readBytes(buffer, bytesToRead);
            
            if (bytesRead > 0) {
                // Update hash calculation
                mbedtls_sha256_update(&sha256_ctx, buffer, bytesRead);
                
                // Write to flash
                if (Update.write(buffer, bytesRead) != bytesRead) {
                    lastError = "Write failed: " + String(Update.errorString());
                    Update.abort();
                    http.end();
                    mbedtls_sha256_free(&sha256_ctx);
                    return false;
                }
                
                totalBytes += bytesRead;
                updateProgress = (totalBytes * 100) / contentLength;
                
                // Trigger progress callback
                if (onProgress) {
                    onProgress(updateProgress);
                }
                
                Serial.printf("[OTA] Progress: %d%% (%d/%d bytes)\n", 
                             updateProgress, totalBytes, contentLength);
            }
        }
        delay(1); // Yield to other tasks
    }
    
    http.end();
    
    // Finalize hash calculation
    uint8_t calculatedHash[32];
    mbedtls_sha256_finish(&sha256_ctx, calculatedHash);
    mbedtls_sha256_free(&sha256_ctx);
    
    // Convert hash to hex string
    String calculatedHashStr = "";
    for (int i = 0; i < 32; i++) {
        calculatedHashStr += String(calculatedHash[i], HEX);
    }
    calculatedHashStr.toLowerCase();
    
    // Verify hash
    if (expectedHash.length() > 0 && calculatedHashStr != expectedHash.toLowerCase()) {
        lastError = "Hash verification failed";
        Update.abort();
        return false;
    }
    
    // Verify signature if enabled
    if (verifySignature && !verifyFirmwareSignature(buffer, totalBytes, 
                                                   preferences.getString("update_signature", ""))) {
        lastError = "Signature verification failed";
        Update.abort();
        return false;
    }
    
    // Finalize update
    if (!Update.end(true)) {
        lastError = "Update end failed: " + String(Update.errorString());
        return false;
    }
    
    Serial.println("[OTA] Firmware download and verification completed");
    return true;
}

bool OTAManager::verifyFirmwareSignature(const uint8_t* firmware, size_t size, const String& signature) {
    if (publicKey.length() == 0 || signature.length() == 0) {
        return false;
    }
    
    // This is a simplified signature verification
    // In production, implement proper RSA/ECDSA signature verification
    Serial.println("[OTA] Signature verification not fully implemented");
    return true; // Placeholder
}

bool OTAManager::enableSignatureVerification(const String& key) {
    publicKey = key;
    verifySignature = true;
    preferences.putString("public_key", key);
    preferences.putBool("verify_sig", true);
    return true;
}

void OTAManager::disableSignatureVerification() {
    verifySignature = false;
    preferences.putBool("verify_sig", false);
}

bool OTAManager::canRollback() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* update = esp_ota_get_next_update_partition(NULL);
    
    return (running != NULL && update != NULL);
}

bool OTAManager::performRollback() {
    const esp_partition_t* update = esp_ota_get_next_update_partition(NULL);
    
    if (update == NULL) {
        lastError = "No rollback partition available";
        return false;
    }
    
    esp_err_t err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        lastError = "Rollback failed: " + String(esp_err_to_name(err));
        return false;
    }
    
    Serial.println("[OTA] Rollback initiated, restarting...");
    delay(1000);
    ESP.restart();
    
    return true;
}

// Getter methods
bool OTAManager::isUpdateInProgress() { return updateInProgress; }
int OTAManager::getUpdateProgress() { return updateProgress; }
String OTAManager::getCurrentVersion() { return currentVersion; }
String OTAManager::getLastError() { return lastError; }

// Callback setters
void OTAManager::setProgressCallback(UpdateProgressCallback callback) { onProgress = callback; }
void OTAManager::setCompleteCallback(UpdateCompleteCallback callback) { onComplete = callback; }
void OTAManager::setAvailableCallback(UpdateAvailableCallback callback) { onAvailable = callback; }

void OTAManager::resetUpdateState() {
    updateInProgress = false;
    updateProgress = 0;
    currentRetries = 0;
    lastError = "";
}

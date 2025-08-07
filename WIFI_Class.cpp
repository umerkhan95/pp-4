#include "WIFI_Class.h"

bool WIFI::isConnected = false;
wifi_credential_t WIFI::credentials = {0};
BLEHandler _ble;
TaskHandle_t WIFI::wifiTaskHandle = nullptr;
void (*WIFI::connectedCallback)() = nullptr;
WiFiScanClass wifi_scan;
bool WIFI::hasWiFiSSID = false;
bool WIFI::hasWiFiPass = false;

/**
 * @brief Initialize the WiFi module and BLE service
 * 
 */
void WIFI::init() {
  Serial.println("[WIFI] WiFi init");
  nvs_flash_init();

  hasWiFiSSID = loadFromNVS("wifi_ssid", credentials.ssid, sizeof(credentials.ssid));
  hasWiFiPass = loadFromNVS("wifi_pass", credentials.pass, sizeof(credentials.pass));
  Serial.println("SSID: " + String(credentials.ssid) + " Pass: " + String(credentials.pass));
  WiFi.mode(WIFI_STA);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf((char *)g_ret_mac_id, sizeof(g_ret_mac_id), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  scanAndSendAPs((char*)g_ret_list_ap);

  if (wifiTaskHandle == nullptr) {
    xTaskCreate([](void*) {
      delay(500);
      while (true) {
        handleBLECommand();
        if (!isConnected) connect();
        else _ble.end();
        vTaskDelay(pdMS_TO_TICKS(3000));
      }
    }, "wifi_loop", 12000, nullptr, 3, &wifiTaskHandle);
  }

  _ble.begin();
}

/**
 * @brief Connect to the WiFi network
 * 
 */
void WIFI::connect() {
    WiFi.mode(WIFI_STA);
    WiFi.onEvent([](WiFiEvent_t event) {
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            if (!isConnected) {
                isConnected = true;
                Serial.println("WiFi connected with IP: " + WiFi.localIP().toString());
                
                // Tránh gọi callback trong interrupt context
                // if (connectedCallback) {
                    // Tạo task riêng để xử lý callback
                    xTaskCreate([](void* parameter) {
                        // Delay để đảm bảo WiFi ổn định
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        if(_ble.pWIFIStatusChar) {
                            _ble.pWIFIStatusChar->setValue("WIFI_CONNECTED");
                            _ble.pWIFIStatusChar->notify();
                        }
                        vTaskDelay(pdMS_TO_TICKS(500));
                        _ble.end();
                        vTaskDelay(pdMS_TO_TICKS(500));
                        Serial.println("[WIFI]: Calling callback function");
                        // connectedCallback();
                        vTaskDelete(NULL);
                    }, "WiFi_CB", 4096, NULL, 1, NULL);
                // }
            }
        }
    });
    Serial.println("SSID: " + String(credentials.ssid) + " Pass: " + String(credentials.pass));
    if (strlen(credentials.ssid) > 0 && strlen(credentials.pass) > 0) {
      Serial.println("SSID: " + String(credentials.ssid) + " Pass: " + String(credentials.pass));
        WiFi.begin(credentials.ssid, credentials.pass);
        vTaskDelay(pdMS_TO_TICKS(5000));
        Serial.println("WiFi status: " + String(WiFi.status()));
        if (WiFi.status() == WL_CONNECTED) isConnected = true;
        else Serial.println("Initial WiFi connect failed");
    }
}

/**
 * @brief Disconnect from the WiFi network
 * 
 */
void WIFI::disconnect() {
  WiFi.disconnect();
  // _ble.pWIFIStatusChar->setValue("WIFI_DISCONNECTED");
  // _ble.pWIFIStatusChar->notify();
  isConnected = false;
}

/**
 * @brief Handle BLE commands received from the app
 * 
 */
void WIFI::handleBLECommand() {
  char* input = _ble.ble_app_return_rx_buffer();
  if (input == nullptr) return;

  Serial.printf("BLE received: [%s]\n", input);
  if (strcmp(input, "RELOAD_WIFI\n") == 0) {
    Serial.println("[WIFI] WiFi Reload");
    scanAndSendAPs((char*)g_ret_list_ap);
    _ble.instance->sendSSID((char*)g_ret_list_ap);
  }
  else if (strncmp(input, "EMAIL=", 6) == 0) {
    Serial.println("[WIFI] Received Email");
    // Parse email and password
    char* email_start = input + 6;
    char* pass_start = strstr(input, ";PASS=");

    if (pass_start) {
      *pass_start = '\0';  // terminate email string
      pass_start += 6;     // move past ";PASS="

      Serial.printf("Parsed Email: [%s]\n", email_start);
      Serial.printf("Parsed Password: [%s]\n", pass_start);

      saveToNVS("user_email", email_start);
      saveToNVS("user_pw", pass_start);
    } else {
      Serial.println("Invalid format. Expected: EMAIL=...;PASS=...");
    }
  }
  else {
    Serial.println("[WIFI] Receive WiFi Credentials");
    char ssid[MAX_WIFI_SSID_LEN] = {0};
    char pass[MAX_WIFI_PASS_LEN] = {0};
    if (parseWiFiCommand(input, ssid, pass)) {
      strncpy(credentials.ssid, ssid, sizeof(credentials.ssid));
      strncpy(credentials.pass, pass, sizeof(credentials.pass));
      saveToNVS("wifi_ssid", credentials.ssid);
      saveToNVS("wifi_pass", credentials.pass);
      disconnect();  // reset connection
    }
  }
  memset(input, 0, strlen(input));
}

/**
 * @brief Scan for available WiFi networks and send the list to the BLE client
 * 
 * @param ap_list The buffer to store the list of available SSIDs
 */
void WIFI::scanAndSendAPs(char* ap_list) {
  char* wifi_ap_list = (char*)malloc(MAX_WIFI_LIST_LEN);
  if (!wifi_ap_list) {
    Serial.println("Malloc wifi_ap_list failed");
    return;
  }
  memset(wifi_ap_list, 0, MAX_WIFI_LIST_LEN);

  if (isConnected) disconnect();

  int16_t num_of_wifi = wifi_scan.scanNetworks();
  if (num_of_wifi > 10) num_of_wifi = 10;

  if (num_of_wifi > 0 && wifi_scan.scanComplete()) {
    for (int i = 0; i < num_of_wifi; ++i) {
      String ssid = wifi_scan.SSID(i);
      if (ssid.length() == 0) continue;
      strncat(wifi_ap_list, ssid.c_str(), ssid.length());
      strcat(wifi_ap_list, ",");
      delay(10 / portTICK_PERIOD_MS);
    }
    memcpy(ap_list, wifi_ap_list, strlen(wifi_ap_list) + 1);
  }
  wifi_scan.scanDelete();
  free(wifi_ap_list);
}

/**
 * @brief Parse the WiFi command received from the app
 * 
 * @param input The command string
 * @param ssid Output buffer for SSID
 * @param pass Output buffer for password
 * @return true if parsing was successful, false otherwise
 */
bool WIFI::parseWiFiCommand(const char* input, char* ssid, char* pass) {
  const char* prefix = "WIFI:";
  if (strncmp(input, prefix, strlen(prefix)) != 0) return false;

  const char* ssidStart = input + strlen(prefix);
  const char* colon = strchr(ssidStart, ':');
  if (!colon) return false;

  size_t ssidLen = colon - ssidStart;
  size_t passLen = strlen(colon + 1);

  if (ssidLen >= MAX_WIFI_SSID_LEN || passLen >= MAX_WIFI_PASS_LEN) return false;

  strncpy(ssid, ssidStart, ssidLen); ssid[ssidLen] = '\0';
  strncpy(pass, colon + 1, passLen); pass[passLen] = '\0';

  Serial.printf("parse_wifi_info:  ssid: %s  -  pass: %s", ssid, pass);

  return true;
}

/**
 * @brief Save a value to NVS with the specified key
 * 
 * @param key The key to save the value under
 * @param val The value to save
 */
void WIFI::saveToNVS(const char* key, const char* val) {
  nvs_handle h;
  if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_str(h, key, val);
    nvs_commit(h);
    nvs_close(h);
  }
}

/**
 * @brief Load a value from NVS with the specified key
 * 
 * @param key The key to load the value from
 * @param out Output buffer for the loaded value
 * @param len Length of the output buffer
 * @return true if loading was successful, false otherwise
 */
bool WIFI::loadFromNVS(const char* key, char* out, size_t len) {
  nvs_handle h;
  size_t required = len;
  if (nvs_open("storage", NVS_READONLY, &h) != ESP_OK) return false;
  if (nvs_get_str(h, key, out, &required) != ESP_OK) {
    nvs_close(h);
    return false;
  }
  nvs_close(h);
  return true;
}

/**
 * @brief Set a callback function to be called when WiFi is connected
 * 
 * @param cb The callback function to set
 */
void WIFI::setConnectedCallback(void (*cb)()) {
  connectedCallback = cb;
}

/**
 * @brief Get the MAC address of the WiFi interface
 * 
 * @return String The MAC address as a string
 */
String WIFI::getMacAddress() {
  return WiFi.macAddress();
}

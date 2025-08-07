#include "HardwareSerial.h"
#include "BLE_Class.h"


uint8_t g_ret_mac_id[MAX_MAC_ID_LEN]    = {0x11, 0x22, 0x33, 0x44};
uint8_t g_ret_list_ap[MAX_LIST_AP_LEN]  = {0xFF, 0x22, 0x33, 0x44, 0x55, 0};
char rxBuffer[MAX_LEN_RX_BUFFER];
bool isRxBufferDone = false;
size_t rxOffSet = 0;

BLEHandler* BLEHandler::instance = nullptr;

/**
 * @brief Construct a new BLEHandler::BLEHandler object
 * 
 */
BLEHandler::BLEHandler() {
  pServer = nullptr;
  pService = nullptr;
  pSSIDRequestChar = nullptr;
  pSSIDResponseChar = nullptr;
  pWIFIStatusChar = nullptr;
  is_ble_start = false;
  instance = this;
}

/**
 * @brief Start the BLE service
 * 
 */
void BLEHandler::begin() {
  if (!is_ble_start) {
    Serial.println("Starting BLE......");
    uint8_t mac[6];
    sscanf((const char*)g_ret_mac_id, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX", 
           &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    
    // Create device name using last 2 bytes
    char deviceName[32];
    snprintf(deviceName, sizeof(deviceName), "Porch-Potty-BLE-%02X%02X",
             mac[4], mac[5]);  // Use last 2 bytes (0C:BC)
    
    Serial.printf("Creating BLE device with name: %s\n", deviceName);
    BLEDevice::init(deviceName);

    // GATT Server
    pServer = BLEDevice::createServer();

    // Service
    pService = pServer->createService(SERVICE_UUID);

    // Write/Notify wifi status
    pWIFIStatusChar = pService->createCharacteristic(
      CHAR_WIFI_STATUS,
      BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_NOTIFY
    );
    pWIFIStatusChar->setCallbacks(new BLECallbacks());
    

    // Request char (App → ESP32)
    pSSIDRequestChar = pService->createCharacteristic(
      CHAR_SSID_REQUEST,
      BLECharacteristic::PROPERTY_WRITE
    );
    pSSIDRequestChar->setCallbacks(new BLECallbacks());

    // Response char (ESP32 → App)
    pSSIDResponseChar = pService->createCharacteristic(
      CHAR_SSID_RESPONSE,
      BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_NOTIFY
    );
    // pSSIDResponseChar->addDescriptor(new BLE2902());

    BLEDevice::setMTU(MAX_MTU_SIZE); // đề nghị MTU cao hơn mặc định
    connectID = pServer->getConnId();
    pService->start();

    // Advertising
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->start();

    pWIFIStatusChar->setValue((const char*)g_ret_mac_id);
    pWIFIStatusChar->notify();

    pSSIDResponseChar->setValue((const char*)g_ret_list_ap);

    is_ble_start = true;

    Serial.println("BLE service with multiple characteristics is advertising...");
  }
  
}

/**
 * @brief Stop the BLE service
 * 
 */
void BLEHandler::end() {
    if (!is_ble_start) {
        return;
    }

    Serial.println("Stopping BLE...");
    
    // 1. Dừng advertising trước
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->stop();
        delay(100);
    }

    // 2. Ngắt kết nối client nếu có
    if (pServer && pServer->getConnectedCount() > 0) {
        // Disconnect the current connection if exists
        uint16_t connId = pServer->getConnId();
        if (connId != 0xFFFF) {
            pServer->disconnect(connId);
            delay(100);
        }
    }

    // 3. Dừng service
    if (pService) {
        pService->stop();
        delay(100);
    }

    // 4. Xóa callbacks và descriptors
    if (pWIFIStatusChar) {
        pWIFIStatusChar->setCallbacks(nullptr);
    }
    if (pSSIDRequestChar) {
        pSSIDRequestChar->setCallbacks(nullptr);
    }

    // 5. Reset các con trỏ
    pWIFIStatusChar = nullptr;
    pSSIDRequestChar = nullptr;
    pSSIDResponseChar = nullptr;
    pService = nullptr;
    pServer = nullptr;

    // 6. Deinit BLE
    BLEDevice::deinit(false);
    delay(200);

    is_ble_start = false;
    Serial.println("BLE stopped successfully");
}

/**
 * @brief Send the list of SSIDs to the BLE client
 * 
 * @param ssid_list The list of SSIDs to send
 */
void BLEHandler::sendSSID(char* ssid_list) {
  Serial.println("Ready to send: " + (String)ssid_list);

  if (pSSIDResponseChar) {
    pSSIDResponseChar->setValue(ssid_list);
    pSSIDResponseChar->notify();
  }
}

/**
 * @brief Callback to handle write requests from the BLE client
 * 
 * @param pChar The characteristic that received the write request
 */
// Callback xử lý yêu cầu từ app
void BLEHandler::BLECallbacks::onWrite(BLECharacteristic* pChar) {
  String val = pChar->getValue();
  Serial.println("[BLE] App requested: " + String(val.c_str()) + " len: " + String(pChar->getLength()));

  for (size_t i = 0; i < pChar->getLength(); i++) {
    char c = val[i];

    if (rxOffSet < MAX_LEN_RX_BUFFER - 1) {
      rxBuffer[rxOffSet++] = c;

      if (c == '\n'){
        rxBuffer[rxOffSet-1] = '\0';
        isRxBufferDone = true;
        Serial.printf("[BLE] Full command received: %s\n", rxBuffer);

        rxOffSet = 0;
        
        break;
      }
    }
    else {
      rxOffSet = 0;
      memset(rxBuffer, 0, sizeof(rxBuffer));  // <-- Sửa lỗi bằng cách này
      break;
    }
  }
}

/**
 * @brief Return the received command buffer
 * 
 * @return char* Pointer to the received command buffer
 */
char* BLEHandler::ble_app_return_rx_buffer(void) {
  if (isRxBufferDone == true)
  {
    isRxBufferDone = false;
    rxOffSet = 0;
    Serial.printf("return rxBuffer: %s\n", rxBuffer);
    return rxBuffer;
  }

  return NULL;
}

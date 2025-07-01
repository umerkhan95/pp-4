#include "WiFiClient.h"
#include "WIFI_Class.h"
#include <WiFiManager.h>
#include <WiFiUdp.h>

WiFiManager wm;
WiFiScanClass wifi_scan;
WiFiUDP udp;

bool WIFI::statusWifi = false;
char WIFI::ssid_list[WIFI_AP_SSID_LEN] = {0};
TaskHandle_t WIFI::checkWifiTaskHandle = NULL;
TaskHandle_t WIFI::loopWebPortalTaskHandle = NULL;

void WIFI::ConnectWifi(void) {
  wm.setConfigPortalBlocking(false);
  statusWifi = wm.autoConnect("AutoConnectAP", "12345678");

  if (checkWifiTaskHandle == NULL) 
    xTaskCreate(check_wifi_connection, "Task check wifi", 4096, NULL, 3, &(checkWifiTaskHandle));
  if (!WiFi.isConnected() && loopWebPortalTaskHandle == NULL)
    xTaskCreate(loop_webPortal, "Task loop WebPortal", 4096, NULL, 2, &loopWebPortalTaskHandle);
}

void WIFI::DisconnectWifi(void) {
  wm.disconnect();
  if (checkWifiTaskHandle != NULL) {
    vTaskDelete(checkWifiTaskHandle);
  }
}

void WIFI::wifi_scan_handle(void) {
  char* wifi_ap_list = (char*)malloc(WIFI_AP_SSID_LEN);
  if (!wifi_ap_list) {
    Serial.println("Malloc wifi_ap_list failed");
    return;
  }
  memset(wifi_ap_list, 0, WIFI_AP_SSID_LEN);
  if (statusWifi) {
    DisconnectWifi();
  }
  int16_t num_of_wifi = wifi_scan.scanNetworks();
  if (num_of_wifi > DEFAULT_SCAN_LIST_SIZE) num_of_wifi = DEFAULT_SCAN_LIST_SIZE;
  if (num_of_wifi != 0 && wifi_scan.scanComplete()) {
    for (int i = 0; i < num_of_wifi; ++i) {
      String ssid = wifi_scan.SSID(i);
      Serial.printf("[%d] SSID: %s\n", i + 1, ssid.c_str());
      if (strlen(ssid.c_str()) == 0) continue;
      strncat(wifi_ap_list, ssid.c_str(), strlen(ssid.c_str()));
      strcat(wifi_ap_list, CHAR_SEPARATE_SSID_STR);
      delay(10 / portTICK_PERIOD_MS);
    }
    memcpy(WIFI::ssid_list, wifi_ap_list, strlen(wifi_ap_list) + 1);
  }
  wifi_scan.scanDelete();
  free(wifi_ap_list);
}

void WIFI::taskScanWifi(void *param) {
  Serial.println("SSID_list: " + String(WIFI::ssid_list));
  for (;;) {
    if (Serial.available()) {
      String recv = Serial.readStringUntil('\n');
      if (recv.indexOf("scan") != -1) {
        Serial.println("Scanning...");
        DisconnectWifi();
        wifi_scan_handle();
        Serial.println("SSID_list: " + String(WIFI::ssid_list));
        WIFI::ConnectWifi();
        wifi_scan.scanDelete();
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void WIFI::check_wifi_connection(void *param) {
  for (;;) {
    while (!statusWifi) {
      Serial.println("Attempting to connect to WiFi...");
      WIFI::ConnectWifi();
      if (!statusWifi) {
        Serial.println("Failed to connect. Retrying in 5 seconds...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
      }
    }
    Serial.println("Connected");
    vTaskDelay(30000 / portTICK_PERIOD_MS);
  }
}

void WIFI::loop_webPortal(void *param) {
  for (;;) {
    wm.process();  // Need to loop to maintain web portal

    // If wifi connected, delete this task
    if (WiFi.isConnected()) {
      Serial.println("[WiFiManager] Connected to AP via portal.");
      statusWifi = true;

      if (loopWebPortalTaskHandle != NULL) {
        vTaskDelete(loopWebPortalTaskHandle);
        loopWebPortalTaskHandle = NULL;
      }

    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
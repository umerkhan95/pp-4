#ifndef _WIFI_H_
#define _WIFI_H_

#include <Arduino.h>

#define DEFAULT_SCAN_LIST_SIZE 10
#define WIFI_AP_SSID_LEN DEFAULT_SCAN_LIST_SIZE * 33
#define CHAR_SEPARATE_SSID_STR "|"

class WIFI {
private:
  static TaskHandle_t checkWifiTaskHandle;
  static void check_wifi_connection(void* param);
  static void taskScanWifi(void* param);

public:
  static bool statusWifi;
  static char ssid_list[WIFI_AP_SSID_LEN];

  static void wifi_scan_handle(void);
  static void ConnectWifi(void);
  static void DisconnectWifi(void);
  static bool getWifiStatus() { return statusWifi; }
};


#endif //_WIFI_H_
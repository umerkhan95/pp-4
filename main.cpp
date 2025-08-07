#include "esp32-hal-timer.h"
#include "esp32-hal-ledc.h"
#include "main.h"
// ============================================================CLASS OBJECT============================================================
WIFI _wifi;
// MQTT _mqtt;
POSTGET _httpClient;
// ====================================================================================================================================

// =============================================================GLOBAL VAR=============================================================
TimeSys_t _timeSys;
configPump_t _sprinkler;
configPump_t _filter;

bool isManualRunning = false;
bool haveMessageViaMQTT;
bool hasNewSettings = false; // Flag to track if new settings have been received

bool isTimeInitialized = false; // Flag to track if time has been initialized
bool isTimeSyncedFromApp = false; // Flag to track if time has been synced from app

const char* loginUrl = "https://porchpotty.codefied.co/api/login.php";
const char* updateStatusUrl = "https://porchpotty.codefied.co/api/update_device_status.php";
const char* getStatusUrl = "https://porchpotty.codefied.co/api/get_device_status.php";
const char* getScheduleUrl = "https://porchpotty.codefied.co/api/get_sprinkler_schedule.php";
String jwtToken = "";
String user_email = "";
String user_password = "";
String device_id = "";
bool isConfigured = false;

uint32_t lastTokenRefresh;
uint32_t lastPost;
uint32_t lastStatusCheck;
uint32_t lastScheduleCheck;
uint32_t currentMillis;

hw_timer_t *cycleTimer = NULL;
hw_timer_t *countToStartTimer = NULL;
hw_timer_t *tickTimer = NULL;

struct tm currentTime;
time_t startupTime = 0;
volatile uint64_t tickSecond = 0;
uint64_t lastProcessedTimeSync = 0; // Track the last tickSecond processed
uint64_t lastCycleTick = 0; // Theo dõi tick cuối cùng khi chu kỳ được kích hoạt
uint32_t nextTickPeriod = 0;

uint32_t start_time_sprinkler[2] = {0, 0};
bool isMotorRunning = false;        // Flag to ensure only 1 motor run at the same time
bool sprinklerJustFinished = false; // Flag check if sprinkler have done duration
bool filterDoneToday = false;       // Flag check if Filter run today yet
int startMinuteSprinkler;           // Start time of Sprinkler in minute
int currentSprinklerCycle = 0;
int lastRunDay = -1;
String macAddress;

// typedef void (*MQTTReconnectFunc) ();

SemaphoreHandle_t timerMux = NULL;
// SemaphoreHandle_t settingsMux = NULL;
// Task handles
TaskHandle_t saveSettingsTaskHandle;
TaskHandle_t autoTaskHandle;
TaskHandle_t manualTaskHandle;
// ====================================================================================================================================

// ===============================================================SETUP================================================================
/**
 * @brief Initialize the system
 * 
 */
void Setup() {
  Serial.begin(115200);
  Serial.println("VERSION: " + String(VERSION));
  //=================================DEFINE PINS=================================
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  pinMode(FILTER_PIN, OUTPUT);
  pinMode(SPRINKLER_PIN, OUTPUT);

  // Wire.begin(I2C_SDA, I2C_SCL);

  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_RED_PIN, HIGH);
  digitalWrite(LED_BLUE_PIN, HIGH);
  digitalWrite(FILTER_PIN, LOW);
  digitalWrite(SPRINKLER_PIN, HIGH);

  //=====================================LEDC====================================
  ledcAttach(SPRINKLER_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(SPRINKLER_PIN, 255);
  //====================================FLASH====================================
  loadConfiguration();
  //====================================WI-FI & MQTT====================================
  // _wifi.wifi_scan_handle();
  // _wifi.ConnectWifi();
  _wifi.init();
  delay(2000);
  macAddress = _wifi.getMacAddress();
  Serial.println("MAC Address: " + macAddress);

  // MQTTReconnectFunc reconnect_handler = reConfigMQTT; // function pointer point to function reConfigMQTT()
  // _wifi.setConnectedCallback(reconnect_handler);
  // _mqtt.setMessageHandler(handleMQTTSettings);
  Serial.println("Period: " + String(_timeSys.period * HOUR));
  startTimer(&timer_itr, &cycleTimer, 1); // timer count every 1 second
  //====================================TASKS====================================
  xTaskCreate(taskAuto, "Auto Task", 8192, NULL, configMAX_PRIORITIES - 1, &autoTaskHandle);
  xTaskCreate(taskManual, "Manual Task", 4096, NULL, 5, &manualTaskHandle);
  xTaskCreate(taskSaveSettings, "Task Save Settings", 4096, NULL, 2, &saveSettingsTaskHandle);
  xTaskCreate(taskUpdateTime, "Update Time Task", 2048, NULL, 3, NULL);
  xTaskCreate(taskPOSTGET, "POST GET Task", 8192, NULL, 4, NULL);
#ifdef DEBUG
  Serial.println("11Initialized testFunction task==============================================");
  xTaskCreate(testFunction_viaSerial, "Test Serial", 4096, NULL, 6, NULL);
  Serial.println("Initialized testFunction task==============================================");
#endif
  //=====================================END=====================================
  Serial.println("End setup.");
}

// ===============================================================LOOP================================================================
void Loop() {
  vTaskDelay(1 / portTICK_PERIOD_MS);
}

// ==============================================================TASKS================================================================
/**
 * @brief Task to handle POST and GET requests
 * 
 * This task periodically checks for updates, refreshes the JWT token, and retrieves device status.
 */
void taskPOSTGET(void *param) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      _wifi.connect();
      continue;
    }

    currentMillis = millis();
    Serial.println(currentMillis);

    // Refresh token periodically
    if (currentMillis - lastTokenRefresh > TOKEN_REFRESH_INTERVAL) {
      Serial.println("Refreshing token...");
      loginAndGetToken();
      lastTokenRefresh = currentMillis;
    }

    // Check for pump commands from server
    if (currentMillis - lastStatusCheck > STATUS_CHECK_INTERVAL) {
      Serial.println("Get Pumps status...");
      getDeviceStatus();
      lastStatusCheck = currentMillis;
    }
  }
}

// Task to update time every minute
/**
 * @brief Task to update time every minute
 * 
 * This task checks if the time is initialized and updates the time information.
 */
void taskUpdateTime(void *param) {
  for (;;) {
    if (isTimeInitialized) {
      updateTimeInfo();
    }
    vTaskDelay(60000 / portTICK_PERIOD_MS); // Delay 60 seconds
  }
}

// Task save settings received via MQTT Broker
/**
 * @brief Task to save settings received via MQTT Broker
 * 
 * This task waits for notifications to save settings to NVS.
 */
void taskSaveSettings(void *param) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (haveMessageViaMQTT) {
      Serial.println("Saving settings to NVS...");
      writeValueToNVS("ds", _sprinkler.duration);
      writeValueToNVS("cycles", _sprinkler.cyclesInDay);
      writeValueToNVS("df", _filter.duration);
      saveDaysToNVS(_timeSys.days);
      writeValueToNVS("start_hour", start_time_sprinkler[0]);
      writeValueToNVS("start_minute", start_time_sprinkler[1]);
      writeValueToNVS("has_settings", 1); // Save hasNewSettings as true
      hasNewSettings = true;
      haveMessageViaMQTT = false;
      Serial.println("Settings saved to NVS.");
    }
  }
}

// Task run pumps automatically
/**
 * @brief Task to run pumps automatically based on the schedule
 * 
 * This task checks the current time and runs the sprinkler and filter pumps according to the configured schedule.
 */
void taskAuto(void *param) {
  for (;;) {
    // Chờ thông báo từ ISR hoặc timeout sau 1 giây để kiểm tra thời gian
    if (!hasNewSettings && _timeSys.firstTime == 1) {
      Serial.println("No new settings received, running default configuration...");
      _sprinkler.autoControl = true;
      runSprinkler();
      _sprinkler.autoControl = false;
      sprinklerJustFinished = true;

      if (!filterDoneToday) {
        sprinklerJustFinished = false;
        runFilter();
      }
      
      _timeSys.firstTime = 0;
      // currentSprinklerCycle = 1;
      // nextTickPeriod = start_time_sprinkler[0] * 3600 + start_time_sprinkler[1] * 60 + 24 * HOUR; // 0:00 ngày tiếp theo
       nextTickPeriod = 24 * HOUR; // 0:00 ngày tiếp theo
      vTaskDelay(10);
      continue;
    }

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    Serial.println("Receive notify");

    int wday = currentTime.tm_wday;
    int hour = currentTime.tm_hour;
    int min  = currentTime.tm_min;
    int sec  = currentTime.tm_sec;

    int minuteNow = hour * 60 + min;

    if (isTimeInitialized && wday != lastRunDay && currentSprinklerCycle == _sprinkler.cyclesInDay) {
      Serial.println("New day detected! Reset variables.");
      currentSprinklerCycle = 0;
      _timeSys.firstTime = 1;
      filterDoneToday = false;
      lastRunDay = wday;
      time_t rawTime = mktime(&currentTime) + 24 * 3600;
      currentTime = *localtime(&rawTime);
      saveTimeToNVS(rawTime);
      nextTickPeriod = 0;
    }

    if (isTimeInitialized && hasNewSettings && _timeSys.days[wday] == 1 && !isManualRunning) {
      if (currentSprinklerCycle >= _sprinkler.cyclesInDay) continue;

      if (!isMotorRunning && !_sprinkler.manual) {
        Serial.println("Sprinkler running auto");
        _sprinkler.autoControl = true;
        runSprinkler();
        _sprinkler.autoControl = false;
        sprinklerJustFinished = true;
        currentSprinklerCycle++;
        nextTickPeriod = currentSprinklerCycle * _timeSys.period * HOUR; // 0:00 ngày tiếp theo
      }
    }
    if (isTimeInitialized && minuteNow >= startMinuteSprinkler && !filterDoneToday && !_filter.manual && !isMotorRunning) {
      if (sprinklerJustFinished || _timeSys.days[wday] == 0) {
        sprinklerJustFinished = false;
        runFilter();
      }
    }

    if (!isManualRunning) {
      digitalWrite(SPRINKLER_PIN, HIGH);
      ledcWrite(SPRINKLER_PIN, 255);
      digitalWrite(FILTER_PIN, LOW);
      digitalWrite(LED_GREEN_PIN, HIGH);
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(LED_BLUE_PIN, HIGH);
    }
    Serial.println("Done running");

    vTaskDelay(10);
  }
}

// Task run pumps manually
// Need to change logic run (PWM increase & decrease continuously)
/**
 * @brief Task to run pumps manually
 * 
 * This task checks the manual control flags from the app and runs the sprinkler and filter pumps accordingly.
 */
void taskManual(void *param) {
  for (;;) {
    // ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (_sprinkler.autoControl || _filter.autoControl) {
      // Serial.println("Auto control is ON, continue");
      continue;
    }
    // isManualRunning = false; // default set to false, be true if there is a SPRINKLER_PIN on

    // Check and control Sprinkler - Yellow LED
    if (_sprinkler.manual == 1 && !isMotorRunning) {
      Serial.println("Manual Sprinkler ON");
      isMotorRunning = true;
      isManualRunning = true;
      digitalWrite(LED_GREEN_PIN, HIGH);
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(LED_BLUE_PIN, LOW);
      Serial.println("PWM increase");
      ledcFade(SPRINKLER_PIN, 200, 0, 2000);
      vTaskDelay(pdMS_TO_TICKS(2000));

      vTaskDelay(pdMS_TO_TICKS(2000));

      Serial.println("PWM decrease");
      ledcFade(SPRINKLER_PIN, 0, 200, 2000);
      vTaskDelay(pdMS_TO_TICKS(2000));
    } else if (_sprinkler.manual == 0) {
      Serial.println("Manual Sprinkler OFF");
      // Decrease gradually PWM for Sprinkler in ~5s
      ledcFade(SPRINKLER_PIN, 0, 255, 2000);
      vTaskDelay(pdMS_TO_TICKS(2000));
      digitalWrite(SPRINKLER_PIN, HIGH);
    }

    // Check and control Filter - Purple LED
    if (_filter.manual == 1 && !isMotorRunning) {
      Serial.println("Manual Filter ON");
      isMotorRunning = true;
      isManualRunning = true;
      digitalWrite(FILTER_PIN, HIGH); // Turn ON FILTER_PIN
      digitalWrite(LED_GREEN_PIN, LOW);
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(LED_BLUE_PIN, HIGH);
    } else if (_filter.manual == 0) {
      Serial.println("Manual Filter OFF");
      digitalWrite(FILTER_PIN, LOW); // Turn OFF FILTER_PIN
    }

    // Update isMotorRunning & isManualRunning
    isMotorRunning = (_sprinkler.manual == 1 || _filter.manual == 1);
    if (!isMotorRunning) {
      digitalWrite(LED_GREEN_PIN, HIGH);
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(LED_BLUE_PIN, HIGH);
    }
    isManualRunning = false; // isManualRunning be true if there is a SPRINKLER_PIN ON
  }
}

#ifdef DEBUG
static uint8_t isNextDay = false;
// Task to simulate new day via Serial input
/**
 * @brief Task to simulate new day via Serial input
 *
 * This task reads time input from Serial and updates the current time accordingly for debugging.
 */
void testFunction_viaSerial(void *param) {
  for (;;) {
    Serial.println("Tick value now: " + String(tickSecond));
    Serial.println("nextTickPeriod: " + String(nextTickPeriod));
    Serial.println("Timer one shot tick: " + String(timerRead(countToStartTimer)));
    // mqtt_publish_message(WATER_LEVEL, macAddress.c_str(), String(tickSecond));
    if (Serial.available()) {
      String rec = Serial.readStringUntil('\n');
      Serial.println("Received value: " + rec);

      if (!hasNewSettings) {
        tickSecond = rec.toInt();
        Serial.println("Tick second manually set to: " + String(tickSecond));
        continue; // quay lại vòng lặp không làm gì thêm
      }

      // Parse thời gian từ chuỗi "HH:MM:SS"
      int hour, minute, second;
      if (sscanf(rec.c_str(), "%d:%d:%d", &hour, &minute, &second) == 3) {
        // Lấy ngày hiện tại từ currentTime
        struct tm newTime = currentTime;
        newTime.tm_hour = hour;
        newTime.tm_min = minute;
        newTime.tm_sec = second;

        // Tính toán tickSecond
        int64_t current_seconds = hour * 3600LL + minute * 60LL + second;
        int64_t start_seconds = start_time_sprinkler[0] * 3600LL + start_time_sprinkler[1] * 60LL;
        int64_t temp_tickSecond = current_seconds - start_seconds;

        // Xử lý trường hợp thời gian hiện tại nhỏ hơn start_time_sprinkler
        if (temp_tickSecond < 0) {
          temp_tickSecond += 24 * 3600LL;
          // Tăng ngày lên 1
          if (!isNextDay) {
            time_t rawTime = mktime(&newTime);
            if (rawTime != -1) {
              rawTime += 24 * 3600; // Cộng 1 ngày
              newTime = *localtime(&rawTime);
            }
            isNextDay = true;
          }
        }

        // Cập nhật currentTime
        currentTime = newTime;

        // Giới hạn tickSecond trong phạm vi 24 giờ
        tickSecond = temp_tickSecond % (24 * 3600LL);

        // Tìm chu kỳ tưới nước tiếp theo
        int i;
        for (i = 0; i <= _sprinkler.cyclesInDay; i++) {
          if (_timeSys.period * i * 3600LL > tickSecond) break;
        }
        // Tính số giây đến chu kỳ tiếp theo
        int64_t secondsToNextCycle;
        if (i < _sprinkler.cyclesInDay) {
          secondsToNextCycle = (_timeSys.period * i * 3600LL) - tickSecond;
        } else {
          // Nếu vượt quá chu kỳ trong ngày, chuyển sang chu kỳ đầu tiên của ngày tiếp theo
          secondsToNextCycle = (24 * 3600LL) - tickSecond;
        }

        // Khởi tạo hoặc cập nhật countToStartTimer
        if (countToStartTimer == NULL && hasNewSettings) {
          stopTimer(&cycleTimer);
          startTimer(&tickTimer_itr, &tickTimer, 1);
          startTimer(&countToStartTimer_itr, &countToStartTimer, secondsToNextCycle - 5);
        } else {
          // Dừng timer cũ và khởi động lại
          stopTimer(&countToStartTimer);
          stopTimer(&cycleTimer);
          startTimer(&countToStartTimer_itr, &countToStartTimer, secondsToNextCycle - 5);
          startTimer(&tickTimer_itr, &tickTimer, 1);
        }
        Serial.println("Write tick to timer: " + String(secondsToNextCycle * 1000000LL));

        // Cập nhật startupTime để đồng bộ với currentTime
        time_t rawTime = mktime(&currentTime);
        if (rawTime != -1) {
          startupTime = rawTime - (tickSecond % (24 * 3600LL));
          lastProcessedTimeSync = tickSecond;
          saveTimeToNVS(rawTime);
          Serial.println("Time updated from Serial: ");
          Serial.println(&currentTime, "%A, %B %d %Y %H:%M:%S");
        } else {
          Serial.println("Failed to convert time to time_t");
        }

      } else {
        Serial.println("Invalid time format. Please use HH:MM:SS");
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
#endif
// ===========================================================FLASH FUNCTIONs===========================================================
// Load Saved Configuration in NVS
/**
 * @brief Load configuration from NVS
 * 
 * This function initializes the NVS and reads the saved configuration values.
 */
void loadConfiguration(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Read hasNewSettings from NVS
  // hasNewSettings = readValueFromNVS("has_settings") != 0;
  hasNewSettings = false;
  writeValueToNVS("has_settings", 0);

  //Sprinkler duration
  _sprinkler.duration = readValueFromNVS("ds");
  if (_sprinkler.duration == 0 || !hasNewSettings) {
#ifndef DEBUG
    _sprinkler.duration = 4;
#else
    _sprinkler.duration = 1;
#endif
    writeValueToNVS("ds", _sprinkler.duration);
  }
  // Filter duration
  _filter.duration = readValueFromNVS("df");
  if (_filter.duration == 0 || !hasNewSettings) {
#ifndef DEBUG
    _filter.duration = 4;
#else
    _filter.duration = 1;
#endif
    writeValueToNVS("df", _filter.duration);
  }

  _sprinkler.cyclesInDay = readValueFromNVS("cycles");
  if (_sprinkler.cyclesInDay == 0 || !hasNewSettings) {
    _sprinkler.cyclesInDay = 1;
    writeValueToNVS("cycles", 1);
  }

  Serial.println("Days: ");
  if (!readDaysFromNVS(_timeSys.days) || !hasNewSettings) {
    for (int i = 0; i < 7; i++) {
      _timeSys.days[i] = 1; // Chạy tất cả các ngày
    }
    saveDaysToNVS(_timeSys.days);
  }

  _timeSys.firstTime = 1;
  _timeSys.period = 24 / _sprinkler.cyclesInDay;
  startMinuteSprinkler = start_time_sprinkler[0] * 60 + start_time_sprinkler[1]; // 0 phút (0:00)

  currentSprinklerCycle = 0;
  nextTickPeriod = currentSprinklerCycle * _timeSys.period * HOUR;
  
  user_email = readStringFromNVS("user_email");
  user_password = readStringFromNVS("user_pw");
  device_id = readStringFromNVS("device_id");

  // nếu cần fallback:
  user_email = user_email.length() ? user_email : "";
  user_password = user_password.length() ? user_password : "";
  device_id = device_id.length() ? device_id : "1";

  if (_wifi.hasWiFiSSID && _wifi.hasWiFiPass) {
    isConfigured = true;
  }
  

  // Show settings in NVS
  Serial.println("===========================SETTING===========================");
  Serial.println("Sprinkler Duration: " + String(_sprinkler.duration)+" Minute");
  Serial.println("Filter Duration: " + String(_filter.duration) + " Hour");
  Serial.println("Sprinkler Cycles: " + String(_sprinkler.cyclesInDay));
  Serial.println("Has New Settings: " + String(hasNewSettings));
  Serial.println("Start time: " + String(start_time_sprinkler[0]) + ":" + String(start_time_sprinkler[1]));
  Serial.println("next tick period in setup: " + String(nextTickPeriod));
  Serial.println("User email: " + user_email);
  Serial.println("User password: " + user_password);
  Serial.println("=============================================================");
}

// Read flash
/**
 * @brief Read an integer value from NVS
 * 
 * @param key The key to read the value from
 * @return int16_t The value read from NVS, or 0 if not found
 */
int16_t readValueFromNVS(const char* key) {
  nvs_handle my_handle;
  esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
  if (err != ESP_OK) {
    Serial.println("Failed to open NVS!");
  }

  int16_t value = 0;
  err = nvs_get_i16(my_handle, key, &value);

  nvs_close(my_handle);
  return value;
}

/**
 * @brief Read a string value from NVS
 * 
 * @param key The key to read the string from
 * @return String The string value read from NVS, or an empty string if not found
 */
String readStringFromNVS(const char* key) {
  nvs_handle handle;
  if (nvs_open("storage", NVS_READONLY, &handle) != ESP_OK) {
    Serial.println("Failed to open NVS!");
    return "";
  }

  size_t len = 0;
  if (nvs_get_str(handle, key, NULL, &len) != ESP_OK || len == 0) {
    nvs_close(handle);
    return "";
  }

  char value[len];
  if (nvs_get_str(handle, key, value, &len) != ESP_OK) {
    nvs_close(handle);
    return "";
  }

  nvs_close(handle);
  return String(value);
}

// Write flash
/**
 * @brief Write an integer value to NVS
 * 
 * @param key The key to write the value to
 * @param value The integer value to write
 */
void writeValueToNVS(const char* key, int16_t value) {
  nvs_handle my_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);

  err = nvs_set_i16(my_handle, key, value);
  ESP_ERROR_CHECK(err);

  err = nvs_commit(my_handle);
  ESP_ERROR_CHECK(err);

  nvs_close(my_handle);
}

/**
 * @brief Write a string value to NVS
 * 
 * @param key The key to write the string to
 * @param value The string value to write
 */
void writeValueToNVS(const char* key, String value) {
  nvs_handle my_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);

  err = nvs_set_str(my_handle, key, value.c_str());
  ESP_ERROR_CHECK(err);

  err = nvs_commit(my_handle);
  ESP_ERROR_CHECK(err);

  nvs_close(my_handle);
}

// Save scheduled days to flash
/**
 * @brief Save scheduled days to NVS
 * 
 * @param days Pointer to an array of integers representing the days of the week
 */
void saveDaysToNVS(int *days) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
  if (err == ESP_OK) {
      err = nvs_set_blob(handle, "days_array", days, sizeof(int) * 7);
      if (err == ESP_OK) {
          nvs_commit(handle);
      }
      nvs_close(handle);
  }
}

// Get scheduled days to flash
/**
 * @brief Read scheduled days from NVS
 * 
 * @param days Pointer to an array of integers where the days will be stored
 * @return bool True if read successfully, false otherwise
 */
bool readDaysFromNVS(int *days) {
  nvs_handle_t handle;
  size_t required_size = sizeof(int) * 7;
  esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
  if (err == ESP_OK) {
      err = nvs_get_blob(handle, "days_array", days, &required_size);
      nvs_close(handle);
      return err == ESP_OK;
  }
  return false;
}

// Save time to NVS
/**
 * @brief Save the current time to NVS
 * 
 * @param time The time to save, in seconds since epoch
 */
void saveTimeToNVS(time_t time) {
  nvs_handle my_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err == ESP_OK) {
    err = nvs_set_i64(my_handle, "saved_time", time);
    if (err == ESP_OK) {
      err = nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    Serial.println("Time saved to NVS: " + String(time));
  }
}

// Read time from NVS
/**
 * @brief Read the saved time from NVS
 * 
 * @return time_t The saved time, or 0 if not found
 */
time_t readTimeFromNVS() {
  nvs_handle my_handle;
  esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
  if (err != ESP_OK) {
    Serial.println("Không thể mở NVS!");
    return 0;
  }

  int64_t value = 0;
  err = nvs_get_i64(my_handle, "saved_time", &value);
  nvs_close(my_handle);
  return (time_t)value;
}

// ===========================================================TIME FUNCTIONs===========================================================
// get current time from NTP server
/**
* @brief Set the current time from a string
* 
* @param timeString The time string to parse
 */
void setCurrentTime(const char *timeString) {
  if (timeString != NULL) {
    struct tm tmTime;
    if (strptime(timeString, "%A, %B %d %Y %H:%M:%S", &tmTime) != NULL) {
      currentTime = tmTime;
      time_t appTime = mktime(&tmTime);
      if (appTime != -1) {
        startupTime = appTime;
        isTimeInitialized = true;
        isTimeSyncedFromApp = true;
        // Tính toán tickSecond từ start_time_sprinkler
        int64_t current_seconds = currentTime.tm_hour * 3600LL + currentTime.tm_min * 60LL + currentTime.tm_sec;
        int64_t start_seconds = start_time_sprinkler[0] * 3600LL + start_time_sprinkler[1] * 60LL;
        tickSecond = current_seconds - start_seconds;
        if (tickSecond < 0) {
          tickSecond += 24 * 3600LL;
          // Tăng ngày lên 1
          time_t rawTime = appTime + 24 * 3600;
          currentTime = *localtime(&rawTime);
        }
        tickSecond = tickSecond % (24 * 3600LL);
        lastProcessedTimeSync = tickSecond;
        Serial.println("App time: " + String(appTime));
        saveTimeToNVS(appTime);
        Serial.println("Time set from app: ");
        Serial.println(&currentTime, "%A, %B %d %Y %H:%M:%S");
      } else {
        Serial.println("Failed to convert time string to time_t");
      }
    } else {
      Serial.println("Failed to parse time string");
    }
  }
}

// on timer for counting tick second
/**
 * @brief Start a timer with a specified function and period
 * 
 * @param func The function to call when the timer expires
 * @param timer Pointer to the timer handle
 * @param period The period in seconds for the timer
 */
void startTimer(void (*func)(), hw_timer_t **timer, uint32_t period) {
  *timer = timerBegin(TIMER_FREQ);                         // Frequency: 1MHz
  timerAttachInterrupt(*timer, func);                      // Attach interrupt function
  timerAlarm(*timer, (period * 1000000), true, 1);         // Alarm every time end period
}

// Stop timer
/**
 * @brief Stop a timer and detach its interrupt
 * 
 * @param timer Pointer to the timer handle
 */
void stopTimer(hw_timer_t **timer) {
  if (*timer != NULL) {
    timerDetachInterrupt(*timer);
    timerEnd(*timer);
    *timer = NULL;
  }
}

// Interrupt function for one-shot timer to start cycle
/**
 * @brief Interrupt function for one-shot timer to start cycle
 * 
 */
void IRAM_ATTR countToStartTimer_itr() {
  stopTimer(&countToStartTimer);
  stopTimer(&tickTimer); // Stop tickTimer when countToStartTimer ends
  // Serial.println("One-shot timer stopped, cycle timer already running");
  // if (autoTaskHandle != NULL) {
  //   // xTaskNotifyGive(autoTaskHandle); // Thông báo cho taskAuto
  //   vTaskNotifyGiveFromISR(autoTaskHandle, NULL);
  // }
  // tickSecond = 0;
  startTimer(&timer_itr, &cycleTimer, 1);
}

// Interrupt function for tickTimer to increment tickSecond
/**
 * @brief Interrupt function for tickTimer to increment tickSecond when one-shot timer running
 * 
 */
void IRAM_ATTR tickTimer_itr() {
  // tickSecond++;
  tickSecond = (tickSecond + 1) % (24 * HOUR);
}

// Interrupt function for cycle timer to handle time ticks
/**
 * @brief Interrupt function for cycle timer to handle time ticks
 * 
 * This function is called every second to update the tickSecond and check for new day or sprinkler cycles.
 */
void IRAM_ATTR timer_itr() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  tickSecond++;
  if (tickSecond >= 24 * HOUR) {
    tickSecond -= 24 * HOUR;
    _timeSys.firstTime = 1;
    filterDoneToday = false;
    currentSprinklerCycle = 0;
    nextTickPeriod = 0;
#ifdef DEBUG
    isNextDay = false;
#endif
    if (autoTaskHandle != NULL) {
      // Serial.println("Give notify in timer_itr (new day)");
      vTaskNotifyGiveFromISR(autoTaskHandle, &xHigherPriorityTaskWoken);
    }
  }
  else if (tickSecond == nextTickPeriod && currentSprinklerCycle < _sprinkler.cyclesInDay) {
    if (autoTaskHandle != NULL) {
      // Serial.println("Give notify in timer_itr (sprinkler cycle)");
      vTaskNotifyGiveFromISR(autoTaskHandle, &xHigherPriorityTaskWoken);
    }
  }

  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

// Function for updating time information
/**
 * @brief Update the current time information
 * 
 * This function updates the current time based on the tickSecond and saves it to NVS.
 */
void updateTimeInfo(void) {
  static int lastWday = -1;
  static time_t rawTime;
  if (isTimeInitialized) {
    if (startupTime == 0) {
      startupTime = readTimeFromNVS();
      rawTime = startupTime;
    }
    else {
      rawTime = mktime(&currentTime) + (tickSecond - lastProcessedTimeSync);
    }
    
    struct tm *newTime = localtime(&rawTime);
    if (newTime != NULL) {
      currentTime = *newTime;
      lastProcessedTimeSync = tickSecond;
      saveTimeToNVS(rawTime);
      Serial.println("Time synced internally: ");
      Serial.println(&currentTime, "%A, %B %d %Y %H:%M:%S");
    } else {
      Serial.println("Failed to update time locally");
    }
  }

  if (isTimeInitialized) {
    lastWday = currentTime.tm_wday;
  }

  char currentTime_str[64];
  strftime(currentTime_str, sizeof(currentTime_str), "%A, %B %d %Y %H:%M:%S", &currentTime);
}
// =============================================================RUN PUMPS=============================================================
// Function running Filter pump - Green LED
/**
 * @brief Run the filter pump for a specified duration
 * 
 * This function turns on the filter pump and runs it for the configured duration.
 */
void runFilter(void) {
  isMotorRunning = true;
  _filter.autoControl = true;
  Serial.println("Filter On");
  Serial.print("Active ");
  Serial.print(_filter.duration);
  Serial.println(" Hour");

  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_RED_PIN, HIGH);
  digitalWrite(LED_BLUE_PIN, HIGH);

  digitalWrite(FILTER_PIN, HIGH); // Turn ON relay for filter
#ifndef DEBUG
  vTaskDelay(_filter.duration * HOUR * 1000 / portTICK_PERIOD_MS);
#else
  vTaskDelay(_filter.duration * MINUTE * 1000 / portTICK_PERIOD_MS);
#endif
  digitalWrite(FILTER_PIN, LOW);  // Turn OFF relay

  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_RED_PIN, HIGH);
  digitalWrite(LED_BLUE_PIN, HIGH);
  Serial.println("Filter Off");
  _filter.autoControl = false;

  filterDoneToday = true;
  isMotorRunning = false;
}

// Function running Sprinkler pump - Blue LED
/**
 * @brief Run the sprinkler pump for a specified duration
 * 
 * This function turns on the sprinkler pump and runs it with a PWM fade effect.
 */
void runSprinkler(void) {
  isMotorRunning = true;

  Serial.println("Sprinkler ON");
  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_RED_PIN, HIGH);
  digitalWrite(LED_BLUE_PIN, LOW);

  Serial.print("Active ");
  Serial.print(_sprinkler.duration);
  Serial.println(" Minute");

  uint32_t totalDurationMs = _sprinkler.duration * MINUTE * 1000;

  uint32_t fadeTime = 2000;           // Time to fade up or down (2 seconds)
  uint32_t holdTime = 2000;           // Time to hold PWM at full level
  uint32_t cycleTime = 2 * (fadeTime + holdTime); // Total time for one fade-up/down cycle
  uint32_t elapsedTime = 0;
  ledcWrite(SPRINKLER_PIN, 200);
  // Loop for as many full fade cycles as fit within the total duration
  while (elapsedTime + cycleTime <= totalDurationMs) {
    // Fade from HIGH to LOW (simulate increasing motor speed)
    ledcFade(SPRINKLER_PIN, 200, 0, fadeTime);
    vTaskDelay(fadeTime / portTICK_PERIOD_MS);

    // Hold at LOW (maximum speed)
    vTaskDelay(holdTime / portTICK_PERIOD_MS);

    // Fade from LOW to HIGH (simulate decreasing motor speed)
    ledcFade(SPRINKLER_PIN, 0, 200, fadeTime);
    vTaskDelay(fadeTime / portTICK_PERIOD_MS);

    // Hold at HIGH (idle or minimal speed)
    vTaskDelay(holdTime / portTICK_PERIOD_MS);

    elapsedTime += cycleTime;
  }

  // Handle any remaining time not covered by full cycles
  uint32_t remaining = totalDurationMs - elapsedTime;
  if (remaining > 0) {
    // Use remaining time for one short fade-up and down
    ledcFade(SPRINKLER_PIN, 200, 0, remaining / 2);
    vTaskDelay((remaining / 2) / portTICK_PERIOD_MS);

    ledcFade(SPRINKLER_PIN, 0, 200, remaining / 2);
    vTaskDelay((remaining / 2) / portTICK_PERIOD_MS);
  }

  Serial.println("Sprinkler Off");

  isMotorRunning = false;
  ledcWrite(SPRINKLER_PIN, 255);
  digitalWrite(SPRINKLER_PIN, HIGH); // Stop the sprinkler
}

// =======================================================PROCESS DATA FUNCTIONs=======================================================
/**
 * @brief Configure the system based on the type of dog
 * 
 * This function sets the filter and sprinkler durations based on the type of dog.
 * 
 * @param type The type of dog (SMALL_DOG, MEDIUM_DOG, LARGE_DOG)
 */
void configurationForDog(int type){
  if(type == SMALL_DOG){
    _filter.duration = FILTER_DURATION_SMALL_DOG;
    _sprinkler.duration = SPRINKLER_DURATION_SMALL_DOG;

    Serial.println("Config DOG SMALL");
    writeValueToNVS("ds", _sprinkler.duration);
    Serial.println("Sprinkler Duration: " + String(_sprinkler.duration) + "minute");

    writeValueToNVS("df", _filter.duration);
    Serial.println("Filter Duration: " + String(_filter.duration) + "h");

    writeValueToNVS("cycles", _sprinkler.cyclesInDay);
    Serial.println("Sprinkler Cycles: " + String(_sprinkler.cyclesInDay));

    _timeSys.period = 24;
  }else if(type == MEDIUM_DOG){
    _filter.duration = FILTER_DURATION_MEDIUM_DOG;
    _sprinkler.duration = SPRINKLER_DURATION_MEDIUM_DOG;

    Serial.println("Config DOG MEDIUM");
    writeValueToNVS("ds", _sprinkler.duration);
    Serial.println("Sprinkler Duration: " + String(_sprinkler.duration) + "minute");

    writeValueToNVS("df", _filter.duration);
    Serial.println("Filter Duration: " + String(_filter.duration) + "h");

    writeValueToNVS("cycles", _sprinkler.cyclesInDay);
    Serial.println("Sprinkler Cycles: " + String(_sprinkler.cyclesInDay));

    _timeSys.period = 24;
  }else if(type == LARGE_DOG){
    _filter.duration = FILTER_DURATION_LARGE_DOG;
    _sprinkler.duration = SPRINKLER_DURATION_LARGE_DOG;

    Serial.println("Config DOG LARGE");
    writeValueToNVS("ds", _sprinkler.duration);
    Serial.println("Sprinkler Duration: " + String(_sprinkler.duration) + "minute");

    writeValueToNVS("df", _filter.duration);
    Serial.println("Filter Duration: " + String(_filter.duration) + "h");

    writeValueToNVS("cycles", _sprinkler.cyclesInDay);
    Serial.println("Sprinkler Cycles: " + String(_sprinkler.cyclesInDay));

    _timeSys.period = 24;
  }
}

/**
 * @brief Login to the server and retrieve the JWT token
 * 
 * This function logs in to the server using the provided email and password, and retrieves the JWT token and device ID.
 */
void loginAndGetToken(void) {
  if (_httpClient.login(loginUrl, user_email, user_password)) {
    jwtToken = _httpClient.getToken();
    _httpClient.setToken(jwtToken);
    device_id = _httpClient.getDeviceID();
    Serial.println("Login successful!");
    lastTokenRefresh = millis();
  } else {
    Serial.println("Login failed");
  }
}

/**
 * @brief Get the device status from the server
 * 
 * This function retrieves the device status from the server using the stored JWT token and device ID.
 */
void getDeviceStatus(void) {
  if (!_httpClient.isTokenValid() || _httpClient.getDeviceID() == "") {
    Serial.println("Missing token or device ID");
    loginAndGetToken();
    return;
  }

  String url = String(getStatusUrl) + "?device_id=" + device_id;

  String response = _httpClient.get(url, true);

  if (response.isEmpty()) {
    Serial.println("Failed to get status response.");
    return;
  }

  Serial.println("Received JSON response:");
  Serial.println(response);

  StaticJsonDocument<1024> respDoc;
  DeserializationError error = deserializeJson(respDoc, response);

  if (error) {
    Serial.println("Failed to parse JSON response");
    return;
  }

  if (respDoc["status"] == "success") {
    JsonObject data = respDoc["data"];

    int sprinklerPumpState = data["pump1_status"].as<int>();
    int filterPumpState = data["pump2_status"].as<int>();

    _sprinkler.manual = sprinklerPumpState == 1 ? 1 : 0;
    _filter.manual = filterPumpState == 1 ? 1 : 0;

    Serial.println("Device Status: " + data["status"].as<String>());
    // Serial.println("Water Level: " + String(data["water_level"].as<int>()) + "%");
  } else {
    Serial.println("Server returned non-success status.");
  }

}
#include "esp32-hal-timer.h"
#include "esp32-hal-ledc.h"
#include "main.h"
// ============================================================CLASS OBJECT============================================================
WIFI _wifi;
MQTT _mqtt;
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

const char* PP4_MQTT_BROKER = "broker.emqx.io";
const int PP4_MQTT_PORT = 8883;
const char* PP4_MQTT_USERNAME = "porch-potty_4";
const char* PP4_MQTT_PASSWORD = "porch-potty_4";

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

typedef void (*MQTTReconnectFunc) ();

SemaphoreHandle_t timerMux = NULL;
// SemaphoreHandle_t settingsMux = NULL;
// Task handles
TaskHandle_t saveSettingsTaskHandle;
TaskHandle_t autoTaskHandle;
TaskHandle_t manualTaskHandle;
// ====================================================================================================================================

// ===============================================================SETUP================================================================
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
  // if (readDaysFromNVS(_timeSys.days)) {
  //   for (int i = 0; i < 7; i++) {
  //     Serial.print(_timeSys.days[i]);
  //     if (i < 6) Serial.print(", ");
  //   }
  // }
  // else {
  //   for (int i = 0; i < 7; i++) {
  //     _timeSys.days[i] = 0;
  //     Serial.print(_timeSys.days[i]);
  //     if (i < 6) Serial.print(", ");
  //   }
  // }
  // Serial.println();
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
  // nextTickPeriod = start_time_sprinkler[0] * 3600 + start_time_sprinkler[1] * 60 +  currentSprinklerCycle * _timeSys.period * HOUR;
  nextTickPeriod = currentSprinklerCycle * _timeSys.period * HOUR;
  
  // Read time from NVS
  // time_t savedTime = readTimeFromNVS();
  // if (savedTime > 0) {
  //   struct tm *tmTime = localtime(&savedTime);
  //   if (tmTime) {
  //     currentTime = *tmTime;
  //     isTimeInitialized = true;
  //     tickSecond = (currentTime.tm_hour * 3600 + currentTime.tm_min * 60 + currentTime.tm_sec) - (start_time_sprinkler[0] * 3600 + start_time_sprinkler[1] * 60); // Đồng bộ tickSecond với thời gian thực
  //     if (tickSecond < 0) {
  //       tickSecond += 24 * 3600; // Xử lý thời gian hiện tại trước 0:00
  //     }
  //     lastProcessedTimeSync = tickSecond;
  //     Serial.println("Time restored from NVS: ");
  //     Serial.println(&currentTime, "%A, %B %d %Y %H:%M:%S");
  //     Serial.println("Tick second in setting: " + String(tickSecond));
  //   }
  // }

  

  // Show settings in NVS
  Serial.println("===========================SETTING===========================");
  Serial.println("Sprinkler Duration: " + String(_sprinkler.duration)+" Minute");
  Serial.println("Filter Duration: " + String(_filter.duration) + " Hour");
  Serial.println("Sprinkler Cycles: " + String(_sprinkler.cyclesInDay));
  Serial.println("Has New Settings: " + String(hasNewSettings));
  Serial.println("Start time: " + String(start_time_sprinkler[0]) + ":" + String(start_time_sprinkler[1]));
  Serial.println("next tick period in setup: " + String(nextTickPeriod));
  Serial.println("=============================================================");
  //====================================WI-FI & MQTT====================================
  // _wifi.wifi_scan_handle();
  // _wifi.ConnectWifi();
  _wifi.init();
  macAddress = _wifi.getMacAddress();
  Serial.println("MAC Address: " + macAddress);

  MQTTReconnectFunc reconnect_handler = reConfigMQTT; // function pointer point to function reConfigMQTT()
  _wifi.setConnectedCallback(reconnect_handler);

  if (WiFi.status() == WL_CONNECTED) {
    //====================================MQTT====================================
    _mqtt.MQTT_init(PP4_MQTT_BROKER, PP4_MQTT_PORT, PP4_MQTT_USERNAME, PP4_MQTT_PASSWORD);
    vTaskDelay(pdMS_TO_TICKS(500));
    for (int i = 0; i < TOPIC_COUNT - 1; i++) {
      mqtt_subscribe(i, macAddress.c_str());
      vTaskDelay(pdMS_TO_TICKS(500));
    }
    //====================================TIME====================================
  }
  _mqtt.setMessageHandler(handleMQTTSettings);
  Serial.println("Period: " + String(_timeSys.period * HOUR));
  startTimer(&timer_itr, &cycleTimer, 1); // timer count every 1 second
  //====================================TASKS====================================
  xTaskCreate(taskAuto, "Auto Task", 8192, NULL, configMAX_PRIORITIES - 1, &autoTaskHandle);
  xTaskCreate(taskManual, "Manual Task", 4096, NULL, 5, &manualTaskHandle);
  xTaskCreate(taskSaveSettings, "Task Save Settings", 4096, NULL, 2, &saveSettingsTaskHandle);
  xTaskCreate(taskUpdateTime, "Update Time Task", 2048, NULL, 3, NULL);
#ifdef DEBUG
  xTaskCreate(testFunction_viaSerial, "Test Serial", 4096, NULL, 4, NULL);
#endif
  //=====================================END=====================================
  Serial.println("End setup.");
}

// ===============================================================LOOP================================================================
void Loop() {
  vTaskDelay(1 / portTICK_PERIOD_MS);
}

// ==============================================================TASKS================================================================
// Task to update time every minute
void taskUpdateTime(void *param) {
  for (;;) {
    if (isTimeInitialized) {
      updateTimeInfo();
    }
    vTaskDelay(60000 / portTICK_PERIOD_MS); // Delay 60 seconds
  }
}

// Task save settings received via MQTT Broker
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
        mqtt_publish_message(WATER_LEVEL, macAddress.c_str(), "Sprinkler running auto");
        _sprinkler.autoControl = true;
        runSprinkler();
        _sprinkler.autoControl = false;
        sprinklerJustFinished = true;
        currentSprinklerCycle++;
        nextTickPeriod = currentSprinklerCycle * _timeSys.period * HOUR; // 0:00 ngày tiếp theo
      }
    }
    Serial.println("Before checking filter run condition...");
    if (isTimeInitialized && minuteNow >= startMinuteSprinkler && !filterDoneToday && !_filter.manual && !isMotorRunning) {
      if (sprinklerJustFinished || _timeSys.days[wday] == 0) {
        sprinklerJustFinished = false;
        mqtt_publish_message(WATER_LEVEL, macAddress.c_str(), "Filter running auto");
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
void taskManual(void *param) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    
    isManualRunning = false; // default set to false, be true if there is a SPRINKLER_PIN on

    // Check and control Sprinkler - Yellow LED
    if (_sprinkler.manual == 1 && !isMotorRunning) {
      Serial.println("Manual Sprinkler ON");
      isMotorRunning = true;
      isManualRunning = true;
      digitalWrite(LED_GREEN_PIN, LOW);
      digitalWrite(LED_RED_PIN, LOW);
      digitalWrite(LED_BLUE_PIN, HIGH);
      // Increase gradually PWM for Sprinkler in ~5s
      ledcFade(SPRINKLER_PIN, 255, 0, 5000);
    } else if (_sprinkler.manual == 0) {
      Serial.println("Manual Sprinkler OFF");
      // Decrease gradually PWM for Sprinkler in ~5s
      ledcFade(SPRINKLER_PIN, 0, 255, 5000);
      digitalWrite(LED_GREEN_PIN, HIGH);
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(LED_BLUE_PIN, HIGH);
    }

    // Check and control Filter - Purple LED
    if (_filter.manual == 1 && !isMotorRunning) {
      Serial.println("Manual Filter ON");
      isMotorRunning = true;
      isManualRunning = true;
      digitalWrite(FILTER_PIN, HIGH); // Turn ON FILTER_PIN
      digitalWrite(LED_GREEN_PIN, HIGH);
      digitalWrite(LED_RED_PIN, LOW);
      digitalWrite(LED_BLUE_PIN, LOW);
    } else if (_filter.manual == 0) {
      Serial.println("Manual Filter OFF");
      digitalWrite(FILTER_PIN, LOW); // Turn OFF FILTER_PIN
      digitalWrite(LED_GREEN_PIN, HIGH);
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(LED_BLUE_PIN, HIGH);
    }

    // Update isMotorRunning & isManualRunning
    isMotorRunning = (_sprinkler.manual == 1 || _filter.manual == 1);
    isManualRunning = isMotorRunning; // isManualRunning be true if there is a SPRINKLER_PIN ON
  }
}

#ifdef DEBUG
static uint8_t isNextDay = false;
// Task to simulate new day via Serial input
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
// Read flash
int16_t readValueFromNVS(const char* key) {
  nvs_handle my_handle;
  esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
  if (err != ESP_OK) {
    Serial.println("Không thể mở NVS!");
  }

  int16_t value = 0;
  err = nvs_get_i16(my_handle, key, &value);

  nvs_close(my_handle);
  return value;
}

// Write flash
void writeValueToNVS(const char* key, int16_t value) {
  nvs_handle my_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);

  err = nvs_set_i16(my_handle, key, value);
  ESP_ERROR_CHECK(err);

  err = nvs_commit(my_handle);
  ESP_ERROR_CHECK(err);

  nvs_close(my_handle);
}

// Save scheduled days to flash
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
void startTimer(void (*func)(), hw_timer_t **timer, uint32_t period) {
  *timer = timerBegin(TIMER_FREQ);                         // Frequency: 1MHz
  timerAttachInterrupt(*timer, func);                      // Attach interrupt function
  timerAlarm(*timer, (period * 1000000), true, 1);         // Alarm every time end period
}

// Stop timer
void stopTimer(hw_timer_t **timer) {
  if (*timer != NULL) {
    timerDetachInterrupt(*timer);
    timerEnd(*timer);
    *timer = NULL;
  }
}

// Interrupt function for one-shot timer to start cycle
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
void IRAM_ATTR tickTimer_itr() {
  // tickSecond++;
  tickSecond = (tickSecond + 1) % (24 * HOUR);
}

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
  mqtt_publish_message(WATER_LEVEL, macAddress.c_str(), currentTime_str);
  mqtt_publish_message(WATER_LEVEL, macAddress.c_str(), String(nextTickPeriod));
}
// =============================================================RUN PUMPS=============================================================
// Function running Filter pump - Green LED
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
void reConfigMQTT() {
  Serial.println("==================Re-config MQTT==================");
  
  // Tạo task riêng để xử lý MQTT init và subscribe
  xTaskCreate([](void* parameter) {
      _mqtt.MQTT_init(PP4_MQTT_BROKER, PP4_MQTT_PORT, PP4_MQTT_USERNAME, PP4_MQTT_PASSWORD);
      vTaskDelay(pdMS_TO_TICKS(1000));
      
      for (int i = 0; i < TOPIC_COUNT - 1; i++) {
          mqtt_subscribe(i, macAddress.c_str());
          vTaskDelay(pdMS_TO_TICKS(500));
      }
      vTaskDelete(NULL);
  }, "MQTT_Config", 4096, NULL, 1, NULL);
}

// void reConfigMQTT() {
//     Serial.println("==================Re-config MQTT==================");
    
//     // Tạo task riêng để xử lý MQTT init và subscribe
//     xTaskCreate([](void* parameter) {
//         _mqtt.MQTT_init(PP4_MQTT_BROKER, PP4_MQTT_PORT, PP4_MQTT_USERNAME, PP4_MQTT_PASSWORD);
        
//         // Đợi kết nối MQTT thành công (tối đa 15 giây)
//         int retries = 0;
//         while (!client.connected() && retries < 15) {
//             Serial.println("Waiting for MQTT connection...");
//             vTaskDelay(pdMS_TO_TICKS(1000));
//             retries++;
//         }

//         if (client.connected()) {
//             Serial.println("MQTT connected, starting subscribe...");
//             vTaskDelay(pdMS_TO_TICKS(500)); // Đợi kết nối ổn định
            
//             // Subscribe các topic
//             for (int i = 0; i < TOPIC_COUNT - 1; i++) {
//                 if (mqtt_subscribe(i, macAddress.c_str())) {
//                     Serial.printf("Successfully subscribed to topic %d\n", i);
//                 } else {
//                     Serial.printf("Failed to subscribe to topic %d\n", i);
//                 }
//                 vTaskDelay(pdMS_TO_TICKS(100)); // Delay giữa các lần subscribe
//             }
//         } else {
//             Serial.println("Failed to connect to MQTT broker, skipping subscribe");
//         }
        
//         vTaskDelete(NULL);
//     }, "MQTT_Config", 4096, NULL, 1, NULL); // Tăng stack size lên 8KB
// }

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

// Xử lý manual controls
// void handleManualControls(const JsonDocument &doc) {
//     if (doc.containsKey("manual_sprinkler")) {
//         _sprinkler.manual = doc["manual_sprinkler"] | _sprinkler.manual;
//     }
    
//     if (doc.containsKey("manual_filter")) {
//         _filter.manual = doc["manual_filter"] | _filter.manual;
//     }
// }

// // Xử lý các cài đặt thời gian
// void handleDurationSettings(const JsonDocument &doc) {
//     if (doc.containsKey("duration_sprinkler")) {
//         _sprinkler.duration = int(doc["duration_sprinkler"]) | _sprinkler.duration;
//         Serial.println("Sprinkler duration: " + String(_sprinkler.duration));
//     }
    
//     if (doc.containsKey("duration_filter")) {
//         _filter.duration = int(doc["duration_filter"]) | _filter.duration;
//         Serial.println("Filter duration: " + String(_filter.duration));
//     }

//     if (doc.containsKey("cycles_sprinkler")) {
//         _sprinkler.cyclesInDay = doc["cycles_sprinkler"] | _sprinkler.cyclesInDay;
//         Serial.println("Sprinkler cyclesInDay: " + String(_sprinkler.cyclesInDay));
//     }
// }

// // Xử lý cài đặt các ngày hoạt động
// void handleActiveDays(const JsonDocument &doc) {
//     if (doc.containsKey("active_days") && doc["active_days"].is<JsonArrayConst>()) {
//         JsonArrayConst daysArray = doc["active_days"].as<JsonArrayConst>();
//         for (int i = 0; i < 7; i++) {
//             _timeSys.days[i] = daysArray[i].as<int>();
//         }
//     }
// }

// // Xử lý thời gian bắt đầu
// void handleStartTime(const JsonDocument &doc) {
//     if (doc.containsKey("start_time") && doc["start_time"].is<JsonArrayConst>()) {
//         JsonArrayConst startTimeSprinkler_Array = doc["start_time"].as<JsonArrayConst>();
//         start_time_sprinkler[0] = startTimeSprinkler_Array[0].as<uint32_t>();
//         start_time_sprinkler[1] = startTimeSprinkler_Array[1].as<uint32_t>();
//         startMinuteSprinkler = start_time_sprinkler[0] * 60 + start_time_sprinkler[1];
//     }
// }

// // Xử lý đồng bộ thời gian và khởi tạo timer
// void handleTimeSync(const JsonDocument &doc) {
//     if (!doc.containsKey("current_time")) return;
    
//     String timeString = doc["current_time"].as<String>();
//     Serial.println("Time string received: " + timeString);
//     setCurrentTime(timeString.c_str());

//     // Tính toán các thông số thời gian
//     int currentSec = currentTime.tm_hour * 3600 + currentTime.tm_min * 60 + currentTime.tm_sec;
//     int startSec = start_time_sprinkler[0] * 3600 + start_time_sprinkler[1] * 60;
//     _timeSys.period = 24 / _sprinkler.cyclesInDay;

//     // Dừng các timer hiện tại
//     stopAllTimers();

//     // Khởi tạo timer mới dựa trên thời gian hiện tại
//     initializeNewTimers(currentSec, startSec);
// }

// // Dừng tất cả các timer
// void stopAllTimers() {
//   portENTER_CRITICAL(&timerMux);
//   if (countToStartTimer) {
//       stopTimer(&countToStartTimer);
//   }
//   if (cycleTimer) {
//       stopTimer(&cycleTimer);
//   }
//   if (tickTimer) {
//       stopTimer(&tickTimer);
//   }
//   portEXIT_CRITICAL(&timerMux);
// }

// // Khởi tạo timer mới
// void initializeNewTimers(int currentSec, int startSec) {
//     if (currentSec < startSec) {
//         // Trường hợp 1: Thời gian hiện tại trước thời gian bắt đầu
//         uint32_t secondsToStart = startSec - currentSec;
//         startTimer(&tickTimer_itr, &tickTimer, 1);
//         startTimer(&countToStartTimer_itr, &countToStartTimer, secondsToStart - 5);
//     } else {
//         // Trường hợp 2: Thời gian hiện tại sau thời gian bắt đầu
//         calculateAndSetNextCycle(currentSec, startSec);
//     }
// }

// // Tính toán và thiết lập chu kỳ tiếp theo
// void calculateAndSetNextCycle(int currentSec, int startSec) {
//     int minutesSinceStart = (currentSec - startSec) / 60;
//     int periodInMinutes = _timeSys.period * 60;
//     int cyclesElapsed = minutesSinceStart / periodInMinutes + 1;
//     int nextCycleMinute = startMinuteSprinkler + (cyclesElapsed) * periodInMinutes;
//     int currentMinute = currentTime.tm_hour * 60 + currentTime.tm_min;
//     uint32_t secondsToNextCycle = (nextCycleMinute - currentMinute) * 60;

//     currentSprinklerCycle = cyclesElapsed;
//     nextTickPeriod = currentSprinklerCycle * _timeSys.period * HOUR;

//     if (cyclesElapsed >= _sprinkler.cyclesInDay) {
//         secondsToNextCycle += (24 * HOUR - (currentSec % (24 * HOUR)));
//     }

//     startTimer(&tickTimer_itr, &tickTimer, 1);
//     startTimer(&countToStartTimer_itr, &countToStartTimer, secondsToNextCycle - 5);
// }

// void handleDogTypeConfiguration(int type) {
//     if (type < SMALL_DOG || type > LARGE_DOG) {
//         Serial.println("Invalid dog type received: " + String(type));
//         return;
//     }

//     configurationForDog(type);
//     writeValueToNVS("type_dog", type);
//     mqtt_publish_message(TYPE_DOG, macAddress.c_str(), String(type));
//     Serial.println("Dog type configured: " + String(type));
// }

// Hàm xử lý MQTT settings chính đã được tách nhỏ
// void handleMQTTSettings(const JsonDocument &doc) {
//     haveMessageViaMQTT = false;
//     uint8_t lastSprinklerManualState = _sprinkler.manual;
//     uint8_t lastFilterManualState = _filter.manual;

//     // Xử lý từng phần riêng biệt
//     handleManualControls(doc);
//     handleDurationSettings(doc);
//     handleActiveDays(doc);
//     handleStartTime(doc);
//     handleTimeSync(doc);

//     // Xử lý cấu hình loại chó
//     if (doc.containsKey("type_dog")) {
//         handleDogTypeConfiguration(doc["type_dog"].as<int>());
//     }

//     // Cập nhật trạng thái và thông báo
//     haveMessageViaMQTT = true;
//     if (saveSettingsTaskHandle != NULL) {
//         xTaskNotifyGive(saveSettingsTaskHandle);
//     }

//     if (lastSprinklerManualState != _sprinkler.manual || lastFilterManualState != _filter.manual) {
//         xTaskNotifyGive(manualTaskHandle);
//     }
// }

void handleMQTTSettings(const JsonDocument &doc) {
  haveMessageViaMQTT = false;
  uint8_t lastSprinklerManualState = _sprinkler.manual;
  uint8_t lastFilterManualState = _filter.manual;

  if (doc.containsKey("manual_sprinkler")) {
    _sprinkler.manual = doc["manual_sprinkler"] | _sprinkler.manual;
  }
  
  if (doc.containsKey("duration_sprinkler")) {
    _sprinkler.duration = int(doc["duration_sprinkler"]) | _sprinkler.duration;
  }

  if (doc.containsKey("cycles_sprinkler")) {
    _sprinkler.cyclesInDay = doc["cycles_sprinkler"] | _sprinkler.cyclesInDay;
  }

  
  
  if (doc.containsKey("active_days") && doc["active_days"].is<JsonArrayConst>()) {
    JsonArrayConst daysArray = doc["active_days"].as<JsonArrayConst>();
    for (int i = 0; i < 7; i++) {
      _timeSys.days[i] = daysArray[i].as<int>();
    }
  }

  if (doc.containsKey("start_time") && doc["start_time"].is<JsonArrayConst>()) {
    JsonArrayConst startTimeSprinkler_Array = doc["start_time"].as<JsonArrayConst>();
    start_time_sprinkler[0] = startTimeSprinkler_Array[0].as<uint32_t>();
    start_time_sprinkler[1] = startTimeSprinkler_Array[1].as<uint32_t>();
    startMinuteSprinkler = start_time_sprinkler[0] * 60 + start_time_sprinkler[1];
    writeValueToNVS("start_hour", start_time_sprinkler[0]);
    writeValueToNVS("start_minute", start_time_sprinkler[1]);
  }

  if (doc.containsKey("manual_filter")) {
    _filter.manual = doc["manual_filter"] | _filter.manual;
  }

  if (doc.containsKey("duration_filter")) {
    _filter.duration = int(doc["duration_filter"]) | _filter.duration;
  }
  

  // Handle time sync and timer initialization
  if (doc.containsKey("current_time")) {
    String timeString = doc["current_time"].as<String>();
    Serial.println("Time string received: " + timeString);
    setCurrentTime(timeString.c_str());

    int currentSec = currentTime.tm_hour * 3600 + currentTime.tm_min * 60 + currentTime.tm_sec;
    int startSec = start_time_sprinkler[0] * 3600 + start_time_sprinkler[1] * 60;
    _timeSys.period = 24 / _sprinkler.cyclesInDay;
    Serial.println("Period in MQTT handle: " + String(_timeSys.period));

    // Stop existing timers to avoid conflicts
    if (countToStartTimer != NULL) {
      stopTimer(&countToStartTimer);
      countToStartTimer = NULL;
    }
    if (cycleTimer != NULL) {
      stopTimer(&cycleTimer);
      cycleTimer = NULL;
    }
    if (tickTimer != NULL) {
      stopTimer(&tickTimer);
      tickTimer = NULL;
    }

    if (currentSec < startSec) {
      // Case 1: Current time is before start time
      uint32_t secondsToStart = startSec - currentSec;
      Serial.print("Starting one-shot timer to wait until first cycle: ");
      Serial.println(secondsToStart);
      startTimer(&tickTimer_itr, &tickTimer, 1); // Start tickTimer to increment tickSecond
      startTimer(&countToStartTimer_itr, &countToStartTimer, secondsToStart - 5);
    } else {
      // Case 2: Current time is at or past start time
      int minutesSinceStart = (currentSec - startSec) / 60;
      int periodInMinutes = _timeSys.period * 60;
      int cyclesElapsed = minutesSinceStart / periodInMinutes + 1;
      int nextCycleMinute = startMinuteSprinkler + (cyclesElapsed) * periodInMinutes;
      int currentMinute = currentTime.tm_hour * 60 + currentTime.tm_min;
      uint32_t secondsToNextCycle = (nextCycleMinute - currentMinute) * 60;
      currentSprinklerCycle = cyclesElapsed;
      nextTickPeriod = currentSprinklerCycle * _timeSys.period * HOUR;
      Serial.println("Cycle elapsed: " + String(cyclesElapsed));
      Serial.println("Second to next cycle before adjust: " + String(secondsToNextCycle));
      if (cyclesElapsed >= _sprinkler.cyclesInDay) {
        // If all cycles for today are done, schedule for next day's first cycle
        secondsToNextCycle += (24 * HOUR - (currentSec % (24 * HOUR)));
      }

      Serial.print("Missed first start time, starting one-shot timer to next cycle: ");
      Serial.println(secondsToNextCycle);
      startTimer(&tickTimer_itr, &tickTimer, 1); // Start tickTimer to increment tickSecond
      startTimer(&countToStartTimer_itr, &countToStartTimer, secondsToNextCycle - 5);
    }
  }

  if (doc.containsKey("type_dog")) {
    int typeDog = doc["type_dog"];
    if (typeDog == 0) {
      Serial.println("================== MQTT SETTING ==================");
      Serial.println("=================== SPRINKLER ====================");
      Serial.println("Manual Sprinkler: " + String(_sprinkler.manual));
      Serial.println("Duration Sprinkler: " + String(_sprinkler.duration) + " Minute");
      Serial.println("Cycles Sprinkler: " + String(_sprinkler.cyclesInDay));
      Serial.println("Start time Sprinkler: " + String(start_time_sprinkler[0]) + ":" + String(start_time_sprinkler[1]));
      Serial.println("==================== FILTER ======================");
      Serial.println("Manual Filter: " + String(_filter.manual));
      Serial.println("Duration Filter: " + String(_filter.duration) + " h");
      Serial.println("==================== WEEK ========================");
      Serial.print("Sprinkler Days: ");
      for (int i = 0; i < 7; i++) {
          Serial.print(_timeSys.days[i]);
          if (i < 6) Serial.print(", ");
      }
      Serial.println();
      Serial.println("==================================================");
    } else {
      Serial.println("TYPE DOG CONFIGURATION");
      Serial.print("Type: ");
      Serial.println(typeDog);
      configurationForDog(typeDog);
    }
  }

  
  // Đặt cờ và gửi thông báo khi nhận setting mới
  haveMessageViaMQTT = true;
  if (saveSettingsTaskHandle != NULL) {
    xTaskNotifyGive(saveSettingsTaskHandle);
  }

  if (lastSprinklerManualState != _sprinkler.manual || lastFilterManualState != _filter.manual) {
    Serial.println("Send notify to run MANUAL");
    xTaskNotifyGive(manualTaskHandle);
  }
}

void mqtt_publish_message(int topic_id, const char *mac_id, String message) {
  char topic[MQTT_TOPIC_MAX_LEN];
  switch (topic_id) {
    case SPRINKLER_AUTO:
      snprintf(topic, sizeof(topic), "device/%s/motor/sprinkler/auto", mac_id);
      break;
    case SPRINKLER_MANUAL:
      snprintf(topic, sizeof(topic), "device/%s/motor/sprinkler/manual", mac_id);
      break;
    case FILTER_AUTO:
      snprintf(topic, sizeof(topic), "device/%s/motor/filter/auto", mac_id);
      break;
    case FILTER_MANUAL:
      snprintf(topic, sizeof(topic), "device/%s/motor/filter/manual", mac_id);
      break;
    case TYPE_DOG:
      snprintf(topic, sizeof(topic), "device/%s/type_dog", mac_id);
      break;
    case WATER_LEVEL:
      snprintf(topic, sizeof(topic), "device/%s/water_level", mac_id);
      break;
  }

  if (message != NULL) {
    _mqtt.publishMessage(topic, message.c_str());
  }

  Serial.println(String(topic));
}

void mqtt_subscribe(int topic_id, const char *mac_id) {
  char topic[MQTT_TOPIC_MAX_LEN];
  switch (topic_id) {
    case SPRINKLER_AUTO:
      snprintf(topic, sizeof(topic), "device/%s/motor/sprinkler/auto", mac_id);
      break;
    case SPRINKLER_MANUAL:
      snprintf(topic, sizeof(topic), "device/%s/motor/sprinkler/manual", mac_id);
      break;
    case FILTER_AUTO:
      snprintf(topic, sizeof(topic), "device/%s/motor/filter/auto", mac_id);
      break;
    case FILTER_MANUAL:
      snprintf(topic, sizeof(topic), "device/%s/motor/filter/manual", mac_id);
      break;
    case TYPE_DOG:
      snprintf(topic, sizeof(topic), "device/%s/type_dog", mac_id);
      break;
    case WATER_LEVEL:
      snprintf(topic, sizeof(topic), "device/%s/water_level", mac_id);
      break;
  }

  // _mqtt.subscribeTopic(topic);
  // Thử subscribe với retry
    int retries = 0;
    const int MAX_RETRIES = 3;
    bool subscribed = false;

    while (!subscribed && retries < MAX_RETRIES) {
      if (_mqtt.subscribeTopic(topic)) {
          Serial.printf("Successfully subscribed to: %s\n", topic);
          subscribed = true;
          break;
      } else {
          Serial.printf("Failed to subscribe to: %s (attempt %d/%d)\n", 
                      topic, retries + 1, MAX_RETRIES);
          retries++;
          vTaskDelay(pdMS_TO_TICKS(500)); // Đợi 500ms trước khi thử lại
      }
    }
}
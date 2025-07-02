#include "esp32-hal-ledc.h"
#include "main.h"
#include "driver/ledc.h"

// ============================================================CLASS OBJECT============================================================
WIFI _wifi;
MQTT _mqtt;
// ====================================================================================================================================

// =============================================================GLOBAL VAR=============================================================
TimeSys_t _timeSys;
configPump_t _sprinkler;
configPump_t _filter;
uint8_t _timer_state = TIMER_IDLE;

bool isManualRunning = false;
bool haveMessageViaMQTT;
bool hasNewSettings = false; // Flag to track if new settings have been received

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 6*3600;
const int   daylightOffset_sec = 3600;

const char* PP4_PUB_TOPIC = "rick/pp4/pub";
const char* PP4_SUB_TOPIC = "rick/pp4/sub";
const char* PP4_MQTT_BROKER = "broker.hivemq.com";
const int PP4_MQTT_PORT = 1883;

hw_timer_t *countTimer = NULL;
struct tm currentTime;
time_t startupTime = 0;
volatile uint64_t tickSecond = 0;

uint32_t start_time_sprinkler[2] = {0, 0};
bool isMotorRunning = false;        // Flag to ensure only 1 motor run at the same time
bool sprinklerJustFinished = false; // Flag check if sprinkler have done duration
bool filterDoneToday = false;       // Flag check if Filter run today yet
int startMinuteSprinkler;    // Start time of Sprinkler in minute

// Task handles
TaskHandle_t saveSettingsTaskHandle;
TaskHandle_t autoTaskHandle;
TaskHandle_t getTimeTaskHandle;
TaskHandle_t manualTaskHandle;

StaticJsonDocument<250> jsonDocument;
char buffer[250];
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

  Wire.begin(I2C_SDA, I2C_SCL);

  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_RED_PIN, HIGH);
  digitalWrite(LED_BLUE_PIN, HIGH);
  digitalWrite(FILTER_PIN, LOW);
  digitalWrite(SPRINKLER_PIN, LOW);

  
  //=====================================LEDC====================================
  ledcAttach(SPRINKLER_PIN, PWM_FREQ, PWM_RESOLUTION);
  //====================================FLASH====================================
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Read hasNewSettings from NVS
  hasNewSettings = readValueFromNVS("has_settings") != 0;

  //Sprinkler duration
  _sprinkler.duration = readValueFromNVS("ds");
  if (_sprinkler.duration == 0) {
#ifndef DEBUG
    _sprinkler.duration = 4;
#else
    _sprinkler.duration = 1;
#endif
    writeValueToNVS("ds", _sprinkler.duration);
  }
  // Filter duration
  _filter.duration = readValueFromNVS("df");
  if (_filter.duration == 0) {
#ifndef DEBUG
    _filter.duration = 4;
#else
    _filter.duration = 1;
#endif
    writeValueToNVS("df", _filter.duration);
  }

  _sprinkler.cyclesInDay = readValueFromNVS("cycles");
  if (_sprinkler.cyclesInDay == 0) {
    _sprinkler.cyclesInDay = 1;
    writeValueToNVS("cycles", 1);
  }

  Serial.println("Days: ");
  if (readDaysFromNVS(_timeSys.days)) {
    for (int i = 0; i < 7; i++) {
      Serial.print(_timeSys.days[i]);
      if (i < 6) Serial.print(", ");
    }
  }
  else {
    for (int i = 0; i < 7; i++) {
      _timeSys.days[i] = 0;
      Serial.print(_timeSys.days[i]);
      if (i < 6) Serial.print(", ");
    }
  }
  Serial.println();

  _timeSys.firstTime = 1;
  _timeSys.period = 24 / _sprinkler.cyclesInDay;

  // Show settings in NVS
  Serial.println("===========================SETTING===========================");
  Serial.println("Sprinkler Duration: " + String(_sprinkler.duration)+" Minute");
  Serial.println("Filter Duration: " + String(_filter.duration) + " Hour");
  Serial.println("Sprinkler Cycles: " + String(_sprinkler.cyclesInDay));
  Serial.println("Has New Settings: " + String(hasNewSettings));
  Serial.println("=============================================================");
  //====================================WI-FI====================================
  // delay(500 / portTICK_PERIOD_MS);
  _wifi.wifi_scan_handle();
  _wifi.ConnectWifi();
  //====================================MQTT====================================
  _mqtt.MQTT_init(PP4_PUB_TOPIC, PP4_SUB_TOPIC, PP4_MQTT_BROKER, PP4_MQTT_PORT);
  _mqtt.setMessageHandler(handleMQTTSettings);
  //====================================TIME====================================
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  getCurrentTime();
  tickSecond = currentTime.tm_sec;
  Serial.println("Period: " + String(_timeSys.period * HOUR));
  startTimer(&timer_itr, &countTimer, 1);
  _timer_state = TIMER_DEFAULT;
  //====================================TASKS====================================
  xTaskCreate(taskAuto, "Auto Task", 4096, NULL, 5, &autoTaskHandle);
  xTaskCreate(taskManual, "Manual Task", 2048, NULL, 5, &manualTaskHandle);
  xTaskCreate(getTime, "Get Time", 2048, NULL, 4, &getTimeTaskHandle);
  xTaskCreate(taskSaveSettings, "Task Save Settings", 2048, NULL, 3, &saveSettingsTaskHandle);
  xTaskCreate(testFunction_viaSerial, "Test Serial", 2048, NULL, 4, NULL);
  //=====================================END=====================================
  Serial.println("End setup.");
}

// ===============================================================LOOP================================================================
void Loop() {
  vTaskDelay(1 / portTICK_PERIOD_MS);
}

// ==============================================================TASKS================================================================
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
void taskAuto(void *pvParameters) {
  int currentSprinklerCycle = 0;          // Sprinkler current cycles
  int lastRunDay;                         // Last day that have set to run
  while (true) {
    // Serial.println("Checking in taskAuto...");
    if (!hasNewSettings && _timeSys.firstTime == 1) {
      // Run default settings if no new settings received and it's the first time
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
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }
    else if (hasNewSettings && _timer_state == TIMER_DEFAULT) {
      stopTimer(&countTimer, 0);
      _timeSys.period = 24/_sprinkler.cyclesInDay;
      startTimer(&timer_itr, &countTimer, _timeSys.period * HOUR);
      _timer_state = TIMER_NORMAL;
    }

    updateTimeInfo();

    int wday = currentTime.tm_wday;
    int hour = currentTime.tm_hour;
    int min  = currentTime.tm_min;
    int sec  = currentTime.tm_sec;
    
    int minuteNow = hour * 60 + min;  // exchange current time (hour & minute) to minute
    if (wday != lastRunDay && minuteNow == startMinuteSprinkler) {
      Serial.println("New day! Reset variable.");
      currentSprinklerCycle = 0;
      _timeSys.firstTime = 1;
      filterDoneToday = false;
      lastRunDay = wday;
    }

    if (_timeSys.days[wday] == 1 && !isManualRunning) {
      currentSprinklerCycle = (minuteNow - startMinuteSprinkler) / _timeSys.period * MINUTE;
      // Serial.printf("currentSprinklerCycle: %d\n", currentSprinklerCycle);
      if (currentSprinklerCycle < 0) currentSprinklerCycle = 0;
      else if (currentSprinklerCycle >= _sprinkler.cyclesInDay) {
        // Serial.println("All sprinkler cycles finished today");
        continue;
      }
      int cycleMinute = startMinuteSprinkler + currentSprinklerCycle * _timeSys.period * MINUTE; 
      // Serial.printf("minuteNow: %d - cycleMinute: %d\n", minuteNow, cycleMinute);
      if (minuteNow == cycleMinute && sec == 0 && !isMotorRunning && !_sprinkler.manual) {
        Serial.println("Sprinkler run auto");
        _sprinkler.autoControl = true;
        runSprinkler();
        _sprinkler.autoControl = false;
        sprinklerJustFinished = true;
        currentSprinklerCycle++;
        if (currentSprinklerCycle > _sprinkler.cyclesInDay) {
          vTaskDelay(10 / portTICK_PERIOD_MS);
          continue;
        }
      }
    }

    // Run filter every day if conditions are met
    if (minuteNow >= startMinuteSprinkler && !filterDoneToday && !_filter.manual && !isMotorRunning) {
      if (sprinklerJustFinished || _timeSys.days[wday] == 0) {
        sprinklerJustFinished = false;
        runFilter();
      }
    }

    if (!isManualRunning) { // Only OFF when task Manual is not running
      digitalWrite(SPRINKLER_PIN, LOW);
      digitalWrite(FILTER_PIN, LOW);
      digitalWrite(LED_GREEN_PIN, HIGH);
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(LED_BLUE_PIN, HIGH);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// Task run pumps manually
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
      for (int pwm = 106; pwm < 256; pwm++) {
        analogWrite(SPRINKLER_PIN, pwm);
        vTaskDelay(5000 / (256 - 106) / portTICK_PERIOD_MS);  // ~34ms
      }
    } else if (_sprinkler.manual == 0 && digitalRead(SPRINKLER_PIN) != LOW) {
      Serial.println("Manual Sprinkler OFF");
      // Decrease gradually PWM for Sprinkler in ~5s
      for (int pwm = 255; pwm >= 0; pwm--) {
        analogWrite(SPRINKLER_PIN, pwm);
        vTaskDelay(5000 / 256 / portTICK_PERIOD_MS);  // ~19ms
      }
      digitalWrite(LED_GREEN_PIN, HIGH);
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(LED_BLUE_PIN, HIGH);
    }

    // Check and control Filter - Purple LED
    else if (_filter.manual == 1 && !isMotorRunning) {
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

// Task to simulate new day via Serial input
void testFunction_viaSerial(void *param) {
  for (;;) {
    Serial.println("Tick value now: " + String(tickSecond));
    if (Serial.available()) {
      String rec = Serial.readStringUntil('\n');
      Serial.println("Received value: " + rec);
      uint64_t seconds = rec.toInt();
      
      // timerWrite(countTimer, seconds * 1000000);
      tickSecond = seconds;
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
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

// ===========================================================TIME FUNCTIONs===========================================================
// get current time from NTP server
void getCurrentTime(){
  if (!getLocalTime(&currentTime)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&currentTime, "%A, %B %d %Y %H:%M:%S");
  if (startupTime == 0) {
    startupTime = mktime(&currentTime);
    Serial.print("Startup time: ");
    Serial.println(startupTime);
  }
}

// on timer for counting tick second
// void onTimer(void (*func)(), hw_timer_t **timer) {
//   *timer = timerBegin(TIMER_FREQ);                         // Frequency: 1MHz
//   timerAttachInterrupt(*timer, func);                      // Attach interrupt function
//   timerAlarm(*timer, (1 * 1000000), true, 0);              // Alarm every 1s
// }

void startTimer(void (*func)(), hw_timer_t **timer, uint32_t period) {
  *timer = timerBegin(TIMER_FREQ);                         // Frequency: 1MHz
  timerAttachInterrupt(*timer, func);                      // Attach interrupt function
  timerAlarm(*timer, (period * 1000000), true, 0);         // Alarm every time end period
}

void stopTimer(hw_timer_t **timer, int numbertimer) {
  /* Delete Timer in Timer Group */
  timerDetachInterrupt(*timer);
  timerEnd(*timer);
}

// Task for getting time by receiving notify from interrupt
void getTime(void *param) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (_wifi.getWifiStatus()) {
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      getCurrentTime();
    }
    _timeSys.period = 24/_sprinkler.cyclesInDay;
  }
}

// Interrupt function for Timer
void IRAM_ATTR timer_itr() {
  tickSecond++;
  if (hasNewSettings) { // has wifi interrupt
    if (tickSecond == 60) {
      if (getTimeTaskHandle != NULL) {
        xTaskNotifyGive(getTimeTaskHandle);
      }
    }
  }
  else {  // default interrupt
    if (tickSecond >= 24 * HOUR) {
      Serial.println("Done today, go to next day");
      timerRestart(countTimer);
      tickSecond = 0;
      _timeSys.firstTime = 1;
      filterDoneToday = false;
    }
    
  }
  
}

// Function for updating time infomation
void updateTimeInfo(void) {
  if (_wifi.getWifiStatus()) {
    _timeSys.period = 24/_sprinkler.cyclesInDay;
    struct tm t;
    if (getLocalTime(&t)) {
      currentTime = t;
      tickSecond = 0;
    } else {
      Serial.println("Failed to sync time from NTP.");
    }
    // statusWifi = 0;
  }
  else {
    if (hasNewSettings) {
      currentTime.tm_sec += tickSecond;
      tickSecond = 0;
      mktime(&currentTime);
      Serial.println(&currentTime, "%A, %B %d %Y %H:%M:%S");
    }
      
    
    
  }
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

  // uint32_t increaseTimeMs = 5000;
  // uint32_t decreaseTimeMs = 5000;
  uint32_t totalDurationMs = _sprinkler.duration * MINUTE * 1000;
  // uint32_t middleDelayMs = totalDurationMs - increaseTimeMs - decreaseTimeMs;

  // Serial.printf("sprinkler.duration: %lu - totalDurationMs: %lu - middleDelayMs: %lu\n", _sprinkler.duration, totalDurationMs, middleDelayMs);
  // if (middleDelayMs < 0) middleDelayMs = 0;  // avoid positive value

  ledcFade(SPRINKLER_PIN, 0, 255, 5000);
  // Stay in maximum speed
  vTaskDelay(totalDurationMs / portTICK_PERIOD_MS);

  ledcFade(SPRINKLER_PIN, 255, 0, 5000);

  Serial.println("Sprinkler Off");
  
  isMotorRunning = false;
}

// =======================================================PROCESS DATA FUNCTIONs=======================================================
void configurationForDog(int type){
  if(type == SMALL_DOG){
    _filter.duration = 2;
    _sprinkler.duration = 4;

    Serial.println("Config DOG SMALL");
    writeValueToNVS("ds", _sprinkler.duration);
    Serial.println("Sprinkler Duration: " + String(_sprinkler.duration) + "minute");

    writeValueToNVS("df", _filter.duration);
    Serial.println("Filter Duration: " + String(_filter.duration) + "h");

    writeValueToNVS("cycles", 1);
    _timeSys.period = 24;
  }else if(type == MEDIUM_DOG){
    _filter.duration = 3;
    _sprinkler.duration = 5;

    Serial.println("Config DOG MEDIUM");
    writeValueToNVS("ds", _sprinkler.duration);
    Serial.println("Sprinkler Duration: " + String(_sprinkler.duration) + "minute");

    writeValueToNVS("df", _filter.duration);
    Serial.println("Filter Duration: " + String(_filter.duration) + "h");

    writeValueToNVS("cycles", 1);
    _timeSys.period = 24;
  }else if(type == LARGE_DOG){
    _filter.duration = 4;
    _sprinkler.duration = 7;

    Serial.println("Config DOG LARGE");
    writeValueToNVS("ds", _sprinkler.duration);
    Serial.println("Sprinkler Duration: " + String(_sprinkler.duration) + "minute");

    writeValueToNVS("df", _filter.duration);
    Serial.println("Filter Duration: " + String(_filter.duration) + "h");

    writeValueToNVS("cycles", 1);
    _timeSys.period = 24;
  }
}

void handleMQTTSettings(const JsonDocument &doc) {
  haveMessageViaMQTT = false;
  uint8_t lastSprinklerManualState = _sprinkler.manual;
  uint8_t lastFilterManualState = _filter.manual;

  _sprinkler.manual = doc["manual_sprinkler"];
  _sprinkler.duration = int(doc["duration_sprinkler"]);
  _sprinkler.cyclesInDay = doc["cycles_sprinkler"];

  if (doc["days"].is<JsonArrayConst>()) {
    JsonArrayConst daysArray = doc["days"].as<JsonArrayConst>();
    for (int i = 0; i < 7; i++) {
      _timeSys.days[i] = daysArray[i].as<int>();
    }
  }

  if (doc["start_time_sprinkler"].is<JsonArrayConst>()) {
    JsonArrayConst startTimeSprinkler_Array = doc["start_time_sprinkler"].as<JsonArrayConst>();
    start_time_sprinkler[0] = startTimeSprinkler_Array[0].as<uint32_t>();
    start_time_sprinkler[1] = startTimeSprinkler_Array[1].as<uint32_t>();
  }

  startMinuteSprinkler = start_time_sprinkler[0] * 60 + start_time_sprinkler[1];

  _filter.manual = doc["manual_filter"];
  _filter.duration = int(doc["duration_filter"]);

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
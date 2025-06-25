#include <Wire.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "driver/timer.h"
#include "time.h"
#include <PubSubClient.h>
//==========================================================AsyncWebServer==========================================================
WiFiManager wm;
WiFiUDP udp;
NTPClient timeClient(udp, "pool.ntp.org", 7 * 3600, 60000);  // UTC+7
WiFiClient espClient;
PubSubClient client(espClient);
//==============================================================================JSON====================================================================
StaticJsonDocument<250> jsonDocument;
char buffer[250];
//==============================================================================DEFINE==================================================================
#define I2C_SDA 6 
#define I2C_SCL 5

#define MINUTE 60000
#define HOUR 3600000

#define GET_TIME_PERIOD 60

#define TIMER_FREQ 1000000

#define LED_GREEN 10
#define LED_RED 3
#define LED_BLUE 0
#define RELAY1 20
#define MOTOR 21
#define TMP36 1

#define DRDY 7

// Task handles
TaskHandle_t autoTaskHandle;
TaskHandle_t getTimeTaskHandle;
//=============================================================================INIT VAR==================================================================
typedef struct {
  unsigned int period;
  unsigned int cycles;
  int days[7];
  int firstTime;
}timer;

typedef struct{
  uint8_t manual;
  unsigned long long duration;
  uint16_t cyclesInDay;
}configPump;

timer timeSys;
configPump sprinkler;
configPump filter;

bool statusWifi;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 6*3600;
const int   daylightOffset_sec = 3600;

hw_timer_t *countTimer = NULL;
struct tm currentTime;
time_t startupTime = 0;
volatile unsigned long tickSecond = 0;
uint32_t start_time_sprinkler[2] = {0, 0};
bool isMotorRunning = false;        // Flag to ensure only 1 motor run at the same time
bool filterDoneToday = false;       // Flag check if Filter run today yet
static int startMinuteSprinkler;    // Start time of Sprinkler in minute

const char* ssid = "DevBrix";
const char* password = "0971705423";

// ===== HiveMQ Broker =====
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_sub_topic = "rick/pp4/sub";  // Use to receive message from App/MQTT broker
const char* mqtt_pub_topic = "rick/pp4/pub";  // Use to send message to App/MQTT broker
//=============================================================FUNCTION PROTOTYPE=============================================================
void configurationForDog(int type);
void taskAuto(void *pvParameters);

void writeValueToNVS(const char* key, int8_t value);
int8_t readValueFromNVS(const char* key);

void getCurrentTime(void);
void onTimer(void (*func)(), hw_timer_t **timer);
void offTimer(hw_timer_t **timer, int numbertimer);
void getTime(void *param);
void IRAM_ATTR timer_itr();
void updateTimeInfo(void);
void runFilter(void);
void check_server_connection(void *param);

void reconnectMQTT();
void mqttCallback(char *topic, byte *payload, unsigned int length);
//=================================================================================MAIN==================================================================
void setup() {
  Serial.begin(115200);

  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  statusWifi = wm.autoConnect("AutoConnectAP","12345678");
  while (!statusWifi) {
    Serial.println("Attempting to connect to WiFi...");
    statusWifi = wm.autoConnect("AutoConnectAP", "12345678");

    if (!statusWifi) {
      Serial.println("Failed to connect. Retrying in 5 seconds...");
      delay(5000); // Wait for 5s before retrying
    }
  }

  Serial.println("Connected... :)");
  client.setServer(mqtt_server, mqtt_port);
  if (!client.connected()) reconnectMQTT();
  client.setCallback(mqttCallback);
  client.publish(mqtt_pub_topic, "Connected to MQTT");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  getCurrentTime();
  tickSecond = currentTime.tm_sec;
  onTimer(&timer_itr, &countTimer);
  //=======================Config pin=============================
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(RELAY1, OUTPUT);
  pinMode(MOTOR, OUTPUT);

  timeSys.firstTime = 1;
  sprinkler.cyclesInDay = 1;

  Wire.begin(I2C_SDA, I2C_SCL); //pin I2C
  //==================================================================SET OFF=====================================================================
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(RELAY1, LOW);
  digitalWrite(MOTOR, LOW);
  //===================================================================FLASH======================================================================
  //Sprinkler duration
  sprinkler.duration = readValueFromNVS("ds");
  if (sprinkler.duration == 0) {
    sprinkler.duration = 5 * MINUTE;
    writeValueToNVS("ds", 5);
  } else {
    sprinkler.duration *= MINUTE;
  }
  // Filter duration
  filter.duration = readValueFromNVS("df");
  if (filter.duration == 0) {
    filter.duration = 3 * HOUR;
    writeValueToNVS("df", 3);
  } else {
    filter.duration *= HOUR;
  }

  // TimeSys
  timeSys.period = readValueFromNVS("pd");
  if (timeSys.period == 0) {
    timeSys.period = 24 * HOUR;
    writeValueToNVS("pd", 24);
  } else {
    // timeSys.period *= HOUR; // Nếu cần nhân, hãy mở dòng này
  }

  sprinkler.cyclesInDay = readValueFromNVS("cycles");
  if (sprinkler.cyclesInDay== 0) {
    sprinkler.cyclesInDay = 1;
    writeValueToNVS("cycles", 1);
  }

  // Hiển thị thông tin
  Serial.println("===========================SETTING===========================");
  Serial.println("Sprinkler Duration: " + String(sprinkler.duration/MINUTE)+" Minute");
  Serial.println("Filter Duration: " + String(filter.duration/HOUR) + "h");
  Serial.println("TimeSys Period: " + String(timeSys.period/HOUR) + "h");
  Serial.println("TimeSys Cycles: " + String(sprinkler.cyclesInDay));
  Serial.println("=============================================================");
  //===================================================================SETUP TASK=================================================================
  xTaskCreate(taskAuto, "Auto Task", 2048, NULL, 5, &autoTaskHandle);

  xTaskCreate(check_server_connection, "check_server_connection", 2048, NULL, 3, NULL);

  xTaskCreate(getTime, "Get Time", 2048, NULL, 4, &getTimeTaskHandle);
}

void loop() {
  if(statusWifi){
    if (!client.connected()) reconnectMQTT();
    client.loop(); 
  }
  vTaskDelay(1 / portTICK_PERIOD_MS);
}

void configurationForDog(int type){
  if(type == 1){
    filter.duration = 2;
    sprinkler.duration = 4;

    Serial.println("Config DOG ECO");
    writeValueToNVS("ds", sprinkler.duration);
    sprinkler.duration *= MINUTE;
    Serial.println("Sprinkler Duration: " + String(sprinkler.duration/MINUTE) + "minute");

    writeValueToNVS("df", filter.duration);
    filter.duration *= HOUR;
    Serial.println("Filter Duration: " + String(filter.duration/HOUR) + "h");

    writeValueToNVS("cycles", 1);
    timeSys.period = 24*HOUR;
  }else if(type == 2){
    filter.duration = 3;
    sprinkler.duration = 5;

    Serial.println("Config DOG STANDARD");
    writeValueToNVS("ds", sprinkler.duration);
    sprinkler.duration *= MINUTE;
    Serial.println("Sprinkler Duration: " + String(sprinkler.duration/MINUTE) + "minute");

    writeValueToNVS("df", filter.duration);
    filter.duration *= HOUR;
    Serial.println("Filter Duration: " + String(filter.duration/HOUR) + "h");

    writeValueToNVS("cycles", 1);
    timeSys.period = 24*HOUR;
  }else if(type == 3){
    filter.duration = 4;
    sprinkler.duration = 7;

    Serial.println("Config DOG HIGH");
    writeValueToNVS("ds", sprinkler.duration);
    sprinkler.duration *= MINUTE;
    Serial.println("Sprinkler Duration: " + String(sprinkler.duration/MINUTE) + "minute");

    writeValueToNVS("df", filter.duration);
    filter.duration *= HOUR;
    Serial.println("Filter Duration: " + String(filter.duration/HOUR) + "h");

    writeValueToNVS("cycles", 1);
    timeSys.period = 24*HOUR;
  }
}

void taskAuto(void *pvParameters) {
  int currentSprinklerCycle = 0;          // Sprinkler current cycles
  int lastRunDay;                         // Last day that have set to run
  bool sprinklerJustFinished = false;     // Flag check if sprinkler have done duration
  while (true) {
    updateTimeInfo();

    int wday = currentTime.tm_wday;
    int hour = currentTime.tm_hour;
    int min  = currentTime.tm_min;
    int sec  = currentTime.tm_sec;
    
    int minuteNow = hour * 60 + min;  // exchange current time (hour & minute) to minute
    if (wday != lastRunDay && minuteNow == startMinuteSprinkler) {
      currentSprinklerCycle = 0;
      timeSys.firstTime = 1;
      filterDoneToday = false;
      lastRunDay = wday;
    }

    if (timeSys.days[wday] == 1) {
      currentSprinklerCycle = (minuteNow - startMinuteSprinkler) / timeSys.period;
      Serial.printf("currentSprinklerCycle: %d\n", currentSprinklerCycle);
      if (currentSprinklerCycle < 0) currentSprinklerCycle = 0;
      else if (currentSprinklerCycle >= sprinkler.cyclesInDay) {
        Serial.println("All sprinkler cycles finished today");
        continue;
      }
      int cycleMinute = startMinuteSprinkler + currentSprinklerCycle * timeSys.period; 
      // Serial.printf("minuteNow: %d - cycleMinute: %d\n", minuteNow, cycleMinute);
      if (minuteNow == cycleMinute && sec == 0 && !isMotorRunning) {
        isMotorRunning = true;
        Serial.println("Sprinkler ON");
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, LOW);
        digitalWrite(LED_BLUE, LOW);
        Serial.print("Active ");
        Serial.print(sprinkler.duration/MINUTE);
        Serial.println(" Minute");

        unsigned long increaseTimeMs = 5000;
        unsigned long decreaseTimeMs = 5000;
        unsigned long totalDurationMs = (unsigned long)sprinkler.duration * MINUTE;
        unsigned long middleDelayMs = totalDurationMs - increaseTimeMs - decreaseTimeMs;
        if (middleDelayMs < 0) middleDelayMs = 0;  // avoid positive value

        // Increase speed of Sprinkler gradually in ~5s
        for (int pwm = 106; pwm < 256; pwm++) {
          analogWrite(MOTOR, pwm);
          vTaskDelay(increaseTimeMs / (256 - 106) / portTICK_PERIOD_MS);  // ~34ms
        }
        // Stay in maximum speed
        vTaskDelay(middleDelayMs / portTICK_PERIOD_MS);

        // Decrease speed of Sprinkler gradually in ~5s
        for (int pwm = 255; pwm >= 0; pwm--) {
          analogWrite(MOTOR, pwm);
          vTaskDelay(decreaseTimeMs / 256 / portTICK_PERIOD_MS);  // ~19ms
        }

        timeSys.firstTime = 0;
        isMotorRunning = false;
        sprinklerJustFinished = true;
        currentSprinklerCycle++;
        if (currentSprinklerCycle > sprinkler.cyclesInDay) {
          vTaskDelay(10 / portTICK_PERIOD_MS);
          continue;
        }
      }

      if (minuteNow >= startMinuteSprinkler && !filterDoneToday) {
        if (!isMotorRunning && sprinklerJustFinished) {
          sprinklerJustFinished = false;
          runFilter();
        }
        else if (!isMotorRunning && currentSprinklerCycle >= sprinkler.cyclesInDay) {
          runFilter();
        }
      }
    }
    else {
      digitalWrite(RELAY1, LOW);
      digitalWrite(LED_GREEN, HIGH);
      digitalWrite(LED_RED, HIGH);
      digitalWrite(LED_BLUE, HIGH);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

//read flash
int8_t readValueFromNVS(const char* key) {
  nvs_handle my_handle;
  esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
  if (err != ESP_OK) {
    Serial.println("Không thể mở NVS!");
  }

  int8_t value = 0;
  err = nvs_get_i8(my_handle, key, &value);

  nvs_close(my_handle);
  return value;
}

//write flash
void writeValueToNVS(const char* key, int8_t value) {
  nvs_handle my_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);

  err = nvs_set_i8(my_handle, key, value);
  ESP_ERROR_CHECK(err);

  err = nvs_commit(my_handle);
  ESP_ERROR_CHECK(err);

  nvs_close(my_handle);
}

// get current time from NTP server
void getCurrentTime(){
  // struct tm timeinfo;
  // if(!getLocalTime(&timeinfo)){
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
void onTimer(void (*func)(), hw_timer_t **timer) {
  *timer = timerBegin(TIMER_FREQ);                         // Frequency: 1MHz
  timerAttachInterrupt(*timer, func);                      // Attach interrupt function
  timerAlarm(*timer, (1 * 1000000), true, 0);              // Alarm every 1s
}

void offTimer(hw_timer_t **timer, int numbertimer) {
  /* Delete Timer in Timer Group */
  timerDetachInterrupt(*timer);
  timerEnd(*timer);
}

// Task for getting time by receiving notify from interrupt
void getTime(void *param) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    getCurrentTime();
    timeSys.period = 24*HOUR/sprinkler.cyclesInDay;
  }
}

// Interrupt function for Timer
void IRAM_ATTR timer_itr() {
  tickSecond++;
  if (tickSecond == 60) {
    if (getTimeTaskHandle != NULL) {
      xTaskNotifyGive(getTimeTaskHandle);
    }
  }
}

// Function for updating time infomation
void updateTimeInfo(void) {
  if (statusWifi) {
    timeSys.period = 24*60/sprinkler.cyclesInDay;
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
    currentTime.tm_sec += tickSecond;
    tickSecond = 0;
    mktime(&currentTime);
    Serial.println(&currentTime, "%A, %B %d %Y %H:%M:%S");
  }
}

// Function running Filter pump
void runFilter(void) {
  isMotorRunning = true;
  Serial.println("Filter On");

  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_BLUE, LOW);

  digitalWrite(RELAY1, HIGH); // Turn ON relay for filter
  vTaskDelay(filter.duration * HOUR / portTICK_PERIOD_MS);
  digitalWrite(RELAY1, LOW);  // Turn OFF relay

  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_BLUE, HIGH);
  Serial.println("Filter Off");

  filterDoneToday = true;
  isMotorRunning = false;
}

// Task checking connection with server (Wifi & MQTT)
void check_server_connection(void *param) {
  for (;;) {
    while (!statusWifi) {
      Serial.println("Attempting to connect to WiFi...");
      statusWifi = wm.autoConnect("AutoConnectAP", "12345678");

      if (!statusWifi) {
        Serial.println("Failed to connect. Retrying in 5 seconds...");
        vTaskDelay(5000/portTICK_PERIOD_MS); // Đợi 5 giây rồi thử lại
      }
    }
    Serial.println("Connected");
    
    vTaskDelay(30000/portTICK_PERIOD_MS);
  }
}

// Function to reconnect to MQTT server
void reconnectMQTT() {
  String clientId = "esp32-client-" + String(random(0xffff), HEX);
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT connected");
      client.subscribe(mqtt_sub_topic); // Sub vào topic
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// Function to receive and process data from MQTT
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);

  // Convert payload to String
  String body = "";
  for (int i = 0; i < length; i++) {
    body += (char)payload[i];
  }

  Serial.print("Message: ");
  Serial.println(body);
  Serial.println("-----------------------");

  // Parse JSON payload
  DeserializationError error = deserializeJson(jsonDocument, body);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  // ======= Cấu hình như trong handlePost() ========
  sprinkler.manual = jsonDocument["manual_sprinkler"];
  sprinkler.duration = int(jsonDocument["duration_sprinkler"]);
  sprinkler.cyclesInDay = jsonDocument["cycles_sprinkler"];

  JsonArray daysArray = jsonDocument["days"].as<JsonArray>();
  for (int i = 0; i < 7; i++) {
    timeSys.days[i] = daysArray[i].as<int>();
  }

  JsonArray startTimeSprinkler_Array = jsonDocument["start_time_sprinkler"].as<JsonArray>();
  start_time_sprinkler[0] = startTimeSprinkler_Array[0].as<uint32_t>();
  start_time_sprinkler[1] = startTimeSprinkler_Array[1].as<uint32_t>();

  startMinuteSprinkler = start_time_sprinkler[0] * 60 + start_time_sprinkler[1]; // chuyển giờ và phút bắt đầu của sprinkler sang phút

  filter.manual = jsonDocument["manual_filter"];
  filter.duration = int(jsonDocument["duration_filter"]);

  int typeDog = jsonDocument["type_dog"];
  if(typeDog == 0) {
    Serial.println("================ MQTT SETTING ====================");
    Serial.println("=================== SPRINKLER ====================");
    Serial.println("Manual Sprinkler: " + String(sprinkler.manual));
    writeValueToNVS("ds", sprinkler.duration);
    Serial.println("Duration Sprinkler: " + String(sprinkler.duration) + " Minute");
    writeValueToNVS("cycles", sprinkler.cyclesInDay);
    Serial.println("Cycles Sprinkler: " + String(sprinkler.cyclesInDay));
    Serial.println("Start time Sprinkler: " + String(start_time_sprinkler[0]) + ":" + String(start_time_sprinkler[1]));

    Serial.println("==================== FILTER ======================");
    Serial.println("Manual Filter: " + String(filter.manual));
    writeValueToNVS("df", filter.duration);
    Serial.println("Duration Filter: " + String(filter.duration) + " h");

    Serial.println("==================== WEEK ========================");
    Serial.print("Sprinkler Days: ");
    for (int i = 0; i < 7; i++) {
      Serial.print(timeSys.days[i]);
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
#include <Wire.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "driver/timer.h"
#include "time.h"
#include <SparkFun_ADS122C04_ADC_Arduino_Library.h>
#include <PubSubClient.h>
//==========================================================AsyncWebServer==========================================================
WebServer server(80);
SFE_ADS122C04 pt100_sensor;
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

// Task handles
TaskHandle_t autoTaskHandle;
TaskHandle_t getTimeTaskHandle;
// TaskHandle_t Task3Handle;

#define LEVEL_WATER 400

#define LED_GREEN 10
#define LED_RED 3
#define LED_BLUE 0
#define RELAY1 20
#define MOTOR 21
#define TMP36 1

#define DRDY 7


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
float temperature;                  // Variable receive from sensor PT100

const char* ssid = "DevBrix";
const char* password = "0971705423";

// ===== HiveMQ Broker =====
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_sub_topic = "esp32/test";
const char* mqtt_pub_topic = "esp32/test";
//=============================================================FUNCTION PROTOTYPE=============================================================
void configurationForDog(int type);
// void handlePost();
// void getData();
// void createJson(char *tag, float value, char *unit);
// void addJsonObject(char *tag, float value, char *unit);
// void setupRouting(); 

void taskAuto(void *pvParameters);

void writeValueToNVS(const char* key, int8_t value);
int8_t readValueFromNVS(const char* key);

void getCurrentTime(void);
void onTimer(void (*func)(), hw_timer_t **timer, int numbertimer);
void offTimer(hw_timer_t **timer, int numbertimer);
void getTime(void *param);
void IRAM_ATTR timer_itr();
void updateTimeInfo(void);
void runFilter(void);
void PT100_sensor_task(void *param);

void reconnectMQTT();
void mqttCallback(char *topic, byte *payload, unsigned int length);
//=================================================================================MAIN==================================================================
void setup() {
  Serial.begin(115200);

  // Khởi tạo bộ nhớ NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  WiFiManager wm;
  statusWifi = wm.autoConnect("AutoConnectAP","12345678");
  if(!statusWifi) {
    Serial.println("Failed to connect");
    // ESP.restart();
  } 
  else {
    Serial.println("Connected... :)");
    // setupRouting();
    client.setServer(mqtt_server, mqtt_port);
    if (!client.connected()) reconnectMQTT();
    client.setCallback(mqttCallback);
    client.publish(mqtt_pub_topic, "Connected to MQTT");
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  getCurrentTime();
  onTimer(&timer_itr, &countTimer, 0);
  //=======================Config pin=============================
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(RELAY1, OUTPUT);
  pinMode(MOTOR, OUTPUT);

  timeSys.firstTime = 1;
  sprinkler.cyclesInDay = 1;

  Wire.begin(I2C_SDA, I2C_SCL); //pin I2C
  pinMode(4,INPUT);
  pinMode(7,INPUT);
  pt100_sensor.enableDebugging();
  if (pt100_sensor.begin(0x40) == false)
  {
    Serial.println(F("PT100 Init failed"));
    while (1);
  }
  pt100_sensor.configureADCmode(ADS122C04_3WIRE_MODE,ADS122C04_DATA_RATE_20SPS);
  pt100_sensor.setInputMultiplexer(ADS122C04_MUX_AIN0_AIN1);
  //==================================================================SET OFF=====================================================================
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(RELAY1, LOW);
  digitalWrite(MOTOR, LOW);
  //===================================================================FLASH======================================================================
  sprinkler.duration = readValueFromNVS("ds");
  if (sprinkler.duration == 0) {
    sprinkler.duration = 5 * MINUTE;
    writeValueToNVS("ds", 5);
  } else {
    sprinkler.duration *= MINUTE;
  }
  // // Filter
  filter.duration = readValueFromNVS("df");
  if (filter.duration == 0) {
    filter.duration = 3 * HOUR;
    writeValueToNVS("df", 3);
  } else {
    filter.duration *= HOUR;
  }

  // // TimeSys
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
  if(!statusWifi){
    Serial.println("==========================MANUAl MODE==========================");
  }else{
    Serial.println("===========================AUTO MODE===========================");
  }

  //===================================================================SETUP TASK=================================================================
  xTaskCreate(taskAuto, "Auto Task", 2048, NULL, 2, &autoTaskHandle);

  xTaskCreate(PT100_sensor_task, "PT100_sensor_task", 2048, NULL, 3, NULL);

  xTaskCreate(getTime, "Get Time", 2048, NULL, 5, &getTimeTaskHandle);
}

void loop() {
  if(statusWifi){
    // server.handleClient();
    if (!client.connected()) reconnectMQTT();
    client.loop(); 
  }
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

// void setupRouting(){
//   server.on("/PT100", getLevelWater); 
//   server.on("/status", HTTP_POST, handlePost);    
//   server.begin();  
// }

// void getLevelWater(){
//   // Serial.println("Get Level Water");
//   createJson("temp", temperature, "°C");
//   server.send(200, "application/json", buffer);
// }

// void handlePost() {
//   if (server.hasArg("plain") == false) {
//   }
//   String body = server.arg("plain");
//   deserializeJson(jsonDocument, body);

//   sprinkler.manual = jsonDocument["manual_sprinkler"];
//   sprinkler.duration = int(jsonDocument["duration_sprinkler"]);
//   sprinkler.cyclesInDay = jsonDocument["cycles_sprinkler"];
//   JsonArray daysArray = jsonDocument["days"].as<JsonArray>();
//   for (int i = 0; i < 7; i++) {
//     timeSys.days[i] = daysArray[i].as<int>();
//   }

//   JsonArray startTimeSprinkler_Array = jsonDocument["start_time_sprinkler"].as<JsonArray>();
//   start_time_sprinkler[0] = startTimeSprinkler_Array[0].as<uint32_t>();
//   start_time_sprinkler[1] = startTimeSprinkler_Array[1].as<uint32_t>();
//   Serial.print("Start time Sprinkler: ");
//   for (int i = 0; i < 2; i++) {
//     Serial.print(start_time_sprinkler[i]);
//     if (i == 0) Serial.print(":");
//   }
//   Serial.println();

//   filter.manual = jsonDocument["manual_filter"];
//   filter.duration = int(jsonDocument["duration_filter"]);

//   int typeDog = jsonDocument["type_dog"];
//   if(typeDog == 0){
//       Serial.println("===========================SETTING FROM APP===========================");
//       Serial.println("===============================SPRINKER===============================");
//       Serial.println("Manual Sprinkler: " + String(sprinkler.manual));
//       writeValueToNVS("ds", sprinkler.duration);
//       sprinkler.duration *= MINUTE;
//       Serial.println("Duration Sprinkler: " + String(sprinkler.duration/MINUTE) + "Minute");
//       writeValueToNVS("cycles", sprinkler.cyclesInDay);
//       Serial.println("Cycles Sprinkler: " + String(sprinkler.cyclesInDay));

//       Serial.println("================================FILTER================================");
//       Serial.println("Manual Filter: " + String(filter.manual));
//       writeValueToNVS("df", filter.duration);
//       filter.duration *= HOUR;
//       Serial.println("Duration Filter: " + String(filter.duration/HOUR) + "h");

//       Serial.println("=================================WEEK=================================");
//       Serial.print("Sprinkler Days array: ");
//       for (int i = 0; i < 7; i++) {
//         Serial.print(timeSys.days[i]);
//         if (i < 6) {
//           Serial.print(", ");
//         }
//       }
//       Serial.println();
//       Serial.println("=======================================================================");
//   }else{
//     Serial.println();
//     Serial.println("TYPE DOG");
//     Serial.print("type: ");
//     Serial.println(typeDog);
//     configurationForDog(typeDog);
//   }

//   server.send(200, "application/json", "{}");
// }

// void getData() {
//   jsonDocument.clear();
//   serializeJson(jsonDocument, buffer);
//   server.send(200, "application/json", buffer);
// }

// void createJson(char *tag, float value, char *unit) {  
//   jsonDocument.clear();
//   jsonDocument["type"] = tag;
//   jsonDocument["value"] = value;
//   jsonDocument["unit"] = unit;
//   serializeJson(jsonDocument, buffer);  
// }
 
// void addJsonObject(char *tag, float value, char *unit) {
//   JsonObject obj = jsonDocument.createNestedObject();
//   obj["type"] = tag;
//   obj["value"] = value;
//   obj["unit"] = unit; 
// }

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
    
    int minuteNow = hour * 60 + min;  // chuyển giờ hiện tại và phút hiện tại sang phút
    if (wday != lastRunDay) {
      currentSprinklerCycle = 0;
      timeSys.firstTime = 1;
      filterDoneToday = false;
      lastRunDay = wday;
    }

    if (timeSys.days[wday] == 1) {
      
      static int startMinuteSprinkler = start_time_sprinkler[0] * 60 + start_time_sprinkler[1]; // chuyển giờ và phút bắt đầu của sprinkler sang phút

      currentSprinklerCycle = (minuteNow - startMinuteSprinkler) / timeSys.period;
      Serial.printf("currentSprinklerCycle: %d\n", currentSprinklerCycle);
      if (currentSprinklerCycle < 0) currentSprinklerCycle = 0;
      else if (currentSprinklerCycle >= sprinkler.cyclesInDay) {
        Serial.println("All sprinkler cycles finished today");
        continue;
      }
      int cycleMinute = startMinuteSprinkler + currentSprinklerCycle * timeSys.period;  // 
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
        unsigned long totalDurationMs = (unsigned long)sprinkler.duration;
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

void onTimer(void (*func)(), hw_timer_t **timer, int numbertimer) {
  *timer = timerBegin(numbertimer, 80, true);                         // prescaler 80 → 1 tick = 1μs
  timerAttachInterrupt(*timer, func, true);                           // Gán hàm ngắt
  timerAlarmWrite(*timer, (1 * 1000000), true);                       // 60s
  timerAlarmEnable(*timer);                                           // Kích hoạt timer
}

void offTimer(hw_timer_t **timer, int numbertimer) {
  /* Delete Timer in Timer Group */
  timerDetachInterrupt(*timer);
  timerEnd(*timer);
}

void getTime(void *param) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    getCurrentTime();
    timeSys.period = 24*HOUR/sprinkler.cyclesInDay;
  }
}

void IRAM_ATTR timer_itr() {
  tickSecond++;
  if (tickSecond == 60) {
    if (getTimeTaskHandle != NULL) {
      xTaskNotifyGive(getTimeTaskHandle);
    }
  }
  
}

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
  }
  else {
    currentTime.tm_sec += tickSecond;
    tickSecond = 0;
    mktime(&currentTime);
    Serial.println(&currentTime, "%A, %B %d %Y %H:%M:%S");
  }
}

void runFilter(void) {
  isMotorRunning = true;
  Serial.println("Filter On");

  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_BLUE, LOW);

  digitalWrite(RELAY1, HIGH); // Turn ON relay for filter
  vTaskDelay(filter.duration / portTICK_PERIOD_MS);
  digitalWrite(RELAY1, LOW);  // Turn OFF relay

  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_BLUE, HIGH);
  Serial.println("Filter Off");

  filterDoneToday = true;
  isMotorRunning = false;
}

void PT100_sensor_task(void *param) {
  for (;;) {
    temperature = pt100_sensor.readPT100Centigrade();
    vTaskDelay(500/portTICK_PERIOD_MS);
  }
}

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

  filter.manual = jsonDocument["manual_filter"];
  filter.duration = int(jsonDocument["duration_filter"]);

  int typeDog = jsonDocument["type_dog"];
  if(typeDog == 0) {
    Serial.println("================ MQTT SETTING ====================");
    Serial.println("=================== SPRINKLER ====================");
    Serial.println("Manual Sprinkler: " + String(sprinkler.manual));
    writeValueToNVS("ds", sprinkler.duration);
    sprinkler.duration *= MINUTE;
    Serial.println("Duration Sprinkler: " + String(sprinkler.duration/MINUTE) + " Minute");
    writeValueToNVS("cycles", sprinkler.cyclesInDay);
    Serial.println("Cycles Sprinkler: " + String(sprinkler.cyclesInDay));

    Serial.println("==================== FILTER ======================");
    Serial.println("Manual Filter: " + String(filter.manual));
    writeValueToNVS("df", filter.duration);
    filter.duration *= HOUR;
    Serial.println("Duration Filter: " + String(filter.duration/HOUR) + " h");

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

#ifndef _MAIN_H_
#define _MAIN_H_

#include "PP4.h"
#include "WIFI_Class.h"
#include "MQTT.h"
#include "driver/timer.h"
#include "time.h"
// #include <Wire.h>
#include <nvs_flash.h>


typedef enum {
  SPRINKLER_AUTO,
  SPRINKLER_MANUAL,
  FILTER_AUTO,
  FILTER_MANUAL,
  TYPE_DOG,
  WATER_LEVEL,
  TOPIC_COUNT
} mqtt_topic_id_t;

void writeValueToNVS(const char* key, int16_t value);
int16_t readValueFromNVS(const char* key);
void saveDaysToNVS(int *days);
bool readDaysFromNVS(int *days);
void saveTimeToNVS(time_t time);
time_t readTimeFromNVS();

void setCurrentTime(const char* timeString = NULL);
// void onTimer(void (*func)(), hw_timer_t **timer);
void stopTimer(hw_timer_t **timer);
void startTimer(void (*func)(), hw_timer_t **timer, uint32_t period);
void getTime(void *param);
void IRAM_ATTR tickTimer_itr();
void IRAM_ATTR timer_itr(void);
void IRAM_ATTR countToStartTimer_itr();
void updateTimeInfo(void);

void runFilter(void);
void runSprinkler(void);

void reConfigMQTT();
void configurationForDog(int type);
void handleDogTypeConfiguration(int type);
void handleManualControls(const JsonDocument &doc);
void handleDurationSettings(const JsonDocument &doc);
void handleActiveDays(const JsonDocument &doc);
void handleStartTime(const JsonDocument &doc);
void handleTimeSync(const JsonDocument &doc);
void handleMQTTSettings(const JsonDocument &doc);
void mqtt_publish_message(int topic_id, const char *mac_id, String message);
void mqtt_subscribe(int topic_id, const char *mac_id);

void taskUpdateTime(void *param);
void taskSaveSettings(void *param);
void taskAuto(void *pvParameters);
void taskManual(void *param);
void getTime(void *param);
void stopAllTimers();
void initializeNewTimers(int currentSec, int startSec);
void calculateAndSetNextCycle(int currentSec, int startSec);
void testFunction_viaSerial(void *param);

void Setup();
void Loop();

#endif //_MAIN_H_
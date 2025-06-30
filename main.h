#ifndef _MAIN_H_
#define _MAIN_H_

#include "PP4.h"
#include "WIFI_Class.h"
#include "MQTT.h"
#include "driver/timer.h"
#include "time.h"
#include <Wire.h>
#include <ArduinoJson.h>
#include <nvs_flash.h>
#include <nvs.h>

void writeValueToNVS(const char* key, int16_t value);
int16_t readValueFromNVS(const char* key);
void saveDaysToNVS(int *days);
bool readDaysFromNVS(int *days);

void getCurrentTime(void);
void onTimer(void (*func)(), hw_timer_t **timer);
void getTime(void *param);
void IRAM_ATTR timer_itr(void);
void updateTimeInfo(void);

void runFilter(void);
void runSprinkler(void);

void configurationForDog(int type);
void handleMQTTSettings(const JsonDocument &doc);

void taskSaveSettings(void *param);
void taskAuto(void *pvParameters);
void taskManual(void *param);

void Setup();
void Loop();

#endif //_MAIN_H_
#ifndef _MAIN_H_
#define _MAIN_H_

#include "PP4.h"
#include "WIFI_Class.h"
// #include "MQTT.h"
#include "driver/timer.h"
#include "time.h"
// #include "POST_GET.h"
// #include <nvs_flash.h>

void loadConfiguration(void);
void writeValueToNVS(const char* key, int16_t value);
void writeValueToNVS(const char* key, String value);
int16_t readValueFromNVS(const char* key);
String readStringFromNVS(const char* key);
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


void configurationForDog(int type);
void loginAndGetToken(void);
void getDeviceStatus(void);
void getSprinklerSchedule(void);
void updateDeviceStatus(void);

void taskPOSTGET(void *param);
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
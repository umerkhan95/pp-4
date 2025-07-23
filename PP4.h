#ifndef _PP4_H_
#define _PP4_H_
#include <Arduino.h>
#define DEBUG

#define VERSION "v16.7.0"
#define MQTT_TOPIC_MAX_LEN 80

// Định nghĩa hằng số
#define MINUTE 60
#define HOUR   3600
#define GET_TIME_PERIOD 60
#define TIMER_FREQ 1000000

#define PWM_FREQ 4000
#define PWM_RESOLUTION 8

#define LED_GREEN_PIN 10
#define LED_RED_PIN   3
#define LED_BLUE_PIN  0
#define FILTER_PIN    20
#define SPRINKLER_PIN 21
#define TMP36 1
#define DRDY 7
#define I2C_SDA 6
#define I2C_SCL 5

typedef struct {
  uint32_t period;
  uint32_t cycles;
  int days[7];
  int firstTime;
} TimeSys_t;

typedef struct{
  uint8_t manual:1;
  uint8_t autoControl:1;
  uint32_t duration;
  uint16_t cyclesInDay;
} configPump_t;

typedef enum {
  SMALL_DOG = 1,
  MEDIUM_DOG,
  LARGE_DOG
} Dog_type;

typedef enum {
  FILTER_DURATION_SMALL_DOG = 2,
  FILTER_DURATION_MEDIUM_DOG,
  FILTER_DURATION_LARGE_DOG
} Filter_duration;

typedef enum {
  SPRINKLER_DURATION_SMALL_DOG = 4,
  SPRINKLER_DURATION_MEDIUM_DOG,
  SPRINKLER_DURATION_LARGE_DOG = 7
} Sprinkler_duration;


#endif //_PP4_H_
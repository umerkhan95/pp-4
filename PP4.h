#ifndef _PP4_H_
#define _PP4_H_
#include <Arduino.h>
#define DEBUG

// Định nghĩa hằng số
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
#define I2C_SDA 6
#define I2C_SCL 5

typedef struct {
  uint32_t period;
  uint32_t cycles;
  int days[7];
  int firstTime;
}timer;

typedef struct{
  uint8_t manual:1;
  uint8_t autoControl:1;
  uint32_t duration;
  uint16_t cyclesInDay;
}configPump;

typedef enum {
  SMALL_DOG = 1,
  MEDIUM_DOG,
  LARGE_DOG
} Dog_type;



#endif //_PP4_H_
#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

extern TFT_eSPI tft;

// Hàm khởi tạo
void TFT_init();

// Vẽ toàn bộ màn hình
void drawScreen();

// Dùng chung biến từ sensor.h (khai báo extern)
extern float temp;
extern float humi;
extern float co_ppm;
extern float nh3_ppm;
extern float dust;

extern String receivedAlerts[10];
extern int receivedAlertCount;

#endif

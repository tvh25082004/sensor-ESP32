#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include <DHT.h>
// ==== DHT22 ====
extern float temp, humi, co_ppm, nh3_ppm, dust;
void DHT22_init();
void DHT22_run();

// ==== MQ135 ====
extern float nh3_ppm;
void MQ135_Init();
void MQ135_run();

// ==== MQ7 ====
extern float co_ppm;
void MQ7_Init();
void MQ7_run();

// ==== GP2Y Dust ====
extern float dust;
void GP2Y_setup();
float Run_GP2Y();

#endif

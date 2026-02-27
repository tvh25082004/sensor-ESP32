#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "time.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>

// ===== Các khai báo giữ nguyên =====
extern const char *ntpServer;
extern const long gmtOffset_sec;
extern const int daylightOffset_sec;

extern const char *ssid;
extern const char *password;

extern const char *apSSID;
extern const char *apPASS;

extern WebServer server;
extern Preferences prefs;

// ==== AI ALERT SERVER ====
extern String receivedAlerts[10];
extern int receivedAlertCount;

// ===== Khai báo hàm =====
String htmlPage();
void handleSave();
void startConfigPortal();
void Wifi_init();
void AI_Start();
bool isWifiConnected();

#endif

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>
#include <PubSubClient.h>

extern unsigned long lastSend;
extern const unsigned long SEND_INTERVAL;
extern bool firstSendDone;

void MQTT_init();
void MQTT_reconnect();
void MQTT_publish();
String getTimestamp();

#endif
